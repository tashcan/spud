#if SPUD_COMPARE_LIBS

#include <catch2/catch_test_macros.hpp>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/benchmark/catch_constructor.hpp>

#include <spud/arch.h>
#include <spud/detour.h>

#include <lime/hooks/hook.hpp>

#include "MinHook.h"

static bool hook_ran = false;
static bool condition_intact_for_hook = false;

#if defined(__clang__) && !defined(_MSC_VER)
#define SPUD_COMPILER_CLANG 1
#elif defined(__GNUC__)
#define SPUD_COMPILER_GNUC 1
#elif defined(_MSC_VER)
#define SPUD_COMPILER_MSVC 1
#endif

#if SPUD_COMPILER_MSVC
#define SPUD_NO_INLINE __declspec(noinline)
#elif SPUD_COMPILER_GNUC
#define SPUD_NO_INLINE __attribute__((__noinline__))
#elif SPUD_COMPILER_CLANG
#define SPUD_NO_INLINE __attribute__((noinline))
#else
#error "Unsupported compiler"
#endif

SPUD_NO_INLINE void test_function(int n) {
  if (n == 0) {
    condition_intact_for_hook = true;
    return;
  }
}

void hook(auto original, int n) {
  original(0);
}

void hook2(auto original, int n) {
  original->original()(0);
}

void hook3(int n);
decltype(hook3) *fpHook = nullptr;
void hook3(int n) {
  fpHook(0);
}

TEST_CASE("compare install", "[benchmark]") {
  BENCHMARK_ADVANCED("spud")(Catch::Benchmark::Chronometer meter) {
    auto t = SPUD_AUTO_HOOK(test_function, hook);
    meter.measure([&] { t.install(); });
  };

  BENCHMARK_ADVANCED("lime")(Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      lime::hook<void(int)>::create(
          test_function, [](auto original, int n) { original->original()(0); });
    });
  };

  BENCHMARK_ADVANCED("minhook")(Catch::Benchmark::Chronometer meter) {
    MH_Initialize();
    meter.measure([&] {
      MH_CreateHook(&test_function, &hook3,
                    reinterpret_cast<LPVOID *>(&fpHook));
      MH_EnableHook(&test_function);
    });
    MH_DisableHook(&test_function);
  };
}
#endif
