#pragma once

#include "aarch64/aarch64.h"
#include "x86_64/x86_64.h"

#include <cstdint>
#include <map>
#include <variant>
#include <vector>

namespace spud {
namespace detail {

struct trampoline_buffer {
  uintptr_t start;
  std::vector<uint8_t> data;
};

using relocation_entry = std::variant<x64::relocation_entry>;

struct relocation_info {
  std::vector<relocation_entry> relocations;
};

} // namespace detail
} // namespace spud
