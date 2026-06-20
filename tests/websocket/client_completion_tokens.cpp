#include <chrono>
#include <concepts>
#include <string_view>
#include <tuple>
#include <ut/ut.hpp>

#include <asio/as_tuple.hpp>
#include <asio/cancel_after.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include "aero/http/response.hpp"
#include "aero/websocket/client.hpp"
#include "aero/websocket/message.hpp"

using namespace ut;

template <typename Client>
void compile_token_matrix() {
  using namespace std::chrono_literals;

  using response_awaitable = asio::awaitable<std::tuple<std::error_code, aero::http::response>>;
  using error_awaitable = asio::awaitable<std::tuple<std::error_code>>;
  using message_awaitable = asio::awaitable<std::tuple<std::error_code, aero::websocket::message>>;

  constexpr auto as_tuple_awaitable = asio::as_tuple(asio::use_awaitable);

  static_assert(
    std::same_as<decltype(std::declval<Client&>().async_connect(std::string_view{}, as_tuple_awaitable)), response_awaitable>);

  static_assert(std::same_as<decltype(std::declval<Client&>().async_connect(std::string_view{},
                               asio::cancel_after(1s, as_tuple_awaitable))),
    response_awaitable>);

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
    std::future<aero::http::response>>);
}

int main() {
  suite websocket_client_completion_tokens = [] {
    "compiles the supported completion token matrix"_test = [] {
      compile_token_matrix<aero::websocket::client>();
    };
  };
}
