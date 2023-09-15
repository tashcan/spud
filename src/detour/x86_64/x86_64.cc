#include "x86_64.h"
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
#include <unordered_map>
#include <variant>
#include <vector>

// Here shall be dragons, at some point

namespace spud {
namespace detail {
namespace x64 {

struct RelocationResult {
  std::vector<uint8_t> data;
  size_t copy_offset;
};

static RelocationResult
do_far_relocations(std::span<uint8_t> target,
                   const uintptr_t trampoline_address,
                   const RelocationInfo &relocation_info,
                   asmjit::CodeHolder &code, asmjit::x86::Assembler &assembler);

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
needs_relocate(uintptr_t decoder_offset, intptr_t code_end,
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

  return false;
};

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

  // ZydisDecodedInstruction instruction;
  ZydisDisassembledInstruction instruction;
  ZydisDecoderContext context;
  uintptr_t decoder_offset = 0;
  intptr_t decode_length = 0x40;

  RelocationInfo relocation_info;

  uintptr_t extend_trampoline_to = 0;
  uintptr_t relocation_offset = 0x0;

  std::vector<RelocationEntry> relocations;
  relocations.reserve(10);

  constexpr auto kRuntimeAddress = 0x0;

  while (decode_length >= 0 &&
         ZYAN_SUCCESS(ZydisDisassembleIntel(
             ZYDIS_MACHINE_MODE_LONG_64, kRuntimeAddress,
             reinterpret_cast<void *>(address + decoder_offset),
             decode_length - decoder_offset, &instruction))) {
    auto inst = instruction.info;

    relocation_info.relocation_offset[decoder_offset] = relocation_offset;

    if (is_jump(inst) || needs_relocate(decoder_offset, extend_trampoline_to,
                                        inst, instruction.operands)) {
      auto &r_meta = relo_meta.at(instruction.info);
      relocation_offset += r_meta.expand;

      relocations.emplace_back(RelocationEntry{
          decoder_offset, inst,
          std::array{instruction.operands[0], instruction.operands[1],
                     instruction.operands[2], instruction.operands[3],
                     instruction.operands[4], instruction.operands[5],
                     instruction.operands[6], instruction.operands[7],
                     instruction.operands[8], instruction.operands[9]}});
      extend_trampoline_to =
          std::max(decoder_offset + inst.length, extend_trampoline_to);
    } else if (extend_trampoline_to < jump_size) {
      extend_trampoline_to =
          std::max(decoder_offset + inst.length, extend_trampoline_to);
    }

    // TODO(alexander): Can we somehow detect a function end here and then stop?

    decoder_offset += inst.length;
  }

  relocations.erase(
      std::remove_if(begin(relocations), end(relocations),
                     [&](auto &v) {
                       return !(
                           is_jump(v.instruction) ||
                           needs_relocate(v.address, extend_trampoline_to - 1,
                                          v.instruction, v.operands.data()));
                     }),
      end(relocations));

  // Adjust relocation offsets for jump targets
  for (auto &relocation : relocations) {
    if (relocation.instruction.meta.branch_type == ZYDIS_BRANCH_TYPE_NONE)
      continue;

    ZyanU64 result;
    constexpr auto kRuntimeAddress = 0x7700000000;
    ZydisCalcAbsoluteAddress(&instruction.info, &relocation.operands[0],
                             kRuntimeAddress + decoder_offset, &result);

    const auto jump_in_trampoline =
        result > kRuntimeAddress &&
        result < (kRuntimeAddress + extend_trampoline_to);
    if (!jump_in_trampoline)
      continue;

    relocation_info.relocation_offset[relocation.address] = 0;
  }

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

Trampoline create_trampoline(uintptr_t trampoline_address,
                             uintptr_t return_address,
                             std::span<uint8_t> target,
                             const RelocationInfo &relocations) {
  using namespace asmjit;
  using namespace asmjit::x86;

  CodeHolder code;
  code.init(Environment::host(), trampoline_address);
  x86::Assembler assembler(&code);

  const auto is_far_relocate = true;

  if (is_far_relocate) {
    // We don't embed the data here as we are re-building instructions, so
    // we can't just copy it

    auto relocation_result = do_far_relocations(target, trampoline_address,
                                                relocations, code, assembler);
    assembler.embed(target.data() + relocation_result.copy_offset,
                    target.size() - relocation_result.copy_offset);
    assembler.jmp(ptr(rip, 0));
    assembler.embed(&return_address, sizeof(return_address));
    auto data_offset = assembler.code()->textSection()->bufferSize();
    data_offset = data_offset;
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

  uintptr_t decoder_offset = 0;
  intptr_t decode_length = 0x40;
  ZydisDisassembledInstruction instruction;

  return {0, {buffer.begin(), buffer.end()}};
}

static RelocationResult do_far_relocations(
    std::span<uint8_t> target, const uintptr_t trampoline_address,
    const RelocationInfo &relocation_info, asmjit::CodeHolder &code,
    asmjit::x86::Assembler &assembler) {
  using namespace asmjit;
  using namespace asmjit::x86;

  std::vector<uintptr_t> relocation_data;

  size_t relocation_data_offset = target.size() + 14;

  // TODO(alexander): This is effectively what get_trampoline_size does
  // so we should probably clean that up here at some point

  // Here we determine how much space we need for all the instructions to fit
  // as we already know how big our original code block was
  // we only have to extend this by the expand size of each relocation
  // After this we know how much space the instrcutions will take which allows
  // us to then place the relocation support data at the end
  for (const auto &relocation : relocation_info.relocations) {
    const auto &relo = std::get<x64::RelocationEntry>(relocation);
    const auto &r_meta = relo_meta.at(relo.instruction);

    relocation_data_offset += r_meta.expand;
  }

  CodeHolder reloc_code;
  reloc_code.init(Environment::host(), relocation_data_offset); // Need base?
  x86::Assembler reloc_assembler(&reloc_code);

  size_t relocation_data_idx = 0;
  size_t copy_offset = 0;

  for (const auto &relocation : relocation_info.relocations) {
    const auto &relo = std::get<x64::RelocationEntry>(relocation);
    const auto &r_meta = relo_meta.at(relo.instruction);

    relocation_data.emplace_back(relocation_data_offset +
                                 reloc_code.textSection()->bufferSize());
    const auto did_generate_data =
        r_meta.gen_relo_data(target, relo, reloc_assembler, relocation_info);

    if (r_meta.expand == 0) {
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
    r_meta.gen_relo_code(trampoline_address, target, relo,
                         relocation_info, did_generate_data,
                         relocation_data[relocation_data_idx], assembler);
    ++relocation_data_idx;
  }

  auto &buffer = reloc_code.textSection()->buffer();
  return {{buffer.begin(), buffer.end()}, copy_offset};
}

std::vector<uint8_t> create_absolute_jump(uintptr_t target_address,
                                          uintptr_t data) {
  using namespace asmjit;
  using namespace asmjit::x86;

  CodeHolder code;
  code.init(Environment{asmjit::Arch::kX64});
  Assembler assembler(&code);

  assembler.mov(r15, data);
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
