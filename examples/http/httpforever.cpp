#include <print>

#include "aero/http/client.hpp"

namespace http = aero::http;

int main() {
  auto response = http::get("http://httpforever.com/");
  if (!response) {
    std::println("Request failed: {}", response.error().message());
    return 1;
  }

  std::println("Received response from example.com:");
  std::println("Response Headers:");
  for (const auto& [name, value] : response->headers) {
    std::println("{}: {}", name, value);
  }
  std::println("Status: {} ({})", response->status_line.reason_phrase, response->status_code());
  if (response->content_type() == "text/html") {
    std::println("Body (first 100 bytes): {}", response->text().substr(0, 100));
  } else {
    std::println("Body: {}", response->text());
  }
}
