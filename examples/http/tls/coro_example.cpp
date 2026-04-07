#include <print>

#include <asio/awaitable.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <system_error>

#include "aero/default_executor.hpp"
#include "aero/http/client.hpp"
#include "aero/tls/system_context.hpp"

namespace http = aero::http;
namespace tls = aero::tls;

asio::awaitable<std::error_code> do_request(http::client& client) {
  auto [ec, response] = co_await client.async_get("https://example.com/", asio::as_tuple(asio::use_awaitable));
  if (ec) {
    co_return ec;
  }

  std::println("Received response from example.com:");
  std::println("Response Headers:");
  for (const auto& [name, value] : response.headers) {
    std::println("{}: {}", name, value);
  }
  std::println("Status: {} ({})", response.status_line.reason_phrase, response.status_code());

  if (response.content_type() == "text/html") {
    std::println("Body (first 100 bytes): {}", response.text().substr(0, 100));
  } else {
    std::println("Body: {}", response.text());
  }

  co_return std::error_code{};
}

int main() {
  tls::system_context tls_context{tls::version::tlsv1_3};
  tls_context.disable_deprecated_versions();

  http::client client{aero::get_default_executor(),
    http::client_options{
      .max_response_body_size = 32768,
      .tls_context = std::ref(tls_context.context()),
    }};

  auto fut = asio::co_spawn(client.get_executor(), do_request(client), asio::use_future);

  try {
    auto request_ec = fut.get();
    if (request_ec) {
      std::println("HTTPS request failed with error: {} ({})", request_ec.message(), request_ec.category().name());
    }
  } catch (const std::exception& e) {
    std::println("Exception: {}", e.what());
  }

  std::println("Request completed");
}
