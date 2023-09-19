#include "relocators.h"

#include "detour/detour_impl.h"

#include <cassert>

namespace spud::detail::x64 {

using namespace asmjit;
using namespace asmjit::x86;
/*
    push r11
    mov r11, qword ptr [0x10]
    cmp byte ptr [r11], 1
    pop r11
    0x00000000000
*/
constexpr auto kReloCompareSize = 16 + sizeof(uintptr_t);
constexpr auto kReloCompareExpandSize =
    kReloCompareSize - 8 - sizeof(uintptr_t);

/*
    push   r11
    mov    r11, qword ptr[0x10]
    addsd  xmm3,mmword ptr [r11]
    pop    r11
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

bool write_absolute_address(auto target, auto &relo, auto &assembler,
                            auto &relo_info) {
  auto target_start = reinterpret_cast<uintptr_t>(target.data());
  ZyanU64 absolute_target = 0;
  for (auto i = 0; i < relo.instruction.operand_count; ++i) {
    ZyanU64 result = 0;
    const auto &operand = relo.operands[i];
    if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY) {
      if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&relo.instruction, &operand,
                                                target_start + relo.address,
                                                &absolute_target))) {
        assembler.embed(&absolute_target, sizeof(absolute_target));
      } else {
        assert(false);
      }
    }
  }
  return true;
}

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
                  assert(false &&
                         "Failed to calculate absolute target jump address");
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

                const auto data_offset =
                    relo.instruction.raw.imm[0].size > 0
                        ? relo.instruction.raw.imm[0].offset
                        : relo.instruction.raw.disp.offset;
                const auto data_size = relo.instruction.raw.imm[0].size > 0
                                           ? relo.instruction.raw.imm[0].size
                                           : relo.instruction.raw.disp.size;

                auto target_code = reinterpret_cast<uint8_t *>(
                    code_data + assembler.code()->textSection()->bufferSize() -
                    relo.instruction.length + data_offset);

                if (has_data) {
                  const auto jump_target =
                      relocation_data -
                      assembler.code()->textSection()->bufferSize();
                  write_adjusted_target(data_size, target_code, jump_target);
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

                    offset_target(data_size, target_code,
                                  offset - preceeding_relo_offset);
                  }
                }
              },
          .copy_instruction = true}},
        {ZYDIS_MNEMONIC_CMP,
         {.size = kReloCompareSize,
          .expand = kReloCompareExpandSize,
          .gen_relo_data = write_absolute_address,
          .gen_relo_code =
              [](uintptr_t trampoline_address, std::span<uint8_t> target,
                 const RelocationEntry &relo,
                 const RelocationInfo &relocation_info, bool has_data,
                 uintptr_t relocation_data, asmjit::x86::Assembler &assembler) {
                const auto relo_lea_target =
                    trampoline_address + relocation_data;
                assembler.push(r11);
                assembler.mov(r11, qword_ptr(relo_lea_target));
                assembler.cmp(byte_ptr(r11),
                              relo.operands[1].imm.is_signed
                                  ? relo.operands[1].imm.value.s
                                  : relo.operands[1].imm.value.u);
                assembler.pop(r11);
              }}},
        {ZYDIS_MNEMONIC_LEA,
         {.size = sizeof(uintptr_t),
          .expand = 0,
          .gen_relo_data = write_absolute_address,
          .gen_relo_code =
              [](uintptr_t trampoline_address, std::span<uint8_t> target,
                 const RelocationEntry &relo,
                 const RelocationInfo &relocation_info, bool has_data,
                 uintptr_t relocation_data, asmjit::x86::Assembler &assembler) {
                auto *code = assembler.code();
                auto *code_data = code->textSection()->data();
                assembler.mov(zydis_q_reg_to_asmjit(relo.operands[0].reg.value),
                              qword_ptr(trampoline_address + relocation_data));
              },
          .copy_instruction = false}},
        {ZYDIS_MNEMONIC_ADDSD,
         {.size = kReloAddsdSize,
          .expand = kReloAddsdExpandSize,
          .gen_relo_data = write_absolute_address,
          .gen_relo_code =
              [](uintptr_t trampoline_address, std::span<uint8_t> target,
                 const RelocationEntry &relo,
                 const RelocationInfo &relocation_info, bool has_data,
                 uintptr_t relocation_data, asmjit::x86::Assembler &assembler) {
                auto relo_lea_target = trampoline_address + relocation_data;
                assembler.push(r11);
                assembler.mov(r11, qword_ptr(relo_lea_target));
                assembler.addsd(
                    zydis_xmm_reg_to_asmjit(relo.operands[0].reg.value),
                    qword_ptr(r11));
                assembler.pop(r11);
              }}},
        {ZYDIS_MNEMONIC_MOV,
         {.size = kReloMovSize,
          .expand = kReloMovExpandSize,
          .gen_relo_data = write_absolute_address,
          .gen_relo_code =
              [](uintptr_t trampoline_address, std::span<uint8_t> target,
                 const RelocationEntry &relo,
                 const RelocationInfo &relocation_info, bool has_data,
                 uintptr_t relocation_data, asmjit::x86::Assembler &assembler) {
                auto relo_lea_target = trampoline_address + relocation_data;
                assembler.push(r11);
                assembler.mov(r11, qword_ptr(relo_lea_target));
                assembler.mov(byte_ptr(r11), relo.operands[1].imm.value.s);
                assembler.pop(r11);
              }}},
        {ZYDIS_MNEMONIC_MOVZX,
         {.size = kReloMovzxSize,
          .expand = kReloMovzxExpandSize,
          .gen_relo_data = write_absolute_address,
          .gen_relo_code =
              [](uintptr_t trampoline_address, std::span<uint8_t> target,
                 const RelocationEntry &relo,
                 const RelocationInfo &relocation_info, bool has_data,
                 uintptr_t relocation_data, asmjit::x86::Assembler &assembler) {
                auto relo_lea_target = trampoline_address + relocation_data;
                assembler.push(r11);
                assembler.mov(r11, qword_ptr(relo_lea_target));
                assembler.mov(zydis_d_reg_to_asmjit(relo.operands[0].reg.value),
                              byte_ptr(r11));
                assembler.pop(r11);
              }}}};

} // namespace spud::detail::x64
