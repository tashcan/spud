#pragma once

#include "detour/fwd.h"

#include <cstdint>
#include <span>
#include <tuple>
#include <vector>

namespace spud::detail::arm64 {
std::vector<uint8_t> create_absolute_jump(uintptr_t target, uintptr_t container);
std::tuple<RelocationInfo, size_t> collect_relocations(uintptr_t address,
                                                       size_t jump_size);
size_t get_trampoline_size(std::span<uint8_t> target,
                           const RelocationInfo &relocation);
Trampoline create_trampoline(uintptr_t trampoline_address,
                             uintptr_t return_address,
                             std::span<uint8_t> target,
                             const RelocationInfo &relocation_infos);
} // namespace spud::detail::arm64
