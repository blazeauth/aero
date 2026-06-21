#pragma once

#include <utility>

namespace aero {

  // Credits: https://github.com/microsoft/GSL/blob/main/include/gsl/util

  template <class F>
  class final_action {
   public:
    explicit final_action(const F& fn) noexcept: fn_(fn) {}
    explicit final_action(F&& fn) noexcept: fn_(std::move(fn)) {}

    ~final_action() noexcept {
      if (invoke_) {
        f();
      }
    }

    final_action(final_action&& other) noexcept: fn_(std::move(other.fn_)), invoke_(std::exchange(other.invoke_, false)) {}

    final_action(const final_action&) = delete;
    void operator=(const final_action&) = delete;
    void operator=(final_action&&) = delete;

   private:
    F fn_;
    bool invoke_ = true;
  };

  template <class F>
  [[nodiscard]] auto finally(F&& fn) noexcept {
    return final_action<std::decay_t<F>>{std::forward<F>(fn)};
  }

} // namespace aero
