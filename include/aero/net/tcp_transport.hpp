#ifndef AERO_NET_TCP_TRANSPORT_HPP
#define AERO_NET_TCP_TRANSPORT_HPP

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <asio/async_result.hpp>
#include <asio/co_composed.hpp>
#include <asio/connect.hpp>
#include <asio/error.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/strand.hpp>

#include "aero/net/concepts/transport.hpp"
#include "aero/net/detail/basic_transport.hpp"
#include "aero/net/error.hpp"

namespace aero::net {

  template <typename Stream = asio::basic_stream_socket<asio::ip::tcp>>
  class tcp_transport final {
   public:
    using stream_type = Stream;
    using mutable_buffer = std::vector<std::byte>;
    using const_buffer = std::span<const std::byte>;
    using port_type = asio::ip::port_type;
    using executor_type = typename detail::basic_transport<stream_type>::executor_type;

    constexpr static auto default_buffer_size = 32 * 1024;

    explicit tcp_transport(executor_type executor, std::size_t buffer_size = default_buffer_size)
      : basic_transport_(executor, buffer_size), resolver_(basic_transport_.get_strand()) {}

    template <typename CompletionToken>
    auto async_connect(std::string host, port_type port, CompletionToken&& token) {
      return asio::async_initiate<CompletionToken, void(std::error_code)>(
        asio::co_composed<void(std::error_code)>(
          [this](auto, std::string host, port_type port) -> void {
            using net::error::connect_error;

            std::error_code address_parse_ec;
            auto address = asio::ip::make_address(host, address_parse_ec);

            bool using_address = !address_parse_ec;
            if (using_address) {
              asio::ip::tcp::endpoint endpoint(address, port);
              auto [connect_ec] = co_await lowest_layer().async_connect(endpoint, asio::as_tuple(asio::deferred));
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

            std::error_code connect_ec;
            co_await asio::async_connect(lowest_layer(), resolved_endpoints, asio::redirect_error(asio::deferred, connect_ec));
            if (connect_ec) {
              co_return connect_ec;
            }

            co_return std::error_code{};
          },
          get_strand()),
        token,
        std::move(host),
        port);
    }

    template <typename CompletionToken>
    auto async_shutdown(CompletionToken&& token) {
      return asio::async_initiate<CompletionToken, void(std::error_code)>(
        asio::co_composed<void(std::error_code)>(
          [this](auto) -> void {
            resolver_.cancel();

            auto& socket = basic_transport_.stream();
            std::error_code shutdown_ec;
            std::error_code close_ec;

            socket.shutdown(asio::ip::tcp::socket::shutdown_both, shutdown_ec);
            if (is_ignorable_close_error(shutdown_ec)) {
              shutdown_ec.clear();
            }

            socket.close(close_ec);
            if (is_ignorable_close_error(close_ec)) {
              close_ec.clear();
            }

            co_return shutdown_ec ? shutdown_ec : close_ec;
          },
          get_strand()),
        token);
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

    net::detail::basic_transport<stream_type> basic_transport_;
    asio::ip::tcp::resolver resolver_;
  };

  static_assert(concepts::transport<tcp_transport<>>);

} // namespace aero::net

#endif
