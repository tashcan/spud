#include <catch2/catch_test_macros.hpp>

#include <spud/detour.h>

#include <cstdio>

#include <test_util.h>

static bool hook_ran_shared_basic = false;
static bool condition_intact_for_hook_shared_basic = false;

SPUD_NO_INLINE static void test_function_shared_basic(int n) {
  if (n == 0) {
    condition_intact_for_hook_shared_basic = true;
    return;
  }
  return;
}

SPUD_NO_INLINE static void hook_shared_basic(auto original, int n) {
  hook_ran_shared_basic = true;
  original(0);
}

// Make sure test_function is not inlined here...this happens on GCC
auto *test_function_shared_basic_ptr = &test_function_shared_basic;
TEST_CASE("Simple Hook with a condition", "[detour:shared:basic]") {
  hook_ran_shared_basic = false;
  auto t = SPUD_AUTO_HOOK(test_function_shared_basic_ptr, hook_shared_basic);
  t.install();
  test_function_shared_basic_ptr(1);
  REQUIRE(hook_ran_shared_basic == true);
  REQUIRE(condition_intact_for_hook_shared_basic == true);
}
