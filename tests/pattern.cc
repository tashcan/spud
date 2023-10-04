#include <catch2/catch_test_macros.hpp>

#include <spud/pattern.h>

#include "test.bin.h"

TEST_CASE("Search 2 byte pattern", "[simple]") {
  const auto pattern = "40 53";
  std::string mask;
  std::string data;
  spud::detail::generate_mask_and_data(pattern, mask, data);
  const auto result = spud::detail::find_matches(mask, data, test_bin);
  REQUIRE(result.size() == 15);
}

TEST_CASE("Search pattern with wildcard", "[simple]") {
  const auto pattern = "4C 89 44 24 ? 48 89 54 24";
  std::string mask;
  std::string data;
  spud::detail::generate_mask_and_data(pattern, mask, data);
  const auto result = spud::detail::find_matches(
      mask, data, test_bin, spud::cpu_feature::FEATURE_ALL);
  REQUIRE(result.size() == 1996);
}
