#include <exception>
#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <ut/ut.hpp>
#include <utility>

#include "aero/http/detail/line_endings.hpp"
#include "aero/http/error.hpp"
#include "aero/http/headers.hpp"
#include "aero/http/response.hpp"
#include "aero/http/status.hpp"
#include "aero/http/status_line.hpp"
#include "aero/websocket/client.hpp"
#include "aero/websocket/detail/accept_challenge.hpp"
#include "aero/websocket/error.hpp"
#include "websocket/tcp_acceptor.hpp"

using namespace ut;

namespace http = aero::http;
namespace websocket = aero::websocket;
namespace http_test = test::http;

std::string extract_sec_websocket_key(std::string_view raw_request) {
  auto request_line_end = raw_request.find(http::detail::crlf);
  if (request_line_end == std::string_view::npos) {
    throw std::runtime_error{"websocket request line terminator is missing"};
  }

  auto parsed_headers = http::headers::parse(raw_request.substr(request_line_end + http::detail::crlf.size()));
  if (!parsed_headers.has_value()) {
    throw std::system_error{parsed_headers.error()};
  }

  auto key = parsed_headers->first_value("sec-websocket-key");
  if (!key.has_value()) {
    throw std::runtime_error{"sec-websocket-key header is missing"};
  }

  return std::string{*key};
}

std::string make_valid_switching_response(std::string_view raw_request, std::string_view extra_headers = {}) {
  auto accept = websocket::detail::compute_sec_websocket_accept(extract_sec_websocket_key(raw_request));

  std::string response;
  response.reserve(128U + extra_headers.size() + accept.size());
  response.append("HTTP/1.1 101 Switching Protocols\r\n");
  response.append("Upgrade: websocket\r\n");
  response.append("Connection: Upgrade\r\n");
  response.append("Sec-WebSocket-Accept: ").append(accept).append(http::detail::crlf);
  response.append(extra_headers);
  response.append(http::detail::crlf);
  return response;
}

template <typename WriteResponse>
std::tuple<std::error_code, http::response> connect_with_server_response_tuple(WriteResponse&& write_response) {
  http_test::tcp_acceptor server{[&write_response](http_test::tcp_acceptor& acceptor) {
    auto socket = acceptor.accept();
    std::string read_buffer;
    auto raw_request = http_test::read_http_request(socket, read_buffer);
    std::forward<WriteResponse>(write_response)(socket, raw_request);
  }};

  websocket::client client;
  auto [connect_ec, server_resp] = client.connect("ws://127.0.0.1:" + std::to_string(server.port()) + "/socket");

  server.join();
  if (server.exception()) {
    std::rethrow_exception(server.exception());
  }

  return {connect_ec, std::move(server_resp)};
}

template <typename WriteResponse>
std::expected<http::response, std::error_code> connect_with_server_response(WriteResponse&& write_response) {
  auto [connect_ec, server_resp] = connect_with_server_response_tuple(std::forward<WriteResponse>(write_response));
  if (connect_ec) {
    return std::unexpected(connect_ec);
  }

  return std::move(server_resp);
}

int main() {
  suite websocket_basic_client = [] {
    "returns parsed server response"_test = [] {
      auto result = connect_with_server_response([](http_test::tcp::socket& socket, std::string_view raw_request) {
        http_test::write_http_response(socket, make_valid_switching_response(raw_request, "X-Test: yes\r\n"));
      });

      expect[result.has_value()];
      expect(result->status_code() == http::status::switching_protocols);
      expect(result->headers.contains_token("upgrade", "websocket"));

      auto test_header = result->headers.first_value("x-test");
      expect[test_header.has_value()];
      expect(*test_header == "yes");
    };

    "propagates status line parse error"_test = [] {
      auto result = connect_with_server_response([](http_test::tcp::socket& socket, std::string_view) {
        http_test::write_http_response(socket,
          "TP/1.1 101 Switching Protocols\r\n"
          "Upgrade: websocket\r\n"
          "Connection: Upgrade\r\n"
          "\r\n");
      });

      expect[not result.has_value()];
      expect(result.error() == http::status_line::parse("TP/1.1 101 Switching Protocols").error());
    };

    "propagates headers parse error"_test = [] {
      auto result = connect_with_server_response([](http_test::tcp::socket& socket, std::string_view) {
        http_test::write_http_response(socket,
          "HTTP/1.1 101 Switching Protocols\r\n"
          "Upgrade websocket\r\n"
          "\r\n");
      });

      expect[not result.has_value()];
      expect(result.error() == http::header_error::field_invalid);
    };

    "returns parsed response when websocket challenge fails"_test = [] {
      auto [connect_ec, server_response] =
        connect_with_server_response_tuple([](http_test::tcp::socket& socket, std::string_view) {
          http_test::write_http_response(socket,
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: definitely-not-the-accept-challenge\r\n"
            "X-Trace: parsed\r\n"
            "\r\n");
        });

      expect(connect_ec == websocket::handshake_error::accept_challenge_failed);
      expect(server_response.status_code() == http::status::switching_protocols);
      expect(server_response.headers.contains_token("upgrade", "websocket"));

      auto trace = server_response.headers.first_value("x-trace");
      expect[trace.has_value()];
      expect(*trace == "parsed");
    };
  };
}
