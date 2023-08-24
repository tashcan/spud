#include <spud/detour.h>

#include <cstdio>

#if defined(_MSC_VER)
#define SPUD_NO_INLINE __declspec(noinline)
#else
#define SPUD_NO_INLINE __attribute__((noinline))
#endif


void test_function(int n);
decltype(test_function) *o_test_function = nullptr;

bool hook_ran = false;

void hook(int n) {
  printf("I'm a distraction\n");

  hook_ran = true;

  return o_test_function(2);
}

SPUD_NO_INLINE void test_function(int n) {
  if (n == 0) {
    return;
  }
  printf("test %d\n", n);
}

int main() {
  test_function(1);

  auto t = spud::create_detour(&test_function, &hook);
  t.install();
  o_test_function = t.trampoline();
  test_function(1);
  // o_test_function(2);
  // auto n = SPUD_STATIC_DETOUR(test_function, hook);

  if (hook_ran) {
    return 0;
  } else {
    return 1;
  }
}
