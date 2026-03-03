#ifndef AERO_IO_RUNTIME_HPP
#define AERO_IO_RUNTIME_HPP

#include <atomic>
#include <exception>
#include <functional>
#include <latch>
#include <thread>
#include <utility>
#include <vector>

#include "asio/any_io_executor.hpp"
#include "asio/executor_work_guard.hpp"
#include "asio/io_context.hpp"

#include "aero/error.hpp"
#include "aero/wait_threads.hpp"

namespace aero {

  using threads_count_t = std::size_t;

  class io_runtime {
    using wait_threads_tag = aero::detail::wait_threads_tag;

   public:
    using thread = std::thread;
    using thread_id = thread::id;
    using thread_init_callback = std::function<void(thread_id)>;
    using exception_callback = std::function<void(std::exception_ptr)>;

    io_runtime(): io_runtime(1) {}
    explicit io_runtime(wait_threads_tag): io_runtime(1, aero::wait_threads) {}

    explicit io_runtime(thread_init_callback init_callback): io_runtime(1, std::move(init_callback)) {}
    explicit io_runtime(thread_init_callback init_callback, wait_threads_tag)
      : io_runtime(1, std::move(init_callback), aero::wait_threads) {}

    explicit io_runtime(exception_callback exception_callback): io_runtime(1, std::move(exception_callback)) {}
    explicit io_runtime(exception_callback exception_callback, wait_threads_tag)
      : io_runtime(1, std::move(exception_callback), aero::wait_threads) {}

    explicit io_runtime(thread_init_callback init_callback, exception_callback exception_callback, wait_threads_tag)
      : io_runtime(1, std::move(init_callback), std::move(exception_callback), aero::wait_threads) {}

    explicit io_runtime(threads_count_t num_threads): work_guard_(asio::make_work_guard(io_context_)) {
      run_threads(num_threads, {});
    }

    explicit io_runtime(threads_count_t num_threads, wait_threads_tag): work_guard_(asio::make_work_guard(io_context_)) {
      run_threads(num_threads, {}, aero::wait_threads);
    }

    explicit io_runtime(threads_count_t num_threads, thread_init_callback init_callback)
      : work_guard_(asio::make_work_guard(io_context_)) {
      run_threads(num_threads, std::move(init_callback));
    }

    explicit io_runtime(threads_count_t num_threads, thread_init_callback init_callback, wait_threads_tag)
      : work_guard_(asio::make_work_guard(io_context_)) {
      run_threads(num_threads, std::move(init_callback), aero::wait_threads);
    }

    explicit io_runtime(threads_count_t num_threads, exception_callback exception_callback)
      : work_guard_(asio::make_work_guard(io_context_)), exception_callback_(std::move(exception_callback)) {
      run_threads(num_threads, {});
    }

    explicit io_runtime(threads_count_t num_threads, exception_callback exception_callback, wait_threads_tag)
      : work_guard_(asio::make_work_guard(io_context_)), exception_callback_(std::move(exception_callback)) {
      run_threads(num_threads, {}, aero::wait_threads);
    }

    explicit io_runtime(threads_count_t num_threads, thread_init_callback init_callback, exception_callback exception_callback)
      : work_guard_(asio::make_work_guard(io_context_)), exception_callback_(std::move(exception_callback)) {
      run_threads(num_threads, std::move(init_callback));
    }

    explicit io_runtime(threads_count_t num_threads, thread_init_callback init_callback, exception_callback exception_callback,
      wait_threads_tag)
      : work_guard_(asio::make_work_guard(io_context_)), exception_callback_(std::move(exception_callback)) {
      run_threads(num_threads, std::move(init_callback), aero::wait_threads);
    }

    io_runtime(io_runtime&&) = delete;
    io_runtime(const io_runtime&) = delete;
    void operator=(const io_runtime&) = delete;
    void operator=(io_runtime&&) = delete;

    // Destructor should not be invoked from worker thread,
    // otherwise, the std::terminate will be called
    ~io_runtime() {
      std::ignore = stop();
    }

    [[nodiscard]] std::size_t threads_count() const noexcept {
      return threads_.size();
    }

    [[nodiscard]] asio::any_io_executor get_executor() {
      return io_context_.get_executor();
    }

    [[nodiscard]] asio::io_context& native_handle() {
      return io_context_;
    }

    void request_stop() {
      if (!stop_signaled_.exchange(true)) {
        work_guard_.reset();
        io_context_.stop();
      }
    }

    [[nodiscard]] std::error_code join() {
      const auto current_thread_id = std::this_thread::get_id();
      bool join_called_from_worker_thread = false;

      for (auto& worker_thread : threads_) {
        if (!worker_thread.joinable()) {
          continue;
        }
        if (worker_thread.get_id() == current_thread_id) {
          join_called_from_worker_thread = true;
          continue;
        }
        worker_thread.join();
      }

      std::erase_if(threads_, [](const thread& worker_thread) { return !worker_thread.joinable(); });

      if (join_called_from_worker_thread) {
        return aero::error::basic_error::deadlock_would_occur;
      }
      return {};
    }

    [[nodiscard]] std::error_code stop() {
      request_stop();
      const auto join_error = join();
      if (!join_error && threads_.empty()) {
        threads_.shrink_to_fit();
      }
      return join_error;
    }

   private:
    void run_threads(threads_count_t num_threads, thread_init_callback init_callback) {
      threads_.reserve(threads_.size() + num_threads);

      for (threads_count_t i{}; i < num_threads; i++) {
        threads_.emplace_back([this, init_callback] {
          invoke_nothrow_init_callback(init_callback);
          thread_loop();
        });
      }
    }

    void run_threads(threads_count_t num_threads, thread_init_callback init_callback, wait_threads_tag) {
      std::latch latch{static_cast<std::ptrdiff_t>(num_threads)};
      threads_.reserve(threads_.size() + num_threads);

      for (threads_count_t i{}; i < num_threads; i++) {
        threads_.emplace_back([this, &latch, init_callback] {
          invoke_nothrow_init_callback(init_callback);
          latch.count_down();
          thread_loop();
        });
      }

      latch.wait();
    }

    void thread_loop() {
      for (;;) {
        try {
          io_context_.run();
          return;
        } catch (...) {
          if (exception_callback_) {
            exception_callback_(std::current_exception());
            continue;
          }
          return;
        }
      }
    }

    void invoke_nothrow_init_callback(const thread_init_callback& callback) {
      if (!callback) {
        return;
      }

      try {
        callback(std::this_thread::get_id());
      } catch (...) {
        if (exception_callback_) {
          exception_callback_(std::current_exception());
        }
      }
    }

    asio::io_context io_context_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    std::vector<thread> threads_;
    std::function<void(std::exception_ptr)> exception_callback_;
    std::atomic<bool> stop_signaled_{false};
  };

} // namespace aero

#endif
