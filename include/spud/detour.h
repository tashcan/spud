#pragma once

#include <concepts>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <vector>

#include "arch.h"
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

  detour &operator=(detour &&other) {
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
    uintptr_t func;
    uintptr_t trampoline;
  };

  detour(uintptr_t address, uintptr_t func, uintptr_t wrapper)
      : address_(address), func_(func), wrapper_(wrapper),
        context_container_(std::make_unique<ContextContainer>()) {}

  Self &install(Arch arch = Arch::kHost);
  void remove();
  Self &detach() {
    original_func_data_.clear();

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
};

template <typename T>
concept Address = requires(T a) {
  { a } -> std::convertible_to<std::uintptr_t>;
} || std::is_pointer_v<T>;

template <typename> struct function_traits;

template <typename Function>
struct function_traits
    : public function_traits<
          decltype(&std::remove_reference<Function>::type::operator())> {};

template <typename ClassType, typename ReturnType, typename... Arguments>
struct function_traits<ReturnType (ClassType::*)(Arguments...) const>
    : function_traits<ReturnType (*)(Arguments...)> {};

/* support the non-const operator ()
 * this will work with user defined functors */
template <typename ClassType, typename ReturnType, typename... Arguments>
struct function_traits<ReturnType (ClassType::*)(Arguments...)>
    : function_traits<ReturnType (*)(Arguments...)> {};

template <typename ReturnType, typename FirstArg, typename... Arguments>
struct function_traits<ReturnType (*)(FirstArg, Arguments...)> {
  using FuncType = ReturnType (*)(ReturnType (*)(Arguments...), Arguments...);
};

} // namespace detail

template <typename T> class detour;

template <typename R, typename... Args>
struct detour<R(R (*)(Args...), Args...)> : public detail::detour {
public:
  using func_t = R(R (*)(Args...), Args...);
  using trampoline_t = R(Args...);
  using Self = detour<R(R (*)(Args...), Args...)>;
  using WrapperArgs = std::tuple<Self *, Args...>;

  static R wrapper(Args... args) {
    const auto context = reinterpret_cast<ContextContainer *>(
        detail::detour::get_context_value());
    return ((func_t *)context->func)((trampoline_t *)context->trampoline,
                                     args...);
  };
  template <detail::Address T, detail::Address F>
  static auto create(T target, F func) {
    return Self{reinterpret_cast<uintptr_t>(target),
                reinterpret_cast<uintptr_t>(func),
                reinterpret_cast<uintptr_t>((R(*)(Args...))wrapper)};
  }

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
  detour(uintptr_t address, uintptr_t func, uintptr_t wrapper)
      : detail::detour(address, func, wrapper) {}
};

template <typename R, typename... Args>
struct detour<R (*)(R (*)(Args...), Args...)>
    : public detour<R(R (*)(Args...), Args...)> {};

template <class F, class... Args>
concept invocable = requires(F &&f, Args &&...args) {
  std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
};

template <typename F>
inline auto create_detour(auto target,
                          typename detail::function_traits<F>::FuncType func) {
  return detour<typename detail::function_traits<F>::FuncType>::create(target,
                                                                       func);
}

#define SPUD_AUTO_HOOK(target, func)                                           \
  spud::create_detour<decltype(&func<void (*)(...)>)>(target, &func)

} // namespace spud

// Creates a detour that will live until the end of the program
#define SPUD_STATIC_DETOUR(addr, fn)                                           \
  (SPUD_AUTO_HOOK(addr, fn).install().detach().trampoline())
