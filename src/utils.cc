#include <spud/utils.h>

#ifdef _WIN32
#include <Windows.h>
#else
#error "Unsupported platform"
#endif

namespace spud {
void *alloc_executable_memory(size_t size) {
#ifdef _WIN32
  return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE,
                      PAGE_EXECUTE_READWRITE);
#endif
}
} // namespace spud
