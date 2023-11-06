#include "x86_64.h"
#include "Zycore/Status.h"
#include "Zycore/Types.h"
#include "Zydis/DecoderTypes.h"
#include "detour/detour_impl.h"
#include "relocators.h"

#include <spud/detour.h>
#include <spud/memory/protection.h>
#include <spud/utils.h>

// Private helper stuff
#include <zydis_utils.h>

#include <Zydis/Utils.h>
#include <Zydis/Zydis.h>

#include <asmjit/asmjit.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <variant>
#include <vector>

// Here shall be dragons, at some point

namespace spud {
namespace detail {
namespace x64 {

struct RelocationResult {
  std::vector<asmjit::Label> data_labels;
  size_t copy_offset;
};

static RelocationResult
do_far_relocations(std::span<uint8_t> target,
                   const RelocationInfo &relocation_info,
                   asmjit::CodeHolder &code, asmjit::x86::Assembler &assembler);
static void write_relocation_data(std::span<uint8_t> target,
                                  const RelocationInfo &relocation_info,
                                  std::vector<asmjit::Label> relocation_data,
                                  asmjit::CodeHolder &code,
                                  asmjit::x86::Assembler &assembler);

static bool is_jump(ZydisDecodedInstruction &instruction) {
  // TODO(alexander): This is less than ideal
  // actually we need relative plus jump?
  const auto non_ret_branch =
      (instruction.meta.branch_type != ZYDIS_BRANCH_TYPE_NONE) &&
      (instruction.mnemonic != ZYDIS_MNEMONIC_RET);

  if (non_ret_branch) {
    assert((instruction.attributes & ZYDIS_ATTRIB_IS_RELATIVE) != 0);
  }
  return non_ret_branch;
}

static bool
needs_relocate(uintptr_t decoder_offset, intptr_t code_end, intptr_t jump_size,
               ZydisDecodedInstruction &instruction,
               ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]) {

  if ((instruction.attributes & ZYDIS_ATTRIB_IS_RELATIVE) == 0) {
    return false;
  }

  constexpr auto kRuntimeAddress = 0x7700000000;

  for (auto i = 0; i < instruction.operand_count; ++i) {
    ZyanU64 result = 0;
    const auto &operand = operands[i];
    if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&instruction, &operand,
                                              kRuntimeAddress + decoder_offset,
                                              &result))) {
      const auto instruction_start = decoder_offset;
      const auto inside_trampoline =
          instruction_start < code_end || instruction_start <= jump_size;

      const auto reaches_into =
          (!inside_trampoline && result > kRuntimeAddress &&
           result <= (kRuntimeAddress + code_end));
      const auto reaches_outof =
          (inside_trampoline && (result >= (kRuntimeAddress + code_end) ||
                                 result < kRuntimeAddress));
      const auto need_relocate = (reaches_into || reaches_outof);

      if (need_relocate) {
        return true;
      }
    }
  }

  return false;
};

static inline bool DecodeInstruction(const ZydisDecoder *decoder,
                                     const void *buffer, ZyanUSize length,
                                     ZydisDecodedInstruction *instruction,
                                     ZydisDecodedOperand *operands) {
  ZydisDecoderContext ctx;
  if (ZYAN_FAILED(ZydisDecoderDecodeInstruction(decoder, &ctx, buffer, length,
                                                instruction))) {
    return false;
  }
  if (ZYAN_FAILED(ZydisDecoderDecodeOperands(
          decoder, &ctx, instruction, operands, instruction->operand_count))) {
    return false;
  }
  return true;
}

std::tuple<RelocationInfo, size_t> collect_relocations(uintptr_t address,
                                                       size_t jump_size) {
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
  ZydisDecoder decoder;
  ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
  //

  ZydisDecodedInstruction instruction;
  ZydisDecoderContext context;
  uintptr_t decoder_offset = 0;
  intptr_t decode_length = 0x40;

  RelocationInfo relocation_info;

  uintptr_t extend_trampoline_to = 0;

  std::vector<RelocationEntry> relocations;
  relocations.reserve(10);

  constexpr auto kRuntimeAddress = 0x0;

  ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
  while (decode_length >= 0 &&
         DecodeInstruction(
             &decoder, reinterpret_cast<void *>(address + decoder_offset),
             decode_length - decoder_offset, &instruction, operands)) {

    if (needs_relocate(decoder_offset, extend_trampoline_to, jump_size,
                       instruction, operands)) {
      auto entry = RelocationEntry{
          decoder_offset, instruction,
          std::array{operands[0], operands[1], operands[2], operands[3],
                     operands[4], operands[5], operands[6], operands[7],
                     operands[8], operands[9]}};

      auto relocation_meta_info = relo_meta.find(entry.instruction);
      if (relocation_meta_info == relo_meta.end()) {
        char text[50] = {};
        ZydisFormatter formatter;
        ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);
        ZydisFormatterFormatInstruction(&formatter, &instruction, operands,
                                        instruction.operand_count_visible, text,
                                        sizeof(text), kRuntimeAddress,
                                        ZYAN_NULL);
        fprintf(stderr,
                "Instruction that needs relocation is missing relocator: %s\n",
                text);
      }

      const auto &r_meta = relocation_meta_info->second;

      relocations.emplace_back(entry);
      extend_trampoline_to =
          std::max(decoder_offset + instruction.length, extend_trampoline_to);
    } else if (extend_trampoline_to < jump_size) {
      extend_trampoline_to =
          std::max(decoder_offset + instruction.length, extend_trampoline_to);
    }

    decoder_offset += instruction.length;
  }

  // Remove everything that doesn't actually end up needing relocations after we
  // have determined the total trampoline size
  relocations.erase(
      std::remove_if(begin(relocations), end(relocations),
                     [&](auto &v) {
                       return !(is_jump(v.instruction) ||
                                needs_relocate(v.address,
                                               extend_trampoline_to - 1,
                                               jump_size, v.instruction,
                                               v.operands.data()));
                     }),
      end(relocations));

  relocation_info.relocations = {relocations.begin(), relocations.end()};

  return {relocation_info, extend_trampoline_to};
}

size_t get_trampoline_size(std::span<uint8_t> target,
                           const RelocationInfo &relocation_info) {
  size_t required_space = target.size();
  for (const auto &relocation : relocation_info.relocations) {
    const auto &relo = std::get<RelocationEntry>(relocation);
    const auto &r_meta = relo_meta.at(relo.instruction);

    required_space += r_meta.size;
  }
  required_space += kAbsoluteJumpSize;
  return required_space;
}

Trampoline create_trampoline(uintptr_t return_address,
                             std::span<uint8_t> target,
                             const RelocationInfo &relocations) {
  using namespace asmjit;
  using namespace asmjit::x86;

  CodeHolder code;
  code.init(Environment::host(), 0);
  x86::Assembler assembler(&code);

  const auto is_far_relocate = true;

  if (is_far_relocate) {
    // We don't embed the data here as we are re-building instructions, so
    // we can't just copy it
    auto relocation_result =
        do_far_relocations(target, relocations, code, assembler);
    assembler.embed(target.data() + relocation_result.copy_offset,
                    target.size() - relocation_result.copy_offset);
    assembler.jmp(ptr(rip, 0));
    assembler.embed(&return_address, sizeof(return_address));
    auto data_offset = assembler.code()->textSection()->bufferSize();
    data_offset = data_offset;
    write_relocation_data(target, relocations, relocation_result.data_labels,
                          code, assembler);
  } else {
    assembler.embed(target.data(), target.size());
    assembler.jmp(ptr(rip, 0));
    assembler.embed(&return_address, sizeof(return_address));
    assert(false && "Once we have 2gb relative allocation, we can use this "
                    "branch to generate a slighly faster version of "
                    "the trampoline");
  }

  auto &buffer = code.textSection()->buffer();

  uintptr_t decoder_offset = 0;
  intptr_t decode_length = 0x40;
  ZydisDisassembledInstruction instruction;

  return {0, {buffer.begin(), buffer.end()}};
}

static RelocationResult do_far_relocations(
    std::span<uint8_t> target, const RelocationInfo &relocation_info,
    asmjit::CodeHolder &code, asmjit::x86::Assembler &assembler) {
  using namespace asmjit;
  using namespace asmjit::x86;

  std::vector<Label> relocation_data;
  relocation_data.reserve(10);

  size_t relocation_data_offset = target.size() + 14;

  // TODO(alexander): This is effectively what get_trampoline_size does
  // so we should probably clean that up here at some point

  size_t relocation_data_idx = 0;
  size_t copy_offset = 0;

  for (const auto &relocation : relocation_info.relocations) {
    const auto &relo = std::get<x64::RelocationEntry>(relocation);
    const auto &r_meta = relo_meta.at(relo.instruction);
    auto data_label = assembler.newLabel();
    relocation_data.emplace_back(data_label);
    if (r_meta.copy_instruction) {
      // Embed everything up to here from the last end of instruction
      // Including the currently operated on instruction since we are going to
      // modify it
      assembler.embed(target.data() + copy_offset,
                      relo.address - copy_offset + relo.instruction.length);
    } else if ((relo.address - copy_offset) > 0) {
      // Embed everything up to here from the last end of instruction
      // This skips the currently operated on instruction
      assembler.embed(target.data() + copy_offset, relo.address - copy_offset);
    }

    // Place the cursor at the end of the instruction
    copy_offset = relo.address + relo.instruction.length;
    r_meta.gen_relo_code(target, relo, relocation_info, data_label, assembler);
  }

  return {relocation_data, copy_offset};
}

static void write_relocation_data(std::span<uint8_t> target,
                                  const RelocationInfo &relocation_info,
                                  std::vector<asmjit::Label> relocation_data,
                                  asmjit::CodeHolder &code,
                                  asmjit::x86::Assembler &assembler) {
  size_t relocation_data_idx = 0;
  for (const auto &relocation : relocation_info.relocations) {
    const auto &relo = std::get<x64::RelocationEntry>(relocation);
    const auto &r_meta = relo_meta.at(relo.instruction);
    r_meta.gen_relo_data(target, relo, relocation_data[relocation_data_idx],
                         assembler, relocation_info);
    ++relocation_data_idx;
  }
}

std::vector<uint8_t> create_absolute_jump(uintptr_t target_address,
                                          uintptr_t data) {
  using namespace asmjit;
  using namespace asmjit::x86;

  CodeHolder code;
  code.init(Environment{asmjit::Arch::kX64});
  Assembler assembler(&code);

  // assembler.int3();
  // assembler.push(r14);
  // assembler.mov(r14, data);
  // assembler.mov(qword_ptr(r14), r15);
  // assembler.mov(r15, r14);
  // assembler.pop(r14);

  assembler.mov(r11, data);
  assembler.jmp(ptr(rip, 0));
  assembler.embed(&target_address, sizeof(target_address));

  auto &buffer = code.textSection()->buffer();
  return {buffer.begin(), buffer.end()};
}

uintptr_t maybe_resolve_jump(uintptr_t address) {
  const uint8_t *memory = reinterpret_cast<const uint8_t *>(address);
  if (memory[0] != 0xE9) {
    return address;
  }

  uint32_t offset = 0;
  std::memcpy(&offset, &memory[1], sizeof(offset));
  return address + offset + 5;
}

} // namespace x64
} // namespace detail
} // namespace spud
