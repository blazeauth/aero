#include <atomic>
#include <chrono>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "ut.hpp"
#include <asio/cancel_after.hpp>
#include <asio/error.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_future.hpp>

#include "aero/deadline.hpp"
#include "aero/http/client.hpp"
#include "aero/http/error.hpp"
#include "http/tcp_acceptor.hpp"

using namespace std::chrono_literals;
namespace http = aero::http;
namespace http_test = test::http;
using tcp = asio::ip::tcp;

constexpr inline auto local_request_timeout = 5s;
constexpr inline auto coordination_timeout = 5s;

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

http::client::endpoint make_local_endpoint(std::uint16_t port) {
  return http::client::endpoint{
    .host = "127.0.0.1",
    .port = port,
    .secure = false,
  };
}

void close_socket(tcp::socket& socket) {
  std::error_code ignored_error;
  std::ignore = socket.shutdown(tcp::socket::shutdown_both, ignored_error);
  std::ignore = socket.close(ignored_error);
}

void abort_socket(tcp::socket& socket) {
  std::error_code ignored_error;
  std::ignore = socket.set_option(asio::socket_base::linger(true, 0), ignored_error);
  std::ignore = socket.close(ignored_error);
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
    return std::unexpected{http::client_error::unexpected_failure};
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
    return std::unexpected{http::client_error::unexpected_failure};
  }
}

template <typename ReusedConnectionHandler, typename NewConnectionHandler>
void wait_for_follow_up_request(http_test::tcp_acceptor& server, tcp::socket& first_socket, std::string& first_read_buffer,
  std::atomic<int>& accepted_connections, ReusedConnectionHandler&& handle_reused_connection,
  NewConnectionHandler&& handle_new_connection, std::chrono::steady_clock::duration timeout = coordination_timeout) {
  first_socket.non_blocking(true);

  std::optional<tcp::socket> second_socket;
  std::string second_read_buffer;
  aero::deadline deadline{timeout};

  for (;;) {
    if (auto request = http_test::try_read_http_request_nonblocking(first_socket, first_read_buffer)) {
      first_socket.non_blocking(false);
      std::forward<ReusedConnectionHandler>(handle_reused_connection)(first_socket, std::move(*request));
      return;
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
        first_socket.non_blocking(false);
        second_socket->non_blocking(false);
        std::forward<NewConnectionHandler>(handle_new_connection)(*second_socket, std::move(*request));
        return;
      }
    }

    if (deadline.expired()) {
      first_socket.non_blocking(false);
      if (second_socket.has_value()) {
        second_socket->non_blocking(false);
      }
      throw std::runtime_error{"timed out waiting for reused request or fallback connection"};
    }

    std::this_thread::sleep_for(1ms);
  }
}

ut::suite http_client = [] {
  "default client can send request to endpoint"_test = [] {
    std::string raw_request;

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto socket = server.accept();
      std::string read_buffer;
      raw_request = http_test::read_http_request(socket, read_buffer);
      http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello");
    }};

    http::client client;

    auto response =
      send_with_timeout(client, make_local_endpoint(server.port()), make_request("/hello"), local_request_timeout);

    expect(fatal(response.has_value()));
    expect(response->status_code() == http::status_code::ok);
    expect(response->text() == "hello");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(raw_request.starts_with("GET /hello HTTP/1.1\r\n"));
    expect(raw_request.find("Host: 127.0.0.1:" + std::to_string(server.port()) + "\r\n") != std::string::npos);
    expect(raw_request.find("Content-Length: 0\r\n") == std::string::npos);
  };

  "url overload parses target and reuses connection"_test = [] {
    std::atomic<int> accepted_connections{0};
    std::vector<std::string> raw_requests;

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto socket = server.accept();
      accepted_connections.fetch_add(1, std::memory_order_relaxed);

      std::string read_buffer;
      raw_requests.push_back(http_test::read_http_request(socket, read_buffer));
      http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\none");

      wait_for_follow_up_request(
        server,
        socket,
        read_buffer,
        accepted_connections,
        [&](tcp::socket& reused_socket, std::string request) {
          raw_requests.push_back(std::move(request));
          http_test::write_http_response(reused_socket, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\ntwo");
        },
        [&](tcp::socket& new_socket, std::string request) {
          raw_requests.push_back(std::move(request));
          http_test::write_http_response(new_socket, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\ntwo");
        });
    }};

    http::client client;

    auto first = send_with_timeout(client,
      "http://127.0.0.1:" + std::to_string(server.port()) + "/first?x=1",
      make_request({}),
      local_request_timeout);

    auto second = send_with_timeout(client, make_local_endpoint(server.port()), make_request("/second"), local_request_timeout);

    expect(fatal(first.has_value()));
    expect(fatal(second.has_value()));
    expect(first->text() == "one");
    expect(second->text() == "two");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(fatal(raw_requests.size() == 2U));
    expect(accepted_connections.load(std::memory_order_relaxed) == 1);
    expect(raw_requests[0].starts_with("GET /first?x=1 HTTP/1.1\r\n"));
    expect(raw_requests[1].starts_with("GET /second HTTP/1.1\r\n"));
  };

  "endpoint overload normalizes empty target to slash"_test = [] {
    std::string raw_request;

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto socket = server.accept();
      std::string read_buffer;
      raw_request = http_test::read_http_request(socket, read_buffer);
      http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    }};

    http::client client;
    auto response = send_with_timeout(client, make_local_endpoint(server.port()), make_request(""), local_request_timeout);

    expect(fatal(response.has_value()));
    expect(response->text() == "ok");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(raw_request.starts_with("GET / HTTP/1.1\r\n"));
  };

  "endpoint overload normalizes query only target"_test = [] {
    std::string raw_request;

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto socket = server.accept();
      std::string read_buffer;
      raw_request = http_test::read_http_request(socket, read_buffer);
      http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    }};

    http::client client;
    auto response =
      send_with_timeout(client, make_local_endpoint(server.port()), make_request("?page=1"), local_request_timeout);

    expect(fatal(response.has_value()));
    expect(response->text() == "ok");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(raw_request.starts_with("GET /?page=1 HTTP/1.1\r\n"));
  };

  "sends request body and correct content-length"_test = [] {
    std::string raw_request;

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto socket = server.accept();
      std::string read_buffer;
      raw_request = http_test::read_http_request(socket, read_buffer);
      http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\ncreated");
    }};

    http::client client;
    auto response = send_with_timeout(client,
      make_local_endpoint(server.port()),
      make_request(http::method::post, "/submit", "hello=world"),
      local_request_timeout);

    expect(fatal(response.has_value()));
    expect(response->text() == "created");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(raw_request.starts_with("POST /submit HTTP/1.1\r\n"));
    expect(raw_request.find("Content-Length: 11\r\n") != std::string::npos);
    expect(http_test::request_body(raw_request) == "hello=world");
  };

  "preserves user provided host header"_test = [] {
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
    auto response = send_with_timeout(client, make_local_endpoint(server.port()), std::move(request), local_request_timeout);

    expect(fatal(response.has_value()));
    expect(response->text() == "ok");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(raw_request.find("Host: example.test\r\n") != std::string::npos);
    expect(raw_request.find("Host: 127.0.0.1:" + std::to_string(server.port()) + "\r\n") == std::string::npos);
  };

  "reads chunked response with extensions"_test = [] {
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
    auto response =
      send_with_timeout(client, make_local_endpoint(server.port()), make_request("/chunked"), local_request_timeout);

    expect(fatal(response.has_value()));
    expect(response->status_code() == http::status_code::ok);
    expect(response->text() == "hello world");

    server.join();

    expect(fatal(server.exception() == nullptr));
  };

  "reads close-delimited response and does not reuse connection"_test = [] {
    std::atomic<int> accepted_connections{0};

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      {
        auto socket = server.accept();
        accepted_connections.fetch_add(1, std::memory_order_relaxed);

        std::string read_buffer;
        std::ignore = http_test::read_http_request(socket, read_buffer);
        http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\n\r\nalpha");
        close_socket(socket);
      }

      {
        auto socket = server.accept();
        accepted_connections.fetch_add(1, std::memory_order_relaxed);

        std::string read_buffer;
        std::ignore = http_test::read_http_request(socket, read_buffer);
        http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nbeta");
      }
    }};

    http::client client;

    auto first = send_with_timeout(client, make_local_endpoint(server.port()), make_request("/first"), local_request_timeout);
    auto second = send_with_timeout(client, make_local_endpoint(server.port()), make_request("/second"), local_request_timeout);

    expect(fatal(first.has_value()));
    expect(fatal(second.has_value()));
    expect(first->text() == "alpha");
    expect(second->text() == "beta");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(accepted_connections.load(std::memory_order_relaxed) == 2);
  };

  "reads http/1.1 non-chunked transfer encoding as close-delimited and does not reuse connection"_test = [] {
    std::atomic<int> accepted_connections{0};

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      {
        auto socket = server.accept();
        accepted_connections.fetch_add(1, std::memory_order_relaxed);

        std::string read_buffer;
        std::ignore = http_test::read_http_request(socket, read_buffer);
        http_test::write_http_response(socket,
          "HTTP/1.1 200 OK\r\n"
          "Transfer-Encoding: gzip\r\n"
          "\r\n"
          "alpha");
        close_socket(socket);
      }

      {
        auto socket = server.accept();
        accepted_connections.fetch_add(1, std::memory_order_relaxed);

        std::string read_buffer;
        std::ignore = http_test::read_http_request(socket, read_buffer);
        http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nbeta");
      }
    }};

    http::client client;

    auto first = send_with_timeout(client, make_local_endpoint(server.port()), make_request("/first"), local_request_timeout);
    auto second = send_with_timeout(client, make_local_endpoint(server.port()), make_request("/second"), local_request_timeout);

    expect(fatal(first.has_value()));
    expect(fatal(second.has_value()));
    expect(first->text() == "alpha");
    expect(second->text() == "beta");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(accepted_connections.load(std::memory_order_relaxed) == 2);
  };

  "rejects invalid http/1.1 transfer encoding framing"_test = [] {
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
    auto response = send_with_timeout(client,
      make_local_endpoint(server.port()),
      make_request("/invalid-transfer-encoding"),
      local_request_timeout);

    expect(fatal(not response.has_value()));
    expect(response.error() == http::client_error::response_encoding_unsupported);

    server.join();

    expect(fatal(server.exception() == nullptr));
  };

  "rejects http/1.0 response with transfer encoding"_test = [] {
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
    auto response = send_with_timeout(client,
      make_local_endpoint(server.port()),
      make_request("/http10-transfer-encoding"),
      local_request_timeout);

    expect(fatal(not response.has_value()));
    expect(response.error() == http::client_error::response_encoding_unsupported);

    server.join();

    expect(fatal(server.exception() == nullptr));
  };

  "does not reuse connection when reuse connections disabled"_test = [] {
    std::atomic<int> accepted_connections{0};
    std::vector<std::string> raw_requests;

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      {
        auto socket = server.accept();
        accepted_connections.fetch_add(1, std::memory_order_relaxed);

        std::string read_buffer;
        raw_requests.push_back(http_test::read_http_request(socket, read_buffer));
        http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\none");
      }

      {
        auto socket = server.accept();
        accepted_connections.fetch_add(1, std::memory_order_relaxed);

        std::string read_buffer;
        raw_requests.push_back(http_test::read_http_request(socket, read_buffer));
        http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\ntwo");
      }
    }};

    http::client client{http::client_options{.reuse_connections = false}};

    auto first = send_with_timeout(client, make_local_endpoint(server.port()), make_request("/first"), local_request_timeout);
    auto second = send_with_timeout(client, make_local_endpoint(server.port()), make_request("/second"), local_request_timeout);

    expect(fatal(first.has_value()));
    expect(fatal(second.has_value()));
    expect(first->text() == "one");
    expect(second->text() == "two");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(fatal(raw_requests.size() == 2U));
    expect(accepted_connections.load(std::memory_order_relaxed) == 2);
    expect(raw_requests[0].find("Connection: close\r\n") != std::string::npos);
    expect(raw_requests[1].find("Connection: close\r\n") != std::string::npos);
  };

  "does not reuse connection when response says connection close"_test = [] {
    std::atomic<int> accepted_connections{0};

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      {
        auto socket = server.accept();
        accepted_connections.fetch_add(1, std::memory_order_relaxed);

        std::string read_buffer;
        std::ignore = http_test::read_http_request(socket, read_buffer);
        http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 3\r\n\r\none");
      }

      {
        auto socket = server.accept();
        accepted_connections.fetch_add(1, std::memory_order_relaxed);

        std::string read_buffer;
        std::ignore = http_test::read_http_request(socket, read_buffer);
        http_test::write_http_response(socket, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\ntwo");
      }
    }};

    http::client client;

    auto first = send_with_timeout(client, make_local_endpoint(server.port()), make_request("/first"), local_request_timeout);
    auto second = send_with_timeout(client, make_local_endpoint(server.port()), make_request("/second"), local_request_timeout);

    expect(fatal(first.has_value()));
    expect(fatal(second.has_value()));
    expect(first->text() == "one");
    expect(second->text() == "two");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(accepted_connections.load(std::memory_order_relaxed) == 2);
  };

  "reuses connection after no content response"_test = [] {
    std::atomic<int> accepted_connections{0};
    std::vector<std::string> raw_requests;

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto socket = server.accept();
      accepted_connections.fetch_add(1, std::memory_order_relaxed);

      std::string read_buffer;
      raw_requests.push_back(http_test::read_http_request(socket, read_buffer));
      http_test::write_http_response(socket, "HTTP/1.1 204 No Content\r\n\r\n");

      wait_for_follow_up_request(
        server,
        socket,
        read_buffer,
        accepted_connections,
        [&](tcp::socket& reused_socket, std::string request) {
          raw_requests.push_back(std::move(request));
          http_test::write_http_response(reused_socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
        },
        [&](tcp::socket& new_socket, std::string request) {
          raw_requests.push_back(std::move(request));
          http_test::write_http_response(new_socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
        });
    }};

    http::client client;

    auto first = send_with_timeout(client, make_local_endpoint(server.port()), make_request("/empty"), local_request_timeout);
    auto second =
      send_with_timeout(client, make_local_endpoint(server.port()), make_request("/after-empty"), local_request_timeout);

    expect(fatal(first.has_value()));
    expect(fatal(second.has_value()));
    expect(first->status_code() == http::status_code::no_content);
    expect(first->body.empty());
    expect(second->text() == "ok");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(fatal(raw_requests.size() == 2U));
    expect(accepted_connections.load(std::memory_order_relaxed) == 1);
    expect(raw_requests[0].starts_with("GET /empty HTTP/1.1\r\n"));
    expect(raw_requests[1].starts_with("GET /after-empty HTTP/1.1\r\n"));
  };

  "reuses connection after reset content response"_test = [] {
    std::atomic<int> accepted_connections{0};
    std::vector<std::string> raw_requests;

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto socket = server.accept();
      accepted_connections.fetch_add(1, std::memory_order_relaxed);

      std::string read_buffer;
      raw_requests.push_back(http_test::read_http_request(socket, read_buffer));
      http_test::write_http_response(socket, "HTTP/1.1 205 Reset Content\r\n\r\n");

      wait_for_follow_up_request(
        server,
        socket,
        read_buffer,
        accepted_connections,
        [&](tcp::socket& reused_socket, std::string request) {
          raw_requests.push_back(std::move(request));
          http_test::write_http_response(reused_socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
        },
        [&](tcp::socket& new_socket, std::string request) {
          raw_requests.push_back(std::move(request));
          http_test::write_http_response(new_socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
        });
    }};

    http::client client;

    auto first = send_with_timeout(client, make_local_endpoint(server.port()), make_request("/reset"), local_request_timeout);
    auto second =
      send_with_timeout(client, make_local_endpoint(server.port()), make_request("/after-reset"), local_request_timeout);

    expect(fatal(first.has_value()));
    expect(fatal(second.has_value()));
    expect(std::to_underlying(first->status_code()) == 205);
    expect(first->body.empty());
    expect(second->text() == "ok");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(fatal(raw_requests.size() == 2U));
    expect(accepted_connections.load(std::memory_order_relaxed) == 1);
    expect(raw_requests[0].starts_with("GET /reset HTTP/1.1\r\n"));
    expect(raw_requests[1].starts_with("GET /after-reset HTTP/1.1\r\n"));
  };

  "returns uri error for invalid scheme"_test = [] {
    http::client client;

    auto response = client.send("ftp://127.0.0.1/resource", make_request({}));

    expect(fatal(not response.has_value()));
    expect(response.error() == http::uri_error::invalid_scheme);
  };

  "reads final response after interim response and reuses connection"_test = [] {
    std::atomic<int> accepted_connections{0};
    std::vector<std::string> raw_requests;

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto socket = server.accept();
      accepted_connections.fetch_add(1, std::memory_order_relaxed);

      std::string read_buffer;
      raw_requests.push_back(http_test::read_http_request(socket, read_buffer));
      http_test::write_http_response(socket,
        "HTTP/1.1 103 Early Hints\r\n"
        "Link: </style.css>; rel=preload; as=style\r\n"
        "\r\n"
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "first");

      wait_for_follow_up_request(
        server,
        socket,
        read_buffer,
        accepted_connections,
        [&](tcp::socket& reused_socket, std::string request) {
          raw_requests.push_back(std::move(request));
          http_test::write_http_response(reused_socket, "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nsecond");
        },
        [&](tcp::socket& new_socket, std::string request) {
          raw_requests.push_back(std::move(request));
          http_test::write_http_response(new_socket, "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nsecond");
        });
    }};

    http::client client;

    auto first = send_with_timeout(client, make_local_endpoint(server.port()), make_request("/first"), local_request_timeout);
    auto second = send_with_timeout(client, make_local_endpoint(server.port()), make_request("/second"), local_request_timeout);

    expect(fatal(first.has_value()));
    expect(fatal(second.has_value()));
    expect(first->status_code() == http::status_code::ok);
    expect(first->text() == "first");
    expect(second->status_code() == http::status_code::ok);
    expect(second->text() == "second");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(fatal(raw_requests.size() == 2U));
    expect(accepted_connections.load(std::memory_order_relaxed) == 1);
    expect(raw_requests[0].starts_with("GET /first HTTP/1.1\r\n"));
    expect(raw_requests[1].starts_with("GET /second HTTP/1.1\r\n"));
  };

  "waits for continue before sending request body when server responds immediately"_test = [] {
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
    auto response = send_with_timeout(client, make_local_endpoint(server.port()), std::move(request), local_request_timeout);

    expect(fatal(response.has_value()));
    expect(response->status_code() == http::status_code::ok);
    expect(response->text() == "ok");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(not body_arrived_before_continue);
    expect(raw_request_head.starts_with("POST /upload HTTP/1.1\r\n"));
    expect(received_body == "payload");
  };

  "does not send request body after final response to expect continue"_test = [] {
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
    auto response = send_with_timeout(client, make_local_endpoint(server.port()), std::move(request), local_request_timeout);

    expect(fatal(response.has_value()));
    expect(std::to_underlying(response->status_code()) == 413);
    expect(response->body.empty());

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(not body_arrived_before_final);
    expect(not body_arrived_after_final);
    expect(raw_request_head.starts_with("POST /reject HTTP/1.1\r\n"));
  };

  "retries idempotent request after stale persistent connection closes"_test = [] {
    std::atomic<int> accepted_connections{0};
    std::vector<std::string> raw_requests;

    std::promise<void> may_abort_first_connection_promise;
    auto may_abort_first_connection = may_abort_first_connection_promise.get_future();

    std::promise<void> first_connection_aborted_promise;
    auto first_connection_aborted = first_connection_aborted_promise.get_future();

    http_test::tcp_acceptor server{[&](http_test::tcp_acceptor& server) {
      auto first_socket = server.accept();
      accepted_connections.fetch_add(1, std::memory_order_relaxed);

      std::string first_read_buffer;
      raw_requests.push_back(http_test::read_http_request(first_socket, first_read_buffer));
      http_test::write_http_response(first_socket, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nfirst");

      if (may_abort_first_connection.wait_for(coordination_timeout) != std::future_status::ready) {
        throw std::runtime_error{"timed out waiting for permission to abort first persistent connection"};
      }

      abort_socket(first_socket);
      first_connection_aborted_promise.set_value();

      auto second_socket = server.accept();
      accepted_connections.fetch_add(1, std::memory_order_relaxed);

      std::string second_read_buffer;
      raw_requests.push_back(http_test::read_http_request(second_socket, second_read_buffer));
      http_test::write_http_response(second_socket, "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nsecond");
    }};

    http::client client;

    auto first = send_with_timeout(client, make_local_endpoint(server.port()), make_request("/first"), local_request_timeout);

    expect(fatal(first.has_value()));
    expect(first->text() == "first");

    may_abort_first_connection_promise.set_value();

    expect(fatal(first_connection_aborted.wait_for(coordination_timeout) == std::future_status::ready));

    auto second = send_with_timeout(client, make_local_endpoint(server.port()), make_request("/second"), local_request_timeout);

    expect(fatal(second.has_value()));
    expect(second->text() == "second");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(accepted_connections.load(std::memory_order_relaxed) == 2);
    expect(fatal(raw_requests.size() == 2U));
    expect(raw_requests[0].starts_with("GET /first HTTP/1.1\r\n"));
    expect(raw_requests[1].starts_with("GET /second HTTP/1.1\r\n"));
  };

  "http/1.0 request with expect continue sends body without waiting for interim response"_test = [] {
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
    auto response = send_with_timeout(client, make_local_endpoint(server.port()), std::move(request), local_request_timeout);

    expect(fatal(response.has_value()));
    expect(response->status_code() == http::status_code::ok);
    expect(response->text() == "ok");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(raw_request_head.starts_with("POST /upload HTTP/1.0\r\n"));
    expect(received_body == "payload");
  };

  "connect uses authority form and rejects successful tunnel response"_test = [] {
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
    auto response = send_with_timeout(client, make_local_endpoint(server.port()), std::move(request), local_request_timeout);

    expect(fatal(not response.has_value()));
    expect(response.error() == http::client_error::connect_tunnel_unsupported);

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(raw_request_head.starts_with("CONNECT example.com:443 HTTP/1.1\r\n"));
    expect(raw_request_head.find("Host: example.com:443\r\n") != std::string::npos);
  };

  "preserves absolute form request target for proxy style request"_test = [] {
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
    auto response = send_with_timeout(client, make_local_endpoint(server.port()), std::move(request), local_request_timeout);

    expect(fatal(response.has_value()));
    expect(response->text() == "ok");

    server.join();

    expect(fatal(server.exception() == nullptr));
    expect(raw_request_head.starts_with("GET http://example.com:8080/proxy/path?x=1 HTTP/1.1\r\n"));
    expect(raw_request_head.find("Host: example.com:8080\r\n") != std::string::npos);
  };
};

int main() {}
