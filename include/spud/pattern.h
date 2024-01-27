#pragma once

#include <spud/arch.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace spud {

enum cpu_feature : uint32_t {
  FEATURE_NONE = 0,
  FEATURE_SSE42 = 1 << 1,
  FEATURE_AVX2 = 1 << 2,

  FEATURE_ALL = FEATURE_SSE42 | FEATURE_AVX2
};

namespace detail {

struct PatternResult {
  std::span<uint8_t> buffer;
  uintptr_t offset;
};

void generate_mask_and_data(std::string_view pattern, std::string &mask,
                            std::string &data);
std::vector<PatternResult>
find_matches(std::string_view mask, std::string_view data,
             std::span<uint8_t> search_buffer,
             uint32_t features = cpu_feature::FEATURE_ALL);
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
      return reinterpret_cast<uintptr_t>(data) +
             *reinterpret_cast<int32_t *>(data + 1u) + 5ul;
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

PatternMatches find_matches(std::string_view pattern,
                            std::span<uint8_t> search_buffer,
                            uint32_t features = cpu_feature::FEATURE_ALL);

#if SPUD_OS_WIN
PatternMatches find_in_module(std::string_view pattern,
                              std::string_view module = {},
                              uint32_t features = cpu_feature::FEATURE_ALL);
#endif

} // namespace spud
