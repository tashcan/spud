#include <catch2/catch_test_macros.hpp>

#include <spud/detour.h>

#include <cstdio>

#include <test_util.h>

extern "C" void ASM_FUNC(mov1, ());
extern "C" bool ASM_NAME(msg);

extern "C" void ASM_FUNC(mov2, (int));
extern "C" bool ASM_NAME(msg2);

extern "C" void ASM_FUNC(mov3, (int));
extern "C" bool ASM_NAME(msg3);

void test_function(int n);
decltype(test_function) *o_test_function = nullptr;

bool hook_ran = false;
bool condition_intact_for_hook = false;

SPUD_NO_INLINE void test_function(int n) {
  if (n == 0) {
    condition_intact_for_hook = true;
    return;
  }
}

void hook(int n) {
  hook_ran = true;
  o_test_function(0);
  return o_test_function(2);
}

void hook_mov1() {}

TEST_CASE("Simple Hook with a condition asm", "[simple]") {
  REQUIRE(msg == false);
  mov1();
  REQUIRE(msg == true);
  msg = false;
  {
    auto t = spud::create_detour(&mov1, &hook_mov1);
    t.install();
    t.trampoline();
    mov1();
    REQUIRE(msg == false);
  }
  mov1();
  REQUIRE(msg == true);
}

// Make sure test_function is not inlined here...this happens on GCC
auto *test_function_ptr = &test_function;
TEST_CASE("Simple Hook with a condition", "[simple]") {
  hook_ran = false;
  auto t = spud::create_detour(&test_function, &hook);
  t.install();
  o_test_function = t.trampoline();
  test_function_ptr(1);
  REQUIRE(hook_ran == true);
  REQUIRE(condition_intact_for_hook == true);
}

TEST_CASE("Simple Hook with a condition 3", "[simple]") {
  hook_ran = false;
  auto t = spud::create_detour(&mov3, &hook);
  t.install();
  o_test_function = t.trampoline();
  mov3(1);
  REQUIRE(hook_ran == true);
  REQUIRE(msg3 == true);
}

TEST_CASE("Simple Hook with a condition asm mov jump within trampoline",
          "[simple]") {
  hook_ran = false;
  auto t = spud::create_detour(&mov2, &hook);
  t.install();
  o_test_function = t.trampoline();
  mov2(1);
  REQUIRE(hook_ran == true);
  REQUIRE(msg2 == true);
}
