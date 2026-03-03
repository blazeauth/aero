#ifndef AERO_DEADLINE_HPP
#define AERO_DEADLINE_HPP

#include <chrono>

namespace aero {

  template <typename Clock = std::chrono::steady_clock>
  class deadline {
   public:
    using clock_type = Clock;
    using duration = typename clock_type::duration;
    using time_point = typename clock_type::time_point;

    explicit deadline(duration timeout) noexcept: expiry_(clock_type::now() + timeout) {}

    [[nodiscard]] duration remaining() const noexcept {
      const auto now = clock_type::now();
      if (now >= expiry_) {
        return duration::zero();
      }
      return expiry_ - now;
    }

    [[nodiscard]] bool expired() const noexcept {
      return remaining() == duration::zero();
    }

    template <typename Rep, typename Period>
    [[nodiscard]] std::chrono::duration<Rep, Period> clamp(std::chrono::duration<Rep, Period> max_slice) const noexcept {
      const auto left = std::chrono::duration_cast<std::chrono::duration<Rep, Period>>(remaining());
      return (std::min)(left, max_slice);
    }

    [[nodiscard]] time_point expiry() const noexcept {
      return expiry_;
    }

   private:
    time_point expiry_;
  };

} // namespace aero

#endif
