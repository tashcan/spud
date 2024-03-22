#include <catch2/catch_test_macros.hpp>

#include <spud/signature.h>

#include "test.bin.h"

TEST_CASE("Search 2 byte signature", "[simple]") {
  const auto signature = "40 53";
  std::string mask;
  std::string data;
  spud::detail::generate_mask_and_data(signature, mask, data);
  const auto result = spud::detail::find_matches(
      mask, data, test_bin, spud::cpu_feature::FEATURE_NONE);
  REQUIRE(result.size() == 15);
}

#if SPUD_ARCH_X86_FAMILY
TEST_CASE("Search 2 byte signature SSE4.2", "[simple]") {
  const auto signature = "40 53";
  std::string mask;
  std::string data;
  spud::detail::generate_mask_and_data(signature, mask, data);
  const auto result = spud::detail::find_matches(
      mask, data, test_bin, spud::cpu_feature::FEATURE_SSE42);
  REQUIRE(result.size() == 15);
}

TEST_CASE("Search 2 byte signature AVX2", "[simple]") {
  const auto signature = "40 53";
  std::string mask;
  std::string data;
  spud::detail::generate_mask_and_data(signature, mask, data);
  const auto result = spud::detail::find_matches(
      mask, data, test_bin, spud::cpu_feature::FEATURE_AVX2);
  REQUIRE(result.size() == 15);
}
#elif SPUD_ARCH_ARM_FAMILY
TEST_CASE("Search 2 byte signature NEON", "[simple]") {
  const auto signature = "40 53";
  std::string mask;
  std::string data;
  spud::detail::generate_mask_and_data(signature, mask, data);
  const auto result = spud::detail::find_matches(
      mask, data, test_bin, spud::cpu_feature::FEATURE_NEON);
  REQUIRE(result.size() == 15);
}
#else
#error Unsupported Architecture
#endif

TEST_CASE("Search signature with wildcard", "[simple]") {
  const auto signature = "4C 89 44 24 ? 48 89 54 24";
  std::string mask;
  std::string data;
  spud::detail::generate_mask_and_data(signature, mask, data);
  const auto result = spud::detail::find_matches(
      mask, data, test_bin, spud::cpu_feature::FEATURE_NONE);
  REQUIRE(result.size() == 1996);
}

#if SPUD_ARCH_X86_FAMILY
TEST_CASE("Search signature with wildcard SSE4.2", "[simple]") {
  const auto signature = "4C 89 44 24 ? 48 89 54 24";
  std::string mask;
  std::string data;
  spud::detail::generate_mask_and_data(signature, mask, data);
  const auto result = spud::detail::find_matches(
      mask, data, test_bin, spud::cpu_feature::FEATURE_SSE42);
  REQUIRE(result.size() == 1996);
}

TEST_CASE("Search signature with wildcard AVX2", "[simple]") {
  const auto signature = "4C 89 44 24 ? 48 89 54 24";
  std::string mask;
  std::string data;
  spud::detail::generate_mask_and_data(signature, mask, data);
  const auto result = spud::detail::find_matches(
      mask, data, test_bin, spud::cpu_feature::FEATURE_AVX2);
  REQUIRE(result.size() == 1996);
}
#elif SPUD_ARCH_ARM_FAMILY
TEST_CASE("Search signature with wildcard NEON", "[simple]") {
  const auto signature = "4C 89 44 24 ? 48 89 54 24";
  std::string mask;
  std::string data;
  spud::detail::generate_mask_and_data(signature, mask, data);
  const auto result = spud::detail::find_matches(
      mask, data, test_bin, spud::cpu_feature::FEATURE_NEON);
  REQUIRE(result.size() == 1996);
}
#else
#error Unsupported Architecture
#endif
