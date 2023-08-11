#include <spud/memory/protection.h>

#ifdef _WIN32
#include <Windows.h>
#else
#error "Unsupported platform"
#endif

namespace spud {

protection_scope::protection_scope(uintptr_t address, size_t size,
                                   mem_protection protect)
    : address_(address), size_(size) {

#ifdef _WIN32
  DWORD protection = 0;
  if (protect == mem_protection::READ_WRITE_EXECUTE) {
    protection = PAGE_EXECUTE_READWRITE;
  } else if (protect == mem_protection::READ) {
    protection = PAGE_READONLY;
  } else if (protect == mem_protection::EXECUTE) {
    protection = PAGE_EXECUTE;
  } else if (protect == mem_protection::WRITE ||
             protect == mem_protection::READ_WRITE) {
    protection = PAGE_READWRITE;
  }

  DWORD old_protection = 0;
  VirtualProtect(reinterpret_cast<LPVOID>(address_), size_, protection,
                 &old_protection);
  original_protection_ = old_protection;
#endif
  //
}
protection_scope::~protection_scope() {
  if (address_ == 0) {
    return;
  }

  DWORD old_protection = 0;
  VirtualProtect(reinterpret_cast<LPVOID>(address_), size_,
                 original_protection_, &old_protection);
}

} // namespace spud
