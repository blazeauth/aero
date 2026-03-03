#ifndef AERO_WEBSOCKET_CONCEPTS_WEBSOCKET_CLIENT_HPP
#define AERO_WEBSOCKET_CONCEPTS_WEBSOCKET_CLIENT_HPP

#include <concepts>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>

#include "asio/as_tuple.hpp"
#include "asio/awaitable.hpp"
#include "asio/strand.hpp"
#include "asio/use_awaitable.hpp"

#include "aero/http/headers.hpp"
#include "aero/websocket/close_code.hpp"
#include "aero/websocket/message.hpp"
#include "aero/websocket/uri.hpp"

namespace aero::websocket::concepts {

  template <typename Client>
  concept websocket_client =
    requires {
      typename Client::transport_type;
      typename Client::duration;
      typename Client::executor_type;
    } && requires(Client client, std::string_view uri_string, websocket::uri uri,
           std::expected<websocket::uri, std::error_code> parsed_uri, std::string header_name, std::string header_value,
           http::headers handshake_headers, std::string_view header_name_view, std::string_view text,
           std::span<const std::byte> bytes, websocket::close_code close_code, std::string close_reason,
           typename Client::duration timeout) {
      { client.set_handshake_header(header_name, header_value) } -> std::same_as<std::error_code>;
      { client.set_handshake_headers(handshake_headers) } -> std::same_as<std::error_code>;
      { client.remove_handshake_header(header_name_view) } -> std::same_as<void>;
      { client.clear_handshake_headers() } -> std::same_as<void>;

      {
        client.async_connect(uri_string, asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code, http::headers>>>;

      {
        client.async_connect(parsed_uri, asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code, http::headers>>>;

      {
        client.async_connect(uri, asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code, http::headers>>>;

      {
        client.async_send_text(text, asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code>>>;

      {
        client.async_send_binary(bytes, asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code>>>;

      { client.async_ping(asio::as_tuple(asio::use_awaitable)) } -> std::same_as<asio::awaitable<std::tuple<std::error_code>>>;
      {
        client.async_ping(text, asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code>>>;
      {
        client.async_ping(bytes, asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code>>>;

      { client.async_pong(asio::as_tuple(asio::use_awaitable)) } -> std::same_as<asio::awaitable<std::tuple<std::error_code>>>;
      {
        client.async_pong(text, asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code>>>;
      {
        client.async_pong(bytes, asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code>>>;

      {
        client.async_close(close_code, asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code>>>;

      {
        client.async_close(close_code, close_reason, asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code>>>;

      {
        client.async_force_close(asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code>>>;

      {
        client.async_read(asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code, websocket::message>>>;

      { client.connect(uri) } -> std::same_as<std::expected<http::headers, std::error_code>>;
      { client.connect(uri, timeout) } -> std::same_as<std::expected<http::headers, std::error_code>>;
      { client.connect(parsed_uri) } -> std::same_as<std::expected<http::headers, std::error_code>>;
      { client.connect(parsed_uri, timeout) } -> std::same_as<std::expected<http::headers, std::error_code>>;
      { client.connect(uri_string) } -> std::same_as<std::expected<http::headers, std::error_code>>;
      { client.connect(uri_string, timeout) } -> std::same_as<std::expected<http::headers, std::error_code>>;

      { client.send_text(text) } -> std::same_as<std::error_code>;
      { client.send_binary(bytes) } -> std::same_as<std::error_code>;

      { client.ping() } -> std::same_as<std::error_code>;
      { client.ping(text) } -> std::same_as<std::error_code>;
      { client.ping(bytes) } -> std::same_as<std::error_code>;

      { client.pong() } -> std::same_as<std::error_code>;
      { client.pong(text) } -> std::same_as<std::error_code>;
      { client.pong(bytes) } -> std::same_as<std::error_code>;

      { client.close(close_code) } -> std::same_as<std::error_code>;
      { client.close(close_code, close_reason) } -> std::same_as<std::error_code>;
      { client.force_close() } -> std::same_as<std::error_code>;

      { client.read() } -> std::same_as<std::expected<websocket::message, std::error_code>>;
      { client.read(timeout) } -> std::same_as<std::expected<websocket::message, std::error_code>>;

      { client.get_executor() } -> std::same_as<typename Client::executor_type>;
      { client.get_strand() } -> std::same_as<asio::strand<typename Client::executor_type>>;
      { client.is_open_for_writing() } -> std::same_as<bool>;
      { client.is_connecting() } -> std::same_as<bool>;
      { client.is_closed() } -> std::same_as<bool>;
      { client.is_closing() } -> std::same_as<bool>;
      { client.transport() } -> std::same_as<typename Client::transport_type&>;
    };

} // namespace aero::websocket::concepts

#endif
