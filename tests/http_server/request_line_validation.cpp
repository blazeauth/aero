#include "aero/http/response.hpp"
#include "aero/http/server.hpp"
#include "aero/http/status_line.hpp"

#include <asio/ip/tcp.hpp>
#include <asio/read_until.hpp>
#include <asio/write.hpp>
#include <future>
#include <string>
#include <string_view>
#include <system_error>
#include <ut/ut.hpp>

using namespace ut;
using namespace std::chrono_literals;
namespace http = aero::http;

http::response send_request(http::server<>& server, std::string_view request_buf) {
  asio::ip::tcp::socket socket(server.get_executor());
  socket.connect(server.endpoint());
  asio::write(socket, asio::buffer(request_buf));

  std::error_code read_ec;
  std::string response_buf;
  asio::read_until(socket, asio::dynamic_buffer(response_buf), "\r\n\r\n", read_ec);
  expect(not read_ec) << "failed to read response headers: " << read_ec.message();

  std::size_t status_line_end = response_buf.find("\r\n");
  expect(status_line_end != std::string::npos) << "response has no status line delimiter: " << response_buf;

  std::string status_line_str = response_buf.substr(0, status_line_end);
  auto status_line = http::status_line::parse(status_line_str);
  expect(status_line.has_value()) << "failed to parse response status line: " << status_line_str;

  std::string headers_str = response_buf.substr(status_line_end + 2);
  auto headers = http::headers::parse(headers_str);
  expect(headers.has_value()) << "failed to parse response headers: " << headers_str;

  return {.status_line = *status_line, .headers = *headers};
}

void expect_status(const http::response& response, http::status expected) {
  expect(response.status_line.status_code == expected)
    << "expected status " << static_cast<int>(expected) << ", got " << static_cast<int>(response.status_line.status_code);

  expect(response.status_line.reason_phrase == http::to_string(expected))
    << "expected reason phrase '" << http::to_string(expected) << "', got '" << response.status_line.reason_phrase << "'";
}

int main() {
  http::server server;
  server.set_workers(1);
  server.async_start("127.0.0.1", 0);

  suite http_server_request_line_protocol_validation = [&] {
    "method longer than any implemented is rejected with 501 not implemented"_test = [&] {
      std::string payload = "VERYLONGMETHOD / HTTP/1.1\r\nHost: Hello\r\n\r\n";

      std::future<http::response> f = std::async(std::launch::async, [&] { return send_request(server, payload); });

      http::response response;
      if (f.wait_for(5s) == std::future_status::ready) {
        response = f.get();
      }

      expect_status(response, http::status::not_implemented);
    };
  };
}
