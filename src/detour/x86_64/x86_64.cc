#include "x86_64.h"
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
do_far_relocations(std::span<uint8_t> target, uintptr_t trampoline_address,
                   const size_t &trampoline_start,
                   const RelocationInfo &relocation_info,
                   asmjit::CodeHolder &code, asmjit::x86::Assembler &assembler);

static bool
needs_relocate(uintptr_t decoder_offset, intptr_t offset,
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
      const auto inside_trampoline = instruction_start <= offset;

      const auto reaches_into =
          (!inside_trampoline && result > kRuntimeAddress &&
           result < (kRuntimeAddress + offset));
      const auto reaches_outof =
          (inside_trampoline &&
           (result > (kRuntimeAddress + offset) || result < kRuntimeAddress));
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
    if (needs_relocate(decoder_offset, extend_trampoline_to, inst,
                       instruction.operands)) {

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

  relocations.erase(std::remove_if(begin(relocations), end(relocations),
                                   [&](auto &v) {
                                     return !needs_relocate(
                                         v.address, extend_trampoline_to,
                                         v.instruction, v.operands.data());
                                   }),
                    end(relocations));

  relocation_info.relocations = {relocations.begin(), relocations.end()};

  return {relocation_info, extend_trampoline_to};
}

size_t get_trampoline_size(std::span<uint8_t> target,
                           const RelocationInfo &relocation_info) {
  size_t required_space = target.size();

  auto target_start = reinterpret_cast<uintptr_t>(target.data());
  for (const auto &relot : relocation_info.relocations) {
    const auto &relo = std::get<RelocationEntry>(relot);
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

  for (const auto &relot : relocation_info.relocations) {
    const auto &relo = std::get<x64::RelocationEntry>(relot);
    const auto &r_meta = relo_meta.at(relo.instruction);

    data_offset += r_meta.expand;
  }

  CodeHolder reloc_code;
  reloc_code.init(Environment::host(), data_offset); // Need base?
  x86::Assembler reloc_assembler(&reloc_code);

  for (const auto &relot : relocation_info.relocations) {
    const auto &relo = std::get<x64::RelocationEntry>(relot);
    const auto &r_meta = relo_meta.at(relo.instruction);

    relocation_data.emplace_back(data_offset +
                                 reloc_code.textSection()->bufferSize());
    r_meta.gen_relo_data(target_start, relo, reloc_assembler);
  }

  size_t relocation_data_idx = 0;
  size_t copy_offset = 0;
  // This is for things like cmp that insert new instructions, so we are at the
  // right location in the buffer :)
  size_t target_offset = 0;
  for (const auto &relot : relocation_info.relocations) {
    const auto &relo = std::get<x64::RelocationEntry>(relot);
    const auto &r_meta = relo_meta.at(relo.instruction);

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

std::vector<uint8_t> create_absolute_jump(uintptr_t target_address) {
  using namespace asmjit;
  using namespace asmjit::x86;

  CodeHolder code;
  code.init(Environment{asmjit::Arch::kX64});
  Assembler assembler(&code);

  assembler.jmp(ptr(rip, 0));
  assembler.embed(&target_address, sizeof(target_address));

  auto &buffer = code.textSection()->buffer();
  return {buffer.begin(), buffer.end()};
}

uintptr_t maybe_resolve_jump(uintptr_t address) {
  uint8_t *memory = reinterpret_cast<uint8_t *>(address);
  if (memory[0] != 0xE9) {
    return address;
  }

  uint32_t offset = 0;

  std::memcpy(&offset, &memory[1], sizeof(offset));

  auto result = address + offset + 5;
  result = result;
  return result;
}

} // namespace x64
} // namespace detail
} // namespace spud
