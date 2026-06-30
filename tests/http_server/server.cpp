#include "aero/http/server.hpp"

#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/read_until.hpp>
#include <asio/write.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <string>
#include <string_view>
#include <ut/ut.hpp>

using namespace ut;
using namespace std::chrono_literals;
namespace http = aero::http;

int main() {
  suite http_server = [] {
    "calls GET request handler on valid request"_test = [] {
      http::server server;

      std::promise<bool> promise;
      std::future future = promise.get_future();

      server.get("/aero", [&](http::context&) { promise.set_value(true); });
      server.set_workers(1);
      server.async_start("127.0.0.1", 0);

      std::string multiple_host_headers_buf = "GET /aero HTTP/1.1\r\nHost: example.com\r\n\r\n";

      asio::ip::tcp::socket socket(server.get_executor());
      socket.connect(server.endpoint());
      asio::write(socket, asio::buffer(multiple_host_headers_buf));

      expect(future.wait_for(5s) == std::future_status::ready);
    };

    "passes parsed request data to handler context"_test = [] {
      http::server server;

      std::promise<http::request> promise;
      std::future future = promise.get_future();

      // Copy the request out because context only exposes it during the handler call
      server.get("/aero?x=1", [&](http::context& ctx) { promise.set_value(ctx.request()); });
      server.set_workers(1);
      server.async_start("127.0.0.1", 0);

      std::string request_buf = "GET /aero?x=1 HTTP/1.1\r\nHost: example.test\r\nX-Trace-Id: request-42\r\n\r\n";

      asio::ip::tcp::socket socket(server.get_executor());
      socket.connect(server.endpoint());
      asio::write(socket, asio::buffer(request_buf));

      expect[future.wait_for(5s) == std::future_status::ready];

      auto request = future.get();

      expect(request.method == http::method::get);
      expect(request.protocol == http::version::http1_1);
      expect(request.url == "/aero?x=1");
      expect(request.headers.first_value("Host") == "example.test");
      expect(request.headers.first_value("X-Trace-Id") == "request-42");
    };

    "dispatches GET request to exact target handler"_test = [] {
      http::server server;

      std::atomic first_handler_calls{0};
      std::atomic second_handler_calls{0};

      // The promise records which route ran
      std::promise<std::string> promise;
      std::future future = promise.get_future();

      server.get("/first", [&](http::context&) {
        ++first_handler_calls;
        promise.set_value("first");
      });
      server.get("/second", [&](http::context&) {
        ++second_handler_calls;
        promise.set_value("second");
      });

      server.set_workers(1);
      server.async_start("127.0.0.1", 0);

      std::string request_buf = "GET /second HTTP/1.1\r\nHost: example.test\r\n\r\n";

      asio::ip::tcp::socket socket(server.get_executor());
      socket.connect(server.endpoint());
      asio::write(socket, asio::buffer(request_buf));

      expect[future.wait_for(5s) == std::future_status::ready];

      expect(future.get() == "second");
      expect(first_handler_calls.load() == 0);
      expect(second_handler_calls.load() == 1);
    };

    "serializes response body and headers set by handler"_test = [] {
      http::server server;
      constexpr std::string_view response_body = "hello";

      server.get("/hello", [response_body](http::context& ctx) {
        ctx.response().headers.add("Content-Type", "text/plain");
        for (unsigned char c : response_body) {
          ctx.response().body.push_back(std::byte{c});
        }
      });

      server.set_workers(1);
      server.async_start("127.0.0.1", 0);

      std::string request_buf = "GET /hello HTTP/1.1\r\nHost: example.test\r\n\r\n";
      std::string response_buf;

      asio::ip::tcp::socket socket(server.get_executor());
      socket.connect(server.endpoint());
      asio::write(socket, asio::buffer(request_buf));

      // read_until may return before the whole body arrives, so we read the expected payload bytes
      std::size_t headers_end = asio::read_until(socket, asio::dynamic_buffer(response_buf), "\r\n\r\n");
      if (response_buf.size() < headers_end + response_body.size()) {
        asio::read(socket,
          asio::dynamic_buffer(response_buf),
          asio::transfer_exactly(headers_end + response_body.size() - response_buf.size()));
      }

      expect(response_buf.starts_with("HTTP/1.1 200 "));
      expect(response_buf.find("Content-Type: text/plain\r\n") != std::string::npos);
      expect(response_buf.find("Content-Length: 5\r\n") != std::string::npos);
      expect(response_buf.substr(headers_end, response_body.size()) == response_body);
    };
  };
}
