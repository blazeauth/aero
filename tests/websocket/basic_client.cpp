#include <exception>
#include <expected>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <ut/ut.hpp>
#include <utility>

#include "aero/http/detail/line_endings.hpp"
#include "aero/http/error.hpp"
#include "aero/http/headers.hpp"
#include "aero/http/status.hpp"
#include "aero/http/status_line.hpp"
#include "aero/util/final_action.hpp"
#include "aero/websocket/client.hpp"
#include "aero/websocket/detail/accept_challenge.hpp"
#include "aero/websocket/error.hpp"

#include "tcp_server.hpp"

using namespace ut;

namespace http = aero::http;
namespace websocket = aero::websocket;

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

std::string make_websocket_switching_response(std::string_view request_str, std::string_view extra_headers = {}) {
  auto accept = websocket::detail::compute_sec_websocket_accept(extract_sec_websocket_key(request_str));

  std::string response;
  response.append("HTTP/1.1 101 Switching Protocols\r\n");
  response.append("Upgrade: websocket\r\n");
  response.append("Connection: Upgrade\r\n");
  response.append("Sec-WebSocket-Accept: ").append(accept).append(http::detail::crlf);
  response.append(extra_headers);
  response.append(http::detail::crlf);

  return response;
}

int main() {
  tcp_server server;
  std::string port_str = std::to_string(server.port());
  std::string url_str = "ws://127.0.0.1:" + port_str + "/socket";

  suite websocket_basic_client = [&] {
    "connect completes a valid handshake and returns the parsed 101 response"_test = [&] {
      aero::final_action cleanup{[&] { server.close_last_conn(); }};

      server.on_accept([&](std::shared_ptr<connection> conn) {
        auto raw_request = conn->read_request();
        conn->write_response(make_websocket_switching_response(raw_request, "X-Test: yes\r\n"));
      });

      websocket::client client;
      auto [connect_ec, response] = client.connect(url_str);

      expect(not static_cast<bool>(connect_ec));
      expect(response.status_code() == http::status::switching_protocols);
      expect(response.headers.contains_token("upgrade", "websocket"));
      expect(response.headers.first_value("x-test") == "yes");
    };

    "connect sends a valid RFC 6455 upgrade request"_test = [&] {
      aero::final_action cleanup{[&] { server.close_last_conn(); }};

      std::string raw_request;
      server.on_accept([&](std::shared_ptr<connection> conn) {
        raw_request = conn->read_request();
        conn->write_response(make_websocket_switching_response(raw_request));
      });

      websocket::client client;
      auto [connect_ec, response] = client.connect(url_str);

      expect(not static_cast<bool>(connect_ec));

      auto request_line_end = raw_request.find(http::detail::crlf);
      expect(raw_request.substr(0, request_line_end) == "GET /socket HTTP/1.1");

      auto request_headers = http::headers::parse(raw_request.substr(request_line_end + http::detail::crlf.size()));
      expect(request_headers.has_value());
      if (request_headers.has_value()) {
        expect(request_headers->first_value("host") == "127.0.0.1:" + port_str);
        expect(request_headers->contains_token("upgrade", "websocket"));
        expect(request_headers->contains_token("connection", "upgrade"));
        expect(request_headers->first_value("sec-websocket-version") == "13");
        expect(request_headers->first_value("sec-websocket-key").value_or("").size() == 24U);
      }
    };

    "connect reports the status line parse error for a malformed response status line"_test = [&] {
      aero::final_action cleanup{[&] { server.close_last_conn(); }};

      server.on_accept([&](std::shared_ptr<connection> conn) {
        conn->write_response("TP/1.1 101 Switching Protocols\r\n"
                             "Upgrade: websocket\r\n"
                             "Connection: Upgrade\r\n"
                             "\r\n");
      });

      websocket::client client;
      auto [connect_ec, response] = client.connect(url_str);

      expect(static_cast<bool>(connect_ec));
      expect(connect_ec == http::status_line::parse("TP/1.1 101 Switching Protocols").error());
    };

    "connect reports the header parse error for a malformed response header field"_test = [&] {
      aero::final_action cleanup{[&] { server.close_last_conn(); }};

      server.on_accept([&](std::shared_ptr<connection> conn) {
        conn->write_response("HTTP/1.1 101 Switching Protocols\r\n"
                             "Upgrade websocket\r\n"
                             "\r\n");
      });

      websocket::client client;
      auto [connect_ec, response] = client.connect(url_str);

      expect(static_cast<bool>(connect_ec));
      expect(connect_ec == http::header_error::field_invalid);
    };

    "connect reports accept_challenge_failed but still returns the parsed response for a wrong accept key"_test = [&] {
      aero::final_action cleanup{[&] { server.close_last_conn(); }};

      server.on_accept([&](std::shared_ptr<connection> conn) {
        conn->write_response("HTTP/1.1 101 Switching Protocols\r\n"
                             "Upgrade: websocket\r\n"
                             "Connection: Upgrade\r\n"
                             "Sec-WebSocket-Accept: definitely-not-the-accept-challenge\r\n"
                             "X-Trace: parsed\r\n"
                             "\r\n");
      });

      websocket::client client;
      auto [connect_ec, response] = client.connect(url_str);

      expect(connect_ec == websocket::handshake_error::accept_challenge_failed);
      expect(response.status_code() == http::status::switching_protocols);
      expect(response.headers.contains_token("upgrade", "websocket"));
      expect(response.headers.first_value("x-trace") == "parsed");
    };

    "connect reports status_code_invalid when the response is not 101"_test = [&] {
      aero::final_action cleanup{[&] { server.close_last_conn(); }};

      server.on_accept([&](std::shared_ptr<connection> conn) {
        conn->write_response("HTTP/1.1 200 OK\r\n"
                             "Content-Length: 0\r\n"
                             "\r\n");
      });

      websocket::client client;
      auto [connect_ec, response] = client.connect(url_str);

      expect(connect_ec == websocket::handshake_error::status_code_invalid);
    };

    "connect reports upgrade_header_invalid when the response omits the upgrade header"_test = [&] {
      aero::final_action cleanup{[&] { server.close_last_conn(); }};

      server.on_accept([&](std::shared_ptr<connection> conn) {
        conn->write_response("HTTP/1.1 101 Switching Protocols\r\n"
                             "Connection: Upgrade\r\n"
                             "Sec-WebSocket-Accept: placeholder\r\n"
                             "\r\n");
      });

      websocket::client client;
      auto [connect_ec, response] = client.connect(url_str);

      expect(connect_ec == websocket::handshake_error::upgrade_header_invalid);
    };

    "connect reports connection_header_invalid when the response omits the connection header"_test = [&] {
      aero::final_action cleanup{[&] { server.close_last_conn(); }};

      server.on_accept([&](std::shared_ptr<connection> conn) {
        conn->write_response("HTTP/1.1 101 Switching Protocols\r\n"
                             "Upgrade: websocket\r\n"
                             "Sec-WebSocket-Accept: placeholder\r\n"
                             "\r\n");
      });

      websocket::client client;
      auto [connect_ec, response] = client.connect(url_str);

      expect(connect_ec == websocket::handshake_error::connection_header_invalid);
    };

    "connect reports accept_header_invalid when the response omits the accept header"_test = [&] {
      aero::final_action cleanup{[&] { server.close_last_conn(); }};

      server.on_accept([&](std::shared_ptr<connection> conn) {
        conn->write_response("HTTP/1.1 101 Switching Protocols\r\n"
                             "Upgrade: websocket\r\n"
                             "Connection: Upgrade\r\n"
                             "\r\n");
      });

      websocket::client client;
      auto [connect_ec, response] = client.connect(url_str);

      expect(connect_ec == websocket::handshake_error::accept_header_invalid);
    };

    "connect rejects a non-websocket uri scheme before connecting"_test = [&] {
      websocket::client client;
      auto [connect_ec, response] = client.connect("http://127.0.0.1/socket");

      expect(connect_ec == websocket::uri_error::scheme_invalid);
    };

    "connect rejects a uri without a host before connecting"_test = [&] {
      websocket::client client;
      auto [connect_ec, response] = client.connect("ws:///socket");

      expect(connect_ec == websocket::uri_error::authority_empty);
    };

    "the test server handled all requests without throwing"_test = [&] {
      expect(server.exception() == nullptr);
    };
  };
}
