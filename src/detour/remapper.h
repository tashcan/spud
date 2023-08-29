#pragma once

#include <cstddef>
#include <cstdint>

namespace spud::detail {
struct Remapper {
  Remapper(uintptr_t address, size_t size);
  ~Remapper();

  operator uintptr_t() {
    return remap_;
  }

private:
  uintptr_t address_;
  uintptr_t remap_;
  size_t size_;
};
} // namespace spud::detail
