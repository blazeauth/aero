#ifndef AERO_NET_CONCEPTS_TRANSPORT_HPP
#define AERO_NET_CONCEPTS_TRANSPORT_HPP

#include <concepts>
#include <cstddef>
#include <string>
#include <string_view>

#include <asio/any_io_executor.hpp>
#include <asio/as_tuple.hpp>
#include <asio/ip/basic_endpoint.hpp>
#include <asio/strand.hpp>
#include <asio/use_awaitable.hpp>

namespace aero::net::concepts {

  template <typename Transport>
  concept basic_transport =
    requires {
      typename Transport::stream_type;
      typename Transport::mutable_buffer;
      typename Transport::const_buffer;
    } && requires(Transport transport, typename Transport::mutable_buffer& out_buffer, std::string_view delimiter,
           std::size_t bytes_count, typename Transport::const_buffer buffer_view) {
      {
        transport.async_read_some(asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code, typename Transport::const_buffer>>>;

      {
        transport.async_read_until(out_buffer, delimiter, asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code, std::size_t>>>;

      {
        transport.async_read_exactly(bytes_count, asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code, typename Transport::const_buffer>>>;

      {
        transport.async_write(buffer_view, asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code, std::size_t>>>;

      { transport.get_executor() } -> std::same_as<asio::any_io_executor>;
      { transport.get_strand() } -> std::same_as<asio::strand<asio::any_io_executor>>;
      { transport.stream() } -> std::same_as<typename Transport::stream_type&>;
      { transport.lowest_layer() } -> std::same_as<typename Transport::stream_type::lowest_layer_type&>;
      { transport.buffer() } -> std::same_as<typename Transport::mutable_buffer&>;
    };

  template <typename Transport>
  concept transport =
    basic_transport<Transport> && requires(Transport transport, const std::string& host, asio::ip::port_type port) {
      {
        transport.async_connect(host, port, asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code>>>;

      {
        transport.async_shutdown(asio::as_tuple(asio::use_awaitable))
      } -> std::same_as<asio::awaitable<std::tuple<std::error_code>>>;
    };

} // namespace aero::net::concepts

#endif
