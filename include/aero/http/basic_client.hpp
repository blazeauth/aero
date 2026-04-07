#pragma once

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <future>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
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

#include "aero/detail/aligned_allocator.hpp"
#include "aero/detail/string.hpp"
#include "aero/error.hpp"
#include "aero/http/client_options.hpp"
#include "aero/http/detail/common.hpp"
#include "aero/http/detail/connection_pool.hpp"
#include "aero/http/headers.hpp"
#include "aero/http/port.hpp"
#include "aero/http/request.hpp"
#include "aero/http/request_line.hpp"
#include "aero/http/response.hpp"
#include "aero/http/status_code.hpp"
#include "aero/http/status_line.hpp"
#include "aero/http/uri.hpp"
#include "aero/http/version.hpp"
#include "aero/io_runtime.hpp"
#include "aero/net/concepts/transport.hpp"

namespace aero::http {

  template <net::concepts::transport Transport>
  class basic_client final {
    using client_error = http::error::client_error;
    using connection_error = http::error::connection_error;
    using protocol_error = http::error::protocol_error;

    constexpr static std::size_t default_runtime_threads = 1;
    constexpr static int informational_status_code_min = std::to_underlying(http::status_code::continue_);
    constexpr static int informational_status_code_max = std::to_underlying(http::status_code::ok);
    constexpr static int succesfull_status_code_min = std::to_underlying(http::status_code::ok);
    constexpr static int succesfull_status_code_max = std::to_underlying(http::status_code::multiple_choices);

   public:
    using transport_type = Transport;
    using connection_pool_type = http::detail::connection_pool<transport_type>;
    using duration = std::chrono::steady_clock::duration;
    using executor_type = asio::any_io_executor;

    constexpr static bool secure_transport = connection_pool_type::is_secure_transport();
    constexpr static std::uint16_t default_endpoint_port = secure_transport ? http::default_secure_port : http::default_port;

    struct endpoint {
      std::string host;
      std::uint16_t port{default_endpoint_port};
    };

   private:
    struct prepared_request {
      basic_client::endpoint endpoint;
      http::request request;
    };

    struct parsed_response_head {
      http::response response;
      std::vector<std::byte> body_prefix;
    };

    struct exchange_result {
      std::error_code error;
      http::response response;
      bool response_started{false};
    };

    struct expectation_result {
      std::error_code error;
      bool continue_sending_body{true};
      http::response response;
      std::vector<std::byte> response_buffer;
      bool response_started{false};
    };

    enum class transfer_encoding_framing : std::uint8_t {
      none = 0,
      chunked,
      close_delimited,
      invalid,
    };

    struct transfer_encoding_info final {
      bool present{false};
      bool invalid{false};
      bool final_chunked{false};
    };

   public:
    basic_client()
      : runtime_(make_runtime()),
        executor_(runtime_->get_executor()),
        options_(),
        connection_pool_(make_connection_pool(executor_, options_)) {}

    explicit basic_client(client_options options)
      : runtime_(make_runtime()),
        executor_(runtime_->get_executor()),
        options_(options),
        connection_pool_(make_connection_pool(executor_, options_)) {}

    explicit basic_client(executor_type executor)
      : executor_(std::move(executor)), options_(), connection_pool_(make_connection_pool(executor_, options_)) {}

    basic_client(executor_type executor, client_options options)
      : executor_(std::move(executor)), options_(options), connection_pool_(make_connection_pool(executor_, options_)) {}

    basic_client(const basic_client&) = delete;
    basic_client(basic_client&&) = delete;
    basic_client& operator=(const basic_client&) = delete;
    basic_client& operator=(basic_client&&) = delete;
    ~basic_client() = default;

    template <typename CompletionToken>
    auto async_send(basic_client::endpoint endpoint, http::request request, CompletionToken&& token) {
      auto bound_token = asio::bind_allocator(aero::detail::aligned_allocator<>{}, std::forward<CompletionToken>(token));

      return asio::async_initiate<decltype(bound_token), void(std::error_code, http::response)>(
        asio::co_composed<void(std::error_code, http::response)>(
          [this](auto, basic_client::endpoint endpoint, http::request request) mutable -> void {
            auto prepared = prepare_request(std::move(endpoint), std::move(request));
            if (!prepared.has_value()) {
              co_return {prepared.error(), http::response{}};
            }

            co_return co_await async_send_prepared(std::move(*prepared), return_as_deferred_tuple());
          },
          get_executor()),
        bound_token,
        std::move(endpoint),
        std::move(request));
    }

    template <typename CompletionToken>
    auto async_send(std::string_view uri_text, http::request request, CompletionToken&& token) {
      auto bound_token = asio::bind_allocator(aero::detail::aligned_allocator<>{}, std::forward<CompletionToken>(token));

      return asio::async_initiate<decltype(bound_token), void(std::error_code, http::response)>(
        asio::co_composed<void(std::error_code, http::response)>(
          [this](auto, std::string uri_text, http::request request) mutable -> void {
            auto parsed_uri = http::uri::parse(uri_text);
            if (!parsed_uri.has_value()) {
              co_return {parsed_uri.error(), http::response{}};
            }

            if (parsed_uri->is_https() != secure_transport) {
              co_return {unsupported_scheme_error(parsed_uri->is_https()), http::response{}};
            }

            if (request.url.empty() && request.method != http::method::connect) {
              request.url = parsed_uri->target();
            }

            auto prepared = prepare_request(
              endpoint{
                .host = std::string(parsed_uri->host()),
                .port = parsed_uri->port(),
              },
              std::move(request));

            if (!prepared.has_value()) {
              co_return {prepared.error(), http::response{}};
            }

            co_return co_await async_send_prepared(std::move(*prepared), return_as_deferred_tuple());
          },
          get_executor()),
        bound_token,
        std::string(uri_text),
        std::move(request));
    }

    [[nodiscard]] std::expected<http::response, std::error_code> send(basic_client::endpoint endpoint, http::request request) {
      try {
        auto future = async_send(std::move(endpoint), std::move(request), asio::use_future);
        return future.get();
      } catch (const std::system_error& e) {
        return std::unexpected(e.code());
      } catch (const std::future_error& e) {
        return std::unexpected(e.code());
      } catch (...) {
        return std::unexpected(client_error::unexpected_failure);
      }
    }

    [[nodiscard]] std::expected<http::response, std::error_code> send(std::string_view uri_text, http::request request) {
      try {
        auto future = async_send(uri_text, std::move(request), asio::use_future);
        return future.get();
      } catch (const std::system_error& e) {
        return std::unexpected(e.code());
      } catch (const std::future_error& e) {
        return std::unexpected(e.code());
      } catch (...) {
        return std::unexpected(client_error::unexpected_failure);
      }
    }

    [[nodiscard]] executor_type get_executor() const noexcept {
      return executor_;
    }

    [[nodiscard]] connection_pool_type& connection_pool() & {
      return connection_pool_;
    }

    [[nodiscard]] const connection_pool_type& connection_pool() const& {
      return connection_pool_;
    }

    [[nodiscard]] connection_pool_type& connection_pool() && = delete;
    [[nodiscard]] const connection_pool_type& connection_pool() const&& = delete;

   private:
    template <typename CompletionToken>
    auto async_send_prepared(prepared_request prepared, CompletionToken&& token) {
      auto bound_token = asio::bind_allocator(aero::detail::aligned_allocator<>{}, std::forward<CompletionToken>(token));

      return asio::async_initiate<decltype(bound_token), void(std::error_code, http::response)>(
        asio::co_composed<void(std::error_code, http::response)>(
          [this](auto, prepared_request prepared) mutable -> void {
            for (std::size_t attempt_index = 0;; ++attempt_index) {
              auto connection = connection_pool_.acquire(prepared.endpoint.host, prepared.endpoint.port);
              if (!connection.has_value()) {
                co_return {connection.error(), http::response{}};
              }

              exchange_result result;
              std::tie(result) = co_await async_exchange(connection->transport(),
                prepared.endpoint,
                prepared.request,
                return_as_deferred_tuple());

              if (!result.error) {
                connection->release(prepared.request, result.response);
                co_return {std::error_code{}, std::move(result.response)};
              }

              connection->discard();
              if (!should_retry_request(prepared.request, result, attempt_index)) {
                co_return {result.error, http::response{}};
              }
            }
          },
          get_executor()),
        bound_token,
        std::move(prepared));
    }

    template <typename CompletionToken>
    auto async_exchange(transport_type& transport, basic_client::endpoint endpoint, http::request request,
      CompletionToken&& token) {
      auto bound_token = asio::bind_allocator(aero::detail::aligned_allocator<>{}, std::forward<CompletionToken>(token));

      return asio::async_initiate<decltype(bound_token), void(exchange_result)>(
        asio::co_composed<void(exchange_result)>(
          [this, &transport](auto, basic_client::endpoint endpoint, http::request request) mutable -> void {
            if (!transport.lowest_layer().is_open()) {
              auto [connect_ec] = co_await transport.async_connect(endpoint.host, endpoint.port, return_as_deferred_tuple());
              if (connect_ec) {
                co_return exchange_result{
                  .error = connect_ec,
                  .response = {},
                  .response_started = false,
                };
              }
            }

            auto request_str = serialize_request(request);
            if (!request_str.has_value()) {
              co_return exchange_result{
                .error = request_str.error(),
                .response = {},
                .response_started = false,
              };
            }

            auto [write_headers_ec, bytes_written] =
              co_await transport.async_write(to_bytes(*request_str), return_as_deferred_tuple());
            if (write_headers_ec) {
              co_return exchange_result{
                .error = write_headers_ec,
                .response = {},
                .response_started = false,
              };
            }

            std::vector<std::byte> response_buffer;

            if (should_wait_for_continue(request)) {
              expectation_result expect_result;
              std::tie(expect_result) =
                co_await async_receive_expectation_response(transport, request.method, return_as_deferred_tuple());

              if (expect_result.error) {
                co_return exchange_result{
                  .error = expect_result.error,
                  .response = {},
                  .response_started = expect_result.response_started,
                };
              }

              if (!expect_result.continue_sending_body) {
                co_return exchange_result{
                  .error = std::error_code{},
                  .response = std::move(expect_result.response),
                  .response_started = expect_result.response_started,
                };
              }

              response_buffer = std::move(expect_result.response_buffer);
            }

            if (!request.body.empty()) {
              auto [write_body_ec, bytes_written] = co_await transport.async_write(request.body, return_as_deferred_tuple());
              if (write_body_ec) {
                co_return exchange_result{
                  .error = write_body_ec,
                  .response = {},
                  .response_started = false,
                };
              }
            }

            exchange_result read_result;
            std::tie(read_result) =
              co_await async_read_response(transport, request.method, std::move(response_buffer), return_as_deferred_tuple());

            co_return read_result;
          },
          select_transport_executor(transport)),
        bound_token,
        std::move(endpoint),
        std::move(request));
    }

    template <typename CompletionToken>
    auto async_receive_expectation_response(transport_type& transport, http::method request_method, CompletionToken&& token) {
      auto bound_token = asio::bind_allocator(aero::detail::aligned_allocator<>{}, std::forward<CompletionToken>(token));

      return asio::async_initiate<decltype(bound_token), void(expectation_result)>(
        asio::co_composed<void(expectation_result)>(
          [this, &transport](auto, http::method request_method) mutable -> void {
            std::vector<std::byte> response_buffer;
            bool response_started{false};

            for (;;) {
              auto header_end = find_bytes(response_buffer, http::detail::double_crlf);
              std::size_t bytes_read{};

              if (header_end == std::string_view::npos) {
                auto [read_headers_ec, read_headers_count] = co_await transport.async_read_until(response_buffer,
                  http::detail::double_crlf,
                  asio::cancel_after(options_.expect_continue_timeout, return_as_deferred_tuple()));

                if (read_headers_ec) {
                  if (read_headers_ec == asio::error::operation_aborted && response_buffer.empty()) {
                    co_return expectation_result{
                      .error = std::error_code{},
                      .continue_sending_body = true,
                      .response = {},
                      .response_buffer = std::move(response_buffer),
                      .response_started = false,
                    };
                  }

                  co_return expectation_result{
                    .error = read_headers_ec,
                    .continue_sending_body = false,
                    .response = {},
                    .response_buffer = {},
                    .response_started = response_started || !response_buffer.empty(),
                  };
                }

                bytes_read = read_headers_count;
              } else {
                bytes_read = header_end + http::detail::double_crlf.size();
              }

              auto parsed_head = parse_response_head(response_buffer, bytes_read);
              if (!parsed_head.has_value()) {
                co_return expectation_result{
                  .error = parsed_head.error(),
                  .continue_sending_body = false,
                  .response = {},
                  .response_buffer = {},
                  .response_started = true,
                };
              }

              response_started = true;

              auto response = std::move(parsed_head->response);
              response_buffer = std::move(parsed_head->body_prefix);

              if (response.status_code() == http::status_code::continue_) {
                co_return expectation_result{
                  .error = std::error_code{},
                  .continue_sending_body = true,
                  .response = {},
                  .response_buffer = std::move(response_buffer),
                  .response_started = true,
                };
              }

              if (is_interim_response(response.status_code())) {
                continue;
              }

              std::error_code read_body_ec;
              http::response read_body_response;
              std::tie(read_body_ec, read_body_response) = co_await async_read_final_response_body(transport,
                request_method,
                std::move(response),
                std::move(response_buffer),
                return_as_deferred_tuple());

              co_return expectation_result{
                .error = read_body_ec,
                .continue_sending_body = false,
                .response = std::move(read_body_response),
                .response_buffer = {},
                .response_started = true,
              };
            }
          },
          select_transport_executor(transport)),
        bound_token,
        request_method);
    }

    template <typename CompletionToken>
    auto async_read_response(transport_type& transport, http::method request_method, std::vector<std::byte> response_buffer,
      CompletionToken&& token) {
      auto bound_token = asio::bind_allocator(aero::detail::aligned_allocator<>{}, std::forward<CompletionToken>(token));

      return asio::async_initiate<decltype(bound_token), void(exchange_result)>(
        asio::co_composed<void(exchange_result)>(
          [this, &transport](auto, http::method request_method, std::vector<std::byte> response_buffer) -> void {
            bool response_started = !response_buffer.empty();

            for (;;) {
              auto header_end = find_bytes(response_buffer, http::detail::double_crlf);
              std::size_t bytes_read{};

              if (header_end == std::string_view::npos) {
                auto [read_headers_ec, read_headers_count] =
                  co_await transport.async_read_until(response_buffer, http::detail::double_crlf, return_as_deferred_tuple());
                if (read_headers_ec) {
                  co_return exchange_result{
                    .error = read_headers_ec,
                    .response = {},
                    .response_started = response_started || !response_buffer.empty(),
                  };
                }

                bytes_read = read_headers_count;
              } else {
                bytes_read = header_end + http::detail::double_crlf.size();
              }

              auto parsed_head = parse_response_head(response_buffer, bytes_read);
              if (!parsed_head.has_value()) {
                co_return exchange_result{
                  .error = parsed_head.error(),
                  .response = {},
                  .response_started = true,
                };
              }

              response_started = true;

              auto response = std::move(parsed_head->response);
              response_buffer = std::move(parsed_head->body_prefix);

              if (is_interim_response(response.status_code())) {
                continue;
              }

              std::error_code read_body_ec;
              http::response read_body_response;
              std::tie(read_body_ec, read_body_response) = co_await async_read_final_response_body(transport,
                request_method,
                std::move(response),
                std::move(response_buffer),
                return_as_deferred_tuple());

              co_return exchange_result{
                .error = read_body_ec,
                .response = std::move(read_body_response),
                .response_started = true,
              };
            }
          },
          select_transport_executor(transport)),
        bound_token,
        request_method,
        std::move(response_buffer));
    }

    template <typename CompletionToken>
    auto async_read_final_response_body(transport_type& transport, http::method request_method, http::response response,
      std::vector<std::byte> response_buffer, CompletionToken&& token) {
      auto bound_token = asio::bind_allocator(aero::detail::aligned_allocator<>{}, std::forward<CompletionToken>(token));

      return asio::async_initiate<decltype(bound_token), void(std::error_code, http::response)>(
        asio::co_composed<void(std::error_code, http::response)>(
          [this, &transport](auto, http::method request_method, http::response response, std::vector<std::byte> response_buffer)
            -> void {
            if (is_successful_connect_response(request_method, response.status_code())) {
              co_return {client_error::connect_tunnel_unsupported, http::response{}};
            }

            if (is_bodyless_response(request_method, response.status_code())) {
              co_return {std::error_code{}, std::move(response)};
            }

            auto framing = classify_transfer_encoding(response.status_line.version(), response.headers);

            if (framing == transfer_encoding_framing::invalid) {
              co_return {client_error::response_encoding_unsupported, http::response{}};
            }

            if (framing == transfer_encoding_framing::chunked) {
              co_return co_await async_read_chunked_body(transport,
                std::move(response),
                std::move(response_buffer),
                return_as_deferred_tuple());
            }

            if (framing == transfer_encoding_framing::close_delimited) {
              co_return co_await async_read_close_delimited_body(transport,
                std::move(response),
                std::move(response_buffer),
                return_as_deferred_tuple());
            }

            auto content_length = response.headers.template content_length<std::uint64_t>();
            if (content_length.has_value()) {
              co_return co_await async_read_content_length_body(transport,
                std::move(response),
                std::move(response_buffer),
                *content_length,
                return_as_deferred_tuple());
            }

            co_return co_await async_read_close_delimited_body(transport,
              std::move(response),
              std::move(response_buffer),
              return_as_deferred_tuple());
          },
          select_transport_executor(transport)),
        bound_token,
        request_method,
        std::move(response),
        std::move(response_buffer));
    }

    template <typename CompletionToken>
    auto async_read_content_length_body(transport_type& transport, http::response response, std::vector<std::byte> initial_body,
      std::uint64_t content_length, CompletionToken&& token) {
      auto bound_token = asio::bind_allocator(aero::detail::aligned_allocator<>{}, std::forward<CompletionToken>(token));

      return asio::async_initiate<decltype(bound_token), void(std::error_code, http::response)>(
        asio::co_composed<void(std::error_code, http::response)>(
          [this, &transport](auto, http::response response, std::vector<std::byte> initial_body, std::uint64_t content_length)
            -> void {
            if (initial_body.size() > content_length) {
              co_return {client_error::content_length_mismatch, http::response{}};
            }

            if (content_length > options_.max_response_body_size) {
              co_return {client_error::response_body_too_large, http::response{}};
            }

            response.body = std::move(initial_body);

            auto remaining = content_length - response.body.size();
            if (remaining == 0U) {
              co_return {std::error_code{}, std::move(response)};
            }

            auto [append_ec] = co_await async_append_exactly(transport, response.body, remaining, return_as_deferred_tuple());
            if (append_ec) {
              co_return {append_ec, http::response{}};
            }

            co_return {std::error_code{}, std::move(response)};
          },
          select_transport_executor(transport)),
        bound_token,
        std::move(response),
        std::move(initial_body),
        content_length);
    }

    template <typename CompletionToken>
    auto async_read_close_delimited_body(transport_type& transport, http::response response,
      std::vector<std::byte> initial_body, CompletionToken&& token) {
      auto bound_token = asio::bind_allocator(aero::detail::aligned_allocator<>{}, std::forward<CompletionToken>(token));

      return asio::async_initiate<decltype(bound_token), void(std::error_code, http::response)>(
        asio::co_composed<void(std::error_code, http::response)>(
          [this, &transport](auto, http::response response, std::vector<std::byte> initial_body) -> void {
            if (initial_body.size() > options_.max_response_body_size) {
              co_return {client_error::response_body_too_large, http::response{}};
            }

            response.body = std::move(initial_body);

            for (;;) {
              auto [read_ec, bytes] = co_await transport.async_read_some(return_as_deferred_tuple());
              if (response_body_would_exceed_limit(response.body.size(), bytes.size(), options_.max_response_body_size)) {
                co_return {client_error::response_body_too_large, http::response{}};
              }

              response.body.append_range(bytes);

              if (!read_ec) {
                continue;
              }

              if (read_ec == asio::error::eof) {
                co_return {std::error_code{}, std::move(response)};
              }

              co_return {read_ec, http::response{}};
            }
          },
          select_transport_executor(transport)),
        bound_token,
        std::move(response),
        std::move(initial_body));
    }

    template <typename CompletionToken>
    auto async_read_chunked_body(transport_type& transport, http::response response, std::vector<std::byte> buffer,
      CompletionToken&& token) {
      auto bound_token = asio::bind_allocator(aero::detail::aligned_allocator<>{}, std::forward<CompletionToken>(token));

      return asio::async_initiate<decltype(bound_token), void(std::error_code, http::response)>(
        asio::co_composed<void(std::error_code, http::response)>(
          [this, &transport](auto, http::response response, std::vector<std::byte> buffer) -> void {
            for (;;) {
              auto chunk_size_line_end = find_bytes(buffer, http::detail::crlf);
              if (chunk_size_line_end == std::string_view::npos) {
                auto [read_ec, bytes_read] =
                  co_await transport.async_read_until(buffer, http::detail::crlf, return_as_deferred_tuple());
                if (read_ec || bytes_read == 0U) {
                  co_return {read_ec, http::response{}};
                }

                chunk_size_line_end = find_bytes(buffer, http::detail::crlf);
              }

              auto chunk_size = parse_chunk_size_line(to_string_view(buffer).substr(0, chunk_size_line_end));
              if (!chunk_size.has_value()) {
                co_return {chunk_size.error(), http::response{}};
              }

              consume_prefix(buffer, chunk_size_line_end + http::detail::crlf.size());

              if (*chunk_size == 0U) {
                if (buffer.size() < http::detail::crlf.size()) {
                  auto [read_trailer_ec, bytes_read] =
                    co_await transport.async_read_until(buffer, http::detail::crlf, return_as_deferred_tuple());
                  if (read_trailer_ec || bytes_read == 0U) {
                    co_return {
                      read_trailer_ec ? read_trailer_ec : client_error::chunked_encoding_invalid,
                      http::response{},
                    };
                  }
                }

                if (to_string_view(buffer).starts_with(http::detail::crlf)) {
                  co_return {std::error_code{}, std::move(response)};
                }

                auto trailer_end = find_bytes(buffer, http::detail::double_crlf);
                if (trailer_end == std::string_view::npos) {
                  auto [read_trailer_ec, bytes_read] =
                    co_await transport.async_read_until(buffer, http::detail::double_crlf, return_as_deferred_tuple());
                  if (read_trailer_ec || bytes_read == 0U) {
                    co_return {
                      read_trailer_ec ? read_trailer_ec : client_error::chunked_encoding_invalid,
                      http::response{},
                    };
                  }

                  trailer_end = find_bytes(buffer, http::detail::double_crlf);
                }

                if (trailer_end == std::string_view::npos) {
                  co_return {client_error::chunked_encoding_invalid, http::response{}};
                }

                co_return {std::error_code{}, std::move(response)};
              }

              if (response_body_would_exceed_limit(response.body.size(), *chunk_size, options_.max_response_body_size)) {
                co_return {client_error::response_body_too_large, http::response{}};
              }

              auto required_bytes = *chunk_size + http::detail::crlf.size();
              if (buffer.size() < required_bytes) {
                auto [append_ec] =
                  co_await async_append_exactly(transport, buffer, required_bytes - buffer.size(), return_as_deferred_tuple());
                if (append_ec) {
                  co_return {append_ec, http::response{}};
                }
              }

              if (buffer.size() < required_bytes) {
                co_return {client_error::chunked_encoding_invalid, http::response{}};
              }

              response.body.append_range(buffer | std::views::take(*chunk_size));

              auto chunk_suffix = std::span{buffer}.subspan(*chunk_size, http::detail::crlf.size());
              if (to_string_view(chunk_suffix) != http::detail::crlf) {
                co_return {client_error::chunked_encoding_invalid, http::response{}};
              }

              consume_prefix(buffer, required_bytes);
            }
          },
          select_transport_executor(transport)),
        bound_token,
        std::move(response),
        std::move(buffer));
    }

    template <typename CompletionToken>
    auto async_append_exactly(transport_type& transport, std::vector<std::byte>& out, std::uint64_t bytes_to_read,
      CompletionToken&& token) {
      auto bound_token = asio::bind_allocator(aero::detail::aligned_allocator<>{}, std::forward<CompletionToken>(token));

      return asio::async_initiate<decltype(bound_token), void(std::error_code)>(
        asio::co_composed<void(std::error_code)>(
          [&transport, &out](auto, std::uint64_t bytes_to_read) -> void {
            std::size_t remaining = bytes_to_read;
            std::size_t max_transport_chunk = transport.buffer().size();

            while (remaining != 0U) {
              auto chunk_size = (std::min)(remaining, max_transport_chunk);
              auto [read_ec, bytes] = co_await transport.async_read_exactly(chunk_size, return_as_deferred_tuple());
              if (read_ec) {
                co_return read_ec;
              }

              out.append_range(bytes);
              remaining -= bytes.size();
            }

            co_return std::error_code{};
          },
          select_transport_executor(transport)),
        bound_token,
        bytes_to_read);
    }

    [[nodiscard]] std::expected<prepared_request, std::error_code> prepare_request(basic_client::endpoint endpoint,
      http::request request) {
      endpoint = sanitize_endpoint(std::move(endpoint));
      if (endpoint.host.empty()) {
        return std::unexpected(connection_error::endpoint_host_empty);
      }

      if (endpoint.port == 0U) {
        return std::unexpected(connection_error::endpoint_port_invalid);
      }

      if (request.headers.contains("transfer-encoding")) {
        return std::unexpected(client_error::request_encoding_unsupported);
      }

      if (request.method == http::method::connect && request.url.empty()) {
        request.url = format_authority_target(endpoint);
      }

      auto normalized_target = normalize_request_target(request.method, std::move(request.url));
      if (!normalized_target.has_value()) {
        return std::unexpected(normalized_target.error());
      }

      request.url = std::move(*normalized_target);
      request.content_length = static_cast<std::int64_t>(request.body.size());

      if (!request.headers.contains("host")) {
        auto host_header = derive_host_header_value(endpoint, request.method, request.url);
        if (!host_header.has_value()) {
          return std::unexpected(host_header.error());
        }

        request.headers.add("Host", std::move(*host_header));
      }

      apply_content_length_policy(request);
      apply_connection_policy(request.headers, request.protocol, options_.reuse_connections);

      auto request_line = http::request_line{
        .method = request.method,
        .target = request.url,
        .version = request.protocol,
      };
      if (request_line.serialize().empty()) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      return prepared_request{
        .endpoint = std::move(endpoint),
        .request = std::move(request),
      };
    }

    [[nodiscard]] static basic_client::endpoint sanitize_endpoint(basic_client::endpoint endpoint) {
      bool is_host_in_brackets = endpoint.host.starts_with('[') && endpoint.host.ends_with(']');
      std::size_t host_str_length = endpoint.host.length();

      if (is_host_in_brackets && host_str_length > 2U) {
        endpoint.host = endpoint.host.substr(1, endpoint.host.size() - 2U);
      }

      endpoint.host = aero::detail::to_lowercase(endpoint.host);
      return endpoint;
    }

    [[nodiscard]] static std::expected<std::string, std::error_code> serialize_request(const http::request& request) {
      http::request_line request_line{
        .method = request.method,
        .target = request.url,
        .version = request.protocol,
      };

      auto request_str = request_line.serialize();
      if (request_str.empty()) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      auto headers_str = request.headers.serialize();
      if (headers_str.empty()) {
        request_str.append(http::detail::crlf);
      } else {
        request_str.append(headers_str);
      }

      return request_str;
    }

    [[nodiscard]] static std::expected<parsed_response_head, std::error_code> parse_response_head(
      const std::vector<std::byte>& buffer, std::size_t bytes_read) {
      auto head_view = to_string_view(std::span{buffer}.first(bytes_read));

      auto status_line_end = head_view.find(http::detail::crlf);
      if (status_line_end == std::string_view::npos) {
        return std::unexpected(protocol_error::status_line_invalid);
      }

      auto headers_start = status_line_end + http::detail::crlf.size();
      auto parsed_headers = http::headers::parse(head_view.substr(headers_start));
      if (!parsed_headers.has_value()) {
        return std::unexpected(parsed_headers.error());
      }

      auto body_prefix = std::vector<std::byte>{};
      if (buffer.size() > bytes_read) {
        body_prefix.assign(buffer.begin() + static_cast<std::ptrdiff_t>(bytes_read), buffer.end());
      }

      auto status_line = http::status_line::parse(head_view.substr(0, status_line_end));
      if (!status_line.has_value()) {
        return std::unexpected(status_line.error());
      }

      return parsed_response_head{
        .response =
          http::response{
            .body = {},
            .status_line = *status_line,
            .headers = std::move(*parsed_headers),
          },
        .body_prefix = std::move(body_prefix),
      };
    }

    [[nodiscard]] static std::expected<std::size_t, std::error_code> parse_chunk_size_line(std::string_view line) {
      std::size_t extension_separator_pos = line.find(';');
      std::string_view size_token = line.substr(0, extension_separator_pos);
      if (size_token.empty()) {
        return std::unexpected(client_error::chunked_encoding_invalid);
      }

      using aero::detail::hexadecimal_base;
      using aero::detail::to_decimal;

      return to_decimal<std::size_t>(size_token, hexadecimal_base).transform_error([](std::error_code) {
        return client_error::chunked_encoding_invalid;
      });
    }

    static void apply_connection_policy(http::headers& headers, http::version version, bool reuse_connections) {
      if (!reuse_connections) {
        headers.replace("Connection", "close");
        return;
      }

      if (version == http::version::http1_0 && !headers.contains("connection")) {
        headers.add("Connection", "keep-alive");
      }
    }

    static void apply_content_length_policy(http::request& request) {
      if (!request.body.empty() || request_method_expects_content_length(request.method)) {
        request.headers.replace("Content-Length", std::to_string(request.body.size()));
      }
    }

    [[nodiscard]] static bool request_method_expects_content_length(http::method method) noexcept {
      return method == http::method::post || method == http::method::put || method == http::method::patch;
    }

    [[nodiscard]] static std::expected<std::string, std::error_code> normalize_request_target(http::method method,
      std::string target) {
      if (method == http::method::connect) {
        if (!is_valid_authority_form(target)) {
          return std::unexpected(protocol_error::request_line_invalid);
        }

        return target;
      }

      if (target.empty()) {
        return std::string{"/"};
      }

      if (target == "*") {
        if (method != http::method::options) {
          return std::unexpected(protocol_error::request_line_invalid);
        }

        return target;
      }

      if (looks_like_absolute_form(target)) {
        auto parsed_uri = http::uri::parse(target);
        if (!parsed_uri.has_value()) {
          return std::unexpected(parsed_uri.error());
        }

        return target;
      }

      if (target.front() == '/') {
        return target;
      }

      if (target.front() == '?') {
        return std::string{"/"}.append(target);
      }

      target.insert(target.begin(), '/');
      return target;
    }

    [[nodiscard]] static std::expected<std::string, std::error_code> derive_host_header_value(
      const basic_client::endpoint& endpoint, http::method method, const std::string& request_target) {
      if (method == http::method::connect) {
        return request_target;
      }

      if (looks_like_absolute_form(request_target)) {
        auto parsed_uri = http::uri::parse(request_target);
        if (!parsed_uri.has_value()) {
          return std::unexpected(parsed_uri.error());
        }

        return format_host_header(std::string(parsed_uri->host()),
          parsed_uri->port(),
          parsed_uri->is_https() ? http::default_secure_port : http::default_port);
      }

      return format_host_header(endpoint);
    }

    [[nodiscard]] static std::string format_authority_target(const basic_client::endpoint& endpoint) {
      std::string host = endpoint.host;
      bool is_host_in_brackets = host.starts_with('[') && host.ends_with(']');

      if (host.contains(':') && !is_host_in_brackets) {
        host.insert(host.begin(), '[');
        host.push_back(']');
      }

      host.push_back(':');
      host.append(std::to_string(endpoint.port));
      return host;
    }

    [[nodiscard]] static std::string format_host_header(std::string host, std::uint16_t port,
      std::uint16_t default_port_value) {
      bool is_host_in_brackets = host.starts_with('[') && host.ends_with(']');

      if (host.contains(':') && !is_host_in_brackets) {
        host.insert(host.begin(), '[');
        host.push_back(']');
      }

      if (port == default_port_value) {
        return host;
      }

      host.push_back(':');
      host.append(std::to_string(port));
      return host;
    }

    [[nodiscard]] static std::string format_host_header(const basic_client::endpoint& endpoint) {
      return format_host_header(endpoint.host, endpoint.port, default_endpoint_port);
    }

    [[nodiscard]] static bool is_bodyless_response(http::method request_method, http::status_code status_code) noexcept {
      auto status_value = std::to_underlying(status_code);
      bool is_informational_status_code =
        (status_value >= informational_status_code_min && status_value < informational_status_code_max);

      return request_method == http::method::head || is_informational_status_code ||
             status_code == http::status_code::no_content || status_code == http::status_code::reset_content ||
             status_code == http::status_code::not_modified;
    }

    [[nodiscard]] static bool is_interim_response(http::status_code status_code) noexcept {
      auto status_value = std::to_underlying(status_code);
      return status_value >= informational_status_code_min && status_value < informational_status_code_max &&
             status_code != http::status_code::switching_protocols;
    }

    [[nodiscard]] static bool is_successful_connect_response(http::method request_method,
      http::status_code status_code) noexcept {
      auto status_value = std::to_underlying(status_code);
      bool is_succesfull_status_code =
        (status_value >= succesfull_status_code_min && status_value < succesfull_status_code_max);

      return request_method == http::method::connect && is_succesfull_status_code;
    }

    [[nodiscard]] static bool should_wait_for_continue(const http::request& request) {
      return request.protocol == http::version::http1_1 && !request.body.empty() &&
             request.headers.contains_token("expect", "100-continue");
    }

    [[nodiscard]] static bool should_retry_request(const http::request& request, const exchange_result& result,
      std::size_t attempt_index) {
      if (attempt_index != 0U) {
        return false;
      }

      if (result.response_started) {
        return false;
      }

      if (!request_method_is_idempotent(request)) {
        return false;
      }

      return is_retryable_exchange_error(result.error);
    }

    [[nodiscard]] static bool request_method_is_idempotent(const http::request& request) {
      http::request_line request_line{
        .method = request.method,
        .target = "/",
        .version = http::version::http1_1,
      };

      auto serialized_request_line = request_line.serialize();
      return serialized_request_line.starts_with("GET ") || serialized_request_line.starts_with("HEAD ") ||
             serialized_request_line.starts_with("PUT ") || serialized_request_line.starts_with("DELETE ") ||
             serialized_request_line.starts_with("OPTIONS ") || serialized_request_line.starts_with("TRACE ");
    }

    [[nodiscard]] static bool is_retryable_exchange_error(const std::error_code& error) noexcept {
      return error == asio::error::eof || error == asio::error::connection_reset || error == asio::error::connection_aborted ||
             error == asio::error::broken_pipe || error == asio::error::not_connected || error == asio::error::bad_descriptor
#ifdef AERO_USE_TLS
             || error == asio::ssl::error::stream_truncated
#endif
        ;
    }

    static void trim_http_whitespace(std::string_view& value) noexcept {
      while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
      }

      while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
      }
    }

    [[nodiscard]] static bool looks_like_absolute_form(std::string_view target) noexcept {
      return target.find("://") != std::string_view::npos;
    }

    [[nodiscard]] static bool is_valid_port_number(std::string_view value) noexcept {
      if (value.empty()) {
        return false;
      }

      unsigned int port{};
      auto parse_result = std::from_chars(value.data(), value.data() + value.size(), port);

      const auto is_in_uint16_range = port > 0U && port <= 65535U;

      return parse_result.ec == std::errc{} && parse_result.ptr == value.data() + value.size() && is_in_uint16_range;
    }

    [[nodiscard]] static bool is_valid_authority_form(std::string_view target) {
      if (target.empty()) {
        return false;
      }

      if (target.find('/') != std::string_view::npos || target.find('?') != std::string_view::npos ||
          target.find('#') != std::string_view::npos) {
        return false;
      }

      if (target.front() == '[') {
        auto bracket_end = target.find(']');
        if (bracket_end == std::string_view::npos || bracket_end == 1U) {
          return false;
        }

        if (bracket_end + 1U >= target.size() || target[bracket_end + 1U] != ':') {
          return false;
        }

        return is_valid_port_number(target.substr(bracket_end + 2U));
      }

      auto port_separator = target.rfind(':');
      if (port_separator == std::string_view::npos || port_separator == 0U || port_separator + 1U >= target.size()) {
        return false;
      }

      auto host = target.substr(0, port_separator);
      if (host.empty() || host.find(':') != std::string_view::npos) {
        return false;
      }

      return is_valid_port_number(target.substr(port_separator + 1U));
    }

    [[nodiscard]] static transfer_encoding_info inspect_transfer_encoding(const http::headers& headers) {
      auto serialized_headers = headers.serialize();
      auto remaining = std::string_view{serialized_headers};

      transfer_encoding_info info;
      std::vector<std::string> codings;

      while (!remaining.empty()) {
        auto line_end = remaining.find(http::detail::crlf);
        if (line_end == std::string_view::npos) {
          break;
        }

        auto line = remaining.substr(0, line_end);
        remaining.remove_prefix(line_end + http::detail::crlf.size());

        if (line.empty()) {
          break;
        }

        auto value_separator = line.find(':');
        if (value_separator == std::string_view::npos) {
          continue;
        }

        auto header_name = aero::detail::to_lowercase(std::string{line.substr(0, value_separator)});
        if (header_name != "transfer-encoding") {
          continue;
        }

        info.present = true;

        auto value = line.substr(value_separator + 1U);
        for (;;) {
          auto token_separator = value.find(',');
          auto token = token_separator == std::string_view::npos ? value : value.substr(0, token_separator);
          trim_http_whitespace(token);

          if (token.empty()) {
            info.invalid = true;
            return info;
          }

          codings.push_back(aero::detail::to_lowercase(std::string{token}));

          if (token_separator == std::string_view::npos) {
            break;
          }

          value.remove_prefix(token_separator + 1U);
        }
      }

      if (!info.present) {
        return info;
      }

      if (codings.empty()) {
        info.invalid = true;
        return info;
      }

      std::size_t chunked_count = std::ranges::count(codings, std::string_view{"chunked"});
      info.final_chunked = codings.back() == "chunked";
      info.invalid = chunked_count > 1U || (chunked_count == 1U && !info.final_chunked);

      return info;
    }

    [[nodiscard]] static transfer_encoding_framing classify_transfer_encoding(http::version version,
      const http::headers& headers) {
      auto info = inspect_transfer_encoding(headers);
      if (!info.present) {
        return transfer_encoding_framing::none;
      }

      if (info.invalid) {
        return transfer_encoding_framing::invalid;
      }

      if (version != http::version::http1_1) {
        return transfer_encoding_framing::invalid;
      }

      if (info.final_chunked) {
        return transfer_encoding_framing::chunked;
      }

      return transfer_encoding_framing::close_delimited;
    }

    [[nodiscard]] static bool response_body_would_exceed_limit(std::size_t current_size, std::size_t bytes_to_add,
      std::size_t max_response_body_size) noexcept {
      return bytes_to_add > max_response_body_size || current_size > max_response_body_size - bytes_to_add;
    }

    [[nodiscard]] static std::error_code unsupported_scheme_error(bool request_is_https) {
      if (request_is_https && !secure_transport) {
        return aero::error::basic_error::tls_support_unavailable;
      }

      return http::error::uri_error::invalid_scheme;
    }

    [[nodiscard]] static executor_type select_transport_executor(transport_type& transport) {
      return executor_type{transport.lowest_layer().get_executor()};
    }

    [[nodiscard]] static std::string_view to_string_view(std::span<const std::byte> bytes) noexcept {
      return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    [[nodiscard]] static std::string_view to_string_view(const std::vector<std::byte>& bytes) noexcept {
      return to_string_view(std::span<const std::byte>{bytes});
    }

    [[nodiscard]] static std::span<const std::byte> to_bytes(std::string_view text) noexcept {
      return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
    }

    static void consume_prefix(std::vector<std::byte>& buffer, std::size_t bytes_count) {
      if (bytes_count >= buffer.size()) {
        buffer.clear();
        return;
      }

      buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(bytes_count));
    }

    [[nodiscard]] static std::size_t find_bytes(const std::vector<std::byte>& buffer, std::string_view pattern) noexcept {
      return to_string_view(buffer).find(pattern);
    }

    [[nodiscard]] static std::shared_ptr<aero::io_runtime> make_runtime() {
      return std::make_shared<aero::io_runtime>(threads_count_t{default_runtime_threads}, aero::wait_threads);
    }

    [[nodiscard]] static asio::as_tuple_t<asio::deferred_t> return_as_deferred_tuple() {
      return asio::as_tuple(asio::deferred);
    }

    [[nodiscard]] static connection_pool_type make_connection_pool(executor_type executor, const client_options& options) {
      detail::pool_options pool_options{
        .max_idle_connections_per_endpoint = options.max_idle_connections_per_endpoint,
        .transport_buffer_size = options.transport_buffer_size,
      };

#ifdef AERO_USE_TLS
      if constexpr (secure_transport) {
        if (options.tls_context) {
          return connection_pool_type{std::move(executor), *options.tls_context, pool_options};
        }
      }
#endif

      return connection_pool_type{std::move(executor), pool_options};
    }

    std::shared_ptr<aero::io_runtime> runtime_;
    executor_type executor_;
    client_options options_;
    connection_pool_type connection_pool_;
  };

} // namespace aero::http
