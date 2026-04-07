#pragma once

#include <asio/any_io_executor.hpp>

#include "aero/io_runtime.hpp"
#include "aero/wait_threads.hpp"

namespace aero {

  [[nodiscard]] inline aero::io_runtime& get_default_runtime() {
    // \todo: In the future, we definitely need to move away from this
    // approach; it might be worth implementing our own thread pool with
    // a dynamic number of threads to support high-load scenarios.
    // The current default runtime is only suitable for clients without
    // high thread contention; we should standardize this in the future.
    // asio::system_context and its executor do not suit us, as they
    // create a static number of threads where N=hardware_concurrency*2
    // with a fallback to N=2, which is unsuitable for typical client
    // scenarios, since it pre-allocates too many resources.
    // asio::thread_pool does not seem like a suitable solution here
    // either, as it cannot dynamically resize the thread pool.
    // Most likely, in the future, we will simply extend the io_runtime
    // implementation and delegate this task to it

    // This is an intentional leak, since the default runtime must
    // be process-lifetime after the first access, and creating an
    // instance using 'new' avoids shutdown-order issues
    static auto& runtime{*new aero::io_runtime{1, aero::wait_threads}};
    return runtime;
  }

  [[nodiscard]] inline asio::any_io_executor get_default_executor() {
    return get_default_runtime().get_executor();
  }

} // namespace aero
