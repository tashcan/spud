#include "relocators.h"

#include "Zydis/DecoderTypes.h"
#include "Zydis/SharedTypes.h"
#include "asmjit/core/operand.h"
#include "asmjit/x86/x86assembler.h"
#include "asmjit/x86/x86operand.h"
#include "detour/detour_impl.h"

#include <Zydis/Encoder.h>

#include <cassert>

namespace spud::detail::x64 {

using namespace asmjit;
using namespace asmjit::x86;

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

void write_absolute_address(auto target, auto &relo, auto data_label,
                            Assembler &assembler, auto &relo_info) {
  auto target_start = reinterpret_cast<uintptr_t>(target.data());
  ZyanU64 absolute_target = 0;
  for (auto i = 0; i < relo.instruction.operand_count; ++i) {
    ZyanU64 result = 0;
    const auto &operand = relo.operands[i];
    if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY) {
      if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&relo.instruction, &operand,
                                                target_start + relo.address,
                                                &absolute_target))) {
        assembler.bind(data_label);
        assembler.embed(&absolute_target, sizeof(absolute_target));
        return;
      } else {
        assert(false);
      }
    }
  }
}

const static RelocationMeta generic_relocator = {
    .size = sizeof(uintptr_t),
    .gen_relo_data = write_absolute_address,
    .gen_relo_code = [](std::span<uint8_t> target, const RelocationEntry &relo,
                        const RelocationInfo &relocation_info,
                        asmjit::Label relocation_data,
                        asmjit::x86::Assembler &assembler) {
      auto instruction = relo.instruction;
      auto operands = relo.operands;
      ZydisEncoderRequest request;
      ZydisEncoderOperand *req_operand = nullptr;

      // We currently don't support instructions here that have more than 2
      // operands
      assert(instruction.operand_count_visible <= 2);
      ZydisEncoderDecodedInstructionToEncoderRequest(
          &instruction, operands.data(), instruction.operand_count_visible,
          &request);
      asmjit::x86::Gpq scratch_register = r11;
      if (request.operands[0].type == ZYDIS_OPERAND_TYPE_MEMORY) {
        req_operand = &request.operands[0];
        if (request.operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER) {
          if (request.operands[1].reg.value == ZYDIS_REGISTER_R11 ||
              request.operands[1].reg.value == ZYDIS_REGISTER_R11B ||
              request.operands[1].reg.value == ZYDIS_REGISTER_R11D ||
              request.operands[1].reg.value == ZYDIS_REGISTER_R11W) {
            scratch_register = r10;
          }
        }
      } else {
        req_operand = &request.operands[1];
        if (request.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER) {
          if (request.operands[0].reg.value == ZYDIS_REGISTER_R11 ||
              request.operands[0].reg.value == ZYDIS_REGISTER_R11B ||
              request.operands[0].reg.value == ZYDIS_REGISTER_R11D ||
              request.operands[0].reg.value == ZYDIS_REGISTER_R11W) {
            scratch_register = r10;
          }
        }
      }
      req_operand->type = ZYDIS_OPERAND_TYPE_MEMORY;
      req_operand->mem.base =
          scratch_register == r11 ? ZYDIS_REGISTER_R11 : ZYDIS_REGISTER_R10;
      req_operand->mem.index = ZYDIS_REGISTER_NONE;
      req_operand->mem.scale = 0;
      req_operand->mem.displacement = 0;
      req_operand->mem.size =
          request.operands[0].type == ZYDIS_OPERAND_TYPE_MEMORY
              ? operands[0].size
              : operands[1].size;

      std::array<uint8_t, 15> buffer;
      size_t buffer_length = buffer.size();
      if (ZYAN_SUCCESS(ZydisEncoderEncodeInstruction(&request, buffer.data(),
                                                     &buffer_length))) {
        assembler.push(scratch_register);
        assembler.mov(scratch_register, qword_ptr(relocation_data));
        assembler.embed(&buffer, buffer_length);
        assembler.pop(scratch_register);
      } else {
        assert(false);
      }
    }};

const static RelocationMeta jump_relocator = {
    .size = sizeof(uintptr_t),
    .gen_relo_data =
        [](auto target, auto &relo, auto data_label,
           asmjit::x86::Assembler &assembler, auto &relo_info) {
          auto target_start = reinterpret_cast<uintptr_t>(target.data());
          auto target_end = target_start + target.size();

          ZyanU64 jump_target = 0;
          if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(
                  &relo.instruction, &relo.operands[0],
                  target_start + relo.address, &jump_target))) {
            const auto inside_target =
                jump_target >= target_start && jump_target <= target_end;
            if (!inside_target) {
              assembler.bind(data_label);
              assembler.jmp(ptr(rip, 0));
              assembler.embed(&jump_target, sizeof(jump_target));
            }
          } else {
            assert(false && "Failed to calculate absolute target jump address");
          }
        },
    .gen_relo_code =
        [](std::span<uint8_t> target, const RelocationEntry &relo,
           const RelocationInfo &relocation_info, asmjit::Label relocation_data,
           asmjit::x86::Assembler &assembler) {
        },
    .copy_instruction = true};

const static RelocationMeta lea_relocator = {
    .size = sizeof(uintptr_t),
    .gen_relo_data = write_absolute_address,
    .gen_relo_code =
        [](std::span<uint8_t> target, const RelocationEntry &relo,
           const RelocationInfo &relocation_info, asmjit::Label relocation_data,
           asmjit::x86::Assembler &assembler) {
          auto *code = assembler.code();
          auto *code_data = code->textSection()->data();
          assembler.mov(zydis_reg_to_asmjit(relo.operands[0].reg.value),
                        qword_ptr(relocation_data));
        },
    .copy_instruction = false};

const std::unordered_map<ReloInstruction, RelocationMeta, ReloInstructionHasher>
    relo_meta = {{JUMP_RELO_JMP_INSTRUCTION, jump_relocator},
                 {ZYDIS_MNEMONIC_CMP, generic_relocator},
                 {ZYDIS_MNEMONIC_LEA, lea_relocator},
                 {ZYDIS_MNEMONIC_ADDSD, generic_relocator},
                 {ZYDIS_MNEMONIC_MOV, generic_relocator},
                 {ZYDIS_MNEMONIC_MOVZX, generic_relocator}};

} // namespace spud::detail::x64
