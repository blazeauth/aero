#include "aero/http/server.hpp"
#include "aero/http/response.hpp"
#include "aero/http/status.hpp"

#include <asio/any_io_executor.hpp>
#include <asio/io_context.hpp>
#include <asio/static_thread_pool.hpp>
#include <asio/thread_pool.hpp>
#include <print>

namespace http = aero::http;

void aero_handler(http::context& ctx) {
  ctx.status(http::status::conflict);
  ctx.response().headers.add("Aero", "annihilatorq");
}

int main() {
  auto io_context = std::make_shared<asio::io_context>();
  auto threadpool = std::make_shared<asio::thread_pool>(4);

  http::server server(threadpool->get_executor());
  server.on_error([](std::error_code ec, std::source_location location, std::optional<std::string>) {
    std::println("Error \"{}\" ({}) occured at {}", ec.message(), ec.value(), location.function_name());
    std::exit(1); // NOLINT
  });

  server.get("/aero", aero_handler);
  server.bind("127.0.0.1", 8080);
  server.start();

  threadpool->wait();
}
