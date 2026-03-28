#include <print>

#include "aero/deadline.hpp"
#include "aero/error.hpp"
#include "aero/tls/system_context.hpp"
#include "aero/tls/version.hpp"
#include "aero/websocket/close_code.hpp"
#include "aero/websocket/tls/client.hpp"

namespace {

  namespace websocket = aero::websocket;
  namespace tls = aero::tls;
  namespace http = aero::http;

  void print_error(std::string_view message, const std::error_code& ec) {
    std::println("{}: {} ({} - {})", message, ec.message(), ec.value(), ec.category().name());
  }

  void print_headers(const http::headers& headers) {
    std::println("[HEADERS] Printing:");
    for (const auto& [name, value] : headers) {
      std::println("{}: {}", name, value);
    }
    std::println("[HEADERS] Done");
  }

} // namespace

int main() {
  using namespace std::chrono_literals;

  tls::system_context tls_ctx{tls::version::tlsv1_2};
  tls_ctx.disable_deprecated_versions();

  websocket::tls::client client{tls_ctx.context()};

  auto handshake_headers = client.connect("wss://stream.binance.com:9443/ws/btcusdt@trade", 5s);
  if (!handshake_headers) {
    print_error("Connect to binance stream failed", handshake_headers.error());
    return 1;
  }

  std::println("Succesfully connected");
  print_headers(*handshake_headers);

  aero::deadline deadline{5min};

  for (;;) {
    if (deadline.expired()) {
      break;
    }

    auto message = client.read(deadline.remaining());
    if (!message) {
      if (message.error() == aero::error::errc::timeout && deadline.expired()) {
        std::println("Read deadline expired, breaking from read-loop");
        break;
      }
      print_error("Failed to receive message from binance stream", message.error());
      break;
    }

    if (!message->is_text()) {
      std::println("Received non-text message type ({}), skipping", message->kind_string());
      continue;
    }

    std::println("Received message from binance stream: {}", message->text());
  }

  auto close_ec = client.close(websocket::close_code::normal, "thank you, we are leaving.");
  if (close_ec) {
    print_error("Close handshake failed", close_ec);
    std::ignore = client.force_close();
  }

  return 0;
}
