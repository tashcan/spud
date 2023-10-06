#include <catch2/catch_test_macros.hpp>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/benchmark/catch_constructor.hpp>

#include <spud/arch.h>
#include <spud/detour.h>
#include <spud/pattern.h>

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

SPUD_NO_INLINE static void test_function(int n) {
  if (n == 0) {
    condition_intact_for_hook = true;
    return;
  }
}

static void hook(auto original, int n) {
  original(0);
}

/* int main() {
  for (int i = 0; i < 1000000; ++i) {
    auto t = SPUD_AUTO_HOOK(test_function, hook);
    t.install();
  }
} */

TEST_CASE("detour", "[benchmark]") {
  BENCHMARK_ADVANCED("simple detour")(Catch::Benchmark::Chronometer meter) {
    auto t = SPUD_AUTO_HOOK(test_function, hook);
    meter.measure([&] { t.install(); });
  };
}

TEST_CASE("pattern search", "[benchmark]") {
  constexpr auto BUFFER_SIZE = 1 * 1024 * 1024 * 1024;

  std::vector<uint8_t> search_buffer;
  search_buffer.resize(BUFFER_SIZE, 0xCC);

  constexpr auto i = 250 * 1024 * 1024;

  search_buffer[i] = 0x4C;
  search_buffer[i + 1] = 0x89;
  search_buffer[i + 2] = 0x44;
  search_buffer[i + 3] = 0x24;
  search_buffer[i + 4] = 0x33;
  search_buffer[i + 5] = 0x48;
  search_buffer[i + 6] = 0x89;
  search_buffer[i + 7] = 0x54;
  search_buffer[i + 8] = 0x24;

  const auto pattern = "4C 89 44 24 ? 48 89 54 24";

  std::string mask;
  std::string data;
  spud::detail::generate_mask_and_data(pattern, mask, data);

  BENCHMARK_ADVANCED("1GB search")(Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      const auto result = spud::detail::find_matches(
          mask, data, search_buffer, spud::cpu_feature::FEATURE_NONE);
    });
  };

  BENCHMARK_ADVANCED("1GB search SSE 4.2")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      const auto result = spud::detail::find_matches(
          mask, data, search_buffer, spud::cpu_feature::FEATURE_SSE42);
    });
  };

  BENCHMARK_ADVANCED("1GB search AVX2")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      const auto result = spud::detail::find_matches(
          mask, data, search_buffer, spud::cpu_feature::FEATURE_AVX2);
    });
  };
}
