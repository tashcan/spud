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

extern "C" void ASM_FUNC(jz1, (int));
extern "C" bool ASM_NAME(msg6);

extern "C" void ASM_FUNC(jz2, (int));
extern "C" bool ASM_NAME(msg7);

static bool hook_ran_simple_amd64 = false;

SPUD_NO_INLINE static void hook_amd64(auto original, int n) {
  hook_ran_simple_amd64 = true;
  original(0);
}

static void hook_mov1(auto original) {}

TEST_CASE("Simple Hook with a condition asm and removal",
          "[detour:x64:simple]") {
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

TEST_CASE("Simple Hook with a condition asm mov jump within trampoline",
          "[detour:x64:simple]") {
  hook_ran_simple_amd64 = false;
  auto t = SPUD_AUTO_HOOK(mov2, hook_amd64);
  t.install();
  mov2(1);
  REQUIRE(hook_ran_simple_amd64 == true);
  REQUIRE(msg2 == true);
}

TEST_CASE("Simple Hook with a condition asm mov 3", "[detour:x64:simple]") {
  hook_ran_simple_amd64 = false;
  auto t = SPUD_AUTO_HOOK(mov3, hook_amd64);
  t.install();
  mov3(1);
  REQUIRE(hook_ran_simple_amd64 == true);
  REQUIRE(msg3 == true);
}

TEST_CASE("Simple Hook with a condition asm mov jump within trampoline 2",
          "[detour:x64:simple]") {
  hook_ran_simple_amd64 = false;
  auto t = SPUD_AUTO_HOOK(mov4, hook_amd64);
  t.install();
  mov4(0);
  REQUIRE(hook_ran_simple_amd64 == true);
  REQUIRE(msg4 == true);
}

TEST_CASE("Simple Hook with a condition asm mov jump within trampoline 3",
          "[detour:x64:simple]") {
  hook_ran_simple_amd64 = false;
  SPUD_STATIC_DETOUR(mov5, hook_amd64);
  mov5(0);
  REQUIRE(hook_ran_simple_amd64 == true);
  REQUIRE(msg5 == true);
}

TEST_CASE("Simple Hook lambda mov 4", "[detour:x64:simple]") {
  hook_ran_simple_amd64 = false;
  msg4 = false;
  auto t = spud::create_detour(mov4, [](auto original, int n) {
    hook_ran_simple_amd64 = true;
    return original(n);
  });
  t.install();
  mov4(0);
  REQUIRE(hook_ran_simple_amd64 == true);
  REQUIRE(msg4 == true);
}

TEST_CASE("Hook early test, jz", "[detour:x64:simple]") {
  hook_ran_simple_amd64 = false;
  msg6 = false;
  auto t = SPUD_AUTO_HOOK(jz1, hook_amd64);
  t.install();
  jz1(1);
  REQUIRE(hook_ran_simple_amd64 == true);
  REQUIRE(msg6 == true);
}

TEST_CASE("Hook early test, jz2", "[detour:x64:simple]") {
  hook_ran_simple_amd64 = false;
  msg7 = false;
  auto t = SPUD_AUTO_HOOK(jz2, hook_amd64);
  t.install();
  jz2(1);
  REQUIRE(hook_ran_simple_amd64 == true);
  REQUIRE(msg7 == true);
}
