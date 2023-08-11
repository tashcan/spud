#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <vector>

#include <spud/utils.h>

namespace spud {
namespace detail {
class detour {
public:
  detour(uintptr_t address, uintptr_t func) : address_(address), func_(func) {}
  ~detour() { remove(); }

  void install();
  void remove();

protected:
  uintptr_t address_ = 0;
  uintptr_t func_ = 0;
  uintptr_t trampoline_ = 0;

  std::vector<uint8_t> original_func_data_;
};

template <typename T>
concept Address = requires(T a) {
  { a } -> std::convertible_to<std::uintptr_t>;
} || std::is_pointer_v<T>;

} // namespace detail

template <typename T> class detour;
template <typename R, typename... Args>
class detour<R(Args...)> : public detail::detour {
public:
  using func_t = R(Args...);

  template <detail::Address T, detail::Address F>
  detour(T target, F func)
      : detail::detour(reinterpret_cast<uintptr_t>(target),
                       reinterpret_cast<uintptr_t>(func)) {}

  inline func_t *trampoline() {
    return reinterpret_cast<func_t *>(trampoline_);
  }
};

//

} // namespace spud

#define SPUD_STATIC_DETOUR_IMPL(n, addr, fn)                                   \
  (([=]() -> auto {                                                            \
    static ::spud::detour<decltype(fn)> n{addr, fn};                           \
    n.install();                                                               \
    return n.trampoline();                                                     \
  })())

#define SPUD_STATIC_DETOUR(addr, fn)                                           \
  SPUD_STATIC_DETOUR_IMPL(SPUD_PP_CAT(spud_detour, __COUNTER__), addr, fn)
