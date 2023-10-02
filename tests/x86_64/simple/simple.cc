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

extern "C" void ASM_FUNC(mov4, (int));
extern "C" bool ASM_NAME(msg4);

extern "C" void ASM_FUNC(mov5, (int));
extern "C" bool ASM_NAME(msg5);

void test_function(int n);

bool hook_ran = false;
bool condition_intact_for_hook = false;

SPUD_NO_INLINE void test_function(int n) {
  if (n == 0) {
    condition_intact_for_hook = true;
    return;
  }
}

void hook(auto original, int n) {
  hook_ran = true;
  original(0);
}

void hook_mov1(auto original) {}

TEST_CASE("Simple Hook with a condition asm and removal", "[simple]") {
  REQUIRE(msg == false);
  mov1();
  REQUIRE(msg == true);
  msg = false;
  {
    auto t = SPUD_AUTO_HOOK(mov1, hook_mov1);
    t.install();
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
  auto t = SPUD_AUTO_HOOK(test_function, hook);
  t.install();
  test_function_ptr(1);
  REQUIRE(hook_ran == true);
  REQUIRE(condition_intact_for_hook == true);
}

TEST_CASE("Simple Hook with a condition asm mov jump within trampoline",
          "[simple]") {
  hook_ran = false;
  auto t = SPUD_AUTO_HOOK(mov2, hook);
  t.install();
  mov2(1);
  REQUIRE(hook_ran == true);
  REQUIRE(msg2 == true);
}

TEST_CASE("Simple Hook with a condition 3", "[simple]") {
  hook_ran = false;
  auto t = SPUD_AUTO_HOOK(mov3, hook);
  t.install();
  mov3(1);
  REQUIRE(hook_ran == true);
  REQUIRE(msg3 == true);
}

TEST_CASE("Simple Hook with a condition asm mov jump within trampoline 2",
          "[simple]") {
  hook_ran = false;
  auto t = SPUD_AUTO_HOOK(mov4, hook);
  t.install();
  mov4(0);
  REQUIRE(hook_ran == true);
  REQUIRE(msg4 == true);
}

TEST_CASE("Simple Hook with a condition asm mov jump within trampoline 3",
          "[simple]") {
  hook_ran = false;
  SPUD_STATIC_DETOUR(mov5, hook);
  mov5(0);
  REQUIRE(hook_ran == true);
  REQUIRE(msg5 == true);
}

TEST_CASE("Simple Hook lambda", "[simple]") {
  hook_ran = false;
  msg4 = false;
  auto t = spud::create_detour(mov4, [](auto original, int n) {
    hook_ran = true;
    return original(n);
  });
  t.install();
  mov4(0);
  REQUIRE(hook_ran == true);
  REQUIRE(msg4 == true);
}
