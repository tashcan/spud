#include "capstone/arm64.h"
#include "detour/detour_impl.h"
#include "relocators.h"

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

#include <algorithm>
#include <cstdint>
#include <span>
#include <tuple>
#include <vector>

namespace spud {
namespace detail {
namespace arm64 {

struct RelocationResult {
  std::vector<asmjit::Label> data_labels;
  size_t copy_offset;
  std::vector<std::pair<uintptr_t, uintptr_t>> relocation_offsets;
};

static void write_relocation_data(
    std::span<uint8_t> target, const relocation_info &relocation_info,
    std::vector<asmjit::Label> relocation_data,
    std::vector<std::pair<uintptr_t, uintptr_t>> relocation_offsets,
    asmjit::CodeHolder &code, asmjit::a64::Assembler &assembler);

static inline constexpr uint32_t kFullMask =
    std::numeric_limits<uint32_t>::max();
static void write_adjusted_target(size_t offset_bits, size_t size_bits,
                                  uint8_t *target_code, intptr_t target) {
  constexpr auto kBitsPerUnsigned = 32; // Total bits in an unsigned integer
  const uint8_t irrelevant_bits =
      kBitsPerUnsigned - size_bits; // Bits that don't fit
  const uint32_t current_integer_mask =
      (kFullMask >> irrelevant_bits) << offset_bits; // Mask for current integer

  uint32_t *current_integer =
      reinterpret_cast<uint32_t *>(target_code); // Pointer to current integer
  *current_integer = (*current_integer & ~current_integer_mask) |
                     ((target << offset_bits) &
                      current_integer_mask); // Update current integer
}

static void offset_target(auto size, auto target_code, auto target) {
  assert(false && "immediate size too small for relocation");
}

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

static bool needs_relocate(uintptr_t decoder_offset, uintptr_t code_end,
                           uintptr_t jump_size, const cs_insn &instruction,
                           const cs_aarch64 &detail) {

  auto has_group = [&](uint8_t group) {
    for (size_t i = 0; i < instruction.detail->groups_count; i++) {
      if (instruction.detail->groups[i] == group) {
        return true;
      }
    }
    return false;
  };
  if (instruction.id != ARM64_INS_ADRP && instruction.id != ARM64_INS_ADR &&
      !has_group(ARM64_GRP_BRANCH_RELATIVE) && !has_group(ARM64_GRP_JUMP) &&
      !has_group(ARM64_GRP_CALL)) {
    return false;
  }

  uintptr_t result = 0;
  constexpr auto kRuntimeAddress = 0x7700000000u;
  if (instruction.id == ARM64_INS_ADRP) {
    const auto &op = detail.operands[1];
    if (op.type == ARM64_OP_IMM) {
      const auto offset = op.imm;
      const auto target = instruction.address + offset;
      result = target;
    }
  } else if (instruction.id == ARM64_INS_ADR) {
    const auto &op = detail.operands[1];
    if (op.type == ARM64_OP_IMM) {
      const auto offset = op.imm;
      const auto target = instruction.address + offset;
      result = target;
    }
  } else if (instruction.detail->groups_count > 0) {
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
        result += instruction.address;
      }
    }
  }

  if (result == 0) {
    return false;
  }

  const auto instruction_start = decoder_offset;
  const auto inside_trampoline =
      instruction_start < code_end || instruction_start <= jump_size;

  const auto reaches_into = (!inside_trampoline && result > kRuntimeAddress &&
                             result <= (kRuntimeAddress + code_end));
  const auto reaches_outof =
      (inside_trampoline &&
       (result >= (kRuntimeAddress + code_end) || result < kRuntimeAddress));
  const auto need_relocate = (reaches_into || reaches_outof);

  return need_relocate;
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

  cs_open(CS_ARCH_AARCH64, CS_MODE_LITTLE_ENDIAN, &handle);
  cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
  auto count = cs_disasm(
      handle, reinterpret_cast<const uint8_t *>(address),
      std::max(std::min(uintptr_t(0xFF), (uintptr_t)vmsize - (address - addr)),
               uintptr_t(0xFFF)),
      address, 0, &insn);

  // Find the first branch instruction
  //
  // using captsone find all instructions that are rip relative and store the
  // offset of the instruction in the relocation_info
  //

  uintptr_t extend_trampoline_to = 0;

  if (count <= 0) {
    return {relocation_info, 0};
  }

  for (size_t j = 0; j < count; j++) {
    const auto decoder_offset = uintptr_t(insn[j].address - address);

    cs_detail *detail = insn[j].detail;
    if (detail == nullptr) {
      continue;
    }

    const auto &arm64 = detail->aarch64;
    if (needs_relocate(decoder_offset, extend_trampoline_to, jump_size, insn[j],
                       arm64)) {
      const auto entry = relocation_entry{decoder_offset, insn[j], arm64};
      relocation_info.relocations.emplace_back(entry);
      extend_trampoline_to =
          std::max(decoder_offset + insn[j].size, extend_trampoline_to);
    } else if (extend_trampoline_to < jump_size) {
      extend_trampoline_to =
          std::max(decoder_offset + insn[j].size, extend_trampoline_to);
    }
  }
  cs_free(insn, count);
  return {relocation_info, extend_trampoline_to};
#else
  return {relocation_info, jump_size};
#endif
}

static RelocationResult do_relocations(std::span<uint8_t> target,
                                       const relocation_info &relocation_info,
                                       asmjit::CodeHolder &code,
                                       asmjit::a64::Assembler &assembler);
trampoline_buffer create_trampoline(uintptr_t return_address,
                                    std::span<uint8_t> target,
                                    const relocation_info &relocations) {
  using namespace asmjit;
  using namespace asmjit::a64;

  CodeHolder code;
  code.init(Environment{asmjit::Arch::kAArch64}, 0x0);
  Assembler assembler(&code);

  const auto target_start = reinterpret_cast<uintptr_t>(target.data());

  // Do relocations
  auto relocation_result = do_relocations(target, relocations, code, assembler);

  Label L1 = assembler.newLabel();
  assembler.ldr(x17, ptr(L1));
  assembler.br(x17);
  assembler.bind(L1);
  assembler.embed(&return_address, sizeof(return_address));
  write_relocation_data(target, relocations, relocation_result.data_labels,
                        relocation_result.relocation_offsets, code, assembler);

  auto &buffer = code.textSection()->buffer();
  return {0, {buffer.begin(), buffer.end()}};
}

static void write_relocation_data(
    std::span<uint8_t> target, const relocation_info &relocation_info,
    std::vector<asmjit::Label> relocation_data,
    std::vector<std::pair<uintptr_t, uintptr_t>> relocation_offsets,
    asmjit::CodeHolder &code, asmjit::a64::Assembler &assembler) {
  size_t relocation_data_idx = 0;
  for (const auto &relocation : relocation_info.relocations) {
    const auto &relo = std::get<arm64::relocation_entry>(relocation);
    const auto &r_meta = get_relocator_for_instruction(relo.instruction);
    r_meta.gen_relo_data(target, relo, relocation_data[relocation_data_idx],
                         assembler, relocation_info);

    auto has_group = [&](uint8_t group) {
      for (size_t i = 0; i < relo.instruction.detail->groups_count; i++) {
        if (relo.instruction.detail->groups[i] == group) {
          return true;
        }
      }
      return false;
    };
    if (has_group(ARM64_GRP_BRANCH_RELATIVE) || has_group(ARM64_GRP_JUMP) ||
        has_group(ARM64_GRP_CALL)) {

      auto *code_data = code.textSection()->data();

      intptr_t data_offset_bits = 0;
      size_t data_size_bits = 0;
      size_t data_divider = 1;
      if (relo.instruction.id == ARM64_INS_BL) {
        data_offset_bits = 0;
        data_size_bits = 26;
        data_divider = 4;
      }

      auto target_code = reinterpret_cast<uint8_t *>(code_data);

      auto target_start = reinterpret_cast<uintptr_t>(target.data());
      auto target_end = target_start + target.size();

      uintptr_t jump_target = 0;

      const auto &detail = relo.instruction.detail->aarch64;
      if (detail.op_count == 1) {
        jump_target = detail.operands[0].imm;
      } else if (detail.op_count == 2) {
        jump_target = detail.operands[1].imm;
      } else if (detail.op_count == 3) {
        jump_target = detail.operands[2].imm;
      } else {
        assert(false && "Unhandled branch relative operand count");
      }
      if (jump_target == 0) {
        continue;
      }
      if (has_group(ARM64_GRP_BRANCH_RELATIVE)) {
        jump_target += relo.instruction.address;
      }

      uintptr_t relocated_location = 0u;
      for (auto &&[k, v] : relocation_offsets) {
        relocated_location = v;
        if (k >= relo.address)
          break;
      }
      relocated_location += relo.address;

      if (code.isLabelBound(relocation_data[relocation_data_idx])) {
        const auto label_jump_target =
            code.labelOffset(relocation_data[relocation_data_idx]);
        const auto new_target = label_jump_target - relocated_location;
        write_adjusted_target(data_offset_bits, data_size_bits,
                              target_code + relocated_location,
                              new_target / data_divider);
      } else {
        const auto target_offset_address = jump_target - target_start;

        uintptr_t offset = 0u;
        for (auto &&[k, v] : relocation_offsets) {
          offset = v;
          if (k >= target_offset_address)
            break;
        }

        uintptr_t preceeding_relo_offset = 0;
        for (auto &&[k, v] : relocation_offsets) {
          if (k >= target_offset_address)
            break;
          preceeding_relo_offset = v;
        }

        offset_target(data_size_bits, target_code + relocated_location,
                      offset - preceeding_relo_offset);
      }
    }
    ++relocation_data_idx;
  }
}

static RelocationResult do_relocations(std::span<uint8_t> target,
                                       const relocation_info &relocation_info,
                                       asmjit::CodeHolder &code,
                                       asmjit::a64::Assembler &assembler) {
  using namespace asmjit;
  using namespace asmjit::a64;

  std::vector<Label> relocation_data;
  relocation_data.reserve(10);

  size_t copy_offset = 0;

  std::vector<std::pair<uintptr_t, uintptr_t>> relocation_offsets;

  uintptr_t relocation_offset = 0u;
  for (const auto &relocation : relocation_info.relocations) {
    const auto &relo = std::get<arm64::relocation_entry>(relocation);
    const auto &r_meta = get_relocator_for_instruction(relo.instruction);
    auto data_label = assembler.newLabel();
    relocation_data.emplace_back(data_label);
    if (r_meta.copy_instruction) {
      // Embed everything up to here from the last end of instruction
      // Including the currently operated on instruction since we are going to
      // modify it
      assembler.embed(target.data() + copy_offset,
                      relo.address - copy_offset + relo.instruction.size);
    } else if ((relo.address - copy_offset) > 0) {
      // Embed everything up to here from the last end of instruction
      // This skips the currently operated on instruction
      assembler.embed(target.data() + copy_offset, relo.address - copy_offset);
    }

    // Place the cursor at the end of the instruction
    copy_offset = relo.address + relo.instruction.size;
    relocation_offsets.emplace_back(std::pair{relo.address, relocation_offset});
    r_meta.gen_relo_code(target, relo, relocation_info, data_label, assembler);
    const auto relocated_size = assembler.offset();
    relocation_offset += relocated_size - relo.address - relo.instruction.size;
  }

  return {relocation_data, copy_offset, relocation_offsets};
}

} // namespace arm64
} // namespace detail
} // namespace spud
