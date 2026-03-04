#ifndef AERO_WEBSOCKET_BASIC_CLIENT_HPP
#define AERO_WEBSOCKET_BASIC_CLIENT_HPP

#include <atomic>
#include <chrono>
#include <expected>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <asio/as_tuple.hpp>
#include <asio/async_result.hpp>
#include <asio/cancel_after.hpp>
#include <asio/cancellation_state.hpp>
#include <asio/co_composed.hpp>
#include <asio/co_spawn.hpp>
#include <asio/error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include "aero/deadline.hpp"
#include "aero/detail/final_action.hpp"
#include "aero/error.hpp"
#include "aero/http/headers.hpp"
#include "aero/io_runtime.hpp"
#include "aero/net/concepts/transport.hpp"
#include "aero/websocket/client_handshaker.hpp"
#include "aero/websocket/client_options.hpp"
#include "aero/websocket/close_code.hpp"
#include "aero/websocket/detail/client_frame_builder.hpp"
#include "aero/websocket/detail/frame.hpp"
#include "aero/websocket/detail/message_assembler.hpp"
#include "aero/websocket/error.hpp"
#include "aero/websocket/message.hpp"
#include "aero/websocket/state.hpp"
#include "aero/websocket/uri.hpp"

namespace aero::websocket {

  template <net::concepts::transport Transport>
  class basic_client {
    using protocol_error = websocket::error::protocol_error;
    constexpr static std::span<const std::byte> null_bytes{};
    constexpr static std::chrono::seconds default_close_timeout{5};

   public:
    using transport_type = Transport;
    using duration = std::chrono::steady_clock::duration;
    using executor_type = typename transport_type::executor_type;

    basic_client()
      : io_runtime_(std::make_unique<aero::io_runtime>(threads_count_t{1}, aero::wait_threads)),
        transport_(io_runtime_->get_executor()) {}

    explicit basic_client(executor_type executor): transport_(executor) {}
    explicit basic_client(asio::strand<executor_type> strand): transport_(std::move(strand)) {}

    explicit basic_client(client_options options)
      : io_runtime_(std::make_unique<aero::io_runtime>(threads_count_t{1}, aero::wait_threads)),
        client_frame_builder_({
          .validate_utf8 = options.validate_outcoming_utf8,
        }),
        client_handshaker_(options.client_handshaker),
        transport_(io_runtime_->get_executor(), options.max_message_size + detail::max_frame_header_size) {}

    explicit basic_client(executor_type executor, client_options options)
      : client_frame_builder_({
          .validate_utf8 = options.validate_outcoming_utf8,
        }),
        client_handshaker_(options.client_handshaker),
        transport_(executor, options.max_message_size + detail::max_frame_header_size) {}

    explicit basic_client(asio::strand<executor_type> strand, client_options options)
      : client_frame_builder_({
          .validate_utf8 = options.validate_outcoming_utf8,
        }),
        client_handshaker_(options.client_handshaker),
        transport_(std::move(strand), options.max_message_size + detail::max_frame_header_size) {}

    template <typename... TransportArgs>
    explicit basic_client(std::in_place_type_t<transport_type>, TransportArgs&&... transport_args)
      : io_runtime_(std::make_unique<aero::io_runtime>(threads_count_t{1})),
        transport_(io_runtime_->get_executor(), std::forward<TransportArgs>(transport_args)...) {}

    template <typename... TransportArgs>
    explicit basic_client(client_options options, std::in_place_type_t<transport_type>, TransportArgs&&... transport_args)
      : io_runtime_(std::make_unique<aero::io_runtime>(threads_count_t{1})),
        client_frame_builder_({
          .validate_utf8 = options.validate_outcoming_utf8,
        }),
        client_handshaker_(options.client_handshaker),
        transport_(io_runtime_->get_executor(), std::forward<TransportArgs>(transport_args)...,
          options.max_message_size + detail::max_frame_header_size) {}

    template <typename... TransportArgs>
    explicit basic_client(executor_type executor, std::in_place_type_t<transport_type>, TransportArgs&&... transport_args)
      : transport_(executor, std::forward<TransportArgs>(transport_args)...) {}

    template <typename... TransportArgs>
    explicit basic_client(asio::strand<executor_type> strand, std::in_place_type_t<transport_type>,
      TransportArgs&&... transport_args)
      : transport_(std::move(strand), std::forward<TransportArgs>(transport_args)...) {}

    template <typename... TransportArgs>
    explicit basic_client(executor_type executor, client_options options, std::in_place_type_t<transport_type>,
      TransportArgs&&... transport_args)
      : client_frame_builder_({
          .validate_utf8 = options.validate_outcoming_utf8,
        }),
        client_handshaker_(options.client_handshaker),
        transport_(executor, std::forward<TransportArgs>(transport_args)...,
          options.max_message_size + detail::max_frame_header_size) {}

    template <typename... TransportArgs>
    explicit basic_client(asio::strand<executor_type> strand, client_options options, std::in_place_type_t<transport_type>,
      TransportArgs&&... transport_args)
      : client_frame_builder_({
          .validate_utf8 = options.validate_outcoming_utf8,
        }),
        client_handshaker_(options.client_handshaker),
        transport_(std::move(strand), std::forward<TransportArgs>(transport_args)...,
          options.max_message_size + detail::max_frame_header_size) {}

    template <typename CompletionToken>
    auto async_connect(websocket::uri uri, http::headers headers, CompletionToken&& token) {
      return asio::async_initiate<CompletionToken, void(std::error_code, http::headers)>(
        asio::co_composed<void(std::error_code, http::headers)>(
          [this](auto, websocket::uri uri, http::headers headers) -> void {
            reset_connection_state(state::connecting);

            auto [connect_ec] =
              co_await transport_.async_connect(std::string{uri.host()}, uri.port(), return_as_deferred_tuple());
            if (connect_ec) {
              std::ignore = co_await async_finalize_session({}, return_as_deferred_tuple());
              co_return {connect_ec, http::headers{}};
            }

            // Build bodyless HTTP websocket upgrade request
            auto handshake = client_handshaker_.build_request(uri, std::move(headers));
            if (!handshake) {
              std::ignore = co_await async_finalize_session({}, return_as_deferred_tuple());
              co_return {handshake.error(), http::headers{}};
            }

            auto [write_ec, bytes_written] = co_await transport_.async_write(handshake->bytes(), return_as_deferred_tuple());
            if (write_ec) {
              std::ignore = co_await async_finalize_session({}, return_as_deferred_tuple());
              co_return {write_ec, http::headers{}};
            }

            std::vector<std::byte> response_buffer;

            // Read server response until "\r\n\r\n"
            auto [read_ec, bytes_read] = co_await transport_.async_read_until(response_buffer,
              http::detail::headers_end_separator,
              return_as_deferred_tuple());
            if (read_ec) {
              std::ignore = co_await async_finalize_session({}, return_as_deferred_tuple());
              co_return {read_ec, http::headers{}};
            }

            // https://www.boost.org/doc/libs/1_43_0/doc/html/boost_asio/reference/async_read_until.html
            // "After a successful async_read_until operation, the streambuf
            // may contain additional data beyond the delimiter"
            const bool buffer_has_data_after_delimiter = response_buffer.size() > bytes_read;
            if (buffer_has_data_after_delimiter) {
              auto data_after_handshake = std::span{response_buffer}.subspan(bytes_read);
              data_received_in_handshake_ = std::vector{std::from_range, data_after_handshake};
            }

            std::string_view handshake_response{reinterpret_cast<const char*>(response_buffer.data()), bytes_read};

            // Perform upgrade challenge with server handshake response
            auto parsed_headers = client_handshaker_.parse_response(handshake_response, handshake->sec_websocket_key);
            if (!parsed_headers.has_value()) {
              std::ignore = co_await async_finalize_session({}, return_as_deferred_tuple());
              co_return {parsed_headers.error(), http::headers{}};
            }

            set_connection_state(state::open);
            co_return {std::error_code{}, std::move(*parsed_headers)};
          },
          transport_.get_strand()),
        token,
        std::move(uri),
        std::move(headers));
    }

    template <typename CompletionToken>
    auto async_connect(std::expected<websocket::uri, std::error_code> parsed_uri, http::headers headers,
      CompletionToken&& token) {
      return asio::async_initiate<CompletionToken, void(std::error_code, http::headers)>(
        asio::co_composed<void(std::error_code, http::headers)>(
          [this](auto, std::expected<websocket::uri, std::error_code> parsed_uri, http::headers headers) -> void {
            if (!parsed_uri.has_value()) {
              co_return {parsed_uri.error(), http::headers{}};
            }

            co_return co_await this->async_connect(std::move(*parsed_uri), std::move(headers), return_as_deferred_tuple());
          },
          transport_.get_strand()),
        token,
        std::move(parsed_uri),
        std::move(headers));
    }

    template <typename CompletionToken>
    auto async_connect(std::string_view uri, http::headers headers, CompletionToken&& token) {
      return async_connect(websocket::uri::parse(uri), std::move(headers), std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_connect(websocket::uri uri, CompletionToken&& token) {
      return async_connect(std::move(uri), http::headers{}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_connect(std::expected<websocket::uri, std::error_code> parsed_uri, CompletionToken&& token) {
      return async_connect(std::move(parsed_uri), http::headers{}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_connect(std::string_view uri, CompletionToken&& token) {
      return async_connect(uri, http::headers{}, std::forward<CompletionToken>(token));
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    template <typename CompletionToken>
    auto async_send_text(std::string_view text, CompletionToken&& token) {
      return asio::async_initiate<CompletionToken, void(std::error_code)>(
        asio::co_composed<void(std::error_code)>(
          [this](auto, std::string_view text) -> void {
            if (!is_current_state(state::open) || is_close_received()) {
              co_return protocol_error::connection_closed;
            }

            auto frame = client_frame_builder_.build_text_frame(text);
            if (!frame) {
              co_return frame.error();
            }

            co_return co_await async_write_bytes(*frame, return_as_deferred_tuple());
          },
          transport_.get_strand()),
        token,
        text);
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    template <typename CompletionToken>
    auto async_send_binary(std::span<const std::byte> data, CompletionToken&& token) {
      return asio::async_initiate<CompletionToken, void(std::error_code)>(
        asio::co_composed<void(std::error_code)>(
          [this](auto, std::span<const std::byte> data) -> void {
            if (!is_current_state(state::open) || is_close_received()) {
              co_return protocol_error::connection_closed;
            }

            auto frame = client_frame_builder_.build_binary_frame(data);
            if (!frame) {
              co_return frame.error();
            }

            co_return co_await async_write_bytes(*frame, return_as_deferred_tuple());
          },
          transport_.get_strand()),
        token,
        data);
    }

    template <typename CompletionToken>
    auto async_ping(std::string_view text, CompletionToken&& token) {
      std::span text_bytes(reinterpret_cast<const std::byte*>(text.data()), text.size());
      return async_ping(text_bytes, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_ping(CompletionToken&& token) {
      return async_ping(null_bytes, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_ping(std::span<const std::byte> data, CompletionToken&& token) {
      return asio::async_initiate<CompletionToken, void(std::error_code)>(
        asio::co_composed<void(std::error_code)>(
          [this](auto, std::span<const std::byte> data) -> void {
            if (!is_current_state(state::open) || is_close_received()) {
              co_return protocol_error::connection_closed;
            }

            auto frame = client_frame_builder_.build_ping_frame(data);
            if (!frame) {
              co_return frame.error();
            }

            co_return co_await async_write_bytes(*frame, return_as_deferred_tuple());
          },
          transport_.get_strand()),
        token,
        data);
    }

    template <typename CompletionToken>
    auto async_pong(std::span<const std::byte> data, CompletionToken&& token) {
      return asio::async_initiate<CompletionToken, void(std::error_code)>(
        asio::co_composed<void(std::error_code)>(
          [this](auto, std::span<const std::byte> data) -> void {
            if (!is_current_state(state::open, state::closing) || is_close_received()) {
              co_return protocol_error::connection_closed;
            }

            auto frame = client_frame_builder_.build_pong_frame(data);
            if (!frame) {
              co_return frame.error();
            }

            co_return co_await async_write_bytes(*frame, return_as_deferred_tuple());
          },
          transport_.get_strand()),
        token,
        data);
    }

    template <typename CompletionToken>
    auto async_pong(std::string_view text, CompletionToken&& token) {
      std::span text_bytes(reinterpret_cast<const std::byte*>(text.data()), text.size());
      return async_pong(text_bytes, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_pong(CompletionToken&& token) {
      return async_pong(null_bytes, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_close(websocket::close_code code, std::string_view reason, CompletionToken&& token) {
      return asio::async_initiate<CompletionToken, void(std::error_code)>(
        asio::co_composed<void(std::error_code)>(
          [this](auto, websocket::close_code close_code, std::string_view close_reason) -> void {
            if (is_close_code_server_only(close_code)) {
              co_return protocol_error::close_code_server_only;
            }

            if (is_current_state(state::closed)) {
              co_return protocol_error::connection_closed;
            }

            if (is_current_state(state::closing)) {
              co_return protocol_error::already_closing;
            }

            close_result_ec_.reset();
            set_connection_state(state::closing);

            auto [send_close_ec] = co_await async_send_close(close_code, close_reason, return_as_deferred_tuple());
            if (send_close_ec) {
              co_return co_await async_finalize_session(send_close_ec, return_as_deferred_tuple());
            }

            aero::deadline close_deadline{default_close_timeout};
            consume_data_received_in_handshake_if_present();

            // Avoid "lost wakeup" problem
            if (auto result = take_close_result()) {
              co_return *result;
            }

            // An async_read is in progress, we wait for it to receive
            // the close frame response from peer (or to timeout)
            if (is_read_loop_active()) {
              close_timer_.expires_after(close_deadline.remaining());

              // Avoid "lost wakeup" problem
              if (auto result = take_close_result()) {
                co_return *result;
              }

              auto [wait_ec] = co_await close_timer_.async_wait(asio::as_tuple(asio::deferred));
              if (!wait_ec) {
                // Peer close response was not received, timed out
                co_return co_await async_finalize_session(asio::error::timed_out, return_as_deferred_tuple());
              }

              // Close response was received in read loop and it woke up our 'close_timer_'
              if (is_canceled(wait_ec)) {
                if (auto result = take_close_result()) {
                  co_return *result;
                }
                co_return std::error_code{};
              }

              // Unexpected error from timer
              co_return co_await async_finalize_session(wait_ec, return_as_deferred_tuple());
            }

            // If no read loop active, then start our own read-loop
            // until close frame is received or timeout expires
            for (;;) {
              if (close_deadline.expired()) {
                co_return co_await async_finalize_session(asio::error::timed_out, return_as_deferred_tuple());
              }

              auto [read_ec, message] =
                co_await async_read(asio::cancel_after(close_deadline.remaining(), return_as_deferred_tuple()));

              if (read_ec) {
                // Read was canceled due to timeout expiring
                if (is_canceled(read_ec) && close_deadline.expired()) {
                  co_return co_await async_finalize_session(asio::error::timed_out, return_as_deferred_tuple());
                }

                // Consider any other error as transport fail
                co_return co_await async_finalize_session(read_ec, return_as_deferred_tuple());
              }

              // Received peer's close response – handshake complete
              if (message.is_close()) {
                co_return co_await async_finalize_session(std::error_code{}, return_as_deferred_tuple());
              }
            }
          },
          transport_.get_strand()),
        token,
        code,
        reason);
    }

    template <typename CompletionToken>
    auto async_close(websocket::close_code code, CompletionToken&& token) {
      return async_close(code, "", std::forward<CompletionToken>(token));
    }

    // Tear down transport without performing close handshake
    template <typename CompletionToken>
    auto async_force_close(CompletionToken&& token) {
      return async_finalize_session(std::error_code{}, token);
    }

    template <typename CompletionToken>
    auto async_read(CompletionToken&& token) {
      return asio::async_initiate<CompletionToken, void(std::error_code, websocket::message)>(
        asio::co_composed<void(std::error_code, websocket::message)>(
          [this](auto) -> void {
            // Prevent concurrent async_read operations (one read at a time)
            if (is_read_loop_active()) {
              co_return {protocol_error::already_reading, websocket::message{}};
            }

            set_read_loop_active_flag(true);
            auto _ = aero::detail::finally([this] { set_read_loop_active_flag(false); });

            for (;;) {
              // If a close handshake is in progress or connection is closed, stop reading
              if (is_current_state(state::closed) || is_close_received()) {
                co_return {protocol_error::connection_closed, websocket::message{}};
              }

              consume_data_received_in_handshake_if_present();

              // Deliver next assembled message if available
              if (auto message = message_assembler_.poll()) {
                if (message->is_control()) {
                  // Auto-respond to control frames
                  auto [response_ec] = co_await async_respond_to_control_message(*message, return_as_deferred_tuple());
                  if (response_ec) {
                    co_return {response_ec, websocket::message{}};
                  }

                  // Received a close frame – send close reply (if not sent) and finalize session
                  // Also wakes up any pending async_close waiting on a timer
                  if (message->is_close()) {
                    auto [final_ec] = co_await async_finalize_session(std::error_code{}, return_as_deferred_tuple());
                    if (final_ec) {
                      co_return {final_ec, websocket::message{}};
                    }
                    // Return the close message
                    co_return {std::error_code{}, *message};
                  }

                  co_return {std::error_code{}, *message};
                }

                if (is_current_state(state::closing) || is_close_sent()) {
                  continue;
                }

                // Return any non-control or handled control message to the caller
                co_return {std::error_code{}, *message};
              }

              // If a deferred error was stored (e.g. from a previous consume), handle it now
              if (deferred_read_ec_) {
                auto deferred_read_ec = *deferred_read_ec_;
                deferred_read_ec_.reset();
                if (is_fatal_websocket_error(deferred_read_ec)) {
                  co_await async_fail_connection(deferred_read_ec, return_as_deferred_tuple());
                }
                co_return {deferred_read_ec, websocket::message{}};
              }

              auto [read_ec, read_buffer] = co_await transport_.async_read_some(return_as_deferred_tuple());
              if (read_ec) {
                // The read was canceled (possibly by async_close or timeout)
                if (is_canceled(read_ec)) {
                  co_return {read_ec, websocket::message{}};
                }

                // Unexpected transport error – fail the WebSocket connection (RFC 6455 7.2.1)
                auto [final_ec] = co_await async_finalize_session(read_ec, return_as_deferred_tuple());

                // Forward unexpected transport errors to a caller for better
                // understanding of why the transport was closed, who initiated
                // the closure, whether it was broken unexpectedly, etc.
                co_return {final_ec, websocket::message{}};
              }

              // Consume incoming bytes into WebSocket frames/messages
              auto consume_ec = message_assembler_.consume(read_buffer);
              if (consume_ec && !deferred_read_ec_) {
                // Store the first error to report after delivering any remaining message
                deferred_read_ec_ = consume_ec;
              }

              // Loop continues to check for assembled messages or handle errors
            }
          },
          transport_.get_strand()),
        token);
    }

    std::expected<http::headers, std::error_code> connect(websocket::uri uri, http::headers headers) {
      return synchronize_awaitable<http::headers>(
        async_connect(std::move(uri), std::move(headers), return_as_awaitable_tuple()));
    }

    std::expected<http::headers, std::error_code> connect(websocket::uri uri, http::headers headers, duration timeout) {
      return synchronize_awaitable<http::headers>(
        async_connect(std::move(uri), std::move(headers), asio::cancel_after(timeout, return_as_awaitable_tuple())));
    }

    std::expected<http::headers, std::error_code> connect(std::expected<websocket::uri, std::error_code> parsed_uri,
      http::headers headers) {
      if (!parsed_uri) {
        return std::unexpected(parsed_uri.error());
      }
      return connect(std::move(parsed_uri.value()), std::move(headers));
    }

    std::expected<http::headers, std::error_code> connect(std::expected<websocket::uri, std::error_code> parsed_uri,
      http::headers headers, duration timeout) {
      if (!parsed_uri) {
        return std::unexpected(parsed_uri.error());
      }
      return connect(std::move(parsed_uri.value()), std::move(headers), timeout);
    }

    std::expected<http::headers, std::error_code> connect(std::string_view uri_string, http::headers headers) {
      return connect(websocket::uri::parse(uri_string), std::move(headers));
    }

    std::expected<http::headers, std::error_code> connect(std::string_view uri_string, http::headers headers,
      duration timeout) {
      return connect(websocket::uri::parse(uri_string), std::move(headers), timeout);
    }

    std::expected<http::headers, std::error_code> connect(websocket::uri uri) {
      return connect(std::move(uri), http::headers{});
    }

    std::expected<http::headers, std::error_code> connect(websocket::uri uri, duration timeout) {
      return connect(std::move(uri), http::headers{}, timeout);
    }

    std::expected<http::headers, std::error_code> connect(std::expected<websocket::uri, std::error_code> parsed_uri) {
      return connect(std::move(parsed_uri), http::headers{});
    }

    std::expected<http::headers, std::error_code> connect(std::expected<websocket::uri, std::error_code> parsed_uri,
      duration timeout) {
      return connect(std::move(parsed_uri), http::headers{}, timeout);
    }

    std::expected<http::headers, std::error_code> connect(std::string_view uri_string) {
      return connect(uri_string, http::headers{});
    }

    std::expected<http::headers, std::error_code> connect(std::string_view uri_string, duration timeout) {
      return connect(uri_string, http::headers{}, timeout);
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    std::error_code send_text(std::string_view text) {
      return synchronize_awaitable<std::error_code>(async_send_text(text, return_as_awaitable_tuple()));
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    std::error_code send_binary(std::span<const std::byte> data) {
      return synchronize_awaitable<std::error_code>(async_send_binary(data, return_as_awaitable_tuple()));
    }

    std::error_code ping() {
      return synchronize_awaitable<std::error_code>(async_ping(return_as_awaitable_tuple()));
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    std::error_code ping(std::string_view text) {
      return synchronize_awaitable<std::error_code>(async_ping(text, return_as_awaitable_tuple()));
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    std::error_code ping(std::span<const std::byte> data) {
      return synchronize_awaitable<std::error_code>(async_ping(data, return_as_awaitable_tuple()));
    }

    std::error_code pong() {
      return synchronize_awaitable<std::error_code>(async_pong(return_as_awaitable_tuple()));
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    std::error_code pong(std::string_view text) {
      return synchronize_awaitable<std::error_code>(async_pong(text, return_as_awaitable_tuple()));
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    std::error_code pong(std::span<const std::byte> data) {
      return synchronize_awaitable<std::error_code>(async_pong(data, return_as_awaitable_tuple()));
    }

    std::error_code close(websocket::close_code code) {
      return synchronize_awaitable<std::error_code>(async_close(code, return_as_awaitable_tuple()));
    }

    std::error_code close(websocket::close_code code, std::string reason) {
      return synchronize_awaitable<std::error_code>(async_close(code, reason, return_as_awaitable_tuple()));
    }

    std::error_code force_close() {
      return synchronize_awaitable<std::error_code>(async_force_close(return_as_awaitable_tuple()));
    }

    std::expected<websocket::message, std::error_code> read() {
      return synchronize_awaitable<websocket::message>(async_read(return_as_awaitable_tuple()));
    }

    std::expected<websocket::message, std::error_code> read(duration timeout) {
      return synchronize_awaitable<websocket::message>(async_read(asio::cancel_after(timeout, return_as_awaitable_tuple())));
    }

    [[nodiscard]] bool is_open_for_writing() const noexcept {
      return is_current_state(state::open) && !is_close_received();
    }

    [[nodiscard]] bool is_connecting() const noexcept {
      return is_current_state(state::connecting);
    }

    [[nodiscard]] bool is_closed() const noexcept {
      return is_current_state(state::closed);
    }

    [[nodiscard]] bool is_closing() const noexcept {
      return is_current_state(state::closing);
    }

    [[nodiscard]] executor_type get_executor() const noexcept {
      return transport_.get_executor();
    }

    [[nodiscard]] asio::strand<executor_type> get_strand() const noexcept {
      return transport_.get_strand();
    }

    [[nodiscard]] transport_type& transport() {
      return transport_;
    }

   private:
    static asio::as_tuple_t<asio::deferred_t> return_as_deferred_tuple() {
      return asio::as_tuple(asio::deferred);
    }

    static asio::as_tuple_t<asio::use_awaitable_t<>> return_as_awaitable_tuple() {
      return asio::as_tuple(asio::use_awaitable);
    }

    static bool is_canceled(std::error_code ec) {
      return ec == asio::error::operation_aborted;
    }

    static bool is_fatal_websocket_error(std::error_code ec) {
      return websocket::error::is_invalid_payload(ec) || websocket::error::is_protocol_violation(ec);
    }

    static websocket::close_code close_code_for_error(std::error_code ec) {
      if (websocket::error::is_invalid_payload(ec)) {
        return close_code::invalid_payload;
      }
      if (websocket::error::is_protocol_violation(ec)) {
        return close_code::protocol_error;
      }

      return {};
    }

    void consume_data_received_in_handshake_if_present() {
      if (!data_received_in_handshake_) {
        return;
      }

      auto consume_ec = message_assembler_.consume(*data_received_in_handshake_);
      data_received_in_handshake_.reset();
      if (consume_ec && !deferred_read_ec_) {
        deferred_read_ec_ = consume_ec;
      }
    }

    template <typename CompletionToken>
    auto async_write_bytes(std::span<const std::byte> frame, CompletionToken&& token) {
      return asio::async_initiate<CompletionToken, void(std::error_code)>(
        asio::co_composed<void(std::error_code)>(
          [this](auto, std::span<const std::byte> frame) -> void {
            auto [write_ec, bytes_written] = co_await transport_.async_write(frame, return_as_deferred_tuple());
            if (!write_ec) {
              co_return std::error_code{};
            }

            if (is_canceled(write_ec)) {
              co_return write_ec;
            }

            // async_write returned an error that was not a cancellation,
            // so we consider the failed write to be a transport loss
            // P.S. In the future, we may need to add more detailed error
            // checks, depending on what asio::async_write_some might return

            // RFC6455 - 7.2.1. Client-Initiated Closure:
            // If at any point the underlying transport layer connection is
            // unexpectedly lost, the client MUST _Fail the WebSocket Connection_.
            co_return co_await async_finalize_session(write_ec, return_as_deferred_tuple());
          },
          transport_.get_strand()),
        token,
        frame);
    }

    template <typename CompletionToken>
    auto async_send_close(websocket::close_code code, std::optional<std::string_view> reason, CompletionToken&& token) {
      return asio::async_initiate<CompletionToken, void(std::error_code)>(
        asio::co_composed<void(std::error_code)>(
          [this](auto, close_code code, std::optional<std::string_view> reason) -> void {
            if (is_close_sent()) {
              co_return std::error_code{};
            }

            auto close_frame = client_frame_builder_.build_close_frame(code, reason);
            if (!close_frame) {
              co_return close_frame.error();
            }

            auto [write_ec] = co_await async_write_bytes(*close_frame, return_as_deferred_tuple());
            if (write_ec) {
              co_return write_ec;
            }

            set_close_sent_flag(true);
            co_return std::error_code{};
          },
          transport_.get_strand()),
        token,
        code,
        std::move(reason));
    }

    // Fail fast websocket termination path
    // Use when we detected a fatal websocket violation (protocol error, invalid payload etc.)
    // and must actively fail the connection. Sends a 'close' frame with the appropriate error
    // code, drains a little, then force-shutdowns the transport and resets all internal state
    template <typename CompletionToken>
    auto async_fail_connection(std::error_code fatal_ec, CompletionToken&& token) {
      return asio::async_initiate<CompletionToken, void()>(
        asio::co_composed<void()>(
          [this](auto, std::error_code fatal_ec) -> void {
            using namespace std::chrono_literals;
            if (!is_current_state(state::closed)) {
              set_connection_state(state::closing);
            }

            co_await async_send_close(close_code_for_error(fatal_ec), std::nullopt, return_as_deferred_tuple());

            for (;;) {
              auto [read_ec, read_buffer] =
                co_await transport_.async_read_some(asio::cancel_after(1s, return_as_deferred_tuple()));
              if (read_ec) {
                break;
              }
            }

            set_close_received_flag(true);
            deferred_read_ec_.reset();
            data_received_in_handshake_.reset();
            message_assembler_.reset();

            // We don't care whether force-shutdown returned an error or not
            std::ignore = co_await async_finalize_session(fatal_ec, return_as_deferred_tuple());

            co_return {};
          },
          transport_.get_strand()),
        token,
        fatal_ec);
    }

    // Graceful connection finalization path
    // Use after a normal close handshake (or any non-fatal error) to move the client to
    // 'closed' state. Stops all further reads/writes, shutdowns the transport, and resets
    // internal state. This does not initiate a protocol failure, only finalizes/cleans up
    template <typename CompletionToken>
    auto async_finalize_session(std::error_code final_ec, CompletionToken&& token) {
      return asio::async_initiate<CompletionToken, void(std::error_code)>(
        asio::co_composed<void(std::error_code)>(
          [this](auto state, std::error_code final_ec) -> void {
            // Disable cancellation via the coroutine associated cancellation slot for cleanup path.
            // Imagine a situation where we enter 'async_finalize_session' and cancellation
            // has already been signalled through the associated cancellation slot. It means
            // that when we call 'transport_.async_shutdown', the shutdown operation could be
            // cancelled immediately and return 'operation_aborted', potentially leaving the
            // underlying websocket transport still open
            state.reset_cancellation_state(asio::disable_cancellation());

            if (is_current_state(state::closed)) {
              signal_close_completion(final_ec);
              co_return final_ec;
            }

            reset_connection_state(state::closed);

            auto [shutdown_ec] = co_await transport_.async_shutdown(return_as_deferred_tuple());
            std::error_code result_ec = final_ec ? final_ec : shutdown_ec;

            // Wake up any pending close operation
            signal_close_completion(result_ec);
            co_return result_ec;
          },
          transport_.get_strand()),
        token,
        final_ec);
    }

    template <typename CompletionToken>
    auto async_respond_to_control_message(const websocket::message& message, CompletionToken&& token) {
      return asio::async_initiate<CompletionToken, void(std::error_code)>(
        asio::co_composed<void(std::error_code)>(
          [this](auto, const websocket::message& message) -> void {
            if (message.is_ping()) {
              co_return co_await async_pong(message.payload, return_as_deferred_tuple());
            }

            if (message.is_close()) {
              set_close_received_flag(true);

              auto reply_close_code = message.close_code().value_or(close_code::normal);
              co_return co_await async_send_close(reply_close_code, message.close_reason(), return_as_deferred_tuple());
            }

            co_return std::error_code{};
          },
          transport_.get_strand()),
        token,
        message);
    }

    template <typename... States>
      requires((std::same_as<States, websocket::state>) && ...)
    [[nodiscard]] bool is_current_state(States... state) const noexcept
      requires(sizeof...(state) > 0)
    {
      auto current_state = state_.load(std::memory_order_acquire);
      return ((current_state == state) || ...);
    }

    [[nodiscard]] bool is_close_received() const noexcept {
      return close_received_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool is_close_sent() const noexcept {
      return close_sent_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool is_read_loop_active() const noexcept {
      return read_loop_active_.load(std::memory_order_acquire);
    }

    void set_close_received_flag(bool value) {
      close_received_.store(value, std::memory_order_release);
    }

    void set_close_sent_flag(bool value) {
      close_sent_.store(value, std::memory_order_release);
    }

    void set_read_loop_active_flag(bool value) {
      read_loop_active_.store(value, std::memory_order_release);
    }

    void set_connection_state(websocket::state state) {
      state_.store(state, std::memory_order_release);
    }

    void reset_connection_state(websocket::state state) {
      set_connection_state(state);
      set_close_received_flag(false);
      set_close_sent_flag(false);
      deferred_read_ec_.reset();
      data_received_in_handshake_.reset();
      message_assembler_.reset();
    }

    void signal_close_completion(std::error_code result_ec) {
      close_result_ec_ = result_ec;
      close_timer_.cancel();
    }

    std::optional<std::error_code> take_close_result() {
      if (!close_result_ec_) {
        return std::nullopt;
      }
      auto result = *close_result_ec_;
      close_result_ec_.reset();
      return result;
    }

    template <typename ResultT, typename F>
      requires(!std::same_as<ResultT, std::error_code>)
    std::expected<ResultT, std::error_code> synchronize_awaitable(F&& awaitable) {
      if (transport_.get_strand().running_in_this_thread()) {
        return std::unexpected(aero::error::basic_error::deadlock_would_occur);
      }

      try {
        auto [ec, result] = asio::co_spawn(transport_.get_strand(), std::forward<F>(awaitable), asio::use_future).get();
        if (ec) {
          return std::unexpected(ec);
        }
        return std::move(result);
      } catch (const std::system_error& e) {
        return std::unexpected(e.code());
      } catch (const std::future_error& e) {
        return std::unexpected(e.code());
      } catch (...) {
        return std::unexpected(std::make_error_code(std::errc::io_error));
      }
    }

    template <typename ResultT, typename F>
      requires(std::same_as<ResultT, std::error_code>)
    std::error_code synchronize_awaitable(F&& awaitable) {
      if (transport_.get_strand().running_in_this_thread()) {
        return aero::error::basic_error::deadlock_would_occur;
      }

      try {
        auto [ec] = asio::co_spawn(transport_.get_strand(), std::forward<F>(awaitable), asio::use_future).get();
        return ec;
      } catch (const std::system_error& e) {
        return e.code();
      } catch (const std::future_error& e) {
        return e.code();
      } catch (...) {
        return std::make_error_code(std::errc::io_error);
      }
    }

    std::unique_ptr<aero::io_runtime> io_runtime_;
    websocket::detail::client_frame_builder<> client_frame_builder_;
    websocket::detail::message_assembler message_assembler_;
    websocket::client_handshaker client_handshaker_;
    transport_type transport_;
    asio::steady_timer close_timer_{transport_.get_strand()};
    std::optional<std::error_code> close_result_ec_;
    std::optional<std::vector<std::byte>> data_received_in_handshake_;
    std::atomic<websocket::state> state_{state::closed};
    std::atomic<bool> close_sent_{false};
    std::atomic<bool> close_received_{false};
    std::atomic<bool> read_loop_active_{false};
    std::optional<std::error_code> deferred_read_ec_;
  };

} // namespace aero::websocket

#endif
