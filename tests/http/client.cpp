#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <asio/error.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/read_until.hpp>
#include <asio/write.hpp>

#include "aero/http/client.hpp"
#include "aero/http/error.hpp"
#include "tcp_acceptor.hpp"

namespace {

  using namespace std::chrono_literals;
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

  http::request make_request(std::string target) {
    return http::request{
      .method = http::method::get,
      .protocol = http::version::http1_1,
      .url = std::move(target),
      .body = {},
      .headers = {},
      .content_length = 0,
    };
  }

  http::request make_request(http::method method, std::string target, std::string_view body_text = {}) {
    return http::request{
      .method = method,
      .protocol = http::version::http1_1,
      .url = std::move(target),
      .body = make_bytes(body_text),
      .headers = {},
      .content_length = 0,
    };
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

  std::string read_http_request_head(tcp::socket& socket, std::string& buffer) {
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
    return request_head;
  }

  std::string read_http_request_body(tcp::socket& socket, std::string& buffer, std::string_view request_head) {
    auto content_length = parse_content_length(request_head);
    if (buffer.size() < content_length) {
      std::error_code read_error;
      asio::read(socket, asio::dynamic_buffer(buffer), asio::transfer_exactly(content_length - buffer.size()), read_error);
      if (read_error) {
        throw std::system_error{read_error};
      }
    }

    auto request_body = buffer.substr(0, content_length);
    buffer.erase(0, content_length);
    return request_body;
  }

  std::string read_http_request(tcp::socket& socket, std::string& buffer) {
    auto request_head = read_http_request_head(socket, buffer);
    auto request_body = read_http_request_body(socket, buffer, request_head);
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

  std::optional<std::string> try_extract_http_message(std::string& buffer) {
    auto header_end = buffer.find(http::detail::double_crlf);
    if (header_end == std::string::npos) {
      return std::nullopt;
    }

    auto head_size = header_end + http::detail::double_crlf.size();
    auto request_head = std::string_view{buffer}.substr(0, head_size);
    auto content_length = parse_content_length(request_head);

    if (buffer.size() < head_size + content_length) {
      return std::nullopt;
    }

    auto request = buffer.substr(0, head_size + content_length);
    buffer.erase(0, head_size + content_length);
    return request;
  }

  std::optional<std::string> try_read_http_request_nonblocking(tcp::socket& socket, std::string& buffer) {
    if (auto request = try_extract_http_message(buffer)) {
      return request;
    }

    std::array<char, 4096> read_storage{};
    std::error_code read_error;
    auto bytes_read = socket.read_some(asio::buffer(read_storage), read_error);

    if (read_error == asio::error::would_block || read_error == asio::error::try_again) {
      return std::nullopt;
    }

    if (read_error && read_error != asio::error::eof) {
      throw std::system_error{read_error};
    }

    if (bytes_read != 0U) {
      buffer.append(read_storage.data(), bytes_read);
    }

    return try_extract_http_message(buffer);
  }

  std::optional<std::string> try_read_exact_bytes_nonblocking(tcp::socket& socket, std::string& buffer,
    std::size_t bytes_count) {
    if (buffer.size() >= bytes_count) {
      auto result = buffer.substr(0, bytes_count);
      buffer.erase(0, bytes_count);
      return result;
    }

    std::array<char, 4096> read_storage{};
    socket.non_blocking(true);

    std::error_code read_error;
    auto bytes_read = socket.read_some(asio::buffer(read_storage), read_error);

    socket.non_blocking(false);

    if (read_error == asio::error::would_block || read_error == asio::error::try_again) {
      return std::nullopt;
    }

    if (read_error && read_error != asio::error::eof) {
      throw std::system_error{read_error};
    }

    if (bytes_read != 0U) {
      buffer.append(read_storage.data(), bytes_read);
    }

    if (buffer.size() < bytes_count) {
      return std::nullopt;
    }

    auto result = buffer.substr(0, bytes_count);
    buffer.erase(0, bytes_count);
    return result;
  }

  bool try_read_any_bytes_nonblocking(tcp::socket& socket, std::string& buffer) {
    if (!buffer.empty()) {
      return true;
    }

    std::array<char, 4096> read_storage{};
    socket.non_blocking(true);

    std::error_code read_error;
    auto bytes_read = socket.read_some(asio::buffer(read_storage), read_error);

    socket.non_blocking(false);

    if (read_error == asio::error::would_block || read_error == asio::error::try_again) {
      return false;
    }

    if (read_error && read_error != asio::error::eof) {
      throw std::system_error{read_error};
    }

    if (bytes_read != 0U) {
      buffer.append(read_storage.data(), bytes_read);
    }

    return !buffer.empty();
  }

  bool receives_bytes_within(tcp::socket& socket, std::string& buffer, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (try_read_any_bytes_nonblocking(socket, buffer)) {
        return true;
      }

      std::this_thread::sleep_for(1ms);
    }

    return false;
  }

} // namespace

TEST(HttpClient, DefaultClientCanSendRequestToEndpoint) {
  std::string raw_request;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    std::string read_buffer;
    raw_request = read_http_request(socket, read_buffer);
    write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello");
  }};

  http::client client;

  auto response = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/hello"));

  ASSERT_TRUE(response.has_value());
  EXPECT_EQ(response->status_code(), http::status_code::ok);
  EXPECT_EQ(response->text(), "hello");

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_TRUE(raw_request.starts_with("GET /hello HTTP/1.1\r\n"));
  EXPECT_NE(raw_request.find("Host: 127.0.0.1:" + std::to_string(server.port()) + "\r\n"), std::string::npos);
  EXPECT_EQ(raw_request.find("Content-Length: 0\r\n"), std::string::npos);
}

TEST(HttpClient, UrlOverloadParsesTargetAndReusesConnection) {
  std::atomic<int> accepted_connections{0};
  std::vector<std::string> raw_requests;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    accepted_connections.fetch_add(1, std::memory_order_relaxed);

    std::string read_buffer;
    raw_requests.push_back(read_http_request(socket, read_buffer));
    write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\none");

    raw_requests.push_back(read_http_request(socket, read_buffer));
    write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\ntwo");
  }};

  http::client client;

  auto first = client.send("http://127.0.0.1:" + std::to_string(server.port()) + "/first?x=1", make_request({}));
  auto second = client.send("http://127.0.0.1:" + std::to_string(server.port()) + "/second", make_request({}));

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->text(), "one");
  EXPECT_EQ(second->text(), "two");

  server.join();

  ASSERT_FALSE(server.exception());
  ASSERT_EQ(raw_requests.size(), 2U);
  EXPECT_EQ(accepted_connections.load(std::memory_order_relaxed), 1);
  EXPECT_TRUE(raw_requests[0].starts_with("GET /first?x=1 HTTP/1.1\r\n"));
  EXPECT_TRUE(raw_requests[1].starts_with("GET /second HTTP/1.1\r\n"));
}

TEST(HttpClient, EndpointOverloadNormalizesEmptyTargetToSlash) {
  std::string raw_request;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    std::string read_buffer;
    raw_request = read_http_request(socket, read_buffer);
    write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
  }};

  http::client client;
  auto response = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request(""));

  ASSERT_TRUE(response.has_value());
  EXPECT_EQ(response->text(), "ok");

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_TRUE(raw_request.starts_with("GET / HTTP/1.1\r\n"));
}

TEST(HttpClient, EndpointOverloadNormalizesQueryOnlyTarget) {
  std::string raw_request;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    std::string read_buffer;
    raw_request = read_http_request(socket, read_buffer);
    write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
  }};

  http::client client;
  auto response = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("?page=1"));

  ASSERT_TRUE(response.has_value());
  EXPECT_EQ(response->text(), "ok");

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_TRUE(raw_request.starts_with("GET /?page=1 HTTP/1.1\r\n"));
}

TEST(HttpClient, SendsRequestBodyAndCorrectContentLength) {
  std::string raw_request;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    std::string read_buffer;
    raw_request = read_http_request(socket, read_buffer);
    write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\ncreated");
  }};

  http::client client;
  auto response = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request(http::method::post, "/submit", "hello=world"));

  ASSERT_TRUE(response.has_value());
  EXPECT_EQ(response->text(), "created");

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_TRUE(raw_request.starts_with("POST /submit HTTP/1.1\r\n"));
  EXPECT_NE(raw_request.find("Content-Length: 11\r\n"), std::string::npos);
  EXPECT_EQ(request_body(raw_request), "hello=world");
}

TEST(HttpClient, PreservesUserProvidedHostHeader) {
  std::string raw_request;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    std::string read_buffer;
    raw_request = read_http_request(socket, read_buffer);
    write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
  }};

  auto request = make_request("/custom-host");
  request.headers.add("Host", "example.test");

  http::client client;
  auto response = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    std::move(request));

  ASSERT_TRUE(response.has_value());
  EXPECT_EQ(response->text(), "ok");

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_NE(raw_request.find("Host: example.test\r\n"), std::string::npos);
  EXPECT_EQ(raw_request.find("Host: 127.0.0.1:" + std::to_string(server.port()) + "\r\n"), std::string::npos);
}

TEST(HttpClient, ReadsChunkedResponseWithExtensions) {
  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    std::string read_buffer;
    std::ignore = read_http_request(socket, read_buffer);

    write_http_response(socket,
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "5;foo=bar\r\n"
      "hello\r\n"
      "6\r\n"
      " world\r\n"
      "0\r\n"
      "\r\n");
  }};

  http::client client;
  auto response = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/chunked"));

  ASSERT_TRUE(response.has_value());
  EXPECT_EQ(response->status_code(), http::status_code::ok);
  EXPECT_EQ(response->text(), "hello world");

  server.join();

  ASSERT_FALSE(server.exception());
}

TEST(HttpClient, ReadsCloseDelimitedResponseAndDoesNotReuseConnection) {
  std::atomic<int> accepted_connections{0};

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    {
      auto socket = server.accept();
      accepted_connections.fetch_add(1, std::memory_order_relaxed);

      std::string read_buffer;
      std::ignore = read_http_request(socket, read_buffer);
      write_http_response(socket, "HTTP/1.1 200 OK\r\n\r\nalpha");
    }

    {
      auto socket = server.accept();
      accepted_connections.fetch_add(1, std::memory_order_relaxed);

      std::string read_buffer;
      std::ignore = read_http_request(socket, read_buffer);
      write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nbeta");
    }
  }};

  http::client client;

  auto first = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/first"));

  auto second = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/second"));

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->text(), "alpha");
  EXPECT_EQ(second->text(), "beta");

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_EQ(accepted_connections.load(std::memory_order_relaxed), 2);
}

TEST(HttpClient, ReadsHttp11NonChunkedTransferEncodingAsCloseDelimitedAndDoesNotReuseConnection) {
  std::atomic<int> accepted_connections{0};

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    {
      auto socket = server.accept();
      accepted_connections.fetch_add(1, std::memory_order_relaxed);

      std::string read_buffer;
      std::ignore = read_http_request(socket, read_buffer);
      write_http_response(socket,
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: gzip\r\n"
        "\r\n"
        "alpha");
    }

    {
      auto socket = server.accept();
      accepted_connections.fetch_add(1, std::memory_order_relaxed);

      std::string read_buffer;
      std::ignore = read_http_request(socket, read_buffer);
      write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nbeta");
    }
  }};

  http::client client;

  auto first = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/first"));

  auto second = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/second"));

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->text(), "alpha");
  EXPECT_EQ(second->text(), "beta");

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_EQ(accepted_connections.load(std::memory_order_relaxed), 2);
}

TEST(HttpClient, RejectsInvalidHttp11TransferEncodingFraming) {
  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    std::string read_buffer;
    std::ignore = read_http_request(socket, read_buffer);

    write_http_response(socket,
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked, gzip\r\n"
      "\r\n");
  }};

  http::client client;
  auto response = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/invalid-transfer-encoding"));

  ASSERT_FALSE(response.has_value());
  EXPECT_EQ(response.error(), http::error::client_error::response_encoding_unsupported);

  server.join();

  ASSERT_FALSE(server.exception());
}

TEST(HttpClient, RejectsHttp10ResponseWithTransferEncoding) {
  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    std::string read_buffer;
    std::ignore = read_http_request(socket, read_buffer);

    write_http_response(socket,
      "HTTP/1.0 200 OK\r\n"
      "Connection: keep-alive\r\n"
      "Transfer-Encoding: gzip\r\n"
      "\r\n");
  }};

  http::client client;
  auto response = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/http10-transfer-encoding"));

  ASSERT_FALSE(response.has_value());
  EXPECT_EQ(response.error(), http::error::client_error::response_encoding_unsupported);

  server.join();

  ASSERT_FALSE(server.exception());
}

TEST(HttpClient, DoesNotReuseConnectionWhenReuseConnectionsDisabled) {
  std::atomic<int> accepted_connections{0};
  std::vector<std::string> raw_requests;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    {
      auto socket = server.accept();
      accepted_connections.fetch_add(1, std::memory_order_relaxed);

      std::string read_buffer;
      raw_requests.push_back(read_http_request(socket, read_buffer));
      write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\none");
    }

    {
      auto socket = server.accept();
      accepted_connections.fetch_add(1, std::memory_order_relaxed);

      std::string read_buffer;
      raw_requests.push_back(read_http_request(socket, read_buffer));
      write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\ntwo");
    }
  }};

  http::client client{http::client_options{.reuse_connections = false}};

  auto first = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/first"));

  auto second = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/second"));

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->text(), "one");
  EXPECT_EQ(second->text(), "two");

  server.join();

  ASSERT_FALSE(server.exception());
  ASSERT_EQ(raw_requests.size(), 2U);
  EXPECT_EQ(accepted_connections.load(std::memory_order_relaxed), 2);
  EXPECT_NE(raw_requests[0].find("Connection: close\r\n"), std::string::npos);
  EXPECT_NE(raw_requests[1].find("Connection: close\r\n"), std::string::npos);
}

TEST(HttpClient, DoesNotReuseConnectionWhenResponseSaysConnectionClose) {
  std::atomic<int> accepted_connections{0};

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    {
      auto socket = server.accept();
      accepted_connections.fetch_add(1, std::memory_order_relaxed);

      std::string read_buffer;
      std::ignore = read_http_request(socket, read_buffer);
      write_http_response(socket, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 3\r\n\r\none");
    }

    {
      auto socket = server.accept();
      accepted_connections.fetch_add(1, std::memory_order_relaxed);

      std::string read_buffer;
      std::ignore = read_http_request(socket, read_buffer);
      write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\ntwo");
    }
  }};

  http::client client;

  auto first = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/first"));

  auto second = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/second"));

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->text(), "one");
  EXPECT_EQ(second->text(), "two");

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_EQ(accepted_connections.load(std::memory_order_relaxed), 2);
}

TEST(HttpClient, ReusesConnectionAfterNoContentResponse) {
  std::atomic<int> accepted_connections{0};
  std::vector<std::string> raw_requests;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto first_socket = server.accept();
    accepted_connections.fetch_add(1, std::memory_order_relaxed);

    std::string first_read_buffer;
    raw_requests.push_back(read_http_request(first_socket, first_read_buffer));
    write_http_response(first_socket, "HTTP/1.1 204 No Content\r\n\r\n");

    first_socket.non_blocking(true);

    auto reuse_deadline = std::chrono::steady_clock::now() + 500ms;
    for (;;) {
      if (auto request = try_read_http_request_nonblocking(first_socket, first_read_buffer)) {
        raw_requests.push_back(std::move(*request));
        write_http_response(first_socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
        return;
      }

      if (std::chrono::steady_clock::now() >= reuse_deadline) {
        break;
      }

      std::this_thread::sleep_for(1ms);
    }

    auto second_socket = server.accept();
    accepted_connections.fetch_add(1, std::memory_order_relaxed);

    std::string second_read_buffer;
    raw_requests.push_back(read_http_request(second_socket, second_read_buffer));
    write_http_response(second_socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
  }};

  http::client client;

  auto first = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/empty"));

  auto second = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/after-empty"));

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->status_code(), http::status_code::no_content);
  EXPECT_TRUE(first->body.empty());
  EXPECT_EQ(second->text(), "ok");

  server.join();

  ASSERT_FALSE(server.exception());
  ASSERT_EQ(raw_requests.size(), 2U);
  EXPECT_EQ(accepted_connections.load(std::memory_order_relaxed), 1);
  EXPECT_TRUE(raw_requests[0].starts_with("GET /empty HTTP/1.1\r\n"));
  EXPECT_TRUE(raw_requests[1].starts_with("GET /after-empty HTTP/1.1\r\n"));
}

TEST(HttpClient, ReusesConnectionAfterResetContentResponse) {
  std::atomic<int> accepted_connections{0};
  std::vector<std::string> raw_requests;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto first_socket = server.accept();
    accepted_connections.fetch_add(1, std::memory_order_relaxed);

    std::string first_read_buffer;
    raw_requests.push_back(read_http_request(first_socket, first_read_buffer));
    write_http_response(first_socket, "HTTP/1.1 205 Reset Content\r\n\r\n");

    first_socket.non_blocking(true);

    auto reuse_deadline = std::chrono::steady_clock::now() + 500ms;
    for (;;) {
      if (auto request = try_read_http_request_nonblocking(first_socket, first_read_buffer)) {
        raw_requests.push_back(std::move(*request));
        write_http_response(first_socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
        return;
      }

      if (std::chrono::steady_clock::now() >= reuse_deadline) {
        break;
      }

      std::this_thread::sleep_for(1ms);
    }

    auto second_socket = server.accept();
    accepted_connections.fetch_add(1, std::memory_order_relaxed);

    std::string second_read_buffer;
    raw_requests.push_back(read_http_request(second_socket, second_read_buffer));
    write_http_response(second_socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
  }};

  http::client client;

  auto first = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/reset"));

  auto second = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/after-reset"));

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(std::to_underlying(first->status_code()), 205);
  EXPECT_TRUE(first->body.empty());
  EXPECT_EQ(second->text(), "ok");

  server.join();

  ASSERT_FALSE(server.exception());
  ASSERT_EQ(raw_requests.size(), 2U);
  EXPECT_EQ(accepted_connections.load(std::memory_order_relaxed), 1);
  EXPECT_TRUE(raw_requests[0].starts_with("GET /reset HTTP/1.1\r\n"));
  EXPECT_TRUE(raw_requests[1].starts_with("GET /after-reset HTTP/1.1\r\n"));
}

TEST(HttpClient, ReturnsUriErrorForInvalidScheme) {
  http::client client;

  auto response = client.send("ftp://127.0.0.1/resource", make_request({}));

  ASSERT_FALSE(response.has_value());
  EXPECT_EQ(response.error(), http::error::uri_error::invalid_scheme);
}

TEST(HttpClient, ReadsFinalResponseAfterInterimResponseAndReusesConnection) {
  std::atomic<int> accepted_connections{0};
  std::vector<std::string> raw_requests;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    accepted_connections.fetch_add(1, std::memory_order_relaxed);

    std::string read_buffer;
    raw_requests.push_back(read_http_request(socket, read_buffer));
    write_http_response(socket,
      "HTTP/1.1 103 Early Hints\r\n"
      "Link: </style.css>; rel=preload; as=style\r\n"
      "\r\n"
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 5\r\n"
      "\r\n"
      "first");

    raw_requests.push_back(read_http_request(socket, read_buffer));
    write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nsecond");
  }};

  http::client client;

  auto first = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/first"));

  auto second = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/second"));

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->status_code(), http::status_code::ok);
  EXPECT_EQ(first->text(), "first");
  EXPECT_EQ(second->status_code(), http::status_code::ok);
  EXPECT_EQ(second->text(), "second");

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_EQ(accepted_connections.load(std::memory_order_relaxed), 1);
  ASSERT_EQ(raw_requests.size(), 2U);
  EXPECT_TRUE(raw_requests[0].starts_with("GET /first HTTP/1.1\r\n"));
  EXPECT_TRUE(raw_requests[1].starts_with("GET /second HTTP/1.1\r\n"));
}

TEST(HttpClient, WaitsForContinueBeforeSendingRequestBodyWhenServerRespondsImmediately) {
  bool body_arrived_before_continue{false};
  std::string raw_request_head;
  std::string received_body;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    std::string read_buffer;

    raw_request_head = read_http_request_head(socket, read_buffer);
    body_arrived_before_continue = try_read_any_bytes_nonblocking(socket, read_buffer);

    write_http_response(socket, "HTTP/1.1 100 Continue\r\n\r\n");

    received_body = read_http_request_body(socket, read_buffer, raw_request_head);

    write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
  }};

  auto request = make_request(http::method::post, "/upload", "payload");
  request.headers.add("Expect", "100-continue");

  http::client client;
  auto response = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    std::move(request));

  ASSERT_TRUE(response.has_value());
  EXPECT_EQ(response->status_code(), http::status_code::ok);
  EXPECT_EQ(response->text(), "ok");

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_FALSE(body_arrived_before_continue);
  EXPECT_TRUE(raw_request_head.starts_with("POST /upload HTTP/1.1\r\n"));
  EXPECT_EQ(received_body, "payload");
}

TEST(HttpClient, DoesNotSendRequestBodyAfterFinalResponseToExpectContinue) {
  bool body_arrived_before_final{false};
  bool body_arrived_after_final{false};
  std::string raw_request_head;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    std::string read_buffer;

    raw_request_head = read_http_request_head(socket, read_buffer);
    body_arrived_before_final = try_read_any_bytes_nonblocking(socket, read_buffer);

    write_http_response(socket,
      "HTTP/1.1 413 Payload Too Large\r\n"
      "Connection: close\r\n"
      "Content-Length: 0\r\n"
      "\r\n");

    body_arrived_after_final = receives_bytes_within(socket, read_buffer, 100ms);
  }};

  auto request = make_request(http::method::post, "/reject", "payload");
  request.headers.add("Expect", "100-continue");

  http::client client;
  auto response = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    std::move(request));

  ASSERT_TRUE(response.has_value());
  EXPECT_EQ(std::to_underlying(response->status_code()), 413);
  EXPECT_TRUE(response->body.empty());

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_FALSE(body_arrived_before_final);
  EXPECT_FALSE(body_arrived_after_final);
  EXPECT_TRUE(raw_request_head.starts_with("POST /reject HTTP/1.1\r\n"));
}

TEST(HttpClient, RetriesIdempotentRequestAfterStalePersistentConnectionCloses) {
  std::atomic<int> accepted_connections{0};
  std::vector<std::string> raw_requests;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    {
      auto socket = server.accept();
      accepted_connections.fetch_add(1, std::memory_order_relaxed);

      std::string read_buffer;
      raw_requests.push_back(read_http_request(socket, read_buffer));
      write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nfirst");
    }

    {
      auto socket = server.accept();
      accepted_connections.fetch_add(1, std::memory_order_relaxed);

      std::string read_buffer;
      raw_requests.push_back(read_http_request(socket, read_buffer));
      write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nsecond");
    }
  }};

  http::client client;

  auto first = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/first"));

  auto second = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/second"));

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->text(), "first");
  EXPECT_EQ(second->text(), "second");

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_EQ(accepted_connections.load(std::memory_order_relaxed), 2);
  ASSERT_EQ(raw_requests.size(), 2U);
  EXPECT_TRUE(raw_requests[0].starts_with("GET /first HTTP/1.1\r\n"));
  EXPECT_TRUE(raw_requests[1].starts_with("GET /second HTTP/1.1\r\n"));
}

TEST(HttpClient, Http10RequestWithExpectContinueSendsBodyWithoutWaitingForInterimResponse) {
  std::string raw_request_head;
  std::string received_body;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    std::string read_buffer;

    raw_request_head = read_http_request_head(socket, read_buffer);

    auto content_length = parse_content_length(raw_request_head);
    auto deadline = std::chrono::steady_clock::now() + 1s;

    for (;;) {
      if (auto request_body = try_read_exact_bytes_nonblocking(socket, read_buffer, content_length)) {
        received_body = std::move(*request_body);
        break;
      }

      if (std::chrono::steady_clock::now() >= deadline) {
        throw std::runtime_error{"http/1.0 request body was not received"};
      }

      std::this_thread::sleep_for(1ms);
    }

    write_http_response(socket,
      "HTTP/1.0 200 OK\r\n"
      "Connection: keep-alive\r\n"
      "Content-Length: 2\r\n"
      "\r\n"
      "ok");
  }};

  auto request = make_request(http::method::post, "/upload", "payload");
  request.protocol = http::version::http1_0;
  request.headers.add("Expect", "100-continue");

  http::client client;
  auto response = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    std::move(request));

  ASSERT_TRUE(response.has_value());
  EXPECT_EQ(response->status_code(), http::status_code::ok);
  EXPECT_EQ(response->text(), "ok");

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_TRUE(raw_request_head.starts_with("POST /upload HTTP/1.0\r\n"));
  EXPECT_EQ(received_body, "payload");
}

TEST(HttpClient, ConnectUsesAuthorityFormAndRejectsSuccessfulTunnelResponse) {
  std::string raw_request_head;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    std::string read_buffer;
    raw_request_head = read_http_request_head(socket, read_buffer);

    write_http_response(socket,
      "HTTP/1.1 200 Connection Established\r\n"
      "\r\n");
  }};

  auto request = make_request(http::method::connect, "example.com:443");

  http::client client;
  auto response = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    std::move(request));

  ASSERT_FALSE(response.has_value());
  EXPECT_EQ(response.error(), http::error::client_error::connect_tunnel_unsupported);

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_TRUE(raw_request_head.starts_with("CONNECT example.com:443 HTTP/1.1\r\n"));
  EXPECT_NE(raw_request_head.find("Host: example.com:443\r\n"), std::string::npos);
}

TEST(HttpClient, PreservesAbsoluteFormRequestTargetForProxyStyleRequest) {
  std::string raw_request_head;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    std::string read_buffer;
    raw_request_head = read_http_request_head(socket, read_buffer);

    write_http_response(socket,
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 2\r\n"
      "\r\n"
      "ok");
  }};

  auto request = make_request("http://example.com:8080/proxy/path?x=1");

  http::client client;
  auto response = client.send(
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    std::move(request));

  ASSERT_TRUE(response.has_value());
  EXPECT_EQ(response->text(), "ok");

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_TRUE(raw_request_head.starts_with("GET http://example.com:8080/proxy/path?x=1 HTTP/1.1\r\n"));
  EXPECT_NE(raw_request_head.find("Host: example.com:8080\r\n"), std::string::npos);
}
