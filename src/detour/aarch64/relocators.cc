#include "relocators.h"
#include "capstone/arm64.h"
#include "detour/detour_impl.h"

#include <spud/utils.h>

#include <Zydis/Encoder.h>

#include <cassert>

namespace spud::detail::arm64 {

using namespace asmjit;
using namespace asmjit::a64;

static void write_absolute_address(auto target, auto &relo, auto data_label,
                                   Assembler &assembler, auto &) {
  auto target_start = reinterpret_cast<uintptr_t>(target.data());
  ZyanU64 absolute_target = 0;
  const auto &op = relo.detail.operands[1];
  if (op.type == ARM64_OP_IMM) {
    const auto offset = op.imm;
    const auto target = relo.instruction.address + offset;
    assembler.bind(data_label);
    assembler.embed(&offset, sizeof(offset));
  }
}

const static relocation_meta generic_relocator = {
    .size = sizeof(uintptr_t),
    .gen_relo_data = write_absolute_address,
    .gen_relo_code = [](std::span<uint8_t>, const relocation_entry &relo,
                        const relocation_info &, asmjit::Label relocation_data,
                        Assembler &assembler) {
      auto instruction = relo.instruction;

      const auto &detail = relo.detail;
      const auto &op = detail.operands[0];

      assembler.str(x20, Mem{sp, -16});
      assembler.ldr(x20, Mem{relocation_data, 0});

      switch (op.reg) {
      case ARM64_REG_X0:
        assembler.mov(x0, x20);
        break;
      case ARM64_REG_X1:
        assembler.mov(x1, x20);
        break;
      case ARM64_REG_X2:
        assembler.mov(x2, x20);
        break;
      case ARM64_REG_X3:
        assembler.mov(x3, x20);
        break;
      case ARM64_REG_X4:
        assembler.mov(x4, x20);
        break;
      case ARM64_REG_X5:
        assembler.mov(x5, x20);
        break;
      case ARM64_REG_X6:
        assembler.mov(x6, x20);
        break;
      case ARM64_REG_X7:
        assembler.mov(x7, x20);
        break;
      case ARM64_REG_X8:
        assembler.mov(x8, x20);
        break;
      case ARM64_REG_X9:
        assembler.mov(x9, x20);
        break;
      case ARM64_REG_X10:
        assembler.mov(x10, x20);
        break;
      case ARM64_REG_X11:
        assembler.mov(x11, x20);
        break;
      case ARM64_REG_X12:
        assembler.mov(x12, x20);
        break;
      case ARM64_REG_X13:
        assembler.mov(x13, x20);
        break;
      case ARM64_REG_X14:
        assembler.mov(x14, x20);
        break;
      case ARM64_REG_X15:
        assembler.mov(x15, x20);
        break;
      case ARM64_REG_X16:
        assembler.mov(x16, x20);
        break;
      case ARM64_REG_X17:
        assembler.mov(x17, x20);
        break;
      case ARM64_REG_X18:
        assembler.mov(x18, x20);
        break;
      case ARM64_REG_X19:
        assembler.mov(x19, x20);
        break;
      case ARM64_REG_X20:
        assembler.mov(x20, x20);
        break;
      case ARM64_REG_X21:
        assembler.mov(x21, x20);
        break;
      case ARM64_REG_X22:
        assembler.mov(x22, x20);
        break;
      case ARM64_REG_X23:
        assembler.mov(x23, x20);
        break;
      case ARM64_REG_X24:
        assembler.mov(x24, x20);
        break;
      case ARM64_REG_X25:
        assembler.mov(x25, x20);
        break;
      case ARM64_REG_X26:
        assembler.mov(x26, x20);
        break;
      case ARM64_REG_X27:
        assembler.mov(x27, x20);
        break;
      case ARM64_REG_X28:
        assembler.mov(x28, x20);
        break;
      default:
        assert(false && "unsupported register");
      }
      assembler.ldr(x20, Mem{sp, -16});
    }};

const static relocation_meta branch_relocator = {
    .size = sizeof(uintptr_t),
    .gen_relo_data =
        [](auto target, auto &relo, auto data_label, Assembler &assembler,
           auto &) {
          auto label_error = assembler.bind(data_label);
          ASMJIT_ASSERT(label_error == kErrorOk);
          SPUD_UNUSED(label_error);
          printf("Generating branch relocation\n");
          auto has_group = [&](uint8_t group) {
            for (size_t i = 0; i < relo.instruction.detail->groups_count; i++) {
              if (relo.instruction.detail->groups[i] == group) {
                return true;
              }
            }
            return false;
          };
          auto &detail = relo.detail;
          uintptr_t result = 0;
          if (has_group(ARM64_GRP_BRANCH_RELATIVE) ||
              has_group(ARM64_GRP_JUMP) || has_group(ARM64_GRP_CALL)) {
            if (detail.op_count == 1) {
              result = detail.operands[0].imm;
            } else if (detail.op_count == 2) {
              result = detail.operands[1].imm;
            } else if (detail.op_count == 3) {
              result = detail.operands[2].imm;
            } else {
              assert(false && "Unhandled branch relative operand count");
            }
            if (has_group(ARM64_GRP_BRANCH_RELATIVE)) {
              result += relo.instruction.address;
            }
          }
          if (result % 4 == 0) {
            printf("Jumpng to %p\n", result);
            assembler.bl((target.data() - result));
          } else {
            printf("Fallback jump, infinite loop");
            assembler.bl(data_label);
          }
        },
    .gen_relo_code = [](std::span<uint8_t>, const relocation_entry &relo,
                        const relocation_info &, asmjit::Label relocation_data,
                        Assembler &assembler) {},
    .copy_instruction = true};

const relocation_meta &
get_relocator_for_instruction(const cs_insn &instruction) {
  auto has_group = [&](uint8_t group) {
    for (size_t i = 0; i < instruction.detail->groups_count; i++) {
      if (instruction.detail->groups[i] == group) {
        return true;
      }
    }
    return false;
  };
  if (has_group(ARM64_GRP_BRANCH_RELATIVE) || has_group(ARM64_GRP_JUMP) ||
      has_group(ARM64_GRP_CALL)) {
    return branch_relocator;
  }
  return generic_relocator;
}

} // namespace spud::detail::arm64
