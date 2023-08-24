#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "arch.h"
#include "utils.h"

namespace spud {
namespace detail {

struct detour {
public:
  ~detour() { remove(); }

  using Self = detail::detour;

public:
  uintptr_t address_;
  uintptr_t func_;

  uintptr_t trampoline_ = 0;
  std::vector<uint8_t> original_func_data_ = {};

protected:
  detour(uintptr_t address, uintptr_t func) : address_(address), func_(func) {}

  Self &install(Arch arch = Arch::kHost);
  void remove();
  Self &detach() {
    original_func_data_.clear();

    return *this;
  }

private:
  detour(detour const &) = delete;
  detour &operator=(detour const &) = delete;
};

template <typename T>
concept Address = requires(T a) {
  { a } -> std::convertible_to<std::uintptr_t>;
} || std::is_pointer_v<T>;

} // namespace detail

template <typename T> class detour;

template <typename R, typename... Args>
struct detour<R(Args...)> : public detail::detour {
public:
  using func_t = R(Args...);
  using Self = detour<R(Args...)>;

  template <detail::Address T, detail::Address F>
  static auto create(T target, F func) {
    return Self{reinterpret_cast<uintptr_t>(target),
                reinterpret_cast<uintptr_t>(func)};
  }

  inline func_t *trampoline() {
    return reinterpret_cast<func_t *>(trampoline_);
  }
  Self &install(Arch arch = Arch::kHost) {
    this->detail::detour::install(arch);
    return *this;
  }

  Self &detach() {
    this->detail::detour::detach();
    return *this;
  }

private:
  detour(uintptr_t address, uintptr_t func) : detail::detour(address, func) {}
};

inline auto create_detour(auto target, auto func) {
  return detour<decltype(func)>::create(target, func);
}
template <typename R, typename... Args>
struct detour<R (*)(Args...)> : public detour<R(Args...)> {};

} // namespace spud

#define SPUD_STATIC_DETOUR_IMPL(n, addr, fn)                                   \
  (auto n = ::spud::create_detour(addr, fn), n.install().detach().trampoline())

#define SPUD_STATIC_DETOUR(addr, fn)                                           \
  SPUD_STATIC_DETOUR_IMPL(SPUD_PP_CAT(spud_detour, __COUNTER__), addr, fn)
