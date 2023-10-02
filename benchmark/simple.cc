#include <catch2/catch_test_macros.hpp>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/benchmark/catch_constructor.hpp>

#include <spud/arch.h>
#include <spud/detour.h>

bool hook_ran = false;
bool condition_intact_for_hook = false;

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

TEST_CASE("bench simple install", "[benchmark]") {
  BENCHMARK_ADVANCED("test")(Catch::Benchmark::Chronometer meter) {
    auto t = SPUD_AUTO_HOOK(test_function, hook);
    meter.measure([&] { t.install(); });
  };
}
