#pragma once

#include <utility>

namespace aero::detail {

  // Credits: https://github.com/microsoft/GSL/blob/main/include/gsl/util

  template <class F>
  class final_action {
   public:
    explicit final_action(const F& ff) noexcept: f(ff) {}
    explicit final_action(F&& ff) noexcept: f(std::move(ff)) {}

    ~final_action() noexcept {
      if (invoke_) {
        f();
      }
    }

    final_action(final_action&& other) noexcept: f(std::move(other.f)), invoke_(std::exchange(other.invoke_, false)) {}

    final_action(const final_action&) = delete;
    void operator=(const final_action&) = delete;
    void operator=(final_action&&) = delete;

   private:
    F f;
    bool invoke_ = true;
  };

  template <class F>
  [[nodiscard]] auto finally(F&& f) noexcept {
    return final_action<std::decay_t<F>>{std::forward<F>(f)};
  }

} // namespace aero::detail
