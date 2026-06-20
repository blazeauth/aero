#include <charconv>
#include <cstddef>
#include <future>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ut.hpp"
#include <asio/error.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/read_until.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>

#include "aero/http/client.hpp"
#include "aero/http/error.hpp"
#include "http/tcp_acceptor.hpp"

namespace http = aero::http;
namespace http_test = test::http;
using tcp = asio::ip::tcp;

std::vector<std::byte> make_bytes(std::string_view text) {
  std::vector<std::byte> bytes;
  bytes.reserve(text.size());

  for (unsigned char character : text) {
    bytes.push_back(static_cast<std::byte>(character));
  }

  return bytes;
}

std::size_t parse_content_length(std::string_view headers) {
  constexpr std::string_view content_length_name{"Content-Length:"};
  auto header_position = headers.find(content_length_name);
  if (header_position == std::string_view::npos) {
    return 0;
  }

  auto value_begin = header_position + content_length_name.size();
  while (value_begin < headers.size() && (headers[value_begin] == ' ' || headers[value_begin] == '\t')) {
    ++value_begin;
  }

  auto value_end = headers.find(http::detail::crlf, value_begin);
  if (value_end == std::string_view::npos) {
    throw std::runtime_error{"content-length header line is incomplete"};
  }

  std::size_t content_length{};
  auto parse_result = std::from_chars(headers.data() + static_cast<std::ptrdiff_t>(value_begin),
    headers.data() + static_cast<std::ptrdiff_t>(value_end),
    content_length);
  if (parse_result.ec != std::errc{} || parse_result.ptr != headers.data() + static_cast<std::ptrdiff_t>(value_end)) {
    throw std::runtime_error{"content-length header value is invalid"};
  }

  return content_length;
}

std::string read_http_request(tcp::socket& socket, std::string& buffer) {
  std::error_code read_error;
  auto bytes_read = asio::read_until(socket, asio::dynamic_buffer(buffer), http::detail::double_crlf, read_error);
  if (read_error) {
    throw std::system_error{read_error};
  }
  if (bytes_read == 0U) {
    throw std::runtime_error{"http request head is empty"};
  }

  auto request_head = buffer.substr(0, bytes_read);
  buffer.erase(0, bytes_read);

  auto content_length = parse_content_length(request_head);
  if (buffer.size() < content_length) {
    asio::read(socket, asio::dynamic_buffer(buffer), asio::transfer_exactly(content_length - buffer.size()), read_error);
    if (read_error) {
      throw std::system_error{read_error};
    }
  }

  auto request_body = buffer.substr(0, content_length);
  buffer.erase(0, content_length);

  return request_head + request_body;
}

void write_http_response(tcp::socket& socket, std::string_view response_text) {
  std::error_code write_error;
  auto bytes_written = asio::write(socket, asio::buffer(response_text.data(), response_text.size()), write_error);
  if (write_error) {
    throw std::system_error{write_error};
  }
  if (bytes_written != response_text.size()) {
    throw std::runtime_error{"http response was not written completely"};
  }
}

std::string request_body(std::string_view raw_request) {
  auto body_position = raw_request.find(http::detail::double_crlf);
  if (body_position == std::string_view::npos) {
    throw std::runtime_error{"http request does not contain header terminator"};
  }

  body_position += http::detail::double_crlf.size();
  return std::string{raw_request.substr(body_position)};
}

ut::suite http_client_requests = [] {
  "get uri sends get request with parsed target"_test = [] {
    std::string raw_request;

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto socket = server.accept();
      std::string read_buffer;
      raw_request = read_http_request(socket, read_buffer);
      write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    }};

    http::client client;
    auto response = client.get("http://127.0.0.1:" + std::to_string(server.port()) + "/items?page=1");

    expect(fatal(response.has_value()));
    expect(response->text() == "ok");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(raw_request.starts_with("GET /items?page=1 HTTP/1.1\r\n"));
    expect(raw_request.find("Content-Length: 0\r\n") == std::string::npos);
  };

  "head endpoint uses slash target by default"_test = [] {
    std::string raw_request;

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto socket = server.accept();
      std::string read_buffer;
      raw_request = read_http_request(socket, read_buffer);
      write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    }};

    http::client client;
    auto response = client.head(http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    });

    expect(fatal(response.has_value()));

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(raw_request.starts_with("HEAD / HTTP/1.1\r\n"));
    expect(raw_request.find("Content-Length: 0\r\n") == std::string::npos);
  };

  "delete endpoint sends delete method without automatic content-length"_test = [] {
    std::string raw_request;

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto socket = server.accept();
      std::string read_buffer;
      raw_request = read_http_request(socket, read_buffer);
      write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    }};

    http::client client;
    auto response = client.delete_(
      http::client::endpoint{
        .host = "127.0.0.1",
        .port = server.port(),
        .secure = false,
      },
      "/resource");

    expect(fatal(response.has_value()));
    expect(response->text() == "ok");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(raw_request.starts_with("DELETE /resource HTTP/1.1\r\n"));
    expect(raw_request.find("Content-Length: 0\r\n") == std::string::npos);
  };

  "options endpoint uses asterisk target by default"_test = [] {
    std::string raw_request;

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto socket = server.accept();
      std::string read_buffer;
      raw_request = read_http_request(socket, read_buffer);
      write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    }};

    http::client client;
    auto response = client.options(http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    });

    expect(fatal(response.has_value()));
    expect(response->text() == "ok");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(raw_request.starts_with("OPTIONS * HTTP/1.1\r\n"));
    expect(raw_request.find("Content-Length: 0\r\n") == std::string::npos);
  };

  "post uri sends text body and content-length"_test = [] {
    std::string raw_request;

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto socket = server.accept();
      std::string read_buffer;
      raw_request = read_http_request(socket, read_buffer);
      write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    }};

    http::client client;
    auto response = client.post("http://127.0.0.1:" + std::to_string(server.port()) + "/submit", "hello=world");

    expect(fatal(response.has_value()));
    expect(response->text() == "ok");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(raw_request.starts_with("POST /submit HTTP/1.1\r\n"));
    expect(raw_request.find("Content-Length: 11\r\n") != std::string::npos);
    expect(request_body(raw_request) == "hello=world");
  };

  "put endpoint sends binary body and content-length"_test = [] {
    std::string raw_request;
    auto payload = make_bytes("binary-data");

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto socket = server.accept();
      std::string read_buffer;
      raw_request = read_http_request(socket, read_buffer);
      write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    }};

    http::client client;
    auto response = client.put(
      http::client::endpoint{
        .host = "127.0.0.1",
        .port = server.port(),
        .secure = false,
      },
      "/resource",
      std::span<const std::byte>{payload});

    expect(fatal(response.has_value()));
    expect(response->text() == "ok");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(raw_request.starts_with("PUT /resource HTTP/1.1\r\n"));
    expect(raw_request.find("Content-Length: 11\r\n") != std::string::npos);
    expect(request_body(raw_request) == "binary-data");
  };

  "async patch uri sends patch request body"_test = [] {
    std::string raw_request;

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto socket = server.accept();
      std::string read_buffer;
      raw_request = read_http_request(socket, read_buffer);
      write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    }};

    http::client client;
    auto future = client.async_patch("http://127.0.0.1:" + std::to_string(server.port()) + "/patch",
      "delta",
      http::version::http1_1,
      {},
      asio::use_future);

    auto response = future.get();

    expect(response.status_code() == http::status_code::ok);
    expect(response.text() == "ok");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(raw_request.starts_with("PATCH /patch HTTP/1.1\r\n"));
    expect(raw_request.find("Content-Length: 5\r\n") != std::string::npos);
    expect(request_body(raw_request) == "delta");
  };

  "async get endpoint uses http/1.0 when requested"_test = [] {
    std::string raw_request;

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto socket = server.accept();
      std::string read_buffer;
      raw_request = read_http_request(socket, read_buffer);
      write_http_response(socket,
        "HTTP/1.0 200 OK\r\n"
        "Connection: keep-alive\r\n"
        "Content-Length: 2\r\n"
        "\r\n"
        "ok");
    }};

    http::client client;
    auto future = client.async_get(
      http::client::endpoint{
        .host = "127.0.0.1",
        .port = server.port(),
        .secure = false,
      },
      "/legacy",
      http::version::http1_0,
      {},
      asio::use_future);

    auto response = future.get();

    expect(response.status_code() == http::status_code::ok);
    expect(response.text() == "ok");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(raw_request.starts_with("GET /legacy HTTP/1.0\r\n"));
  };

  "get rejects unsupported protocol version before network io"_test = [] {
    http::client client;

    auto response = client.get("http://127.0.0.1/resource", static_cast<http::version>(99));

    expect(fatal(not response.has_value()));
    expect(response.error() == http::protocol_error::version_invalid);
  };

  "async post rejects unsupported protocol version before network io"_test = [] {
    http::client client;

    auto future =
      client.async_post("http://127.0.0.1/resource", "payload", static_cast<http::version>(99), {}, asio::use_future);

    try {
      std::ignore = future.get();
      expect(fatal(false));
    } catch (const std::system_error& system_error) {
      expect(system_error.code() == http::protocol_error::version_invalid);
    } catch (...) {
      expect(fatal(false));
    }
  };

  "get can send absolute form request target"_test = [] {
    std::string raw_request;

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto socket = server.accept();
      std::string read_buffer;
      raw_request = read_http_request(socket, read_buffer);
      write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    }};

    auto request = http::request{
      .method = http::method::get,
      .protocol = http::version::http1_1,
      .url = "http://example.com:8080/proxy/path?x=1",
      .body = {},
      .headers = {},
      .content_length = 0,
    };

    http::client client;
    auto response = client.send(
      http::client::endpoint{
        .host = "127.0.0.1",
        .port = server.port(),
        .secure = false,
      },
      std::move(request));

    expect(fatal(response.has_value()));
    expect(response->text() == "ok");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(raw_request.starts_with("GET http://example.com:8080/proxy/path?x=1 HTTP/1.1\r\n"));
    expect(raw_request.find("Host: example.com:8080\r\n") != std::string::npos);
  };
};

int main() {}
