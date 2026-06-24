#include "aero/http/response.hpp"
#include "aero/http/server.hpp"
#include "aero/http/status_line.hpp"

#include <asio/ip/tcp.hpp>
#include <asio/read_until.hpp>
#include <asio/write.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <ut/ut.hpp>

using namespace ut;
namespace http = aero::http;

http::response send_request(http::server<>& server, std::string_view request_buf) {
  asio::ip::tcp::socket socket(server.get_executor());
  socket.connect(server.endpoint());
  asio::write(socket, asio::buffer(request_buf));

  std::error_code read_ec;
  std::string response_buf;
  asio::read_until(socket, asio::dynamic_buffer(response_buf), "\r\n\r\n", read_ec);
  expect[not read_ec] << "failed to read response headers: " << read_ec.message();

  std::size_t status_line_end = response_buf.find("\r\n");
  expect[status_line_end != std::string::npos] << "response has no status line delimiter: " << response_buf;

  std::string status_line_str = response_buf.substr(0, status_line_end);
  auto status_line = http::status_line::parse(status_line_str);
  if (not status_line) {
    expect[false] << "failed to parse response status line: " << status_line.error().message();
    return {};
  }

  std::string headers_str = response_buf.substr(status_line_end + 2);
  auto headers = http::headers::parse(headers_str);
  if (not headers) {
    expect[false] << "failed to parse response headers: " << headers.error().message();
    return {};
  }

  return {.status_line = *status_line, .headers = *headers};
}

void expect_status(const http::response& response, http::status expected) {
  expect(response.status_line.status_code == expected)
    << "expected status " << static_cast<int>(expected) << ", got " << static_cast<int>(response.status_line.status_code);

  expect(response.status_line.reason_phrase == http::to_string(expected))
    << "expected reason phrase '" << http::to_string(expected) << "', got '" << response.status_line.reason_phrase << "'";
}

int main() {
  suite http_server_host_header_validation = [] {
    "request without host header is rejected with 400 bad request"_test = [] {
      http::server server;

      auto bind_ec = server.bind("127.0.0.1", 0);
      expect[not bind_ec] << "server bind failed: " << bind_ec.message();
      server.async_start();

      auto response = send_request(server, "GET / HTTP/1.1\r\nAero: Hello\r\n\r\n");

      expect_status(response, http::status::bad_request);
    };

    "request with multiple host headers is rejected with 400 bad request"_test = [] {
      http::server server;

      auto bind_ec = server.bind("127.0.0.1", 0);
      expect[not bind_ec] << "server bind failed: " << bind_ec.message();
      server.async_start();

      auto response = send_request(server, "GET / HTTP/1.1\r\nHost: first\r\nHost: second\r\n\r\n");

      expect_status(response, http::status::bad_request);
    };

    "request with case-insensitive duplicate host headers is rejected with 400 bad request"_test = [] {
      http::server server;

      auto bind_ec = server.bind("127.0.0.1", 0);
      expect[not bind_ec] << "server bind failed: " << bind_ec.message();
      server.async_start();

      auto response = send_request(server, "GET / HTTP/1.1\r\nHost: first\r\nhost: second\r\n\r\n");

      expect_status(response, http::status::bad_request);
    };
  };
}
