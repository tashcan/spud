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

struct signature_result {
  std::span<uint8_t> buffer;
  uintptr_t offset;
};

void generate_mask_and_data(std::string_view signature, std::string &mask,
                            std::string &data);
std::vector<signature_result>
find_matches(std::string_view mask, std::string_view data,
             std::span<uint8_t> search_buffer,
             uint32_t features = cpu_feature::FEATURE_ALL);
} // namespace detail

struct signature_matches {
  struct signature_match {
    signature_match(detail::signature_result result, size_t offset = 0)
        : result(result), adjustment(offset) {}

    uintptr_t address() const {
      return reinterpret_cast<uintptr_t>(result.buffer.data()) + offset();
    }

    uintptr_t extract_call() const {
      const auto data = result.buffer.data() + offset();
      return reinterpret_cast<uintptr_t>(data) +
             *reinterpret_cast<int32_t *>(data + 1u) + 5ul;
    }

    signature_match adjust(size_t offset) {
      return {result, adjustment + offset};
    }

  private:
    uintptr_t offset() const {
      return result.offset + adjustment;
    }

    detail::signature_result result;
    uintptr_t adjustment = 0;
  };

  signature_match get(size_t index) {
    return result[index];
  }

  signature_matches(std::vector<detail::signature_result> result = {})
      : result(result) {}

private:
  std::vector<detail::signature_result> result;
};

signature_matches find_matches(std::string_view signature,
                            std::span<uint8_t> search_buffer,
                            uint32_t features = cpu_feature::FEATURE_ALL);

#if SPUD_OS_WIN
signature_matches find_in_module(std::string_view signature,
                              std::string_view module = {},
                              uint32_t features = cpu_feature::FEATURE_ALL);
#endif

} // namespace spud
