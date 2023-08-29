#include <spud/arch.h>
#include <spud/detour.h>
#include <spud/memory/protection.h>
#include <spud/utils.h>

#include "detour_impl.h"
#include "remapper.h"

#if SPUD_OS_APPLE
#include <libkern/OSCacheControl.h>
#endif

#include <cstring>

namespace spud {
namespace detail {
struct DetourImpl {
  std::vector<uint8_t> (*create_absolute_jump)(uintptr_t target);
  std::tuple<RelocationInfo, size_t> (*collect_relocations)(uintptr_t address,
                                                            size_t jump_size);
  size_t (*get_trampoline_size)(std::span<uint8_t> target,
                                const RelocationInfo &relocation);
  Trampoline (*create_trampoline)(uintptr_t trampoline_address,
                                  uintptr_t return_address,
                                  std::span<uint8_t> target,
                                  const RelocationInfo &relocation_infos);
  uintptr_t(*maybe_resolve_jump)(uintptr_t) = [](auto v) {
      return v;
  };
};

const static std::array<DetourImpl, Arch::kCount> kDetourImpls = {
    // x86_64
    DetourImpl{.create_absolute_jump = x64::create_absolute_jump,
               .collect_relocations = x64::collect_relocations,
               .get_trampoline_size = x64::get_trampoline_size,
               .create_trampoline = x64::create_trampoline,
			   .maybe_resolve_jump = x64::maybe_resolve_jump},
    // x86
    DetourImpl{.create_absolute_jump = x64::create_absolute_jump},
    // Arm64
    DetourImpl{.create_absolute_jump = arm64::create_absolute_jump,
               .collect_relocations = arm64::collect_relocations,
               .get_trampoline_size = arm64::get_trampoline_size,
               .create_trampoline = arm64::create_trampoline},
};

detail::detour &detour::install(Arch arch) {
  const auto &impl = kDetourImpls[arch];

  func_ = impl.maybe_resolve_jump(func_);
  address_ = impl.maybe_resolve_jump(address_);

  auto jump = impl.create_absolute_jump(func_);

  auto [relocation_infos, required_trampoline_size] =
      impl.collect_relocations(address_, jump.size());

  auto trampoline_size = impl.get_trampoline_size(
      {reinterpret_cast<uint8_t *>(address_), required_trampoline_size},
      relocation_infos);

  auto trampoline_address = alloc_executable_memory(trampoline_size);
  assert(trampoline_address != nullptr);

  auto trampoline = impl.create_trampoline(
      reinterpret_cast<uintptr_t>(trampoline_address),
      address_ + required_trampoline_size,
      {reinterpret_cast<uint8_t *>(address_), required_trampoline_size},
      relocation_infos);

  // TODO(tashcan): This isn't particularly amazing, we might have to ajdust
  // permissions
  disable_jit_write_protection();

  std::memcpy(trampoline_address, trampoline.data.data(),
              trampoline.data.size());

  trampoline_ =
      trampoline.start + reinterpret_cast<uintptr_t>(trampoline_address);

  {
    // TODO(alexander): Expand this copy stuff to encompass the entire
    // trampoline size that we copied from the original function
    //
    const auto copy_size = jump.size();
    original_func_data_.resize(copy_size);
    std::memcpy(original_func_data_.data(), reinterpret_cast<void *>(address_),
                copy_size);
    auto jump_data = jump.data();

    Remapper remap(address_, copy_size);
    {
      SPUD_SCOPED_PROTECTION(remap, copy_size,
                             mem_protection::READ_WRITE_EXECUTE);

      // TODO
      std::memcpy(reinterpret_cast<void *>(uintptr_t(remap)), jump.data(),
                  copy_size);
#if SPUD_OS_APPLE
      sys_dcache_flush((void *)address_, copy_size);
#endif
    }
#if SPUD_OS_APPLE
    sys_icache_invalidate((void *)address_, copy_size);
#endif
  }

  enable_jit_write_protection();
  return *this;
}

void detour::remove() {
  if (original_func_data_.size() == 0) {
    return;
  }

  disable_jit_write_protection();

  {
    Remapper remap(address_, original_func_data_.size());
    {
      SPUD_SCOPED_PROTECTION(remap, original_func_data_.size(),
                             mem_protection::READ_WRITE_EXECUTE);
      std::memcpy(reinterpret_cast<void *>(uintptr_t(remap)),
                  original_func_data_.data(), original_func_data_.size());
    }
  }
  enable_jit_write_protection();
  original_func_data_.clear();
}
} // namespace detail
} // namespace spud
