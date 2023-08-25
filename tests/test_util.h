#pragma once

#include <spud/arch.h>

#if SPUD_OS_APPLE || SPUD_OS_LINUX
#define ASM_NAME(n) n asm(#n)
#define ASM_FUNC(n, a) n a asm(#n)
#else
#define ASM_NAME(n) n
#define ASM_FUNC(n, a) n a
#endif

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
