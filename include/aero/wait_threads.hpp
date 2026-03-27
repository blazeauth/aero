#pragma once

namespace aero {

  namespace detail {
    struct wait_threads_tag {};
  } // namespace detail

  [[maybe_unused]] constexpr inline detail::wait_threads_tag wait_threads;

} // namespace aero
