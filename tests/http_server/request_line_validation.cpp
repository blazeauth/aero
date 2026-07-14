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
  if (read_ec) {
    return {};
  }

  std::size_t status_line_end = response_buf.find("\r\n");
  expect(status_line_end != std::string::npos) << "response has no status line delimiter: " << response_buf;
  if (status_line_end == std::string::npos) {
    return {};
  }

  std::string status_line_str = response_buf.substr(0, status_line_end);
  auto status_line = http::status_line::parse(status_line_str);
  expect(status_line.has_value()) << "failed to parse response status line: " << status_line_str;
  if (not status_line.has_value()) {
    return {};
  }

  std::string headers_str = response_buf.substr(status_line_end + 2);
  auto headers = http::headers::parse(headers_str);
  expect(headers.has_value()) << "failed to parse response headers: " << headers_str;
  if (not headers.has_value()) {
    return {};
  }

  return {.status_line = *status_line, .headers = *headers};
}

void send_request_and_expect_status(http::server<>& server, std::string payload, http::status expected_status) {
  std::future<http::response> f = std::async(std::launch::async, [&] { return send_request(server, payload); });

  http::response response;
  if (f.wait_for(5s) == std::future_status::ready) {
    response = f.get();
  }

  expect(response.status_line.status_code == expected_status) << "expected status " << static_cast<int>(expected_status)
                                                              << ", got " << static_cast<int>(response.status_line.status_code);

  expect(response.status_line.reason_phrase == http::to_string(expected_status))
    << "expected reason phrase '" << http::to_string(expected_status) << "', got '" << response.status_line.reason_phrase
    << "'";
}

int main() {
  http::server server;
  server.set_workers(1);
  server.async_start("127.0.0.1", 0);

  suite http_server_request_line_suite = [&] {
    "URI longer than 8192 bytes is rejected with status 414"_test = [&] {
      std::string payload = "GET / HTTP/1.1";
      payload += std::string(16384, '0');
      payload += "\r\nHost: hello.com\r\n\r\n";
      send_request_and_expect_status(server, payload, http::status::uri_too_long);
    };
  };

  suite http_server_request_line_method_suite = [&] {
    "method longer than any implemented is rejected with 501 not implemented"_test = [&] {
      std::string payload = "VERYLONGMETHOD / HTTP/1.1\r\nHost: Hello\r\n\r\n";
      send_request_and_expect_status(server, payload, http::status::not_implemented);
    };

    "valid but unrecognized method token is rejected with 501 not implemented"_test = [&] {
      std::string payload = "POP / HTTP/1.1\r\nHost: Hello\r\n\r\n";
      send_request_and_expect_status(server, payload, http::status::not_implemented);

      payload = "get / HTTP/1.1\r\nHost: Hello\r\n\r\n";
      send_request_and_expect_status(server, payload, http::status::not_implemented);
    };

    "method that is not a valid token is rejected with 400 bad request"_test = [&] {
      std::string payload = "G(T / HTTP/1.1\r\nHost: Hello\r\n\r\n";
      send_request_and_expect_status(server, payload, http::status::bad_request);
    };
  };

  suite http_server_request_line_request_target_suite = [&] {
  };

  suite http_server_request_line_protocol_version_suite = [&] {
    "unsupported HTTP version is rejected with 505 not supported"_test = [&] {
      std::string payload = "GET / HTTP/9.4\r\nHost: Hello\r\n\r\n";
      send_request_and_expect_status(server, payload, http::status::http_version_not_supported);
    };

    "non-numeric HTTP version is rejected with 400 bad request"_test = [&] {
      std::string payload = "GET / HTTP/hello.0\r\nHost: Hello\r\n\r\n";
      send_request_and_expect_status(server, payload, http::status::bad_request);
    };

    "invalid HTTP version format is rejected with 400 bad request"_test = [&] {
      std::string payload = "GET / HTTP1.0\r\nHost: Hello\r\n\r\n";
      send_request_and_expect_status(server, payload, http::status::bad_request);

      payload = "GET / HTTP/10\r\nHost: Hello\r\n\r\n";
      send_request_and_expect_status(server, payload, http::status::bad_request);

      payload = "GET / HTTP10\r\nHost: Hello\r\n\r\n";
      send_request_and_expect_status(server, payload, http::status::bad_request);
    };
  };
}
