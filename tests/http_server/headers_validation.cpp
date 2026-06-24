#include "aero/http/server.hpp"
#include "aero/http/status_line.hpp"

#include <asio/completion_condition.hpp>
#include <asio/ip/address.hpp>
#include <asio/read.hpp>
#include <ut/ut.hpp>

using namespace ut;
namespace http = aero::http;

int main() {
  suite http_server_headers_validation = [] {
    "multiple host headers are rejected with status 400"_test = [] {
      http::server server;
      expect[server.bind("127.0.0.1", 5839) == std::error_code{}];
      server.async_start();

      std::string multiple_host_headers_buf = "GET / HTTP/1.1\r\nHost: first\r\nHost: second\r\n\r\n";

      asio::ip::tcp::socket socket(server.get_executor());
      socket.connect(server.endpoint());
      asio::write(socket, asio::buffer(multiple_host_headers_buf));

      // Won't be empty, but this is expected, since server sends close response
      std::error_code ignored_read_ec;

      std::string response_buf;
      asio::read(socket, asio::dynamic_buffer(response_buf), asio::transfer_all(), ignored_read_ec);

      expect[response_buf.ends_with("\r\n\r\n")];

      std::size_t status_line_end = response_buf.find("\r\n");
      expect[status_line_end != std::string::npos];

      std::string_view status_line_str{response_buf.data(), status_line_end};
      auto status_line = http::status_line::parse(status_line_str);
      expect[status_line.has_value()];

      expect(status_line->status_code == http::status::bad_request);
      expect(status_line->reason_phrase == http::to_string(http::status::bad_request));
    };
  };
}
