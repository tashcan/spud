---
title: Detour a function
description: Introduction to detouring a function.
---

Spud is a library for intercepting binary functions on `ARM64`, and `X64` machines.  
Interception code is applied dynamically at runtime. Spud replaces the first few instructions of the target function with an unconditional jump to the user-provided detour function.  
Instructions from the target function are placed in a trampoline. The address of the trampoline is placed in a target pointer.  
The detour function can either replace the target function or extend its semantics by invoking the target function as a subroutine through the target pointer to the trampoline.

Detours are inserted at execution time. The code of the target function is modified in memory, not on disk, thus enabling interception of binary functions at a very fine granularity. For example, the procedures in a DLL can be detoured in one execution of an application, while the original procedures are not detoured in another execution running at the same time. Unlike DLL re-linking or static redirection, the interception techniques used in the Spud library are guaranteed to work regardless of the method used by application or system code to locate the target function.

## Example

Given the following function in a program. 
> _This is the original code we want to patch_

```cpp
extern "C" void test_function(int n) {
  if (n == 0) {
    printf("N was 0\n");
  }
  printf("%d\n", n);
}

int main() {
  test_function(1);
  test_function(0);
}


```

We wish to alter this function so that it prints "I caught N" instead of "N was 0".
Assuming we are able to inject a detour installation routine into the program, we can patch the function by doing the following:

```cpp
void test_function_detour(auto original, int n) {
  if (n == 0) {
    printf("I caught N\n");
    return;
  }
  return original(n);
}

void install_detours() {
  uintptr_t test_function_ptr = <lookup address of test_function within the memory of the current process>;
  SPUD_STATIC_DETOUR(test_function_ptr, test_function_detour);
}
```

