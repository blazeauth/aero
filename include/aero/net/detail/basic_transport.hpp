#pragma once

#include <cstddef>
#include <deque>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <asio/any_io_executor.hpp>
#include <asio/async_result.hpp>
#include <asio/awaitable.hpp>
#include <asio/basic_stream_socket.hpp>
#include <asio/bind_allocator.hpp>
#include <asio/buffer.hpp>
#include <asio/co_composed.hpp>
#include <asio/co_spawn.hpp>
#include <asio/completion_condition.hpp>
#include <asio/detached.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/read_until.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/write.hpp>

#include "aero/detail/aligned_allocator.hpp"
#include "aero/net/concepts/transport.hpp"

namespace aero::net::detail {

  template <typename Stream>
  class basic_transport final {
   public:
    using stream_type = Stream;
    using mutable_buffer = std::vector<std::byte>;
    using const_buffer = std::span<const std::byte>;
    using duration = asio::steady_timer::duration;
    using executor_type = asio::any_io_executor;

    constexpr static auto default_buffer_size = 32 * 1024;

    explicit basic_transport(executor_type executor, std::size_t buffer_size = default_buffer_size)
      : buffer_(buffer_size), strand_(asio::make_strand(executor)), stream_(strand_) {}

    explicit basic_transport(asio::strand<executor_type> strand, std::size_t buffer_size = default_buffer_size)
      : buffer_(buffer_size), strand_(std::move(strand)), stream_(strand_) {}

    template <typename... StreamArgs>
    explicit basic_transport(executor_type executor, std::size_t buffer_size, std::in_place_type_t<stream_type>,
      StreamArgs&&... stream_args)
      : buffer_(buffer_size),
        strand_(asio::make_strand(executor)),
        stream_(strand_, std::forward<StreamArgs>(stream_args)...) {}

    template <typename... StreamArgs>
    explicit basic_transport(asio::strand<executor_type> strand, std::size_t buffer_size, std::in_place_type_t<stream_type>,
      StreamArgs&&... stream_args)
      : buffer_(buffer_size), strand_(std::move(strand)), stream_(strand_, std::forward<StreamArgs>(stream_args)...) {}

    template <typename... StreamArgs>
    explicit basic_transport(executor_type executor, std::in_place_type_t<stream_type>, StreamArgs&&... stream_args)
      : buffer_(default_buffer_size),
        strand_(asio::make_strand(executor)),
        stream_(strand_, std::forward<StreamArgs>(stream_args)...) {}

    template <typename... StreamArgs>
    explicit basic_transport(asio::strand<executor_type> strand, std::in_place_type_t<stream_type>, StreamArgs&&... stream_args)
      : buffer_(default_buffer_size), strand_(std::move(strand)), stream_(strand_, std::forward<StreamArgs>(stream_args)...) {}

    template <typename CompletionToken>
    auto async_read_some(CompletionToken&& token) {
      auto bound_token = asio::bind_allocator(aero::detail::aligned_allocator<>{}, std::forward<CompletionToken>(token));

      return asio::async_initiate<decltype(bound_token), void(std::error_code, const_buffer)>(
        asio::co_composed<void(std::error_code, const_buffer)>(
          [this](auto) -> void {
            auto [read_ec, bytes_read] = co_await stream_.async_read_some(get_mutable_buffer(), asio::as_tuple(asio::deferred));
            if (read_ec) {
              co_return {read_ec, const_buffer{}};
            }
            co_return {std::error_code{}, get_buffer_view(0, bytes_read)};
          },
          strand_),
        bound_token);
    }

    template <typename CompletionToken>
    auto async_read_until(mutable_buffer& out_buffer, std::string_view delimiter, CompletionToken&& token) {
      return asio::async_read_until(stream_, asio::dynamic_buffer(out_buffer), delimiter, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_read_exactly(std::size_t bytes_count, CompletionToken&& token) {
      auto bound_token = asio::bind_allocator(aero::detail::aligned_allocator<>{}, std::forward<CompletionToken>(token));

      return asio::async_initiate<decltype(bound_token), void(std::error_code, const_buffer)>(
        asio::co_composed<void(std::error_code, const_buffer)>(
          [this, bytes_count](auto) -> void {
            auto [read_ec, bytes_read] = co_await asio::async_read(stream_,
              get_mutable_buffer(),
              asio::transfer_exactly(bytes_count),
              asio::as_tuple(asio::deferred));
            if (read_ec) {
              co_return {read_ec, const_buffer{}};
            }
            co_return {std::error_code{}, get_buffer_view(0, bytes_read)};
          },
          strand_),
        bound_token);
    }

    template <typename CompletionToken>
    auto async_write(const_buffer buffer, CompletionToken&& token) {
      auto bound_token = asio::bind_allocator(aero::detail::aligned_allocator<>{}, std::forward<CompletionToken>(token));

      return asio::async_initiate<decltype(bound_token), void(std::error_code, std::size_t)>(
        asio::co_composed<void(std::error_code, std::size_t)>(
          [this](auto, const_buffer buffer) -> void {
            asio::const_buffer asio_buffer(buffer.data(), buffer.size());

            auto write_request = std::make_shared<queued_write_request>(strand_, asio_buffer);
            pending_write_requests_.push_back(write_request);

            if (!write_loop_active_) {
              write_loop_active_ = true;
              asio::co_spawn(strand_, write_loop(), asio::detached);
            }

            // Use timer as a condition variable here. 'write_loop()' will cancel
            // 'completion_timer' that will cause 'async_wait' completion
            std::ignore = co_await write_request->completion_timer.async_wait(asio::as_tuple(asio::deferred));

            co_return {write_request->result.ec, write_request->result.bytes_written};
          },
          strand_),
        bound_token,
        buffer);
    }

    [[nodiscard]] executor_type get_executor() const noexcept {
      return executor_type(strand_);
    }

    [[nodiscard]] asio::strand<executor_type> get_strand() const noexcept {
      return strand_;
    }

    stream_type& stream() {
      return stream_;
    }

    typename stream_type::lowest_layer_type& lowest_layer() {
      return stream_.lowest_layer();
    }

    mutable_buffer& buffer() {
      return buffer_;
    }

   private:
    asio::mutable_buffer get_mutable_buffer() {
      return {buffer_.data(), buffer_.size()};
    }

    [[nodiscard]] const_buffer get_buffer_view(std::size_t offset = 0, std::size_t count = std::dynamic_extent) const noexcept {
      return const_buffer{buffer_}.subspan(offset, count);
    }

    struct queued_write_request {
      // NOLINTNEXTLINE(*-pass-by-value)
      explicit queued_write_request(asio::any_io_executor executor, asio::const_buffer payload)
        : buffer(payload), completion_timer(executor) {
        completion_timer.expires_at((std::chrono::steady_clock::time_point::max)());
      }

      void complete(std::error_code ec, std::size_t bytes_written) {
        result.ec = ec;
        result.bytes_written = bytes_written;
        notify_completion();
      }

      void notify_completion() {
        completion_timer.cancel();
      }

      asio::const_buffer buffer;
      asio::steady_timer completion_timer;
      struct {
        std::error_code ec;
        std::size_t bytes_written{0};
      } result;
    };

    asio::awaitable<void> write_loop() {
      for (;;) {
        if (pending_write_requests_.empty()) {
          write_loop_active_ = false;
          co_return;
        }

        auto write_request = std::move(pending_write_requests_.front());
        pending_write_requests_.pop_front();

        auto [write_ec, bytes_written] =
          co_await asio::async_write(stream_, write_request->buffer, asio::as_tuple(asio::use_awaitable));

        write_request->complete(write_ec, bytes_written);

        if (write_ec) {
          drain_pending_write_requests(write_ec);
          write_loop_active_ = false;
          co_return;
        }
      }
    }

    void drain_pending_write_requests(std::error_code ec) {
      while (!pending_write_requests_.empty()) {
        auto pending = std::move(pending_write_requests_.front());
        pending_write_requests_.pop_front();

        pending->complete(ec, 0);
      }
    }

    mutable_buffer buffer_;
    asio::strand<asio::any_io_executor> strand_;
    stream_type stream_;
    std::deque<std::shared_ptr<queued_write_request>> pending_write_requests_;
    bool write_loop_active_ = false;
  };

  static_assert(concepts::basic_transport<basic_transport<asio::basic_stream_socket<asio::ip::tcp>>>);

} // namespace aero::net::detail
