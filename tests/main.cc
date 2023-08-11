#include <spud/detour.h>

#include <cstdio>

#include <Windows.h>

void test_function(int n);
decltype(test_function) *o_test_function = nullptr;
void hook(int n) {
  printf("I'm a distraction\n");
  return o_test_function(2);
}

void test_function(int n) {
  if (n == 0) {
    return;
  }
  printf("test %d\n", n);
}

int main() {
  test_function(1);

  auto t = spud::detour<void(int)>(test_function, hook);
  t.install();
  o_test_function = t.trampoline();
  test_function(1);
  return 0;
}
