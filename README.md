
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
void test_function(int n);
decltype(test_function) *o_test_function = nullptr;

SPUD_NO_INLINE void test_function(int n) {
    if (n == 0) {
        condition_intact_for_hook = true;
        return;
    }
}

o_test_function = SPUD_STATIC_DETOUR(test_function, hook);
```

### Build & Install

`spud` can be easily added to any cmake-based project. Just add a few lines in `CMakeLists.txt`.

## Missing features

- No documentation (yet)
- Easy way to patch VTables
- A lot more stuff

## References

- [catch2](https://github.com/catchorg/Catch2) for unit-testing
- [x86](http://ref.x86asm.net/coder32.html) and [x86-64](http://ref.x86asm.net/coder64.html) opcode and instruction reference

## License

- MIT
