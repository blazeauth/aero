#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <asio/async_result.hpp>
#include <asio/bind_allocator.hpp>
#include <asio/co_composed.hpp>
#include <asio/connect.hpp>
#include <asio/error.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/ssl/stream.hpp>
#include <asio/strand.hpp>

#include "aero/detail/aligned_allocator.hpp"
#include "aero/net/concepts/transport.hpp"
#include "aero/net/detail/basic_transport.hpp"
#include "aero/net/error.hpp"
#include "aero/tls/detail/alert_capture.hpp"
#include "aero/tls/detail/x509_verify_error.hpp"
#include "aero/tls/peer_identity.hpp"

namespace aero::net {

  template <typename Stream = asio::ssl::stream<asio::ip::tcp::socket>>
  class tls_transport final {
   public:
    using stream_type = Stream;
    using mutable_buffer = std::vector<std::byte>;
    using const_buffer = std::span<const std::byte>;
    using port_type = asio::ip::port_type;
    using executor_type = typename detail::basic_transport<stream_type>::executor_type;

    explicit tls_transport(executor_type executor, std::size_t buffer_size = detail::default_buffer_size)
      : basic_transport_(executor, buffer_size), resolver_(basic_transport_.get_strand()) {}

    explicit tls_transport(asio::strand<executor_type> strand, std::size_t buffer_size = detail::default_buffer_size)
      : basic_transport_(std::move(strand), buffer_size), resolver_(basic_transport_.get_strand()) {}

    explicit tls_transport(executor_type executor, asio::ssl::context& tls_context,
      std::size_t buffer_size = detail::default_buffer_size)
      : basic_transport_(executor, buffer_size, std::in_place_type<stream_type>, tls_context),
        resolver_(basic_transport_.get_strand()),
        tls_context_(std::addressof(tls_context)) {}

    explicit tls_transport(asio::strand<executor_type> strand, asio::ssl::context& tls_context,
      std::size_t buffer_size = detail::default_buffer_size)
      : basic_transport_(std::move(strand), buffer_size, std::in_place_type<stream_type>, tls_context),
        resolver_(basic_transport_.get_strand()),
        tls_context_(std::addressof(tls_context)) {}

    template <typename CompletionToken>
    auto async_connect(std::string host, port_type port, CompletionToken&& token) {
      auto bound_token = asio::bind_allocator(aero::detail::aligned_allocator<>{}, std::forward<CompletionToken>(token));

      return asio::async_initiate<decltype(bound_token), void(std::error_code)>(
        asio::co_composed<void(std::error_code)>(
          [this](auto, std::string host, port_type port) -> void {
            using net::error::connect_error;

            std::error_code address_parse_ec;
            auto address = asio::ip::make_address(host, address_parse_ec);

            bool using_address = !address_parse_ec;
            std::error_code connect_ec;

            if (using_address) {
              asio::ip::tcp::endpoint endpoint(address, port);
              co_await lowest_layer().async_connect(endpoint, asio::redirect_error(asio::deferred, connect_ec));
              co_return connect_ec;
            }

            auto service = std::to_string(port);

            auto [resolve_ec, resolved_endpoints] =
              co_await resolver_.async_resolve(host, service, asio::as_tuple(asio::deferred));
            if (resolve_ec) {
              if (resolve_ec == asio::error::bad_descriptor) {
                co_return connect_error::host_invalid;
              }
              if (resolve_ec == asio::error::operation_aborted) {
                co_return resolve_ec;
              }

              co_return connect_error::host_resolve_failed;
            }

            co_await asio::async_connect(lowest_layer(), resolved_endpoints, asio::redirect_error(asio::deferred, connect_ec));
            if (connect_ec) {
              co_return connect_ec;
            }

            if (!using_address) {
              if (auto ec = tls::set_sni(stream().native_handle(), host); ec) {
                co_return ec;
              }

              if (auto ec = tls::set_expected_peer_host(stream().native_handle(), host); ec) {
                co_return ec;
              }
            }

            co_return co_await this->async_handshake(asio::as_tuple(asio::deferred));
          },
          get_strand()),
        bound_token,
        std::move(host),
        port);
    }

    template <typename CompletionToken>
    auto async_shutdown(CompletionToken&& token) {
      auto bound_token = asio::bind_allocator(aero::detail::aligned_allocator<>{}, std::forward<CompletionToken>(token));

      return asio::async_initiate<decltype(bound_token), void(std::error_code)>(
        asio::co_composed<void(std::error_code)>(
          [this](auto) -> void {
            resolver_.cancel();

            auto& stream = basic_transport_.stream();
            std::error_code shutdown_ec;
            std::error_code close_ec;

            co_await stream.async_shutdown(asio::redirect_error(asio::deferred, shutdown_ec));
            if (is_ignorable_close_error(shutdown_ec)) {
              shutdown_ec.clear();
            }

            stream.lowest_layer().close(close_ec);
            if (is_ignorable_close_error(close_ec)) {
              close_ec.clear();
            }

            co_return shutdown_ec ? shutdown_ec : close_ec;
          },
          get_strand()),
        bound_token);
    }

    template <typename CompletionToken>
    auto async_read_some(CompletionToken&& token) {
      return basic_transport_.async_read_some(std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_read_until(mutable_buffer& out_buffer, std::string_view delimiter, CompletionToken&& token) {
      return basic_transport_.async_read_until(out_buffer, delimiter, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_read_exactly(std::size_t bytes_count, CompletionToken&& token) {
      return basic_transport_.async_read_exactly(bytes_count, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_write(const_buffer buffer, CompletionToken&& token) {
      return basic_transport_.async_write(buffer, std::forward<CompletionToken>(token));
    }

    [[nodiscard]] executor_type get_executor() const noexcept {
      return basic_transport_.get_executor();
    }

    [[nodiscard]] asio::strand<executor_type> get_strand() const noexcept {
      return basic_transport_.get_strand();
    }

    stream_type& stream() {
      return basic_transport_.stream();
    }

    typename stream_type::lowest_layer_type& lowest_layer() {
      return basic_transport_.lowest_layer();
    }

    mutable_buffer& buffer() {
      return basic_transport_.buffer();
    }

   private:
    static bool is_ignorable_close_error(std::error_code ec) {
      return ec == asio::error::not_connected || ec == asio::error::eof || ec == asio::error::bad_descriptor;
    }

    template <typename CompletionToken>
    auto async_handshake(CompletionToken&& token) {
      auto bound_token = asio::bind_allocator(aero::detail::aligned_allocator<>{}, std::forward<CompletionToken>(token));

      return asio::async_initiate<decltype(bound_token), void(std::error_code)>(
        asio::co_composed<void(std::error_code)>(
          [this](auto) -> void {
            using tls::detail::x509_verify_error;
            auto& stream = basic_transport_.stream();

            tls::detail::alert_capture tls_alerts;
            tls_alerts.install(stream.native_handle());

            auto [handshake_ec] =
              co_await stream.async_handshake(asio::ssl::stream_base::client, asio::as_tuple(asio::deferred));

            auto alert = tls_alerts.get_last_tls_alert();
            if (alert) {
              auto alert_ec = tls::detail::tls_alert_to_error_code(*alert);
              if (alert_ec) {
                co_return alert_ec.value();
              }
            }

            auto verify_result = static_cast<x509_verify_error>(::SSL_get_verify_result(stream.native_handle()));
            if (verify_result != x509_verify_error::ok) {
              auto cert_error = tls::detail::verify_error_to_cert_error(verify_result);
              if (cert_error) {
                co_return cert_error.value();
              }
            }

            co_return handshake_ec;
          },
          get_strand()),
        bound_token);
    }

    net::detail::basic_transport<stream_type> basic_transport_;
    asio::ip::tcp::resolver resolver_;
    asio::ssl::context* tls_context_{nullptr};
  };

  static_assert(concepts::transport<tls_transport<>>);

} // namespace aero::net
