#pragma once

#include "detour/fwd.h"

#include <zydis_utils.h>

#include <Zydis/Utils.h>
#include <Zydis/Zydis.h>

#include <array>
#include <cstdint>
#include <span>
#include <tuple>
#include <vector>

namespace spud::detail::x64 {

struct RelocationEntry {
  uintptr_t address;
  ZydisDecodedInstruction instruction;
  std::array<ZydisDecodedOperand, ZYDIS_MAX_OPERAND_COUNT> operands;
};

std::vector<uint8_t> create_absolute_jump(uintptr_t target);
std::tuple<RelocationInfo, size_t> collect_relocations(uintptr_t address,
                                                       size_t jump_size);
size_t get_trampoline_size(std::span<uint8_t> target,
                           const RelocationInfo &relocation);
Trampoline create_trampoline(uintptr_t trampoline_address,
                             uintptr_t return_address,
                             std::span<uint8_t> target,
                             const RelocationInfo &relocation_infos);
uintptr_t maybe_resolve_jump(uintptr_t);

} // namespace spud::detail::x64
