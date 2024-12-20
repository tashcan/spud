#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>

#if __cpp_lib_source_location && SPUD_DETOUR_TRACING
#include <source_location>
#endif

#include "arch.h"
#include "details/function_traits.h"
#include "utils.h"

namespace spud {
namespace detail {

struct detour {
public:
  ~detour() {
    remove();
  }

  uintptr_t trampoline() const {
    return this->trampoline_;
  }

  using Self = detail::detour;

public:
  detour(detour &&other) noexcept {
    this->trampoline_ = other.trampoline_;
    this->address_ = other.address_;
    this->func_ = other.func_;
    this->wrapper_ = other.wrapper_;
    this->context_container_ = std::move(other.context_container_);
    this->original_func_data_ = other.original_func_data_;
    other.original_func_data_.clear();
  }

  detour &operator=(detour &&other) noexcept {
    this->trampoline_ = other.trampoline_;
    this->address_ = other.address_;
    this->func_ = other.func_;
    this->wrapper_ = other.wrapper_;
    this->context_container_ = std::move(other.context_container_);
    this->original_func_data_ = other.original_func_data_;
    other.original_func_data_.clear();
    return *this;
  }

protected:
  struct ContextContainer {
    uintptr_t r15;
    uintptr_t func;
    uintptr_t trampoline;
#if __cpp_lib_source_location && SPUD_DETOUR_TRACING
    std::source_location location;
#endif
  };

#if __cpp_lib_source_location && SPUD_DETOUR_TRACING
  detour(uintptr_t address, uintptr_t func, uintptr_t wrapper,
         const std::source_location location = std::source_location::current())
      : address_(address), func_(func), wrapper_(wrapper),
        context_container_(std::make_unique<ContextContainer>()),
        location_(location) {}
#else
  detour(uintptr_t address, uintptr_t func, uintptr_t wrapper)
      : address_(address), func_(func), wrapper_(wrapper),
        context_container_(std::make_unique<ContextContainer>()) {}
#endif
  Self &install(Arch arch = Arch::kHost);
  void remove();
  Self &detach() {
    original_func_data_.clear();
    // We are intentionally ignoring the return value here
    // TODO(alex): Move this to a global cleanup thing
    (void)context_container_.release();

    return *this;
  }

  static uintptr_t get_context_value();

private:
  detour(detour const &) = delete;
  detour &operator=(detour const &) = delete;

  uintptr_t address_;
  uintptr_t func_;
  uintptr_t wrapper_;

  std::unique_ptr<struct ContextContainer> context_container_ = nullptr;
  uintptr_t trampoline_ = 0;
  std::vector<uint8_t> original_func_data_ = {};
#if __cpp_lib_source_location && SPUD_DETOUR_TRACING
  const std::source_location location_ = std::source_location::current();
#endif
};

template <typename T>
concept Address = requires(T a) {
  { a } -> std::convertible_to<std::uintptr_t>;
} || std::is_pointer_v<T>;

} // namespace detail

template <typename T> struct detour;

template <typename R, typename... Args>
struct detour<R(R (*)(Args...), Args...)> : public detail::detour {
public:
  using func_t = R(R (*)(Args...), Args...);
  using trampoline_t = R(Args...);
  using Self = detour<R(R (*)(Args...), Args...)>;
  using WrapperArgs = std::tuple<Self *, Args...>;

  static inline R wrapper(Args... args) {
    const auto context = reinterpret_cast<ContextContainer *>(
        detail::detour::get_context_value());
#if SPUD_DETOUR_TRACING
    printf("%s\n", context->location.function_name());
#endif
    if constexpr (std::is_same_v<select_last<Args...>, ContextContainer *>)
      return (reinterpret_cast<func_t *>(context->func))(
          reinterpret_cast<trampoline_t *>(context->trampoline), args...,
          context);
    else
      return (reinterpret_cast<func_t *>(context->func))(
          reinterpret_cast<trampoline_t *>(context->trampoline), args...);
  };
#if __cpp_lib_source_location && SPUD_DETOUR_TRACING
  template <detail::Address T, detail::Address F>
  static auto create(
      T target, F func,
      const std::source_location location = std::source_location::current()) {
    return Self{reinterpret_cast<uintptr_t>(target),
                reinterpret_cast<uintptr_t>(func),
                reinterpret_cast<uintptr_t>((R(*)(Args...))wrapper), location};
  }
#else
  template <detail::Address T, detail::Address F>
  static auto create(T target, F func) {
    return Self{reinterpret_cast<uintptr_t>(target),
                reinterpret_cast<uintptr_t>(func),
                reinterpret_cast<uintptr_t>((R(*)(Args...))wrapper)};
  }
#endif

  inline trampoline_t *trampoline() {
    return reinterpret_cast<trampoline_t *>(this->detail::detour::trampoline());
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
#if __cpp_lib_source_location && SPUD_DETOUR_TRACING
  detour(uintptr_t address, uintptr_t func, uintptr_t wrapper,
         const std::source_location location = std::source_location::current())
      : detail::detour(address, func, wrapper, location) {}
#else
  detour(uintptr_t address, uintptr_t func, uintptr_t wrapper)
      : detail::detour(address, func, wrapper) {}

#endif

  template <typename T> struct tag {
    using type = T;
  };

  template <typename... Ts> struct select_last {
    // Use a fold-expression to fold the comma operator over the parameter pack.
    using type = typename decltype((tag<Ts>{}, ...))::type;
  };
};

template <typename R, typename... Args>
struct detour<R (*)(R (*)(Args...), Args...)>
    : public detour<R(R (*)(Args...), Args...)> {};

template <class F, class... Args>
concept invocable = requires(F &&f, Args &&...args) {
  std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
};

namespace detail {
#if __cpp_lib_source_location && SPUD_DETOUR_TRACING
template <typename F>
inline auto create_detour_impl(
    auto target, typename detail::function_traits<F>::FuncType func,
    const std::source_location location = std::source_location::current()) {
  return ::spud::detour<typename detail::function_traits<F>::FuncType>::create(
      target, func, location);
}
#else
template <typename F> inline auto create_detour_impl(auto target, F func) {
  return ::spud::detour<F>::create(target, func);
}
#endif
} // namespace detail

#if __cpp_lib_source_location && SPUD_DETOUR_TRACING
template <typename F,
          typename std::enable_if_t<!std::is_function_v<F>> * = nullptr>
auto create_detour(
    auto target, F func,
    const std::source_location location = std::source_location::current()) {
  using FuncT = typename spud::detail::function_traits<F>::FuncType;
  return detail::create_detour_impl<FuncT>(target, func, location);
}

template <typename F, typename std::enable_if_t<std::is_function_v<
                          std::remove_pointer_t<F>>> * = nullptr>
auto create_detour(
    auto target, typename detail::function_traits_ptr<F>::FuncType func,
    const std::source_location location = std::source_location::current()) {
  return detail::create_detour_impl<
      typename detail::function_traits_ptr<F>::FuncType>(target, func,
                                                         location);
}
#else
template <typename F,
          typename std::enable_if_t<!std::is_function_v<F>> * = nullptr>
auto create_detour(auto target, F func) {
  using FuncT = typename spud::detail::function_traits<F>::FuncType;
  return detail::create_detour_impl<FuncT>(target, func);
}

template <typename F, typename std::enable_if_t<std::is_function_v<
                          std::remove_pointer_t<F>>> * = nullptr>
auto create_detour(auto target,
                   typename detail::function_traits_ptr<F>::FuncType func) {
  return detail::create_detour_impl<
      typename detail::function_traits_ptr<F>::FuncType>(target, func);
}
#endif

} // namespace spud

#if __cpp_lib_source_location && SPUD_DETOUR_TRACING
#define SPUD_AUTO_HOOK(target, func, loc)                                      \
  spud::create_detour<decltype(&func<void (*)(...)>)>(target, func, loc);
#else
#define SPUD_AUTO_HOOK(target, func)                                           \
  spud::create_detour<decltype(&func<void (*)(...)>)>(target, func);
#endif

// Creates a detour that will live until the end of the program
#if __cpp_lib_source_location && SPUD_DETOUR_TRACING
#define SPUD_STATIC_DETOUR(addr, fn)                                           \
  (([=]() -> auto {                                                            \
    static auto dh_static_hook =                                               \
        SPUD_AUTO_HOOK(addr, fn, std::source_location::current());             \
    return dh_static_hook.install().trampoline();                              \
  })())
#else
#define SPUD_STATIC_DETOUR(addr, fn)                                           \
  (([=]() -> auto {                                                            \
    static auto dh_static_hook = SPUD_AUTO_HOOK(addr, fn);                     \
    return dh_static_hook.install().trampoline();                              \
  })())
#endif
