#include <latch>
#include <print>

#include "aero/http/client.hpp"
#include "aero/http/response.hpp"
#include "aero/io_runtime.hpp"
#include "aero/tls/system_context.hpp"

namespace http = aero::http;
namespace tls = aero::tls;

int main() {
  aero::io_runtime io_runtime{1};

  tls::system_context tls_context{tls::version::tlsv1_3};
  tls_context.disable_deprecated_versions();

  http::client client{io_runtime.get_executor(),
    http::client_options{
      .max_response_body_size = 32768,
      .tls_context = std::ref(tls_context.context()),
    }};

  std::latch latch{1};

  client.async_get("https://example.com/", [&](std::error_code ec, http::response response) {
    if (ec) {
      std::println("Request failed: {}", ec.message());
      latch.count_down();
      return;
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

    latch.count_down();
  });

  latch.wait();

  std::println("Request completed");
}
