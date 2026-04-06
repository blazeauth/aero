#include <atomic>
#include <chrono>
#include <expected>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <asio/cancel_after.hpp>
#include <asio/error.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_future.hpp>

#include "aero/http/client.hpp"
#include "aero/http/error.hpp"
#include "tcp_acceptor.hpp"

namespace {

  using namespace std::chrono_literals;
  namespace http = aero::http;
  namespace http_test = test::http;
  using tcp = asio::ip::tcp;

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
      .body = http_test::make_bytes(body_text),
      .headers = {},
      .content_length = 0,
    };
  }

  std::expected<http::response, std::error_code> send_with_timeout(http::client& client, http::client::endpoint endpoint,
    http::request request, std::chrono::steady_clock::duration timeout) {
    try {
      auto future = client.async_send(std::move(endpoint), std::move(request), asio::cancel_after(timeout, asio::use_future));
      return future.get();
    } catch (const std::system_error& system_error) {
      return std::unexpected{system_error.code()};
    } catch (const std::future_error& future_error) {
      return std::unexpected{future_error.code()};
    } catch (...) {
      return std::unexpected{http::error::client_error::unexpected_failure};
    }
  }

  std::expected<http::response, std::error_code> send_with_timeout(http::client& client, std::string_view uri_text,
    http::request request, std::chrono::steady_clock::duration timeout) {
    try {
      auto future = client.async_send(uri_text, std::move(request), asio::cancel_after(timeout, asio::use_future));
      return future.get();
    } catch (const std::system_error& system_error) {
      return std::unexpected{system_error.code()};
    } catch (const std::future_error& future_error) {
      return std::unexpected{future_error.code()};
    } catch (...) {
      return std::unexpected{http::error::client_error::unexpected_failure};
    }
  }

  bool handle_follow_up_request(http_test::tcp_acceptor& server, tcp::socket& first_socket, std::string& first_read_buffer,
    std::atomic<int>& accepted_connections, std::vector<std::string>& raw_requests, std::string_view response_text,
    std::chrono::steady_clock::duration timeout = 5s) {
    first_socket.non_blocking(true);

    std::optional<tcp::socket> second_socket;
    std::string second_read_buffer;
    auto deadline = std::chrono::steady_clock::now() + timeout;

    for (;;) {
      if (std::chrono::steady_clock::now() >= deadline) {
        throw std::runtime_error{"timed out waiting for follow-up request"};
      }

      if (auto request = http_test::try_read_http_request_nonblocking(first_socket, first_read_buffer)) {
        raw_requests.push_back(std::move(*request));
        first_socket.non_blocking(false);
        http_test::write_http_response(first_socket, response_text);
        return true;
      }

      if (!second_socket.has_value()) {
        if (auto accepted_socket = server.try_accept_nonblocking()) {
          accepted_connections.fetch_add(1, std::memory_order_relaxed);
          accepted_socket->non_blocking(true);
          second_socket = std::move(*accepted_socket);
        }
      }

      if (second_socket.has_value()) {
        if (auto request = http_test::try_read_http_request_nonblocking(*second_socket, second_read_buffer)) {
          raw_requests.push_back(std::move(*request));
          second_socket->non_blocking(false);
          http_test::write_http_response(*second_socket, response_text);
          return false;
        }
      }

      std::this_thread::sleep_for(1ms);
    }
  }

} // namespace

TEST(HttpClient, DefaultClientCanSendRequestToEndpoint) {
  std::string raw_request;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    std::string read_buffer;
    raw_request = http_test::read_http_request(socket, read_buffer);
    http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello");
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
  bool reused_connection{false};

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto first_socket = server.accept();
    accepted_connections.fetch_add(1, std::memory_order_relaxed);

    std::string first_read_buffer;
    raw_requests.push_back(http_test::read_http_request(first_socket, first_read_buffer));
    http_test::write_http_response(first_socket, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\none");

    reused_connection = handle_follow_up_request(server,
      first_socket,
      first_read_buffer,
      accepted_connections,
      raw_requests,
      "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\ntwo");
  }};

  http::client client;

  auto first =
    send_with_timeout(client, "http://127.0.0.1:" + std::to_string(server.port()) + "/first?x=1", make_request({}), 2s);
  auto second =
    send_with_timeout(client, "http://127.0.0.1:" + std::to_string(server.port()) + "/second", make_request({}), 2s);

  ASSERT_TRUE(first.has_value()) << first.error().message();
  ASSERT_TRUE(second.has_value()) << second.error().message();
  EXPECT_EQ(first->text(), "one");
  EXPECT_EQ(second->text(), "two");

  server.join();

  ASSERT_FALSE(server.exception());
  ASSERT_EQ(raw_requests.size(), 2U);
  EXPECT_TRUE(reused_connection);
  EXPECT_EQ(accepted_connections.load(std::memory_order_relaxed), 1);
  EXPECT_TRUE(raw_requests[0].starts_with("GET /first?x=1 HTTP/1.1\r\n"));
  EXPECT_TRUE(raw_requests[1].starts_with("GET /second HTTP/1.1\r\n"));
}

TEST(HttpClient, EndpointOverloadNormalizesEmptyTargetToSlash) {
  std::string raw_request;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    std::string read_buffer;
    raw_request = http_test::read_http_request(socket, read_buffer);
    http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
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
    raw_request = http_test::read_http_request(socket, read_buffer);
    http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
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
    raw_request = http_test::read_http_request(socket, read_buffer);
    http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\ncreated");
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
  EXPECT_EQ(http_test::request_body(raw_request), "hello=world");
}

TEST(HttpClient, PreservesUserProvidedHostHeader) {
  std::string raw_request;

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    std::string read_buffer;
    raw_request = http_test::read_http_request(socket, read_buffer);
    http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
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
    std::ignore = http_test::read_http_request(socket, read_buffer);

    http_test::write_http_response(socket,
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
  std::vector<std::string> raw_requests;
  bool reused_connection{true};

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto first_socket = server.accept();
    accepted_connections.fetch_add(1, std::memory_order_relaxed);

    std::string first_read_buffer;
    raw_requests.push_back(http_test::read_http_request(first_socket, first_read_buffer));
    http_test::write_http_response(first_socket, "HTTP/1.1 200 OK\r\n\r\nalpha");

    reused_connection = handle_follow_up_request(server,
      first_socket,
      first_read_buffer,
      accepted_connections,
      raw_requests,
      "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nbeta");
  }};

  http::client client;

  auto first = send_with_timeout(client,
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/first"),
    2s);

  auto second = send_with_timeout(client,
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/second"),
    2s);

  ASSERT_TRUE(first.has_value()) << first.error().message();
  ASSERT_TRUE(second.has_value()) << second.error().message();
  EXPECT_EQ(first->text(), "alpha");
  EXPECT_EQ(second->text(), "beta");

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_FALSE(reused_connection);
  EXPECT_EQ(accepted_connections.load(std::memory_order_relaxed), 2);
}

TEST(HttpClient, ReadsHttp11NonChunkedTransferEncodingAsCloseDelimitedAndDoesNotReuseConnection) {
  std::atomic<int> accepted_connections{0};
  std::vector<std::string> raw_requests;
  bool reused_connection{true};

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto first_socket = server.accept();
    accepted_connections.fetch_add(1, std::memory_order_relaxed);

    std::string first_read_buffer;
    raw_requests.push_back(http_test::read_http_request(first_socket, first_read_buffer));
    http_test::write_http_response(first_socket,
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: gzip\r\n"
      "\r\n"
      "alpha");

    reused_connection = handle_follow_up_request(server,
      first_socket,
      first_read_buffer,
      accepted_connections,
      raw_requests,
      "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nbeta");
  }};

  http::client client;

  auto first = send_with_timeout(client,
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/first"),
    2s);

  auto second = send_with_timeout(client,
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/second"),
    2s);

  ASSERT_TRUE(first.has_value()) << first.error().message();
  ASSERT_TRUE(second.has_value()) << second.error().message();
  EXPECT_EQ(first->text(), "alpha");
  EXPECT_EQ(second->text(), "beta");

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_FALSE(reused_connection);
  EXPECT_EQ(accepted_connections.load(std::memory_order_relaxed), 2);
}

TEST(HttpClient, RejectsInvalidHttp11TransferEncodingFraming) {
  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto socket = server.accept();
    std::string read_buffer;
    std::ignore = http_test::read_http_request(socket, read_buffer);

    http_test::write_http_response(socket,
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
    std::ignore = http_test::read_http_request(socket, read_buffer);

    http_test::write_http_response(socket,
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
  bool reused_connection{true};

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto first_socket = server.accept();
    accepted_connections.fetch_add(1, std::memory_order_relaxed);

    std::string first_read_buffer;
    raw_requests.push_back(http_test::read_http_request(first_socket, first_read_buffer));
    http_test::write_http_response(first_socket, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\none");

    reused_connection = handle_follow_up_request(server,
      first_socket,
      first_read_buffer,
      accepted_connections,
      raw_requests,
      "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\ntwo");
  }};

  http::client client{http::client_options{.reuse_connections = false}};

  auto first = send_with_timeout(client,
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/first"),
    2s);

  auto second = send_with_timeout(client,
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/second"),
    2s);

  ASSERT_TRUE(first.has_value()) << first.error().message();
  ASSERT_TRUE(second.has_value()) << second.error().message();
  EXPECT_EQ(first->text(), "one");
  EXPECT_EQ(second->text(), "two");

  server.join();

  ASSERT_FALSE(server.exception());
  ASSERT_EQ(raw_requests.size(), 2U);
  EXPECT_FALSE(reused_connection);
  EXPECT_EQ(accepted_connections.load(std::memory_order_relaxed), 2);
  EXPECT_NE(raw_requests[0].find("Connection: close\r\n"), std::string::npos);
  EXPECT_NE(raw_requests[1].find("Connection: close\r\n"), std::string::npos);
}

TEST(HttpClient, DoesNotReuseConnectionWhenResponseSaysConnectionClose) {
  std::atomic<int> accepted_connections{0};
  std::vector<std::string> raw_requests;
  bool reused_connection{true};

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto first_socket = server.accept();
    accepted_connections.fetch_add(1, std::memory_order_relaxed);

    std::string first_read_buffer;
    raw_requests.push_back(http_test::read_http_request(first_socket, first_read_buffer));
    http_test::write_http_response(first_socket, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 3\r\n\r\none");

    reused_connection = handle_follow_up_request(server,
      first_socket,
      first_read_buffer,
      accepted_connections,
      raw_requests,
      "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\ntwo");
  }};

  http::client client;

  auto first = send_with_timeout(client,
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/first"),
    2s);

  auto second = send_with_timeout(client,
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/second"),
    2s);

  ASSERT_TRUE(first.has_value()) << first.error().message();
  ASSERT_TRUE(second.has_value()) << second.error().message();
  EXPECT_EQ(first->text(), "one");
  EXPECT_EQ(second->text(), "two");

  server.join();

  ASSERT_FALSE(server.exception());
  EXPECT_FALSE(reused_connection);
  EXPECT_EQ(accepted_connections.load(std::memory_order_relaxed), 2);
}

TEST(HttpClient, ReusesConnectionAfterNoContentResponse) {
  std::atomic<int> accepted_connections{0};
  std::vector<std::string> raw_requests;
  bool reused_connection{false};

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto first_socket = server.accept();
    accepted_connections.fetch_add(1, std::memory_order_relaxed);

    std::string first_read_buffer;
    raw_requests.push_back(http_test::read_http_request(first_socket, first_read_buffer));
    http_test::write_http_response(first_socket, "HTTP/1.1 204 No Content\r\n\r\n");

    reused_connection = handle_follow_up_request(server,
      first_socket,
      first_read_buffer,
      accepted_connections,
      raw_requests,
      "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
  }};

  http::client client;

  auto first = send_with_timeout(client,
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/empty"),
    2s);

  auto second = send_with_timeout(client,
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/after-empty"),
    2s);

  ASSERT_TRUE(first.has_value()) << first.error().message();
  ASSERT_TRUE(second.has_value()) << second.error().message();
  EXPECT_EQ(first->status_code(), http::status_code::no_content);
  EXPECT_TRUE(first->body.empty());
  EXPECT_EQ(second->text(), "ok");

  server.join();

  ASSERT_FALSE(server.exception());
  ASSERT_EQ(raw_requests.size(), 2U);
  EXPECT_TRUE(reused_connection);
  EXPECT_EQ(accepted_connections.load(std::memory_order_relaxed), 1);
  EXPECT_TRUE(raw_requests[0].starts_with("GET /empty HTTP/1.1\r\n"));
  EXPECT_TRUE(raw_requests[1].starts_with("GET /after-empty HTTP/1.1\r\n"));
}

TEST(HttpClient, ReusesConnectionAfterResetContentResponse) {
  std::atomic<int> accepted_connections{0};
  std::vector<std::string> raw_requests;
  bool reused_connection{false};

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto first_socket = server.accept();
    accepted_connections.fetch_add(1, std::memory_order_relaxed);

    std::string first_read_buffer;
    raw_requests.push_back(http_test::read_http_request(first_socket, first_read_buffer));
    http_test::write_http_response(first_socket, "HTTP/1.1 205 Reset Content\r\n\r\n");

    reused_connection = handle_follow_up_request(server,
      first_socket,
      first_read_buffer,
      accepted_connections,
      raw_requests,
      "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
  }};

  http::client client;

  auto first = send_with_timeout(client,
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/reset"),
    2s);

  auto second = send_with_timeout(client,
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/after-reset"),
    2s);

  ASSERT_TRUE(first.has_value()) << first.error().message();
  ASSERT_TRUE(second.has_value()) << second.error().message();
  EXPECT_EQ(std::to_underlying(first->status_code()), 205);
  EXPECT_TRUE(first->body.empty());
  EXPECT_EQ(second->text(), "ok");

  server.join();

  ASSERT_FALSE(server.exception());
  ASSERT_EQ(raw_requests.size(), 2U);
  EXPECT_TRUE(reused_connection);
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
  bool reused_connection{false};

  http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
    auto first_socket = server.accept();
    accepted_connections.fetch_add(1, std::memory_order_relaxed);

    std::string first_read_buffer;
    raw_requests.push_back(http_test::read_http_request(first_socket, first_read_buffer));
    http_test::write_http_response(first_socket,
      "HTTP/1.1 103 Early Hints\r\n"
      "Link: </style.css>; rel=preload; as=style\r\n"
      "\r\n"
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 5\r\n"
      "\r\n"
      "first");

    reused_connection = handle_follow_up_request(server,
      first_socket,
      first_read_buffer,
      accepted_connections,
      raw_requests,
      "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nsecond");
  }};

  http::client client;

  auto first = send_with_timeout(client,
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/first"),
    2s);

  auto second = send_with_timeout(client,
    http::client::endpoint{
      .host = "127.0.0.1",
      .port = server.port(),
      .secure = false,
    },
    make_request("/second"),
    2s);

  ASSERT_TRUE(first.has_value()) << first.error().message();
  ASSERT_TRUE(second.has_value()) << second.error().message();
  EXPECT_EQ(first->status_code(), http::status_code::ok);
  EXPECT_EQ(first->text(), "first");
  EXPECT_EQ(second->status_code(), http::status_code::ok);
  EXPECT_EQ(second->text(), "second");

  server.join();

  ASSERT_FALSE(server.exception());
  ASSERT_EQ(raw_requests.size(), 2U);
  EXPECT_TRUE(reused_connection);
  EXPECT_EQ(accepted_connections.load(std::memory_order_relaxed), 1);
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

    raw_request_head = http_test::read_http_request_head(socket, read_buffer);
    body_arrived_before_continue = http_test::try_read_any_bytes_nonblocking(socket, read_buffer);

    http_test::write_http_response(socket, "HTTP/1.1 100 Continue\r\n\r\n");

    received_body = http_test::read_http_request_body(socket, read_buffer, raw_request_head);

    http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
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

    raw_request_head = http_test::read_http_request_head(socket, read_buffer);
    body_arrived_before_final = http_test::try_read_any_bytes_nonblocking(socket, read_buffer);

    http_test::write_http_response(socket,
      "HTTP/1.1 413 Payload Too Large\r\n"
      "Connection: close\r\n"
      "Content-Length: 0\r\n"
      "\r\n");

    body_arrived_after_final = http_test::receives_bytes_within(socket, read_buffer, 100ms);
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
      raw_requests.push_back(http_test::read_http_request(socket, read_buffer));
      http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nfirst");
    }

    {
      auto socket = server.accept();
      accepted_connections.fetch_add(1, std::memory_order_relaxed);

      std::string read_buffer;
      raw_requests.push_back(http_test::read_http_request(socket, read_buffer));
      http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nsecond");
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

    raw_request_head = http_test::read_http_request_head(socket, read_buffer);

    auto content_length = http_test::parse_content_length(raw_request_head);
    auto deadline = std::chrono::steady_clock::now() + 1s;

    for (;;) {
      if (auto request_body = http_test::try_read_exact_bytes_nonblocking(socket, read_buffer, content_length)) {
        received_body = std::move(*request_body);
        break;
      }

      if (std::chrono::steady_clock::now() >= deadline) {
        throw std::runtime_error{"http/1.0 request body was not received"};
      }

      std::this_thread::sleep_for(1ms);
    }

    http_test::write_http_response(socket,
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
    raw_request_head = http_test::read_http_request_head(socket, read_buffer);

    http_test::write_http_response(socket,
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
    raw_request_head = http_test::read_http_request_head(socket, read_buffer);

    http_test::write_http_response(socket,
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
