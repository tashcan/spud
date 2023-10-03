#pragma once

#include <spud/arch.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace spud {
namespace detail {

struct PatternResult {
  std::span<uint8_t> buffer;
  uintptr_t offset;
};

void generate_mask_and_data(std::string_view pattern, std::string &mask,
                            std::string &data);
std::vector<PatternResult> find_matches(std::string_view mask,
                                        std::string_view data,
                                        std::span<uint8_t> search_buffer);
} // namespace detail

struct PatternMatches {
  struct PatternMatch {
    PatternMatch(detail::PatternResult result, size_t offset = 0)
        : result(result), adjustment(offset) {}

    uintptr_t address() const {
      return reinterpret_cast<uintptr_t>(result.buffer.data()) + offset();
    }

    uintptr_t extract_call() const {
      const auto data = result.buffer.data() + offset();
      return reinterpret_cast<uintptr_t>(data + *(int32_t *)(data + 1) + 5);
    }

    PatternMatch adjust(size_t offset) {
      return {result, adjustment + offset};
    }

  private:
    uintptr_t offset() const {
      return result.offset + adjustment;
    }

    detail::PatternResult result;
    uintptr_t adjustment = 0;
  };

  PatternMatch get(size_t index) {
    return result[index];
  }

  PatternMatches(std::vector<detail::PatternResult> result = {})
      : result(result) {}

private:
  std::vector<detail::PatternResult> result;
};

#if SPUD_OS_WIN
PatternMatches find_in_module(std::string_view pattern,
                              std::string_view module = {});
#else
#endif

} // namespace spud
