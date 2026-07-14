#include "aero/http/detail/request_target_validator.hpp"
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

using http::detail::is_valid_authority;

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

  suite http_server_host_header_protocol_validation = [&] {
    "missing Host header is rejected with 400 bad request"_test = [&] {
      std::string payload = "GET / HTTP/1.1\r\nAero: Hello\r\n\r\n";
      send_request_and_expect_status(server, payload, http::status::bad_request);
    };

    "empty Host header is rejected with 400 bad request"_test = [&] {
      std::string payload = "GET / HTTP/1.1\r\nHost:\r\n\r\n";
      send_request_and_expect_status(server, payload, http::status::bad_request);
    };

    "multiple Host headers are rejected with 400 bad request"_test = [&] {
      std::string payload = "GET / HTTP/1.1\r\nHost: example.com\r\nHost: example.com\r\n\r\n";
      send_request_and_expect_status(server, payload, http::status::bad_request);
    };

    "multiple case-insensitive Host headers are rejected with 400 bad request"_test = [&] {
      std::string payload = "GET / HTTP/1.1\r\nHost: example.com\r\nhost: example.com\r\n\r\n";
      send_request_and_expect_status(server, payload, http::status::bad_request);
    };

    "empty Host header is rejected with 400 bad request"_test = [&] {
      std::string payload = "GET / HTTP/1.1\r\nHost:\r\n\r\n";
      send_request_and_expect_status(server, payload, http::status::bad_request);
    };
  };

  suite http_server_host_header_value_validation = [] {
    "accepts valid Host header DNS-like reg-name values"_test = [] {
      expect(is_valid_authority("a"));
      expect(is_valid_authority("localhost"));
      expect(is_valid_authority("example.com"));
      expect(is_valid_authority("EXAMPLE.com"));
      expect(is_valid_authority("a-b.example"));
      expect(is_valid_authority("0"));
      expect(is_valid_authority("123"));
      expect(is_valid_authority("123.example"));
      expect(is_valid_authority("123.abc"));
      expect(is_valid_authority("example.com."));

      std::string max_label(63, 'a');
      expect(is_valid_authority(max_label));
      expect(is_valid_authority(max_label + ".example"));

      std::string max_host =
        std::string(63, 'a') + "." + std::string(63, 'b') + "." + std::string(63, 'c') + "." + std::string(61, 'd');
      expect(max_host.size() == 253);
      expect(is_valid_authority(max_host));
    };

    "rejects Host header reg-name values outside server DNS-like policy"_test = [] {
      expect(not is_valid_authority(""));
      expect(not is_valid_authority("."));
      expect(not is_valid_authority(".example.com"));
      expect(not is_valid_authority("example..com"));
      expect(not is_valid_authority("example.com.."));
      expect(not is_valid_authority("-example.com"));
      expect(not is_valid_authority("example-.com"));
      expect(not is_valid_authority("example.-com"));
      expect(not is_valid_authority("example.com-"));
      expect(not is_valid_authority("example~com"));
      expect(not is_valid_authority("example!$&'()*+,;=com"));
      expect(not is_valid_authority("example.com/"));
      expect(not is_valid_authority("example.com?x"));
      expect(not is_valid_authority("example.com#x"));
      expect(not is_valid_authority("user@example.com"));
      expect(not is_valid_authority("example%2ecom"));
      expect(not is_valid_authority("%41.example"));
      expect(not is_valid_authority("%F0%9F%92%A9.example"));
      expect(not is_valid_authority("example%"));
      expect(not is_valid_authority("example%2"));
      expect(not is_valid_authority("example%zz"));
      expect(not is_valid_authority("example com"));
      expect(not is_valid_authority(" example.com"));
      expect(not is_valid_authority("example.com "));
      expect(not is_valid_authority("\texample.com"));
      expect(not is_valid_authority("example.com\t"));
      expect(not is_valid_authority("example.com\n"));
      expect(not is_valid_authority(std::string_view{"example.com\0", 12}));

      expect(not is_valid_authority(std::string(64, 'a')));

      std::string too_long_host =
        std::string(63, 'a') + "." + std::string(63, 'b') + "." + std::string(63, 'c') + "." + std::string(62, 'd');
      expect(too_long_host.size() == 254);
      expect(not is_valid_authority(too_long_host));

      std::string del_char_host = "exa";
      del_char_host.push_back('\x7f');
      del_char_host += "mple.com";
      expect(not is_valid_authority(del_char_host));

      std::string non_ascii_host = "exa";
      non_ascii_host.push_back(static_cast<char>(0x80));
      non_ascii_host += "mple.com";
      expect(not is_valid_authority(non_ascii_host));
    };

    "accepts valid Host header port values"_test = [] {
      expect(is_valid_authority("example.com:"));
      expect(is_valid_authority("example.com:00080"));
      expect(is_valid_authority("example.com:80"));
      expect(is_valid_authority("example.com:65535"));
      expect(is_valid_authority("127.0.0.1:"));
      expect(is_valid_authority("127.0.0.1:443"));
      expect(is_valid_authority("[::1]:"));
      expect(is_valid_authority("[::1]:65535"));
    };

    "rejects invalid Host header port values"_test = [] {
      expect(not is_valid_authority(":80"));
      expect(not is_valid_authority("example.com:0"));
      expect(not is_valid_authority("example.com:00"));
      expect(not is_valid_authority("example.com:00000"));
      expect(not is_valid_authority("example.com:65536"));
      expect(not is_valid_authority("example.com:99999"));
      expect(not is_valid_authority("example.com:80:90"));
      expect(not is_valid_authority("example.com:abc"));
      expect(not is_valid_authority("example.com:+80"));
      expect(not is_valid_authority("example.com:-1"));
      expect(not is_valid_authority("example.com: 80"));
      expect(not is_valid_authority("example.com:80 "));
      expect(not is_valid_authority("127.0.0.1:0"));
      expect(not is_valid_authority("127.0.0.1:00000"));
      expect(not is_valid_authority("[::1]:0"));
      expect(not is_valid_authority("[::1]:00000"));
      expect(not is_valid_authority("[::1]:65536"));
      expect(not is_valid_authority("[::1]:abc"));
      expect(not is_valid_authority("[::1]:-1"));
      expect(not is_valid_authority("[::1]:443:extra"));
    };

    "accepts valid Host header IPv4 literal values"_test = [] {
      expect(is_valid_authority("0.0.0.0"));
      expect(is_valid_authority("9.10.99.100"));
      expect(is_valid_authority("127.0.0.1"));
      expect(is_valid_authority("255.255.255.255"));
      expect(is_valid_authority("127.0.0.1:"));
      expect(is_valid_authority("127.0.0.1:80"));
    };

    "rejects invalid Host header IPv4-like values"_test = [] {
      expect(not is_valid_authority("256.0.0.1"));
      expect(not is_valid_authority("1.2.3"));
      expect(not is_valid_authority("127.1"));
      expect(not is_valid_authority("1.2.3.4.5"));
      expect(not is_valid_authority("1..2.3"));
      expect(not is_valid_authority(".1.2.3.4"));
      expect(not is_valid_authority("1.2.3.4."));
      expect(not is_valid_authority("01.2.3.4"));
      expect(not is_valid_authority("1.2.003.4"));
      expect(not is_valid_authority("1.2.3.04"));
      expect(not is_valid_authority("1.2.3.4:65536"));
    };

    "rejects invalid Host header IPv4 literal port values"_test = [] {
      expect(not is_valid_authority("1.2.3.4:abc"));
      expect(not is_valid_authority("1.2.3.4:80:90"));
    };

    "accepts valid Host header bracketed IPv6 literal values"_test = [] {
      expect(is_valid_authority("[::]"));
      expect(is_valid_authority("[::1]"));
      expect(is_valid_authority("[2001:db8::1]"));
      expect(is_valid_authority("[2001:db8::ff00:42:8329]"));
      expect(is_valid_authority("[::ffff:192.0.2.1]"));
      expect(is_valid_authority("[0:0:0:0:0:0:192.0.2.1]"));
      expect(is_valid_authority("[ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255]"));
      expect(is_valid_authority("[::1]:443"));
      expect(is_valid_authority("[2001:db8::1]:443"));
    };

    "rejects invalid Host header IPv6 literal values"_test = [] {
      expect(not is_valid_authority("::1"));
      expect(not is_valid_authority("2001:db8::1"));
      expect(not is_valid_authority("["));
      expect(not is_valid_authority("[]"));
      expect(not is_valid_authority("[::1"));
      expect(not is_valid_authority("[::1]]"));
      expect(not is_valid_authority("[::1]host"));
      expect(not is_valid_authority("[gggg::1]"));
      expect(not is_valid_authority("[12345::1]"));
      expect(not is_valid_authority("[::ffff:999.0.2.1]"));
      expect(not is_valid_authority("[::ffff:192.0.2.01]"));
      expect(not is_valid_authority("[1:2:3:4:5:6:7:8:9]"));
      expect(not is_valid_authority("[::1] :80"));
    };

    "rejects Host header IPvFuture literal values outside server policy"_test = [] {
      expect(not is_valid_authority("[v1.fe80]"));
      expect(not is_valid_authority("[vF.a:b]"));
      expect(not is_valid_authority("[v1.!$&'()*+,;=:_~-]"));
      expect(not is_valid_authority("[v1.fe80]:443"));
      expect(not is_valid_authority("[v.fe80]"));
      expect(not is_valid_authority("[vX.fe80]"));
      expect(not is_valid_authority("[v1.]"));
    };
  };
}
