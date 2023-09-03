#include "relocators.h"

#include "detour/detour_impl.h"

#include <cassert>

namespace spud::detail::x64 {

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

constexpr auto kReloMovSize = 16 + sizeof(uintptr_t);
constexpr auto kReloMovExpandSize = kReloAddsdSize - 9 - sizeof(uintptr_t);

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

const std::unordered_map<ReloInstruction, RelocationMeta, ReloInstructionHasher>
    relo_meta = {
        {JUMP_RELO_JMP_INSTRUCTION,
         {.size = sizeof(uintptr_t),
          .expand = 0,
          .gen_relo_data =
              [](auto target_start, auto &relo, auto &assembler,
                 auto &relo_info) {
                using namespace asmjit;
                using namespace asmjit::x86;

                ZyanU64 jump_target = 0;
                ZydisCalcAbsoluteAddress(&relo.instruction, &relo.operands[0],
                                         target_start + relo.address,
                                         &jump_target);

                assembler.jmp(ptr(rip, 0));
                printf("Magical %p\n",
                       jump_target - target_start - relo.address);
                try {
                  auto offset = relo_info.relocation_offset.at(
                      jump_target - target_start - relo.address);
                  printf("offset %p\n", offset);
                  jump_target += offset;
                } catch (...) {
                }
                printf("Target %p\n", jump_target);
                assembler.embed(&jump_target, sizeof(jump_target));
              },
          .gen_relo_code =
              [](uintptr_t trampoline_address, uintptr_t trampoline_start,
                 uintptr_t target_start, size_t target_offset,
                 const RelocationEntry &relo, uintptr_t relocation_data,
                 asmjit::x86::Assembler &assembler) {
                auto *code = assembler.code();
                auto *code_data = code->textSection()->data();

                auto target_code = reinterpret_cast<uint8_t *>(
                    target_offset + code_data + trampoline_start +
                    relo.address + relo.instruction.raw.imm[0].offset);

                auto jump_target = relocation_data + trampoline_start -
                                   relo.instruction.length - relo.address -
                                   target_offset;

                write_adjusted_target(relo.instruction.raw.imm[0].size,
                                      target_code, jump_target);

                return target_offset;
              }}},
        {ZYDIS_MNEMONIC_CMP,
         {.size = kReloCompareSize,
          .expand = kReloCompareExpandSize,
          .gen_relo_data =
              [](auto target_start, auto &relo, auto &assembler,
                 auto &relo_info) {
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
              [](auto target_start, auto &relo, auto &assembler,
                 auto &relo_info) {
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
                auto *code = assembler.code();
                auto *code_data = code->textSection()->data();

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

                write_adjusted_target(relo.instruction.raw.disp.size,
                                      lea_target, relo_lea_target);

                return target_offset;
              }}},
        {ZYDIS_MNEMONIC_ADDSD,
         {.size = kReloAddsdSize,
          .expand = kReloAddsdExpandSize,
          .gen_relo_data =
              [](auto target_start, auto &relo, auto &assembler,
                 auto &relo_info) {
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
        {ZYDIS_MNEMONIC_MOV,
         {.size = kReloMovSize,
          .expand = kReloMovExpandSize,
          .gen_relo_data =
              [](auto target_start, auto &relo, auto &assembler,
                 auto &relo_info) {
                const auto lea_target = target_start + ZyanI64(relo.address) +
                                        relo.instruction.length +
                                        relo.operands[0].mem.disp.value;
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
                assembler.mov(byte_ptr(r15), relo.operands[1].imm.value.s);
                assembler.pop(r15);
                target_offset += kReloAddsdExpandSize;
                return target_offset;
              }}},
        {ZYDIS_MNEMONIC_MOVZX,
         {.size = kReloMovzxSize,
          .expand = kReloMovzxExpandSize,
          .gen_relo_data =
              [](auto target_start, auto &relo, auto &assembler,
                 auto &relo_info) {
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
                assembler.mov(zydis_d_reg_to_asmjit(relo.operands[0].reg.value),
                              byte_ptr(r15));
                assembler.pop(r15);
                target_offset += kReloMovzxExpandSize;
                return target_offset;
              }}}};

} // namespace spud::detail::x64
