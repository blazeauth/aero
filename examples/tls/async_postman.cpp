#include <chrono>
#include <future>
#include <print>
#include <system_error>

#include "aero/http/headers.hpp"
#include "aero/io_runtime.hpp"
#include "aero/tls/initialize.hpp"
#include "aero/tls/system_context.hpp"
#include "aero/tls/version.hpp"
#include "aero/wait_threads.hpp"
#include "aero/websocket/close_code.hpp"
#include "aero/websocket/tls/client.hpp"

#include <asio/awaitable.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

namespace {

  using namespace std::chrono_literals;
  namespace websocket = aero::websocket;

  void print_error(std::string_view message, const std::error_code& ec) {
    std::println("{}: {} ({} - {})", message, ec.message(), ec.value(), ec.category().name());
  }

  void print_headers(const aero::http::headers& headers) {
    std::println("[HEADERS] Printing:");
    for (const auto& [name, value] : headers) {
      std::println("{}: {}", name, value);
    }
    std::println("[HEADERS] Done");
  }

  void set_english_error_messages() {
#if _WIN32
    ::SetThreadUILanguage(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
#endif
  }

  asio::awaitable<std::error_code> async_run_echo_client(websocket::tls::client& client) {
    // https://blog.postman.com/introducing-postman-websocket-echo-service/
    auto [connect_ec, headers] =
      co_await client.async_connect("wss://ws.postman-echo.com/raw", asio::as_tuple(asio::use_awaitable));
    if (connect_ec) {
      co_return connect_ec;
    }

    print_headers(headers);

    auto [write_ec] = co_await client.async_send_text("hello from aero client!!!", asio::as_tuple(asio::use_awaitable));
    if (write_ec) {
      co_return write_ec;
    }

    auto [read_ec, message] = co_await client.async_read(asio::cancel_after(1500ms, asio::as_tuple(asio::use_awaitable)));
    if (read_ec) {
      co_return read_ec;
    }

    std::println("Received message from postman echo server. Kind: {}. Text: {}", message.kind_string(), message.text());

    auto [close_ec] = co_await client.async_close(websocket::close_code::normal, asio::as_tuple(asio::use_awaitable));
    if (close_ec) {
      if (close_ec == aero::error::errc::timeout) {
        co_await client.async_force_close(asio::use_awaitable);
        co_return std::error_code{};
      }
      co_return close_ec;
    }

    co_return std::error_code{};
  }

} // namespace

int main() {
  set_english_error_messages();

  aero::tls::initialize_library();

  auto on_thread_init = [](std::thread::id) {
    set_english_error_messages();
  };

  aero::io_runtime runtime(aero::threads_count_t{1}, on_thread_init, aero::wait_threads);

  aero::tls::system_context tls_context{aero::tls::version::tlsv1_2};
  tls_context.disable_deprecated_versions();

  websocket::tls::client client{runtime.get_executor(), tls_context};

  try {
    // All coroutines should use client executor to serialize all
    // of the operations correctly & prevent any race conditions
    auto echo_ec = asio::co_spawn(client.get_executor(), async_run_echo_client(client), asio::use_future).get();
    if (echo_ec) {
      print_error("Postman echo client failed", echo_ec);
    }
  } catch (const std::system_error& e) {
    print_error("System error exception catched", e.code());
  } catch (const std::future_error& e) {
    print_error("Future error exception catched", e.code());
  }
}
