cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

if (WIN32)
project(spud C CXX ASM_MASM)
else()
project(spud C CXX ASM)
endif(WIN32)

SET(CMAKE_ASM_FLAGS "${CFLAGS} -x assembler-with-cpp")

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(ASMJIT_STATIC TRUE)

if (NOT TARGET Zydis)
add_subdirectory("third_party/zydis")
endif()

if (NOT TARGET asmjit)
add_subdirectory("third_party/asmjit")
endif()

if (NOT TARGET Catch2)
add_subdirectory("third_party/Catch2")
endif()

add_library("spud" STATIC)
target_link_libraries("spud" PRIVATE "Zydis")
target_link_libraries("spud" PRIVATE "asmjit")
add_dependencies("spud" "Zydis" "asmjit")

target_sources("spud"
	PUBLIC
    "include/spud/arch.h"
	"include/spud/detour.h"
    "include/spud/utils.h"
	"include/spud/memory/protection.h"
)

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
    "${ASM_SRCS}"
)


if( CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR )

if (WIN32)
file(GLOB_RECURSE TEST_SRCS tests/x86_64/*.cc tests/x86_64/*.h tests/x86_64/*.asm)
else()

if (ARCH MATCHES "arm64")
file(GLOB_RECURSE TEST_SRCS tests/arm64/*.cc tests/arm64/*.h tests/arm64/*.S)
else()
file(GLOB_RECURSE TEST_SRCS tests/x86_64/*.cc tests/x86_64/*.h tests/x86_64/*.S)
endif()

endif(WIN32)

add_executable("spud.test" ${TEST_SRCS} "tests/test_util.h")
target_include_directories("spud.test" PRIVATE "tests")
target_link_libraries("spud.test" PRIVATE "Catch2" "Catch2::Catch2WithMain" "spud")
add_dependencies("spud.test" "Catch2" "spud")

add_test(NAME spud.test COMMAND spud.test)

endif()
