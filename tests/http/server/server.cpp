#include <future>
#include <gtest/gtest.h>
#include <source_location>

#include "aero/http/client.hpp"
#include "aero/http/server.hpp"

namespace http = aero::http;

http::headers generate_oversized_header_section() {
  http::headers headers;
  for (int i = 0; i < 10000; i++) {
    headers.add("Header" + std::to_string(i), "hello!");
  }
  return headers;
}

TEST(HttpServerMessageReader, RejectsOversizedHeaderSection) {
  http::server server;

  std::atomic<std::error_code> received_error;

  server.on_error([&](std::error_code ec, std::source_location, std::optional<std::string>) { received_error = ec; });
  server.bind("127.0.0.1", 5839);
  server.get("/aero", [](http::context& ctx) {});
  server.start();

  std::ignore = http::get("http://127.0.0.1:5839/aero", http::version::http1_1, generate_oversized_header_section());

  ASSERT_FALSE(received_error.load());
}
