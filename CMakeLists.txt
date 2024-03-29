cmake_minimum_required(VERSION 3.23 FATAL_ERROR)

if (WIN32)
project(spud C CXX ASM_MASM)
else()
project(spud C CXX ASM)
endif(WIN32)
SET(CMAKE_ASM_FLAGS "${CFLAGS} -x assembler-with-cpp")

include(CTest)
include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CXX_STANDARD_REQUIRED ON)
# set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
set(ASMJIT_STATIC TRUE)

option(SPUD_COMPARE_LIBS "" OFF)
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
set(__SPUD_IS_SELF ON)
else()
set(__SPUD_IS_SELF OFF)
endif()
option(SPUD_BUILD_TESTS "" ${__SPUD_IS_SELF})
option(SPUD_NO_LTO "" OFF)

if (NOT TARGET Zydis)
option(ZYDIS_BUILD_TOOLS "" OFF)
option(ZYDIS_BUILD_EXAMPLES "" OFF)
option(ZYDIS_BUILD_TOOLS "" OFF)
FetchContent_Declare(
  zydis
  GIT_REPOSITORY https://github.com/zyantific/zydis.git
  GIT_TAG        v4.0.0
)

FetchContent_MakeAvailable(zydis)
endif()

if (NOT TARGET asmjit)
FetchContent_Declare(
  asmjit
  GIT_REPOSITORY https://github.com/asmjit/asmjit.git
  GIT_TAG        origin/master 
)

FetchContent_MakeAvailable(asmjit)
endif()

add_library("spud" STATIC)
target_link_libraries("spud" PRIVATE "Zydis")
target_link_libraries("spud" PRIVATE "asmjit")
add_dependencies("spud" "Zydis" "asmjit")

if (MSVC)
target_compile_options(spud PRIVATE
    #    "/WX"
    "/permissive-"
    "/W4"
)
endif()

if(NOT SPUD_NO_LTO)
include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported OUTPUT error)
if(lto_supported)
if(NOT "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    set_property(TARGET spud PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
endif()
endif()

set(SPUD_PUBLIC_HDRS
    "include/spud/arch.h"
    "include/spud/detour.h"
    "include/spud/pattern.h"
    "include/spud/utils.h"
    "include/spud/details/function_traits.h"
    "include/spud/memory/protection.h"
)

target_sources("spud" PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS "include" FILES ${SPUD_PUBLIC_HDRS})
target_include_directories("spud" PUBLIC "include" PRIVATE "src")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ARCH "arm64")
    else()
        set(ARCH "arm")
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^mips.*")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ARCH "mips64el")
    else()
        set(ARCH "mipsel")
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^ppc.*")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ARCH "ppc64le")
    else()
        message(FATAL_ERROR "Architecture is not supported")
    endif()
else()
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ARCH "x86_64")
    else()
        set(ARCH "x86")
    endif()
endif()

# Cross compiling on macOS. The cross compiling architecture should override
# auto-detected system architecture settings.
if(CMAKE_OSX_ARCHITECTURES)
    if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
        set(ARCH "arm64")
    elseif(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
        set(ARCH "x86_64")
    else()
        message(FATAL_ERROR "Architecture ${CMAKE_OSX_ARCHITECTURES} is not "
                            "supported. Only one architecture (arm64, x86_64)"
                            "could be specified at build time.")
    endif()
endif()

if (WIN32)
    file(GLOB_RECURSE ASM_SRCS src/detour/x86_64/*.asm)
else()
    if (ARCH MATCHES "arm64")
        file(GLOB_RECURSE ASM_SRCS src/detour/aarch64/*.S)
    else()
        file(GLOB_RECURSE ASM_SRCS src/detour/x86_64/*.S)
    endif()
endif(WIN32)

file(GLOB_RECURSE SPUD_SSE_SRCS src/*_sse.cc)
file(GLOB_RECURSE SPUD_AVX2_SRCS src/*_avx2.cc)

if (MSVC)
    SET_SOURCE_FILES_PROPERTIES(${SPUD_AVX2_SRCS} PROPERTIES COMPILE_FLAGS "/arch:AVX2")
else()
    SET_SOURCE_FILES_PROPERTIES(${SPUD_AVX2_SRCS} PROPERTIES COMPILE_FLAGS "-mavx2 -mfma -msse4.2")
    SET_SOURCE_FILES_PROPERTIES(${SPUD_SSE_SRCS} PROPERTIES COMPILE_FLAGS "-msse4.2")
endif()

target_sources("spud"
    PRIVATE
    "src/utils.cc"
    "src/zydis_utils.cc"
    "src/zydis_utils.h"
    "src/detour/x86_64/x86_64.cc"
    "src/detour/x86_64/x86_64.h"
    "src/detour/x86_64/relocators.cc"
    "src/detour/x86_64/relocators.h"
    "src/detour/aarch64/aarch64.cc"
    "src/detour/aarch64/aarch64.h"
    "src/detour/detour.cc"
    "src/detour/detour_impl.h"
    "src/detour/fwd.h"
    "src/detour/remapper.cc"
    "src/detour/remapper.h"
    "src/memory/protection.cc"
    "src/pattern/pattern.cc"
    "${ASM_SRCS}"
    "${SPUD_SSE_SRCS}"
    "${SPUD_AVX2_SRCS}"
)

if (NOT SPUD_NO_INSTALL)
    install(TARGETS ${PROJECT_NAME}
            FILE_SET public_headers
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()


if(SPUD_BUILD_TESTS)

if (WIN32)
file(GLOB_RECURSE TEST_SRCS tests/x86_64/*.cc tests/x86_64/*.h tests/x86_64/*.asm)
else()

if (ARCH MATCHES "arm64")
file(GLOB_RECURSE TEST_SRCS tests/arm64/*.cc tests/arm64/*.h tests/arm64/*.S)
else()
file(GLOB_RECURSE TEST_SRCS tests/x86_64/*.cc tests/x86_64/*.h tests/x86_64/*.S)
endif()

endif(WIN32)

file(GLOB_RECURSE BENCHMARK_SRCS benchmark/*.cc)

if (NOT TARGET Catch2)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.0.1
)

FetchContent_MakeAvailable(Catch2)
endif()

if (SPUD_COMPARE_LIBS)

add_compile_definitions("SPUD_COMPARE_LIBS=1")

FetchContent_Declare(
    lime
    GIT_REPOSITORY "https://github.com/Curve/lime"
)

FetchContent_MakeAvailable(lime)

FetchContent_Declare(
    minhook
    GIT_REPOSITORY "https://github.com/TsudaKageyu/minhook"
)

FetchContent_MakeAvailable(minhook)

set(ASMJIT_EXTERNAL ON)
set(POLYHOOK_USE_EXTERNAL_ZYDIS ON)
set(POLYHOOK_USE_EXTERNAL_ASMJIT ON)
FetchContent_Declare(
    PolyHook2
    GIT_REPOSITORY "https://github.com/stevemk14ebr/PolyHook_2_0"
)
FetchContent_MakeAvailable(PolyHook2)

endif()

add_executable("spud.test" ${TEST_SRCS} "tests/test_util.h" "tests/pattern.cc")
target_include_directories("spud.test" PRIVATE "tests")
target_link_libraries("spud.test" PRIVATE "Catch2" "Catch2::Catch2WithMain" "spud")
add_dependencies("spud.test" "Catch2" "spud")

add_executable("spud.benchmark" ${BENCHMARK_SRCS})
target_include_directories("spud.benchmark" PRIVATE "benchmark")
if (SPUD_COMPARE_LIBS)
target_link_libraries("spud.benchmark" PRIVATE "Catch2" "Catch2::Catch2WithMain" "spud" "lime" "minhook" "PolyHook2")
else()
target_link_libraries("spud.benchmark" PRIVATE "Catch2" "Catch2::Catch2WithMain" "spud")
endif()
add_dependencies("spud.benchmark" "Catch2" "spud")

add_test(NAME spud.test COMMAND spud.test)
add_test(NAME spud.benchmark COMMAND spud.benchmark)

endif()
