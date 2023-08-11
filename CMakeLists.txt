cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(spud)

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

add_library("spud" STATIC)
target_link_libraries("spud" PRIVATE "Zydis")
target_link_libraries("spud" PRIVATE "asmjit")
add_dependencies("spud" "Zydis" "asmjit")

target_sources("spud"
	PUBLIC
	"include/spud/detour.h"
	"include/spud/memory/protection.h"
)

target_sources("spud"
	PRIVATE
	"src/utils.cc"
	"src/zydis_utils.cc"
	"src/zydis_utils.h"
	"src/detour/x86_64.cc"
	"src/memory/protection.cc"
)

target_include_directories("spud" PUBLIC "include" PRIVATE "src")

add_subdirectory("third_party/gtest")
add_executable("spud.test" "tests/main.cc")
target_link_libraries("spud.test" "gtest" "spud")
add_dependencies("spud.test" "gtest" "spud")
