#include "aero/http/response.hpp"
#include "aero/http/server.hpp"
#include "aero/http/status_line.hpp"

#include <asio/completion_condition.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>
#include <ut/ut.hpp>

using namespace ut;
namespace http = aero::http;

http::response send_request(http::server<>& server, std::string_view request_buf) {
  asio::ip::tcp::socket socket(server.get_executor());
  socket.connect(server.endpoint());
  asio::write(socket, asio::buffer(request_buf));

  // Header validation failures are close responses, so read until EOF
  std::error_code ignored_read_ec;
  std::string response_buf;
  asio::read(socket, asio::dynamic_buffer(response_buf), asio::transfer_all(), ignored_read_ec);

  expect[response_buf.ends_with("\r\n\r\n")];

  std::size_t status_line_end = response_buf.find("\r\n");
  expect[status_line_end != std::string::npos];

  std::string status_line_str = response_buf.substr(0, status_line_end);
  auto status_line = http::status_line::parse(status_line_str);
  expect[status_line.has_value()];

  std::string headers_str = response_buf.substr(status_line_str.size());
  auto headers = http::headers::parse(headers_str);
  expect[headers.has_value()];

  return {.status_line = *status_line, .headers = *headers};
}

int main() {
  suite http_server_host_header_validation = [] {
    "request without host header is rejected with 400 bad request"_test = [] {
      http::server server;
      expect[server.bind("127.0.0.1", 5839) == std::error_code{}];
      server.async_start();

      auto response = send_request(server, "GET / HTTP/1.1\r\nAero: Hello\r\n\r\n");

      expect(response.status_line.status_code == http::status::bad_request);
      expect(response.status_line.reason_phrase == http::to_string(http::status::bad_request));
    };

    "request with multiple host headers is rejected with 400 bad request"_test = [] {
      http::server server;
      expect[server.bind("127.0.0.1", 5839) == std::error_code{}];
      server.async_start();

      auto response = send_request(server, "GET / HTTP/1.1\r\nHost: first\r\nHost: second\r\n\r\n");

      expect(response.status_line.status_code == http::status::bad_request);
      expect(response.status_line.reason_phrase == http::to_string(http::status::bad_request));
    };

    "request with case-insensitive duplicate host headers is rejected with 400 bad request"_test = [] {
      http::server server;
      expect[server.bind("127.0.0.1", 5839) == std::error_code{}];
      server.async_start();

      auto response = send_request(server, "GET / HTTP/1.1\r\nHost: first\r\nhost: second\r\n\r\n");

      expect(response.status_line.status_code == http::status::bad_request);
      expect(response.status_line.reason_phrase == http::to_string(http::status::bad_request));
    };
  };
}
