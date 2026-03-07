#ifndef AERO_WEBSOCKET_TLS_CLIENT_HPP
#define AERO_WEBSOCKET_TLS_CLIENT_HPP

#include <expected>
#include <span>
#include <string_view>
#include <system_error>

#include <asio/ssl/context.hpp>
#include <asio/ssl/verify_mode.hpp>

#include "aero/http/headers.hpp"
#include "aero/net/tls_transport.hpp"
#include "aero/tls/system_context.hpp"
#include "aero/tls/verify_mode.hpp"
#include "aero/websocket/basic_client.hpp"
#include "aero/websocket/concepts/websocket_client.hpp"

namespace aero::websocket::tls {

  class client final {
   public:
    using transport_type = aero::net::tls_transport<>;
    using basic_client_type = websocket::basic_client<transport_type>;
    using duration = basic_client_type::duration;
    using executor_type = typename transport_type::executor_type;

    explicit client(executor_type executor, asio::ssl::context& tls_context, client_options options = {})
      : tls_context_(tls_context),
        basic_client_(executor, std::move(options), std::in_place_type<transport_type>, tls_context_) {}

    explicit client(asio::strand<executor_type> strand, asio::ssl::context& tls_context, client_options options = {})
      : tls_context_(tls_context),
        basic_client_(std::move(strand), std::move(options), std::in_place_type<transport_type>, tls_context_) {}

    explicit client(executor_type executor, aero::tls::system_context& system_tls_context, client_options options = {})
      : tls_context_(system_tls_context.context()),
        basic_client_(executor, std::move(options), std::in_place_type<transport_type>, tls_context_) {}

    explicit client(asio::strand<executor_type> strand, aero::tls::system_context& system_tls_context,
      client_options options = {})
      : tls_context_(system_tls_context.context()),
        basic_client_(std::move(strand), std::move(options), std::in_place_type<transport_type>, tls_context_) {}

    explicit client(asio::ssl::context& tls_context, client_options options = {})
      : tls_context_(tls_context), basic_client_(std::move(options), std::in_place_type<transport_type>, tls_context_) {}

    explicit client(aero::tls::system_context& tls_context, client_options options = {})
      : tls_context_(tls_context.context()),
        basic_client_(std::move(options), std::in_place_type<transport_type>, tls_context_) {}

    template <typename CompletionToken>
    auto async_connect(std::string_view uri, http::headers headers, CompletionToken&& token) {
      return basic_client_.async_connect(uri, std::move(headers), std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_connect(std::expected<websocket::uri, std::error_code> parsed_uri, http::headers headers,
      CompletionToken&& token) {
      return basic_client_.async_connect(std::move(parsed_uri), std::move(headers), std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_connect(websocket::uri uri, http::headers headers, CompletionToken&& token) {
      return basic_client_.async_connect(std::move(uri), std::move(headers), std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_connect(std::string_view uri, CompletionToken&& token) {
      return basic_client_.async_connect(uri, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_connect(std::expected<websocket::uri, std::error_code> parsed_uri, CompletionToken&& token) {
      return basic_client_.async_connect(std::move(parsed_uri), std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_connect(websocket::uri uri, CompletionToken&& token) {
      return basic_client_.async_connect(std::move(uri), std::forward<CompletionToken>(token));
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    template <typename CompletionToken>
    auto async_send_text(std::string_view text, CompletionToken&& token) {
      return basic_client_.async_send_text(text, std::forward<CompletionToken>(token));
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    template <typename CompletionToken>
    auto async_send_binary(std::span<const std::byte> data, CompletionToken&& token) {
      return basic_client_.async_send_binary(data, std::forward<CompletionToken>(token));
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    template <typename CompletionToken>
    auto async_ping(std::span<const std::byte> data, CompletionToken&& token) {
      return basic_client_.async_ping(data, std::forward<CompletionToken>(token));
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    template <typename CompletionToken>
    auto async_ping(std::string_view text, CompletionToken&& token) {
      return basic_client_.async_ping(text, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_ping(CompletionToken&& token) {
      return basic_client_.async_ping(std::forward<CompletionToken>(token));
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    template <typename CompletionToken>
    auto async_pong(std::span<const std::byte> data, CompletionToken&& token) {
      return basic_client_.async_pong(data, std::forward<CompletionToken>(token));
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    template <typename CompletionToken>
    auto async_pong(std::string_view text, CompletionToken&& token) {
      return basic_client_.async_pong(text, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_pong(CompletionToken&& token) {
      return basic_client_.async_pong(std::forward<CompletionToken>(token));
    }

    // Caller must ensure that 'reason' buffer remains valid until the operation is completed
    template <typename CompletionToken>
    auto async_close(websocket::close_code code, std::string_view reason, CompletionToken&& token) {
      return basic_client_.async_close(code, reason, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_close(websocket::close_code code, CompletionToken&& token) {
      return basic_client_.async_close(code, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_force_close(CompletionToken&& token) {
      return basic_client_.async_force_close(std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_read(CompletionToken&& token) {
      return basic_client_.async_read(std::forward<CompletionToken>(token));
    }

    std::expected<http::headers, std::error_code> connect(websocket::uri uri, http::headers headers) {
      return basic_client_.connect(std::move(uri), std::move(headers));
    }

    std::expected<http::headers, std::error_code> connect(websocket::uri uri, http::headers headers, duration timeout) {
      return basic_client_.connect(std::move(uri), std::move(headers), timeout);
    }

    std::expected<http::headers, std::error_code> connect(std::expected<websocket::uri, std::error_code> parsed_uri,
      http::headers headers) {
      return basic_client_.connect(std::move(parsed_uri), std::move(headers));
    }

    std::expected<http::headers, std::error_code> connect(std::expected<websocket::uri, std::error_code> parsed_uri,
      http::headers headers, duration timeout) {
      return basic_client_.connect(std::move(parsed_uri), std::move(headers), timeout);
    }

    std::expected<http::headers, std::error_code> connect(std::string_view uri_string, http::headers headers) {
      return basic_client_.connect(uri_string, std::move(headers));
    }

    std::expected<http::headers, std::error_code> connect(std::string_view uri_string, http::headers headers,
      duration timeout) {
      return basic_client_.connect(uri_string, std::move(headers), timeout);
    }

    std::expected<http::headers, std::error_code> connect(websocket::uri uri) {
      return basic_client_.connect(std::move(uri));
    }

    std::expected<http::headers, std::error_code> connect(websocket::uri uri, duration timeout) {
      return basic_client_.connect(std::move(uri), timeout);
    }

    std::expected<http::headers, std::error_code> connect(std::expected<websocket::uri, std::error_code> parsed_uri) {
      return basic_client_.connect(std::move(parsed_uri));
    }

    std::expected<http::headers, std::error_code> connect(std::expected<websocket::uri, std::error_code> parsed_uri,
      duration timeout) {
      return basic_client_.connect(std::move(parsed_uri), timeout);
    }

    std::expected<http::headers, std::error_code> connect(std::string_view uri_string) {
      return basic_client_.connect(uri_string);
    }

    std::expected<http::headers, std::error_code> connect(std::string_view uri_string, duration timeout) {
      return basic_client_.connect(uri_string, timeout);
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    std::error_code send_text(std::string_view text) {
      return basic_client_.send_text(text);
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    std::error_code send_binary(std::span<const std::byte> data) {
      return basic_client_.send_binary(data);
    }

    std::error_code ping() {
      return basic_client_.ping();
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    std::error_code ping(std::string_view text) {
      return basic_client_.ping(text);
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    std::error_code ping(std::span<const std::byte> data) {
      return basic_client_.ping(data);
    }

    std::error_code pong() {
      return basic_client_.pong();
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    std::error_code pong(std::string_view text) {
      return basic_client_.pong(text);
    }

    // Caller must ensure that given buffer remains valid until the operation is completed
    std::error_code pong(std::span<const std::byte> data) {
      return basic_client_.pong(data);
    }

    std::error_code close(websocket::close_code code) {
      return basic_client_.close(code);
    }

    std::error_code close(websocket::close_code code, std::string_view reason) {
      return basic_client_.close(code, reason);
    }

    std::error_code force_close() {
      return basic_client_.force_close();
    }

    std::expected<websocket::message, std::error_code> read() {
      return basic_client_.read();
    }

    std::expected<websocket::message, std::error_code> read(duration timeout) {
      return basic_client_.read(timeout);
    }

    [[nodiscard]] executor_type get_executor() noexcept {
      return basic_client_.get_executor();
    }

    [[nodiscard]] asio::strand<executor_type> get_strand() noexcept {
      return basic_client_.get_strand();
    }

    [[nodiscard]] bool is_open_for_writing() const noexcept {
      return basic_client_.is_open_for_writing();
    }

    [[nodiscard]] bool is_connecting() const noexcept {
      return basic_client_.is_connecting();
    }

    [[nodiscard]] bool is_closed() const noexcept {
      return basic_client_.is_closed();
    }

    [[nodiscard]] bool is_closing() const noexcept {
      return basic_client_.is_closing();
    }

    [[nodiscard]] transport_type& transport() {
      return basic_client_.transport();
    }

    void set_verify_mode(aero::tls::verify_mode mode) {
      transport().stream().set_verify_mode(static_cast<asio::ssl::verify_mode>(mode));
    }

   private:
    asio::ssl::context& tls_context_;
    basic_client_type basic_client_;
  };

  static_assert(websocket::concepts::websocket_client<tls::client>);

} // namespace aero::websocket::tls

#endif
