
<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/tashcan/spud/main/assets/img/logo-dark.svg" width="400px">
    <source media="(prefers-color-scheme: light)" srcset="https://raw.githubusercontent.com/tashcan/spud/main/assets/img/logo-light.svg" width="400px">
    <img alt="spud logo" src="https://raw.githubusercontent.com/tashcan/spud/main/assets/img/logo-dark.svg" width="400px">
  </picture>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT">
  <a href="https://github.com/tashcan/spud/actions"><img src="https://github.com/tashcan/spud/actions/workflows/build-and-test.yaml/badge.svg" alt="GitHub Actions"></a>
</p>

<p align="center">
  <b>spud</b> - multi-architecture cross-platform hooking library.
</p>

## Features

- **x86-64 and arm64 support**
- **Windows, Mac and Linux support**

## Building

### With CMake (as a subproject)

Clone repository to subfolder and link `spud` to your project:
```cmake
add_subdirectory(path/to/spud)
target_link_libraries(your-project-name PRIVATE spud)
```

## Examples

-  Simple example
```c++
void hook(auto original, int n) {
  if (n == 0) {
    // Run some custom code here
    return;
  }
  // Forward to the original code
  return original(n);
}

void test_function(int n) {
  if (n == 0) {
    printf("N was 0\n");
  }
  printf("%d\n", n);
}

SPUD_STATIC_DETOUR(test_function, hook);
```

## Build & Install

`spud` can be easily added to any cmake-based project. Just add a few lines in `CMakeLists.txt`.

```cmake
FetchContent_Declare(
      spud
      GIT_REPOSITORY "https://github.com/tashcan/spud.git"
      GIT_TAG origin/main
)
FetchContent_MakeAvailable(spud)
```

## Missing features

- No documentation (yet)
- Easy way to patch VTables
- A lot more stuff

## References

- [catch2](https://github.com/catchorg/Catch2) for unit-testing
- [asmjit](https://github.com/asmjit/asmjit) for code-gen on x86 and arm
- [zydis](https://github.com/zyantific/zydis) for disassembling x86 code
- [x86](http://ref.x86asm.net/coder32.html) and [x86-64](http://ref.x86asm.net/coder64.html) opcode and instruction reference

## License

- MIT
