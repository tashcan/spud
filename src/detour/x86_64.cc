
#include <spud/detour.h>
#include <spud/memory/protection.h>
#include <spud/utils.h>

// Private helper stuff
#include <zydis_utils.h>

#include <Zydis/Utils.h>
#include <Zydis/Zydis.h>

#include <asmjit/asmjit.h>

#include <cstdint>

#include <array>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// Here shall be dragons, at some point

namespace spud {
namespace detail {

/*
  jmp [rip+0]
  0x00000000000
*/
constexpr auto kAbsoluteJumpSize = 6 + sizeof(uintptr_t);

/*
    push r15
    mov r15, qword ptr [0x0]
    cmp byte ptr [r15], 1
    pop r15
*/
constexpr auto kReloCompareSize = 16 + sizeof(uintptr_t);
constexpr auto kReloCompareExpandSize =
    kReloCompareSize - 8 - sizeof(uintptr_t);

/*
push   r15
mov    r15, qword ptr[0x0]
addsd  xmm3,mmword ptr [r15]
pop    r15
*/
constexpr auto kReloAddsdSize = 17 + sizeof(uintptr_t);
constexpr auto kReloAddsdExpandSize = kReloAddsdSize - 9 - sizeof(uintptr_t);

struct RelocationEntry {
  uintptr_t address;
  ZydisDecodedInstruction instruction;
  std::array<ZydisDecodedOperand, ZYDIS_MAX_OPERAND_COUNT> operands;
};

struct RelocationInfo {
  std::unordered_map<uintptr_t, uintptr_t> relocation_offset;
  std::vector<RelocationEntry> relocations;
};

struct RelocationResult {
  std::vector<uint8_t> data;
  size_t copy_offset;
};

struct Trampoline {
  uintptr_t start;
  std::vector<uint8_t> data;
};

struct RelocationMeta {
  uintptr_t size;
  uintptr_t expand;
  std::function<void(uintptr_t, const RelocationEntry &,
                     asmjit::x86::Assembler &)>
      gen_relo_data;
  std::function<uintptr_t(
      uintptr_t trampoline_address, uintptr_t trampoline_start,
      uintptr_t target_start, size_t target_offset, const RelocationEntry &relo,
      uintptr_t relocation_data, asmjit::x86::Assembler &assembler)>
      gen_relo_code;
};

struct ReloInstruction {
  ZydisMnemonic i = ZYDIS_MNEMONIC_INVALID;
  ZydisBranchType b = ZYDIS_BRANCH_TYPE_NONE;

  constexpr ReloInstruction(ZydisMnemonic mn, ZydisBranchType b)
      : i(mn), b(b) {}
  constexpr ReloInstruction(ZydisMnemonic mn) : i(mn) {}
  constexpr ReloInstruction(ZydisDecodedInstruction instruction) {
    if (instruction.meta.branch_type != ZYDIS_BRANCH_TYPE_NONE) {
      i = ZYDIS_MNEMONIC_INVALID;
      b = ZYDIS_BRANCH_TYPE_MAX_VALUE;
    } else {
      i = instruction.mnemonic;
    }
  }

  bool operator==(const ReloInstruction &other) const {
    return (this->i == other.i && this->b == other.b);
  }
};

constexpr ReloInstruction JUMP_RELO_JMP_INSTRUCTION = {
    ZYDIS_MNEMONIC_INVALID, ZYDIS_BRANCH_TYPE_MAX_VALUE};

struct ReloInstructionHasher {
  std::size_t operator()(const ReloInstruction &relo) const {
    return relo.i << 16 | relo.b;
  }
};

const std::unordered_map<ReloInstruction, RelocationMeta, ReloInstructionHasher>
    relo_meta = {
        {JUMP_RELO_JMP_INSTRUCTION,
         {.size = sizeof(uintptr_t),
          .expand = 0,
          .gen_relo_data =
              [](auto target_start, auto &relo, auto &assembler) {
                using namespace asmjit;
                using namespace asmjit::x86;

                const auto jump_target = target_start + relo.address +
                                         (relo.instruction.raw.imm[0].value.s +
                                          relo.instruction.length);

                assembler.jmp(ptr(rip, 0));
                assembler.embed(&jump_target, sizeof(jump_target));
              },
          .gen_relo_code =
              [](uintptr_t trampoline_address, uintptr_t trampoline_start,
                 uintptr_t target_start, size_t target_offset,
                 const RelocationEntry &relo, uintptr_t relocation_data,
                 asmjit::x86::Assembler &assembler) {
                auto code = assembler.code();
                auto code_data = assembler.code()->textSection();

                auto target_code = reinterpret_cast<uint8_t *>(
                    target_offset + code_data + trampoline_start +
                    relo.address + relo.instruction.raw.imm[0].offset);

                auto jump_target = relocation_data + trampoline_start -
                                   relo.instruction.length - relo.address -
                                   target_offset;

                switch (relo.instruction.raw.imm[0].size) {
                case 8: {
                  assert(jump_target < 0xFF &&
                         "immediate size too small for relocation");
                  *(int16_t *)(target_code) = jump_target;
                } break;
                case 16: {
                  *(int16_t *)(target_code) = jump_target;
                } break;
                case 32: {
                  *(int32_t *)(target_code) = jump_target;
                } break;
                case 64: {
                  *(int64_t *)(target_code) = jump_target;
                } break;
                }
                return target_offset;
              }}},
        {ZYDIS_MNEMONIC_CMP,
         {.size = kReloCompareSize,
          .expand = kReloCompareExpandSize,
          .gen_relo_data =
              [](auto target_start, auto &relo, auto &assembler) {
                const auto lea_target =
                    target_start + relo.address +
                    (relo.instruction.raw.disp.value + relo.instruction.length);
                assembler.embed(&lea_target, sizeof(lea_target));
              },
          .gen_relo_code =
              [](uintptr_t trampoline_address, uintptr_t trampoline_start,
                 uintptr_t target_start, size_t target_offset,
                 const RelocationEntry &relo, uintptr_t relocation_data,
                 asmjit::x86::Assembler &assembler) {
                using namespace asmjit;
                using namespace asmjit::x86;

                auto relo_lea_target = trampoline_start + relocation_data;
                assembler.push(r15);
                assembler.mov(r15, qword_ptr(relo_lea_target));
                assembler.cmp(byte_ptr(r15),
                              relo.operands[1].imm.is_signed
                                  ? relo.operands[1].imm.value.s
                                  : relo.operands[1].imm.value.u);
                assembler.pop(r15);
                target_offset += kReloCompareExpandSize;
                return target_offset;
              }}},
        {ZYDIS_MNEMONIC_LEA,
         {.size = sizeof(uintptr_t),
          .expand = 0,
          .gen_relo_data =
              [](auto target_start, auto &relo, auto &assembler) {
                const auto lea_target =
                    target_start + relo.address +
                    (relo.instruction.raw.disp.value + relo.instruction.length);
                assembler.embed(&lea_target, sizeof(lea_target));
              },
          .gen_relo_code =
              [](uintptr_t trampoline_address, uintptr_t trampoline_start,
                 uintptr_t target_start, size_t target_offset,
                 const RelocationEntry &relo, uintptr_t relocation_data,
                 asmjit::x86::Assembler &assembler) {
                auto code = assembler.code();
                auto code_data = assembler.code()->textSection();

                auto target_code = reinterpret_cast<uint8_t *>(
                    target_offset + code_data + trampoline_start +
                    relo.address);
                auto lea_target = reinterpret_cast<uint8_t *>(
                    target_code + relo.instruction.raw.disp.offset);

                // Turn the lea into a mov of a qword ptr, we'll load the
                // address from our data section below the instructions
                *(target_code + 0x1) = 0x8B;

                auto relo_lea_target = relocation_data + trampoline_start -
                                       relo.instruction.length - relo.address -
                                       target_offset;

                switch (relo.instruction.raw.disp.size) {
                case 8: {
                  assert(relo_lea_target < 0xFF &&
                         "displacement size too small for relocation");
                  *(int8_t *)(lea_target) = relo_lea_target;
                } break;
                case 16: {
                  *(int16_t *)(lea_target) = relo_lea_target;
                } break;
                case 32: {
                  *(int32_t *)(lea_target) = relo_lea_target;
                } break;
                case 64: {
                  *(int64_t *)(lea_target) = relo_lea_target;
                } break;
                }
                return target_offset;
              }}},
        {ZYDIS_MNEMONIC_ADDSD,
         {.size = kReloAddsdSize,
          .expand = kReloAddsdExpandSize,
          .gen_relo_data =
              [](auto target_start, auto &relo, auto &assembler) {
                const auto lea_target =
                    target_start + relo.address +
                    (relo.instruction.raw.disp.value + relo.instruction.length);
                assembler.embed(&lea_target, sizeof(lea_target));
              },
          .gen_relo_code =
              [](uintptr_t trampoline_address, uintptr_t trampoline_start,
                 uintptr_t target_start, size_t target_offset,
                 const RelocationEntry &relo, uintptr_t relocation_data,
                 asmjit::x86::Assembler &assembler) {
                using namespace asmjit;
                using namespace asmjit::x86;

                auto relo_lea_target = trampoline_address + relocation_data;
                assembler.push(r15);
                assembler.mov(r15, qword_ptr(relo_lea_target));
                assembler.addsd(
                    zydis_xmm_reg_to_asmjit(relo.operands[0].reg.value),
                    qword_ptr(r15));
                assembler.pop(r15);
                target_offset += kReloAddsdExpandSize;
                return target_offset;
              }}},
};

static std::vector<uint8_t> create_absolute_jump(uintptr_t target_address);
static size_t get_trampoline_size(std::span<uint8_t> target,
                                  const RelocationInfo &relocations);
static Trampoline create_trampoline(uintptr_t trampoline_address,
                                    uintptr_t return_address,
                                    std::span<uint8_t> target,
                                    const RelocationInfo &relocation_info);

static RelocationResult
do_far_relocations(std::span<uint8_t> target, uintptr_t trampoline_address,
                   const size_t &trampoline_start,
                   const RelocationInfo &relocation_info,
                   asmjit::CodeHolder &code, asmjit::x86::Assembler &assembler);

void detour::install() {
  //
  // Create a temp absolute jump
  // * For this we can use a rip+0 jump so we don't spoil a register
  //
  // Look at target location
  //
  // Figure out all the instructions that need relocating
  // * this might require us to go further than absolute jump
  //
  // Copy all instructions found in previous step, into new buffer
  // * New buffer, ideally is in 2GB range, if we can't do that we'll need
  // an intermediary jump/call table for far-ish jumps
  //
  // Relocate all instructions in that buffer to a designated target location
  // * This will be our trampoline, it'll need an absolute at the end as well
  //
  // Copy absolute jump to taget location
  auto jump = create_absolute_jump(func_);

  ZydisDecoder decoder;
  ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
  //

  // ZydisDecodedInstruction instruction;
  ZydisDisassembledInstruction instruction;
  ZydisDecoderContext context;
  uintptr_t decoder_offset = 0;
  intptr_t decode_length = 0x80;

  RelocationInfo relocation_info;

  auto needs_relocate =
      [](uintptr_t decoder_offset, intptr_t offset,
         ZydisDecodedInstruction &instruction,
         ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]) {
        if ((instruction.attributes & ZYDIS_ATTRIB_IS_RELATIVE) == 0) {
          return false;
        }

        const auto RUNTIME_ADDRESS = 0x7700000000;

        for (auto i = 0; i < instruction.operand_count; ++i) {
          ZyanU64 result = 0;
          const auto &operand = operands[i];
          if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(
                  &instruction, &operand, RUNTIME_ADDRESS + decoder_offset,
                  &result))) {
            const auto instruction_start = decoder_offset;
            const auto inside_trampoline = instruction_start <= offset;

            const auto reaches_into =
                (!inside_trampoline && result > RUNTIME_ADDRESS &&
                 result < (RUNTIME_ADDRESS + offset));
            const auto reaches_outof =
                (inside_trampoline && result > (RUNTIME_ADDRESS + offset));
            const auto need_relocate = (reaches_into || reaches_outof);

            if (need_relocate) {
              return true;
            }
          }
        }

        return false;
      };

  uintptr_t extend_trampoline_to = 0;
  uintptr_t relocation_offset = 0x0;

  while (decode_length >= 0 &&
         ZYAN_SUCCESS(ZydisDisassembleIntel(
             ZYDIS_MACHINE_MODE_LONG_64, 0,
             reinterpret_cast<void *>(address_ + decoder_offset), decode_length,
             &instruction))) {
    auto inst = instruction.info;

    relocation_info.relocation_offset[decoder_offset] = relocation_offset;
    if (needs_relocate(decoder_offset, extend_trampoline_to, inst,
                       instruction.operands)) {

      auto &r_meta = relo_meta.at(instruction.info);
      relocation_offset += r_meta.expand;

      relocation_info.relocations.emplace_back(
          decoder_offset, inst,
          std::array<ZydisDecodedOperand, 10>{
              instruction.operands[0], instruction.operands[1],
              instruction.operands[2], instruction.operands[3],
              instruction.operands[4], instruction.operands[5],
              instruction.operands[6], instruction.operands[7],
              instruction.operands[8], instruction.operands[9]});
      extend_trampoline_to =
          std::max(decoder_offset + inst.length, extend_trampoline_to);
    } else if (extend_trampoline_to < jump.size()) {
      extend_trampoline_to =
          std::max(decoder_offset + inst.length, extend_trampoline_to);
    }

    // TODO(alexander): Can we somehow detect a function end here and then stop?

    decoder_offset += inst.length;
    decode_length -= inst.length;
  }

  auto trampoline_size = get_trampoline_size(
      {reinterpret_cast<uint8_t *>(address_), extend_trampoline_to},
      relocation_info);

  auto trampoline_address = alloc_executable_memory(trampoline_size);
  assert(trampoline_address != nullptr);

  auto trampoline = create_trampoline(
      reinterpret_cast<uintptr_t>(trampoline_address),
      address_ + extend_trampoline_to,
      {reinterpret_cast<uint8_t *>(address_), extend_trampoline_to},
      relocation_info);

  // Meh, error checking and whatnot
  std::memcpy(trampoline_address, trampoline.data.data(),
              trampoline.data.size());

  trampoline_ =
      trampoline.start + reinterpret_cast<uintptr_t>(trampoline_address);

  {
    // TODO(alexander): Expand this copy stuff to encompass the entire
    // trampoline size that we copied from the original function
    //
    const auto copy_size = jump.size();
    original_func_data_.resize(copy_size);
    std::memcpy(original_func_data_.data(), reinterpret_cast<void *>(address_),
                copy_size);
    SPUD_SCOPED_PROTECTION(address_, copy_size,
                           mem_protection::READ_WRITE_EXECUTE);
    std::memcpy(reinterpret_cast<void *>(address_), jump.data(), copy_size);
  }
}

void detour::remove() {
  if (original_func_data_.size() == 0) {
    return;
  }

  SPUD_SCOPED_PROTECTION(address_, original_func_data_.size(),
                         mem_protection::READ_WRITE_EXECUTE);
  std::memcpy(reinterpret_cast<void *>(address_), original_func_data_.data(),
              original_func_data_.size());
  original_func_data_.clear();
}

static size_t get_trampoline_size(std::span<uint8_t> target,
                                  const RelocationInfo &relocation_info) {
  size_t required_space = target.size();

  auto target_start = reinterpret_cast<uintptr_t>(target.data());
  for (const auto &relo : relocation_info.relocations) {
    auto &r_meta = relo_meta.at(relo.instruction);
    required_space += r_meta.size;
  }
  required_space += kAbsoluteJumpSize;
  return required_space;
}

static Trampoline create_trampoline(uintptr_t trampoline_address,
                                    uintptr_t return_address,
                                    std::span<uint8_t> target,
                                    const RelocationInfo &relocations) {
  using namespace asmjit;
  using namespace asmjit::x86;

  CodeHolder code;
  code.init(Environment::host(), trampoline_address);
  x86::Assembler assembler(&code);

  const auto target_start = reinterpret_cast<uintptr_t>(target.data());
  const auto trampoline_start = code.textSection()->buffer().size();

  const auto is_far_relocate = true;

  if (is_far_relocate) {
    // We don't embed the data here as we are re-building instructions, so we
    // can't just copy it
    auto relocation_result =
        do_far_relocations(target, trampoline_address, trampoline_start,
                           relocations, code, assembler);
    assembler.embed(target.data() + relocation_result.copy_offset,
                    target.size() - relocation_result.copy_offset);
    assembler.jmp(ptr(rip, 0));
    assembler.embed(&return_address, sizeof(return_address));
    assembler.embed(relocation_result.data.data(),
                    relocation_result.data.size());
  } else {
    assembler.embed(target.data(), target.size());
    assembler.jmp(ptr(rip, 0));
    assembler.embed(&return_address, sizeof(return_address));
    assert(false && "Once we have 2gb relative allocation, we can use this "
                    "branch to generate a slighly faster version of "
                    "the trampoline");
  }

  auto &buffer = code.textSection()->buffer();
  return {trampoline_start, {buffer.begin(), buffer.end()}};
}

static RelocationResult do_far_relocations(
    std::span<uint8_t> target, uintptr_t trampoline_address,
    const size_t &trampoline_start, const RelocationInfo &relocation_info,
    asmjit::CodeHolder &code, asmjit::x86::Assembler &assembler) {
  using namespace asmjit;
  using namespace asmjit::x86;

  auto target_start = uintptr_t(target.data());

  std::vector<uintptr_t> relocation_data;

  size_t data_offset = target.size() + kAbsoluteJumpSize;

  for (const auto &relo : relocation_info.relocations) {
    auto &r_meta = relo_meta.at(relo.instruction);
    data_offset += r_meta.expand;
  }

  CodeHolder reloc_code;
  reloc_code.init(Environment::host(), data_offset); // Need base?
  x86::Assembler reloc_assembler(&reloc_code);

  for (const auto &relo : relocation_info.relocations) {
    relocation_data.emplace_back(data_offset +
                                 reloc_code.textSection()->bufferSize());

    auto &r_meta = relo_meta.at(relo.instruction);
    r_meta.gen_relo_data(target_start, relo, reloc_assembler);
  }

  size_t relocation_data_idx = 0;
  size_t copy_offset = 0;
  // This is for things like cmp that insert new instructions, so we are at the
  // right location in the buffer :)
  size_t target_offset = 0;
  for (const auto &relo : relocation_info.relocations) {
    auto &r_meta = relo_meta.at(relo.instruction);
    if (r_meta.expand == 0) {
      assembler.embed(target.data() + copy_offset,
                      relo.address + relo.instruction.length - copy_offset);
    } else {
      assembler.embed(target.data() + copy_offset, relo.address - copy_offset);
    }

    copy_offset = relo.address + relo.instruction.length;
    target_offset = r_meta.gen_relo_code(
        trampoline_address, trampoline_start, target_start, target_offset, relo,
        relocation_data[relocation_data_idx], assembler);
    ++relocation_data_idx;
  }

  auto &buffer = reloc_code.textSection()->buffer();
  return {{buffer.begin(), buffer.end()}, copy_offset};
}

static std::vector<uint8_t> create_absolute_jump(uintptr_t target_address) {
  using namespace asmjit;
  using namespace asmjit::x86;

  CodeHolder code;
  code.init(Environment::host());
  x86::Assembler assembler(&code);

  assembler.jmp(ptr(rip, 0));
  assembler.embed(&target_address, sizeof(target_address));

  auto &buffer = code.textSection()->buffer();
  return {buffer.begin(), buffer.end()};
}

} // namespace detail
} // namespace spud
