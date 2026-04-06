#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <asio/as_tuple.hpp>
#include <asio/associated_executor.hpp>
#include <asio/async_result.hpp>
#include <asio/bind_allocator.hpp>
#include <asio/cancel_after.hpp>
#include <asio/co_composed.hpp>
#include <asio/deferred.hpp>
#include <asio/dispatch.hpp>
#include <asio/error.hpp>
#ifdef AERO_USE_TLS
#include <asio/ssl/context.hpp>
#include <asio/ssl/error.hpp>
#endif
#include <asio/system_executor.hpp>
#include <asio/use_future.hpp>

#include "aero/http/basic_client.hpp"
#include "aero/http/client_options.hpp"
#include "aero/http/port.hpp"
#include "aero/http/request.hpp"
#include "aero/http/response.hpp"
#include "aero/http/uri.hpp"
#include "aero/http/version.hpp"
#include "aero/io_runtime.hpp"
#include "aero/net/tcp_transport.hpp"
#ifdef AERO_USE_TLS
#include "aero/net/tls_transport.hpp"
#endif

namespace aero::http {

  using tcp_client = basic_client<aero::net::tcp_transport<>>;

#ifdef AERO_USE_TLS
  using tls_client = basic_client<aero::net::tls_transport<>>;
#endif

  class client final {
    using client_error = http::error::client_error;
    using connection_error = http::error::connection_error;
    using protocol_error = http::error::protocol_error;

    constexpr static std::size_t default_runtime_threads = 1;

    struct async_complete_error_initiation {
      using executor_type = asio::any_io_executor;

      std::error_code error;
      executor_type executor;

      [[nodiscard]] executor_type get_executor() const noexcept {
        return executor;
      }

      template <typename Handler>
      void operator()(Handler&& handler) const {
        auto associated_executor = asio::get_associated_executor(handler, executor);
        asio::dispatch(associated_executor,
          [handler = std::forward<Handler>(handler), error = error]() mutable { std::move(handler)(error, http::response{}); });
      }
    };

    template <typename CompletionToken>
    auto async_complete_error(std::error_code error, CompletionToken&& token) {
      return asio::async_initiate<CompletionToken, void(std::error_code, http::response)>(
        async_complete_error_initiation{
          .error = error,
          .executor = get_executor(),
        },
        token);
    }

   public:
    using duration = std::chrono::steady_clock::duration;
    using executor_type = asio::any_io_executor;

    struct endpoint {
      std::string host;
      std::uint16_t port{http::default_port};
      bool secure{false};
    };

    client()
      : runtime_(make_runtime()),
        tcp_client_(runtime_->get_executor(), client_options{})
#ifdef AERO_USE_TLS
        ,
        tls_client_(runtime_->get_executor(), client_options{})
#endif
    {
    }

    explicit client(client_options options)
      : runtime_(make_runtime()),
        tcp_client_(runtime_->get_executor(), options)
#ifdef AERO_USE_TLS
        ,
        tls_client_(runtime_->get_executor(), options)
#endif
    {
    }

    explicit client(executor_type executor): client(std::move(executor), client_options{}) {}

    client(executor_type executor, client_options options)
      : tcp_client_(executor, options)
#ifdef AERO_USE_TLS
        ,
        tls_client_(std::move(executor), options)
#endif
    {
    }

    template <typename CompletionToken>
    auto async_send(client::endpoint endpoint, http::request request, CompletionToken&& token) {
#ifdef AERO_USE_TLS
      if (endpoint.secure) {
        return tls_client_.async_send(
          tls_client::endpoint{
            .host = std::move(endpoint.host),
            .port = endpoint.port,
          },
          std::move(request),
          std::forward<CompletionToken>(token));
      }
#else
      if (endpoint.secure) {
        return async_complete_error(aero::error::basic_error::tls_support_unavailable, std::forward<CompletionToken>(token));
      }
#endif

      return tcp_client_.async_send(
        tcp_client::endpoint{
          .host = std::move(endpoint.host),
          .port = endpoint.port,
        },
        std::move(request),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_send(std::string_view uri_text, http::request request, CompletionToken&& token) {
      auto parsed_uri = http::uri::parse(uri_text);
      if (!parsed_uri.has_value()) {
        return async_complete_error(parsed_uri.error(), std::forward<CompletionToken>(token));
      }

      if (request.url.empty()) {
        request.url = parsed_uri->target();
      }

#ifdef AERO_USE_TLS
      if (parsed_uri->is_https()) {
        return tls_client_.async_send(
          tls_client::endpoint{
            .host = std::string(parsed_uri->host()),
            .port = parsed_uri->port(),
          },
          std::move(request),
          std::forward<CompletionToken>(token));
      }
#else
      if (parsed_uri->is_https()) {
        return async_complete_error(aero::error::basic_error::tls_support_unavailable, std::forward<CompletionToken>(token));
      }
#endif

      return tcp_client_.async_send(
        tcp_client::endpoint{
          .host = std::string(parsed_uri->host()),
          .port = parsed_uri->port(),
        },
        std::move(request),
        std::forward<CompletionToken>(token));
    }

    template <http::method Method, typename CompletionToken>
    auto async_send_bodyless_request(std::string_view uri_text, http::version protocol, http::headers headers,
      CompletionToken&& token) {
      auto request = make_request(Method, protocol, "", {}, std::move(headers));
      if (!request.has_value()) {
        return async_complete_error(request.error(), std::forward<CompletionToken>(token));
      }

      return async_send(uri_text, std::move(*request), std::forward<CompletionToken>(token));
    }

    template <http::method Method, typename CompletionToken>
    auto async_send_bodyless_request(client::endpoint endpoint, std::string target, http::version protocol,
      http::headers headers, CompletionToken&& token) {
      auto request = make_request(Method, protocol, std::move(target), {}, std::move(headers));
      if (!request.has_value()) {
        return async_complete_error(request.error(), std::forward<CompletionToken>(token));
      }

      return async_send(std::move(endpoint), std::move(*request), std::forward<CompletionToken>(token));
    }

    template <http::method Method, typename CompletionToken>
    auto async_send_request_with_body(std::string_view uri_text, std::span<const std::byte> body, http::version protocol,
      http::headers headers, CompletionToken&& token) {
      auto request = make_request(Method, protocol, "", body, std::move(headers));
      if (!request.has_value()) {
        return async_complete_error(request.error(), std::forward<CompletionToken>(token));
      }

      return async_send(uri_text, std::move(*request), std::forward<CompletionToken>(token));
    }

    template <http::method Method, typename CompletionToken>
    auto async_send_request_with_body(client::endpoint endpoint, std::string target, std::span<const std::byte> body,
      http::version protocol, http::headers headers, CompletionToken&& token) {
      auto request = make_request(Method, protocol, std::move(target), body, std::move(headers));
      if (!request.has_value()) {
        return async_complete_error(request.error(), std::forward<CompletionToken>(token));
      }

      return async_send(std::move(endpoint), std::move(*request), std::forward<CompletionToken>(token));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> send(client::endpoint endpoint, http::request request) {
      try {
        auto future = async_send(std::move(endpoint), std::move(request), asio::use_future);
        return future.get();
      } catch (const std::system_error& system_error) {
        return std::unexpected(system_error.code());
      } catch (const std::future_error& future_error) {
        return std::unexpected(future_error.code());
      } catch (...) {
        return std::unexpected(client_error::unexpected_failure);
      }
    }

    [[nodiscard]] std::expected<http::response, std::error_code> send(std::string_view uri_text, http::request request) {
      try {
        auto future = async_send(uri_text, std::move(request), asio::use_future);
        return future.get();
      } catch (const std::system_error& system_error) {
        return std::unexpected(system_error.code());
      } catch (const std::future_error& future_error) {
        return std::unexpected(future_error.code());
      } catch (...) {
        return std::unexpected(client_error::unexpected_failure);
      }
    }

    template <typename CompletionToken>
    auto async_get(std::string_view uri_text, http::version protocol, http::headers headers, CompletionToken&& token) {
      return async_send_bodyless_request<http::method::get>(uri_text,
        protocol,
        std::move(headers),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_get(std::string_view uri_text, CompletionToken&& token) {
      return async_get(uri_text, http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_get(client::endpoint endpoint, std::string target, http::version protocol, http::headers headers,
      CompletionToken&& token) {
      return async_send_bodyless_request<http::method::get>(std::move(endpoint),
        std::move(target),
        protocol,
        std::move(headers),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_get(client::endpoint endpoint, CompletionToken&& token) {
      return async_get(std::move(endpoint), "/", http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_head(std::string_view uri_text, http::version protocol, http::headers headers, CompletionToken&& token) {
      return async_send_bodyless_request<http::method::head>(uri_text,
        protocol,
        std::move(headers),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_head(std::string_view uri_text, CompletionToken&& token) {
      return async_head(uri_text, http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_head(client::endpoint endpoint, std::string target, http::version protocol, http::headers headers,
      CompletionToken&& token) {
      return async_send_bodyless_request<http::method::head>(std::move(endpoint),
        std::move(target),
        protocol,
        std::move(headers),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_head(client::endpoint endpoint, CompletionToken&& token) {
      return async_head(std::move(endpoint), "/", http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_delete_(std::string_view uri_text, http::version protocol, http::headers headers, CompletionToken&& token) {
      return async_send_bodyless_request<http::method::delete_>(uri_text,
        protocol,
        std::move(headers),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_delete_(std::string_view uri_text, CompletionToken&& token) {
      return async_delete_(uri_text, http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_delete_(client::endpoint endpoint, std::string target, http::version protocol, http::headers headers,
      CompletionToken&& token) {
      return async_send_bodyless_request<http::method::delete_>(std::move(endpoint),
        std::move(target),
        protocol,
        std::move(headers),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_delete_(client::endpoint endpoint, CompletionToken&& token) {
      return async_delete_(std::move(endpoint), "/", http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_options(std::string_view uri_text, http::version protocol, http::headers headers, CompletionToken&& token) {
      return async_send_bodyless_request<http::method::options>(uri_text,
        protocol,
        std::move(headers),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_options(std::string_view uri_text, CompletionToken&& token) {
      return async_options(uri_text, http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_options(client::endpoint endpoint, std::string target, http::version protocol, http::headers headers,
      CompletionToken&& token) {
      return async_send_bodyless_request<http::method::options>(std::move(endpoint),
        std::move(target),
        protocol,
        std::move(headers),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_options(client::endpoint endpoint, CompletionToken&& token) {
      return async_options(std::move(endpoint), "*", http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_post(std::string_view uri_text, std::string_view body_text, http::version protocol, http::headers headers,
      CompletionToken&& token) {
      return async_post(uri_text, to_bytes(body_text), protocol, std::move(headers), std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_post(std::string_view uri_text, std::string_view body_text, CompletionToken&& token) {
      return async_post(uri_text, body_text, http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_post(std::string_view uri_text, std::span<const std::byte> body, http::version protocol, http::headers headers,
      CompletionToken&& token) {
      return async_send_request_with_body<http::method::post>(uri_text,
        body,
        protocol,
        std::move(headers),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_post(std::string_view uri_text, std::span<const std::byte> body, CompletionToken&& token) {
      return async_post(uri_text, body, http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_post(client::endpoint endpoint, std::string target, std::string_view body_text, http::version protocol,
      http::headers headers, CompletionToken&& token) {
      return async_post(std::move(endpoint),
        std::move(target),
        to_bytes(body_text),
        protocol,
        std::move(headers),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_post(client::endpoint endpoint, std::string_view body_text, CompletionToken&& token) {
      return async_post(std::move(endpoint), "/", body_text, http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_post(client::endpoint endpoint, std::string target, std::span<const std::byte> body, http::version protocol,
      http::headers headers, CompletionToken&& token) {
      return async_send_request_with_body<http::method::post>(std::move(endpoint),
        std::move(target),
        body,
        protocol,
        std::move(headers),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_post(client::endpoint endpoint, std::span<const std::byte> body, CompletionToken&& token) {
      return async_post(std::move(endpoint), "/", body, http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_put(std::string_view uri_text, std::string_view body_text, http::version protocol, http::headers headers,
      CompletionToken&& token) {
      return async_put(uri_text, to_bytes(body_text), protocol, std::move(headers), std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_put(std::string_view uri_text, std::string_view body_text, CompletionToken&& token) {
      return async_put(uri_text, body_text, http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_put(std::string_view uri_text, std::span<const std::byte> body, http::version protocol, http::headers headers,
      CompletionToken&& token) {
      return async_send_request_with_body<http::method::put>(uri_text,
        body,
        protocol,
        std::move(headers),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_put(std::string_view uri_text, std::span<const std::byte> body, CompletionToken&& token) {
      return async_put(uri_text, body, http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_put(client::endpoint endpoint, std::string target, std::string_view body_text, http::version protocol,
      http::headers headers, CompletionToken&& token) {
      return async_put(std::move(endpoint),
        std::move(target),
        to_bytes(body_text),
        protocol,
        std::move(headers),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_put(client::endpoint endpoint, std::string_view body_text, CompletionToken&& token) {
      return async_put(std::move(endpoint), "/", body_text, http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_put(client::endpoint endpoint, std::string target, std::span<const std::byte> body, http::version protocol,
      http::headers headers, CompletionToken&& token) {
      return async_send_request_with_body<http::method::put>(std::move(endpoint),
        std::move(target),
        body,
        protocol,
        std::move(headers),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_put(client::endpoint endpoint, std::span<const std::byte> body, CompletionToken&& token) {
      return async_put(std::move(endpoint), "/", body, http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_patch(std::string_view uri_text, std::string_view body_text, http::version protocol, http::headers headers,
      CompletionToken&& token) {
      return async_patch(uri_text, to_bytes(body_text), protocol, std::move(headers), std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_patch(std::string_view uri_text, std::string_view body_text, CompletionToken&& token) {
      return async_patch(uri_text, body_text, http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_patch(std::string_view uri_text, std::span<const std::byte> body, http::version protocol, http::headers headers,
      CompletionToken&& token) {
      return async_send_request_with_body<http::method::patch>(uri_text,
        body,
        protocol,
        std::move(headers),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_patch(std::string_view uri_text, std::span<const std::byte> body, CompletionToken&& token) {
      return async_patch(uri_text, body, http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_patch(client::endpoint endpoint, std::string target, std::string_view body_text, http::version protocol,
      http::headers headers, CompletionToken&& token) {
      return async_patch(std::move(endpoint),
        std::move(target),
        to_bytes(body_text),
        protocol,
        std::move(headers),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_patch(client::endpoint endpoint, std::string_view body_text, CompletionToken&& token) {
      return async_patch(std::move(endpoint), "/", body_text, http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_patch(client::endpoint endpoint, std::string target, std::span<const std::byte> body, http::version protocol,
      http::headers headers, CompletionToken&& token) {
      return async_send_request_with_body<http::method::patch>(std::move(endpoint),
        std::move(target),
        body,
        protocol,
        std::move(headers),
        std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_patch(client::endpoint endpoint, std::span<const std::byte> body, CompletionToken&& token) {
      return async_patch(std::move(endpoint), "/", body, http::version::http1_1, {}, std::forward<CompletionToken>(token));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> get(std::string_view uri_text,
      http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return send_bodyless_request<http::method::get>(uri_text, protocol, std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> get(client::endpoint endpoint, std::string target = "/",
      http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return send_bodyless_request<http::method::get>(std::move(endpoint), std::move(target), protocol, std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> head(std::string_view uri_text,
      http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return send_bodyless_request<http::method::head>(uri_text, protocol, std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> head(client::endpoint endpoint, std::string target = "/",
      http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return send_bodyless_request<http::method::head>(std::move(endpoint), std::move(target), protocol, std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> delete_(std::string_view uri_text,
      http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return send_bodyless_request<http::method::delete_>(uri_text, protocol, std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> delete_(client::endpoint endpoint, std::string target = "/",
      http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return send_bodyless_request<http::method::delete_>(std::move(endpoint), std::move(target), protocol, std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> options(std::string_view uri_text,
      http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return send_bodyless_request<http::method::options>(uri_text, protocol, std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> options(client::endpoint endpoint, std::string target = "*",
      http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return send_bodyless_request<http::method::options>(std::move(endpoint), std::move(target), protocol, std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> post(std::string_view uri_text, std::string_view body_text,
      http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return post(uri_text, to_bytes(body_text), protocol, std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> post(std::string_view uri_text,
      std::span<const std::byte> body, http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return send_request_with_body<http::method::post>(uri_text, body, protocol, std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> post(client::endpoint endpoint, std::string target,
      std::string_view body_text, http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return post(std::move(endpoint), std::move(target), to_bytes(body_text), protocol, std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> post(client::endpoint endpoint, std::string target,
      std::span<const std::byte> body, http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return send_request_with_body<http::method::post>(std::move(endpoint),
        std::move(target),
        body,
        protocol,
        std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> put(std::string_view uri_text, std::string_view body_text,
      http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return put(uri_text, to_bytes(body_text), protocol, std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> put(std::string_view uri_text, std::span<const std::byte> body,
      http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return send_request_with_body<http::method::put>(uri_text, body, protocol, std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> put(client::endpoint endpoint, std::string target,
      std::string_view body_text, http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return put(std::move(endpoint), std::move(target), to_bytes(body_text), protocol, std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> put(client::endpoint endpoint, std::string target,
      std::span<const std::byte> body, http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return send_request_with_body<http::method::put>(std::move(endpoint),
        std::move(target),
        body,
        protocol,
        std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> patch(std::string_view uri_text, std::string_view body_text,
      http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return patch(uri_text, to_bytes(body_text), protocol, std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> patch(std::string_view uri_text,
      std::span<const std::byte> body, http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return send_request_with_body<http::method::patch>(uri_text, body, protocol, std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> patch(client::endpoint endpoint, std::string target,
      std::string_view body_text, http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return patch(std::move(endpoint), std::move(target), to_bytes(body_text), protocol, std::move(headers));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> patch(client::endpoint endpoint, std::string target,
      std::span<const std::byte> body, http::version protocol = http::version::http1_1, http::headers headers = {}) {
      return send_request_with_body<http::method::patch>(std::move(endpoint),
        std::move(target),
        body,
        protocol,
        std::move(headers));
    }

    [[nodiscard]] executor_type get_executor() const noexcept {
      return tcp_client_.get_executor();
    }

    [[nodiscard]] tcp_client& tcp() & {
      return tcp_client_;
    }

    [[nodiscard]] const tcp_client& tcp() const& {
      return tcp_client_;
    }

    [[nodiscard]] tcp_client& tcp() && = delete;
    [[nodiscard]] const tcp_client& tcp() const&& = delete;

#ifdef AERO_USE_TLS
    [[nodiscard]] tls_client& tls() & {
      return tls_client_;
    }

    [[nodiscard]] const tls_client& tls() const& {
      return tls_client_;
    }

    [[nodiscard]] tls_client& tls() && = delete;
    [[nodiscard]] const tls_client& tls() const&& = delete;
#endif

   private:
    template <http::method Method>
    [[nodiscard]] std::expected<http::response, std::error_code> send_bodyless_request(std::string_view uri_text,
      http::version protocol, http::headers headers) {
      auto request = make_request(Method, protocol, "", {}, std::move(headers));
      if (!request.has_value()) {
        return std::unexpected(request.error());
      }

      return send(uri_text, std::move(*request));
    }

    template <http::method Method>
    [[nodiscard]] std::expected<http::response, std::error_code> send_bodyless_request(client::endpoint endpoint,
      std::string target, http::version protocol, http::headers headers) {
      auto request = make_request(Method, protocol, std::move(target), {}, std::move(headers));
      if (!request.has_value()) {
        return std::unexpected(request.error());
      }

      return send(std::move(endpoint), std::move(*request));
    }

    template <http::method Method>
    [[nodiscard]] std::expected<http::response, std::error_code> send_request_with_body(std::string_view uri_text,
      std::span<const std::byte> body, http::version protocol, http::headers headers) {
      auto request = make_request(Method, protocol, "", body, std::move(headers));
      if (!request.has_value()) {
        return std::unexpected(request.error());
      }

      return send(uri_text, std::move(*request));
    }

    template <http::method Method>
    [[nodiscard]] std::expected<http::response, std::error_code> send_request_with_body(client::endpoint endpoint,
      std::string target, std::span<const std::byte> body, http::version protocol, http::headers headers) {
      auto request = make_request(Method, protocol, std::move(target), body, std::move(headers));
      if (!request.has_value()) {
        return std::unexpected(request.error());
      }

      return send(std::move(endpoint), std::move(*request));
    }

    [[nodiscard]] static std::expected<http::request, std::error_code> make_request(http::method method, http::version protocol,
      std::string target, std::span<const std::byte> body, http::headers headers) {
      auto version_error = validate_protocol_version(protocol);
      if (version_error) {
        return std::unexpected(version_error);
      }

      return http::request{
        .method = method,
        .protocol = protocol,
        .url = std::move(target),
        .body = std::vector<std::byte>{body.begin(), body.end()},
        .headers = std::move(headers),
        .content_length = 0,
      };
    }

    [[nodiscard]] static std::error_code validate_protocol_version(http::version protocol) noexcept {
      switch (protocol) {
      case http::version::http1_0:
      case http::version::http1_1:
        return {};
      default:
        return protocol_error::version_invalid;
      }
    }

    [[nodiscard]] static std::span<const std::byte> to_bytes(std::string_view text) noexcept {
      return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
    }

    [[nodiscard]] static std::shared_ptr<aero::io_runtime> make_runtime() {
      return std::make_shared<aero::io_runtime>(threads_count_t{default_runtime_threads}, aero::wait_threads);
    }

    std::shared_ptr<aero::io_runtime> runtime_;
    tcp_client tcp_client_;
#ifdef AERO_USE_TLS
    tls_client tls_client_;
#endif
  };

  [[nodiscard]] inline http::client& default_client() {
    static http::client client;
    return client;
  }

  template <typename CompletionToken>
  auto async_get(std::string_view uri_text, http::version protocol, http::headers headers, CompletionToken&& token) {
    return default_client().async_get(uri_text, protocol, std::move(headers), std::forward<CompletionToken>(token));
  }

  template <typename CompletionToken>
  auto async_get(std::string_view uri_text, CompletionToken&& token) {
    return default_client().async_get(uri_text, std::forward<CompletionToken>(token));
  }

  [[nodiscard]] inline auto get(std::string_view uri_text, http::version protocol = http::version::http1_1,
    http::headers headers = {}) {
    return default_client().get(uri_text, protocol, std::move(headers));
  }

  template <typename CompletionToken>
  auto async_head(std::string_view uri_text, http::version protocol, http::headers headers, CompletionToken&& token) {
    return default_client().async_head(uri_text, protocol, std::move(headers), std::forward<CompletionToken>(token));
  }

  template <typename CompletionToken>
  auto async_head(std::string_view uri_text, CompletionToken&& token) {
    return default_client().async_head(uri_text, std::forward<CompletionToken>(token));
  }

  [[nodiscard]] inline auto head(std::string_view uri_text, http::version protocol = http::version::http1_1,
    http::headers headers = {}) {
    return default_client().head(uri_text, protocol, std::move(headers));
  }

  template <typename CompletionToken>
  auto async_delete_(std::string_view uri_text, http::version protocol, http::headers headers, CompletionToken&& token) {
    return default_client().async_delete_(uri_text, protocol, std::move(headers), std::forward<CompletionToken>(token));
  }

  template <typename CompletionToken>
  auto async_delete_(std::string_view uri_text, CompletionToken&& token) {
    return default_client().async_delete_(uri_text, std::forward<CompletionToken>(token));
  }

  [[nodiscard]] inline auto delete_(std::string_view uri_text, http::version protocol = http::version::http1_1,
    http::headers headers = {}) {
    return default_client().delete_(uri_text, protocol, std::move(headers));
  }

  template <typename CompletionToken>
  auto async_options(std::string_view uri_text, http::version protocol, http::headers headers, CompletionToken&& token) {
    return default_client().async_options(uri_text, protocol, std::move(headers), std::forward<CompletionToken>(token));
  }

  template <typename CompletionToken>
  auto async_options(std::string_view uri_text, CompletionToken&& token) {
    return default_client().async_options(uri_text, std::forward<CompletionToken>(token));
  }

  [[nodiscard]] inline auto options(std::string_view uri_text, http::version protocol = http::version::http1_1,
    http::headers headers = {}) {
    return default_client().options(uri_text, protocol, std::move(headers));
  }

  template <typename CompletionToken>
  auto async_post(std::string_view uri_text, std::string_view body_text, http::version protocol, http::headers headers,
    CompletionToken&& token) {
    return default_client().async_post(uri_text, body_text, protocol, std::move(headers), std::forward<CompletionToken>(token));
  }

  template <typename CompletionToken>
  auto async_post(std::string_view uri_text, std::string_view body_text, CompletionToken&& token) {
    return default_client().async_post(uri_text, body_text, std::forward<CompletionToken>(token));
  }

  template <typename CompletionToken>
  auto async_post(std::string_view uri_text, std::span<const std::byte> body, http::version protocol, http::headers headers,
    CompletionToken&& token) {
    return default_client().async_post(uri_text, body, protocol, std::move(headers), std::forward<CompletionToken>(token));
  }

  template <typename CompletionToken>
  auto async_post(std::string_view uri_text, std::span<const std::byte> body, CompletionToken&& token) {
    return default_client().async_post(uri_text, body, std::forward<CompletionToken>(token));
  }

  [[nodiscard]] inline auto post(std::string_view uri_text, std::string_view body_text,
    http::version protocol = http::version::http1_1, http::headers headers = {}) {
    return default_client().post(uri_text, body_text, protocol, std::move(headers));
  }

  [[nodiscard]] inline auto post(std::string_view uri_text, std::span<const std::byte> body,
    http::version protocol = http::version::http1_1, http::headers headers = {}) {
    return default_client().post(uri_text, body, protocol, std::move(headers));
  }

  template <typename CompletionToken>
  auto async_put(std::string_view uri_text, std::string_view body_text, http::version protocol, http::headers headers,
    CompletionToken&& token) {
    return default_client().async_put(uri_text, body_text, protocol, std::move(headers), std::forward<CompletionToken>(token));
  }

  template <typename CompletionToken>
  auto async_put(std::string_view uri_text, std::string_view body_text, CompletionToken&& token) {
    return default_client().async_put(uri_text, body_text, std::forward<CompletionToken>(token));
  }

  template <typename CompletionToken>
  auto async_put(std::string_view uri_text, std::span<const std::byte> body, http::version protocol, http::headers headers,
    CompletionToken&& token) {
    return default_client().async_put(uri_text, body, protocol, std::move(headers), std::forward<CompletionToken>(token));
  }

  template <typename CompletionToken>
  auto async_put(std::string_view uri_text, std::span<const std::byte> body, CompletionToken&& token) {
    return default_client().async_put(uri_text, body, std::forward<CompletionToken>(token));
  }

  [[nodiscard]] inline auto put(std::string_view uri_text, std::string_view body_text,
    http::version protocol = http::version::http1_1, http::headers headers = {}) {
    return default_client().put(uri_text, body_text, protocol, std::move(headers));
  }

  [[nodiscard]] inline auto put(std::string_view uri_text, std::span<const std::byte> body,
    http::version protocol = http::version::http1_1, http::headers headers = {}) {
    return default_client().put(uri_text, body, protocol, std::move(headers));
  }

  template <typename CompletionToken>
  auto async_patch(std::string_view uri_text, std::string_view body_text, http::version protocol, http::headers headers,
    CompletionToken&& token) {
    return default_client().async_patch(uri_text,
      body_text,
      protocol,
      std::move(headers),
      std::forward<CompletionToken>(token));
  }

  template <typename CompletionToken>
  auto async_patch(std::string_view uri_text, std::string_view body_text, CompletionToken&& token) {
    return default_client().async_patch(uri_text, body_text, std::forward<CompletionToken>(token));
  }

  template <typename CompletionToken>
  auto async_patch(std::string_view uri_text, std::span<const std::byte> body, http::version protocol, http::headers headers,
    CompletionToken&& token) {
    return default_client().async_patch(uri_text, body, protocol, std::move(headers), std::forward<CompletionToken>(token));
  }

  template <typename CompletionToken>
  auto async_patch(std::string_view uri_text, std::span<const std::byte> body, CompletionToken&& token) {
    return default_client().async_patch(uri_text, body, std::forward<CompletionToken>(token));
  }

  [[nodiscard]] inline auto patch(std::string_view uri_text, std::string_view body_text,
    http::version protocol = http::version::http1_1, http::headers headers = {}) {
    return default_client().patch(uri_text, body_text, protocol, std::move(headers));
  }

  [[nodiscard]] inline auto patch(std::string_view uri_text, std::span<const std::byte> body,
    http::version protocol = http::version::http1_1, http::headers headers = {}) {
    return default_client().patch(uri_text, body, protocol, std::move(headers));
  }

} // namespace aero::http
