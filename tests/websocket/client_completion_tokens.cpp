#include <gtest/gtest.h>

#include <chrono>
#include <concepts>
#include <string_view>
#include <tuple>

#include <asio/as_tuple.hpp>
#include <asio/cancel_after.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include "aero/http/headers.hpp"
#include "aero/websocket/client.hpp"
#include "aero/websocket/message.hpp"

namespace {

  template <typename Client>
  void compile_token_matrix() {
    using namespace std::chrono_literals;

    using headers_awaitable = asio::awaitable<std::tuple<std::error_code, aero::http::headers>>;
    using error_awaitable = asio::awaitable<std::tuple<std::error_code>>;
    using message_awaitable = asio::awaitable<std::tuple<std::error_code, aero::websocket::message>>;

    auto as_tuple_awaitable = asio::as_tuple(asio::use_awaitable);

    static_assert(
      std::same_as<decltype(std::declval<Client&>().async_connect(std::string_view{}, as_tuple_awaitable)), headers_awaitable>);

    static_assert(std::same_as<decltype(std::declval<Client&>().async_connect(std::string_view{},
                                 asio::cancel_after(1s, as_tuple_awaitable))),
      headers_awaitable>);

    static_assert(
      std::same_as<decltype(std::declval<Client&>().async_send_text(std::string_view{}, as_tuple_awaitable)), error_awaitable>);

    static_assert(std::same_as<decltype(std::declval<Client&>().async_read(as_tuple_awaitable)), message_awaitable>);

    std::error_code send_ec{};
    static_assert(std::same_as<decltype(std::declval<Client&>().async_send_text(std::string_view{},
                                 asio::redirect_error(asio::use_awaitable, send_ec))),
      asio::awaitable<void>>);

    static_assert(
      std::same_as<decltype(std::declval<Client&>().async_send_text(std::string_view{}, asio::use_future)), std::future<void>>);

    static_assert(std::same_as<decltype(std::declval<Client&>().async_connect(std::string_view{}, asio::use_future)),
      std::future<aero::http::headers>>);
  }

} // namespace

TEST(WebsocketClientCompletionTokens, VariousTokenMatrixCompiles) {
  compile_token_matrix<aero::websocket::client>();
}
