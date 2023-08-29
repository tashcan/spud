#pragma once

#include "aarch64/aarch64.h"
#include "x86_64/x86_64.h"

#include <cstdint>
#include <unordered_map>
#include <variant>
#include <vector>

namespace spud {
namespace detail {

struct Trampoline {
  uintptr_t start;
  std::vector<uint8_t> data;
};

using RelocationEntry = std::variant<x64::RelocationEntry>;

struct RelocationInfo {
  std::unordered_map<uintptr_t, uintptr_t> relocation_offset;
  std::vector<RelocationEntry> relocations;
};

} // namespace detail
} // namespace spud
