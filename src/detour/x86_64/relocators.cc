#include "relocators.h"

#include "detour/detour_impl.h"

#include <cassert>

namespace spud::detail::x64 {

using namespace asmjit;
using namespace asmjit::x86;
/*
    push r15
    mov r15, qword ptr [0x10]
    cmp byte ptr [r15], 1
    pop r15
    0x00000000000
*/
constexpr auto kReloCompareSize = 16 + sizeof(uintptr_t);
constexpr auto kReloCompareExpandSize =
    kReloCompareSize - 8 - sizeof(uintptr_t);

/*
    push   r15
    mov    r15, qword ptr[0x10]
    addsd  xmm3,mmword ptr [r15]
    pop    r15
    0x00000000000
*/
constexpr auto kReloAddsdSize = 17 + sizeof(uintptr_t);
constexpr auto kReloAddsdExpandSize = kReloAddsdSize - 9 - sizeof(uintptr_t);

constexpr auto kReloMovzxSize = 16 + sizeof(uintptr_t);
constexpr auto kReloMovzxExpandSize = kReloAddsdSize - 10 - sizeof(uintptr_t);

constexpr auto kReloMovSize = 15 + sizeof(uintptr_t);
constexpr auto kReloMovExpandSize = kReloMovSize - 7 - sizeof(uintptr_t);

static void write_adjusted_target(auto size, auto target_code, auto target) {
  switch (size) {
  case 8: {
    assert(target < 0xFF && "immediate size too small for relocation");
    *(int8_t *)(target_code) = target;
  } break;
  case 16: {
    *(int16_t *)(target_code) = target;
  } break;
  case 32: {
    *(int32_t *)(target_code) = target;
  } break;
  case 64: {
    *(int64_t *)(target_code) = target;
  } break;
  }
}

static void offset_target(auto size, auto target_code, auto target) {
  switch (size) {
  case 8: {
    assert(target < 0xFF && "immediate size too small for relocation");
    *(int8_t *)(target_code) += target;
  } break;
  case 16: {
    *(int16_t *)(target_code) += target;
  } break;
  case 32: {
    *(int32_t *)(target_code) += target;
  } break;
  case 64: {
    *(int64_t *)(target_code) += target;
  } break;
  }
}


/* ZyanU64
CalcAbsoluteAddressForRelocation(const RelocationEntry &relo,
                                 const RelocationInfo &relocation_info, ) {
  const auto &instruction = relo.instruction;
  const auto &operands = relo.operands;

  for (auto i = 0; i < instruction.operand_count; ++i) {
    ZyanU64 result = 0;
    const auto &operand = operands[i];
    if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&instruction, &operand,
                                              kRuntimeAddress + decoder_offset,
                                              &result))) {
      const auto instruction_start = decoder_offset;
      const auto inside_trampoline = instruction_start <= code_end;

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
}
*/

const std::unordered_map<ReloInstruction, RelocationMeta, ReloInstructionHasher>
    relo_meta = {
        {JUMP_RELO_JMP_INSTRUCTION,
         {.size = sizeof(uintptr_t),
          .expand = 0,
          .gen_relo_data =
              [](auto target, auto &relo, auto &assembler, auto &relo_info) {
                auto target_start = reinterpret_cast<uintptr_t>(target.data());
                auto target_end = target_start + target.size();

                ZyanU64 jump_target = 0;
                if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(
                        &relo.instruction, &relo.operands[0],
                        target_start + relo.address, &jump_target))) {
                  const auto inside_target =
                      jump_target >= target_start && jump_target <= target_end;
                  if (!inside_target) {
                    assembler.jmp(ptr(rip, 0));
                    assembler.embed(&jump_target, sizeof(jump_target));
                    return true;
                  }
                  return false;
                } else {
                  assert(false && "Failed to calculate absolute target jump address");
                }
                return false;
              },
          .gen_relo_code =
              [](uintptr_t trampoline_address, std::span<uint8_t> target,
                 const RelocationEntry &relo,
                 const RelocationInfo &relocation_info, bool has_data,
                 uintptr_t relocation_data, asmjit::x86::Assembler &assembler) {
                auto *code = assembler.code();
                auto *code_data = code->textSection()->data();

                auto target_code = reinterpret_cast<uint8_t *>(code_data +
                    assembler.code()->textSection()->bufferSize() - relo.instruction.length +
                    relo.instruction.raw.imm[0].offset);
              
                if (has_data) {
                    const auto jump_target =
                        relocation_data -
                      assembler.code()->textSection()->bufferSize();
                  write_adjusted_target(relo.instruction.raw.imm[0].size,
                                        target_code, jump_target);
                } else {
                  auto target_start =
                      reinterpret_cast<uintptr_t>(target.data());
                  auto target_end = target_start + target.size();
                  ZyanU64 jump_target = 0;
                  if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(
                          &relo.instruction, &relo.operands[0],
                          target_start + relo.address, &jump_target))) {
                    const auto target_offset_address =
                        jump_target - target_start;
                    const auto offset =
                        relocation_info.relocation_offset.contains(
                            target_offset_address)
                            ? relocation_info.relocation_offset.at(
                                  target_offset_address)
                            : 0;

                    uintptr_t preceeding_relo_offset = 0;
                    for (auto &&[k, v] : relocation_info.relocation_offset) {
                      if (k >= target_offset_address)
                        break;
                      preceeding_relo_offset = v;
                    }

                    offset_target(relo.instruction.raw.imm[0].size, target_code,
                                  offset - preceeding_relo_offset);
                  }
                }

              }}},
        {ZYDIS_MNEMONIC_CMP,
         {.size = kReloCompareSize,
          .expand = kReloCompareExpandSize,
          .gen_relo_data =
              [](auto target, auto &relo, auto &assembler, auto &relo_info) {
                auto target_start = reinterpret_cast<uintptr_t>(target.data());
                auto target_end = target_start + target.size();

                ZyanU64 jump_target = 0;
                ZydisCalcAbsoluteAddress(&relo.instruction, &relo.operands[0],
                                         target_start + relo.address,
                                         &jump_target);
                const auto lea_target =
                    target_start + relo.address +
                    (relo.instruction.raw.disp.value + relo.instruction.length);
                assembler.embed(&lea_target, sizeof(lea_target));
                return true;
              },
          .gen_relo_code =
              [](uintptr_t trampoline_address, std::span<uint8_t> target,
                 const RelocationEntry &relo,
                 const RelocationInfo &relocation_info, bool has_data,
                 uintptr_t relocation_data, asmjit::x86::Assembler &assembler) {
                const auto relo_lea_target = relocation_data;
                assembler.push(r15);
                assembler.mov(r15, qword_ptr(relo_lea_target));
                assembler.cmp(byte_ptr(r15),
                              relo.operands[1].imm.is_signed
                                  ? relo.operands[1].imm.value.s
                                  : relo.operands[1].imm.value.u);
                assembler.pop(r15);
              }}},
        {ZYDIS_MNEMONIC_LEA,
         {.size = sizeof(uintptr_t),
          .expand = 0,
          .gen_relo_data =
              [](auto target, auto &relo, auto &assembler, auto &relo_info) {
                auto target_start = reinterpret_cast<uintptr_t>(target.data());
                auto target_end = target_start + target.size();

                const auto lea_target =
                    target_start + relo.address +
                    (relo.instruction.raw.disp.value + relo.instruction.length);
                assembler.embed(&lea_target, sizeof(lea_target));
                return true;
              },
          .gen_relo_code =
              [](uintptr_t trampoline_address, std::span<uint8_t> target,
                 const RelocationEntry &relo,
                 const RelocationInfo &relocation_info, bool has_data,
                 uintptr_t relocation_data, asmjit::x86::Assembler &assembler) {
                auto *code = assembler.code();
                auto *code_data = code->textSection()->data();

                auto target_code = reinterpret_cast<uint8_t *>(
                    assembler.code()->textSection()->bufferSize() + code_data);
                auto lea_target = reinterpret_cast<uint8_t *>(
                    target_code + relo.instruction.raw.disp.offset);

                // Turn the lea into a mov of a qword ptr, we'll load the
                // address from our data section below the instructions
                *(target_code + 0x1) = 0x8B;

                auto relo_lea_target = relocation_data - relo.instruction.length -
                    assembler.code()->textSection()->bufferSize();

                write_adjusted_target(relo.instruction.raw.disp.size,
                                      lea_target, relo_lea_target);
              }}},
        {ZYDIS_MNEMONIC_ADDSD,
         {.size = kReloAddsdSize,
          .expand = kReloAddsdExpandSize,
          .gen_relo_data =
              [](auto target, auto &relo, auto &assembler, auto &relo_info) {
                auto target_start = reinterpret_cast<uintptr_t>(target.data());
                auto target_end = target_start + target.size();

                const auto lea_target =
                    target_start + relo.address +
                    (relo.instruction.raw.disp.value + relo.instruction.length);
                assembler.embed(&lea_target, sizeof(lea_target));
                return true;
              },
          .gen_relo_code =
              [](uintptr_t trampoline_address, std::span<uint8_t> target,
                 const RelocationEntry &relo,
                 const RelocationInfo &relocation_info, bool has_data,
                 uintptr_t relocation_data, asmjit::x86::Assembler &assembler) {
                auto relo_lea_target = trampoline_address + relocation_data;
                assembler.push(r15);
                assembler.mov(r15, qword_ptr(relo_lea_target));
                assembler.addsd(
                    zydis_xmm_reg_to_asmjit(relo.operands[0].reg.value),
                    qword_ptr(r15));
                assembler.pop(r15);
              }}},
        {ZYDIS_MNEMONIC_MOV,
         {.size = kReloMovSize,
          .expand = kReloMovExpandSize,
          .gen_relo_data =
              [](auto target, auto &relo, auto &assembler, auto &relo_info) {
                auto target_start = reinterpret_cast<uintptr_t>(target.data());
                auto target_end = target_start + target.size();
                const auto lea_target = target_start + ZyanI64(relo.address) +
                                        relo.instruction.length +
                                        relo.operands[0].mem.disp.value;
                assembler.embed(&lea_target, sizeof(lea_target));
                return true;
              },
          .gen_relo_code =
              [](uintptr_t trampoline_address, std::span<uint8_t> target,
                 const RelocationEntry &relo,
                 const RelocationInfo &relocation_info, bool has_data,
                 uintptr_t relocation_data, asmjit::x86::Assembler &assembler) {
                auto relo_lea_target = trampoline_address + relocation_data;
                assembler.push(r15);
                assembler.mov(r15, qword_ptr(relo_lea_target));
                assembler.mov(byte_ptr(r15), relo.operands[1].imm.value.s);
                assembler.pop(r15);
              }}},
        {ZYDIS_MNEMONIC_MOVZX,
         {.size = kReloMovzxSize,
          .expand = kReloMovzxExpandSize,
          .gen_relo_data =
              [](auto target, auto &relo, auto &assembler, auto &relo_info) {
                auto target_start = reinterpret_cast<uintptr_t>(target.data());
                auto target_end = target_start + target.size();

                const auto lea_target =
                    target_start + relo.address +
                    (relo.instruction.raw.disp.value + relo.instruction.length);
                assembler.embed(&lea_target, sizeof(lea_target));
                return true;
              },
          .gen_relo_code =
              [](uintptr_t trampoline_address, std::span<uint8_t> target,
                 const RelocationEntry &relo,
                 const RelocationInfo &relocation_info, bool has_data,
                 uintptr_t relocation_data, asmjit::x86::Assembler &assembler) {
                auto relo_lea_target = trampoline_address + relocation_data;
                assembler.push(r15);
                assembler.mov(r15, qword_ptr(relo_lea_target));
                assembler.mov(zydis_d_reg_to_asmjit(relo.operands[0].reg.value),
                              byte_ptr(r15));
                assembler.pop(r15);
              }}}};

} // namespace spud::detail::x64
