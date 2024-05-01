#include "detour/detour_impl.h"

#include <spud/detour.h>
#include <spud/memory/protection.h>
#include <spud/utils.h>

#include <asmjit/a64.h>
#include <asmjit/asmjit.h>
#include <capstone/capstone.h>

#if SPUD_OS_APPLE
#include <mach/mach_init.h>
#include <mach/mach_vm.h>
#include <mach/vm_map.h>
#endif

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace spud {
namespace detail {
namespace arm64 {

std::vector<uint8_t> create_absolute_jump(uintptr_t target_address,
                                          uintptr_t data) {
  using namespace asmjit;
  using namespace asmjit::a64;

  CodeHolder code;
  code.init(Environment{asmjit::Arch::kAArch64});
  Assembler assembler(&code);

  Label L1 = assembler.newLabel();
  assembler.ldr(x9, ptr(L1));
  assembler.mov(x11, data);
  assembler.br(x9);
  assembler.bind(L1);
  assembler.embed(&target_address, sizeof(target_address));

  auto &buffer = code.textSection()->buffer();
  return {buffer.begin(), buffer.end()};
}

std::tuple<relocation_info, size_t> collect_relocations(uintptr_t address,
                                                        size_t jump_size) {

  relocation_info relocation_info;

#if SPUD_OS_APPLE
  // Fill
  csh handle;
  cs_insn *insn;
  vm_size_t vmsize = 0;
  vm_address_t addr = (vm_address_t)address;
  vm_region_basic_info_data_64_t info;
  mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
  memory_object_name_t object;

  kern_return_t status =
      vm_region_64(mach_task_self(), &addr, &vmsize, VM_REGION_BASIC_INFO_64,
                   (vm_region_info_t)&info, &info_count, &object);

  printf("Region 0x%" PRIu64 "(0x%" PRIu64 ") S: 0x%" PRIu64 " \n", address, addr, vmsize - (address- addr));
  cs_open(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, &handle);
  auto count = cs_disasm(handle, reinterpret_cast<const uint8_t *>(address), vmsize - (address - addr),
                         address, 0, &insn);

  // Find the first branch instruction
  //
  // using captsone find all instructions that are rip relative and store the
  // offset of the instruction in the relocation_info
  //
  for (size_t j = 0; j < count; j++) {
    cs_detail *detail = insn[j].detail;
    if (detail == nullptr) {
        continue;
    }
    cs_arm64 &arm64 = detail->arm64;
    const auto opcount = arm64.op_count;
    for (int k = 0; k < opcount; k++) {
      printf("%d\n", k);
      if (arm64.operands[k].type == ARM64_OP_MEM) {
        printf("\t%s\n", insn[j].op_str);
        break;
      }
    }
  }

#endif
  return {relocation_info, jump_size};
}

trampoline_buffer create_trampoline(uintptr_t return_address,
                                    std::span<uint8_t> target,
                                    const relocation_info &relocation_infos) {
  using namespace asmjit;
  using namespace asmjit::a64;

  CodeHolder code;
  code.init(Environment{asmjit::Arch::kAArch64}, 0x0);
  Assembler assembler(&code);

  const auto target_start = reinterpret_cast<uintptr_t>(target.data());
  const auto trampoline_start = code.textSection()->buffer().size();

  assembler.embed(target.data(), target.size());
  Label L1 = assembler.newLabel();
  assembler.ldr(x17, ptr(L1));
  assembler.br(x17);
  assembler.bind(L1);
  assembler.embed(&return_address, sizeof(return_address));

  auto &buffer = code.textSection()->buffer();
  return {trampoline_start, {buffer.begin(), buffer.end()}};
}

} // namespace arm64
} // namespace detail
} // namespace spud
