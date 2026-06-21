#pragma once

#include "aero/http/context.hpp"
#include "aero/http/headers.hpp"
#include "aero/http/method.hpp"
#include "aero/http/port.hpp"
#include "aero/http/request.hpp"
#include "aero/http/request_line.hpp"
#include "aero/http/response.hpp"
#include "aero/http/status.hpp"
#include "aero/http/status_line.hpp"
#include "aero/http/version.hpp"

#include <asio.hpp>
#include <asio/any_io_executor.hpp>
#include <asio/error.hpp>
#include <asio/ip/address.hpp>
#include <asio/thread_pool.hpp>

#include <algorithm>
#include <iterator>
#include <optional>
#include <source_location>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#ifdef AERO_USE_TLS
#include <asio/ssl/stream.hpp>
#endif

#include <memory>

namespace aero::http {

  namespace detail {

    using tcp_socket = asio::ip::tcp::socket;
#ifdef AERO_USE_TLS
    using tls_stream = asio::ssl::stream<tcp_socket>;
#endif

    inline asio::awaitable<std::error_code> co_close_socket(tcp_socket& socket) {
      std::error_code ec, ignored_ec;
      static_cast<void>(socket.shutdown(tcp_socket::shutdown_both, ignored_ec));
      static_cast<void>(socket.close(ec));
      co_return ec;
    }

#ifdef AERO_USE_TLS
    inline asio::awaitable<std::error_code> co_close_socket(tls_stream& stream) {
      std::error_code shutdown_ec, close_ec;
      co_await stream.async_shutdown(asio::redirect_error(asio::use_awaitable, shutdown_ec));
      static_cast<void>(stream.lowest_layer().close(close_ec));
      co_return close_ec ? close_ec : shutdown_ec;
    }
#endif

  } // namespace detail

  // TODO: Stop closing connections on every error, send error responses
  template <bool UseTLS = false>
  class server {
    using socket_type = std::conditional_t<UseTLS,
#ifdef AERO_USE_TLS
      asio::ssl::stream<asio::ip::tcp::socket>,
#else
      asio::ip::tcp::socket,
#endif
      asio::ip::tcp::socket>;

    template <typename T>
    using use_coro_token = asio::as_tuple_t<asio::use_awaitable_t<asio::any_io_executor>>::as_default_on_t<T>;

    using coro_acceptor = use_coro_token<asio::ip::tcp::acceptor>;
    using coro_socket = use_coro_token<socket_type>;

   public:
    using error_handler = std::function<void(std::error_code, std::source_location, std::optional<std::string>)>;
    using request_handler = std::function<void(http::context&)>;

    constexpr static std::size_t max_headers_size = 8ZU * 1024ZU;
    constexpr static std::uint16_t default_port = UseTLS ? http::default_secure_port : http::default_port;

    explicit server(asio::any_io_executor executor = asio::any_io_executor{}) // NOLINT(modernize-pass-by-value)
      : executor_(executor) {
      if (!executor_) {
        io_context_ = std::make_unique<asio::io_context>();
        executor_ = io_context_->get_executor();
        acceptor_ = std::make_unique<coro_acceptor>(executor_);
        handlers_strand_ = std::make_unique<asio::strand<asio::any_io_executor>>(asio::make_strand(executor_));
      }
    }

    // TODO: Add std::shared_ptr<asio::io_context> ctor

    server(const server&) = delete;
    server(server&&) = delete;
    server& operator=(const server&) = delete;
    server& operator=(server&&) = delete;

    ~server() {
      if (io_context_) {
        io_context_->stop();
      }

      if (!threads_.empty()) {
        // Join all of the running threads skipping current worker
        for (auto& thread : threads_) {
          bool running_on_worker = thread.get_id() == std::this_thread::get_id();
          if (running_on_worker) {
            continue;
          }

          thread.join();
        }
      }
    }

    server& on_error(error_handler handler) {
      error_handler_ = std::move(handler);
      return *this;
    }

    server& bind(std::string_view address, std::uint16_t port) {
      try {
        asio::ip::tcp::endpoint endpoint{asio::ip::make_address(address), port};
        acceptor_->bind(endpoint);
        address_ = address;
        port_ = port;
      } catch (...) {
        handle_error(make_error_code(asio::error::address_in_use), std::source_location::current());
      }
      return *this;
    }

    void start(std::optional<std::size_t> num_threads = std::nullopt) {
      std::size_t threads_count{};
      if (num_threads.has_value()) {
        threads_count = *num_threads;
      } else {
        threads_count = (std::max)(1U, std::thread::hardware_concurrency());
      }

      asio::co_spawn(executor_, start_acceptor(), asio::detached);

      if (io_context_ && threads_count > 0) {
        threads_.reserve(threads_count);
        for (std::size_t i{}; i < threads_count; ++i) {
          threads_.emplace_back([this] { io_context_->run(); });
        }
      }
    }

    server& get(std::string path, request_handler handler) {
      get_request_handlers_[path] = std::move(handler);
      return *this;
    }

   private:
    struct connection {
      explicit connection(socket_type&& sock, asio::strand<asio::any_io_executor> strand)
        : socket(std::move(sock)), strand(std::move(strand)) {}

      socket_type socket;
      asio::strand<asio::any_io_executor> strand;
      std::vector<std::byte> read_buffer;
    };

    std::shared_ptr<asio::io_context> io_context_;
    std::vector<std::thread> threads_;
    asio::any_io_executor executor_;
    std::unique_ptr<coro_acceptor> acceptor_;
    error_handler error_handler_;
    // std::vector<connection> connections_;

    // Gets mutated only before .start(), so no synchronization on read is needed
    std::unordered_map<std::string, request_handler> get_request_handlers_;
    std::unordered_map<std::string, request_handler> post_request_handlers_;
    std::unordered_map<std::string, request_handler> put_request_handlers_;
    std::unordered_map<std::string, request_handler> patch_request_handlers_;
    std::unordered_map<std::string, request_handler> delete_request_handlers_;
    std::unordered_map<std::string, request_handler> options_request_handlers_;

    // TODO: Currently it's unique_ptr because strand throws exception in default ctor
    std::unique_ptr<asio::strand<asio::any_io_executor>> handlers_strand_;

    // Only set once in .bind(), no synchronization needed
    std::string address_;
    std::uint16_t port_{0};

    void handle_error(std::error_code ec, std::source_location location, std::optional<std::string> context = std::nullopt) {
      if (error_handler_) {
        error_handler_(ec, location, std::move(context));
      }
    }

    asio::awaitable<void> start_acceptor() {
      for (;;) {
        auto [ec, socket] = co_await acceptor_->async_accept();
        if (ec) {
          handle_error(ec, std::source_location::current(), "tcp accept loop");
          continue;
        }

        auto strand = asio::make_strand(executor_);

        // TODO: Is asio::detached good enough in this context?
        asio::co_spawn(strand, start_session(std::move(socket), strand), asio::detached);
      }
    }

    asio::awaitable<void> start_session(socket_type socket, asio::strand<asio::any_io_executor> strand) {
      auto conn = std::make_shared<connection>(std::move(socket), strand);

      std::vector<std::byte> buffer;

      // TODO: Replace socket shutdown by sending response and close only after that

      for (;;) {
        auto [ec, headers_end] = co_await asio::async_read_until(conn->socket,
          asio::dynamic_buffer(buffer, max_headers_size),
          "\r\n\r\n",
          asio::as_tuple(asio::use_awaitable));
        if (ec) {
          co_await detail::co_close_socket(socket);
          co_return;
        }

        // asio::async_read_until guarantees that buffer contains "\r\n\r\n"
        auto first_line_end_it = std::ranges::find(buffer, std::byte{'\n'});

        std::span first_line_buf{buffer.data(), static_cast<std::size_t>(std::distance(buffer.begin(), first_line_end_it) + 1)};
        std::span headers_buf{buffer.data() + first_line_buf.size(), headers_end - first_line_buf.size()};
        std::span content_buf{buffer.data() + headers_end, buffer.size()};

        auto request_line = http::request_line::parse(first_line_buf);
        if (!request_line) {
          handle_error(request_line.error(), std::source_location::current(), "received invalid request line");
          co_await detail::co_close_socket(socket);
          co_return;
        }

        // TODO: Headers may be empty, check that.
        auto request_headers = http::headers::parse(headers_buf);
        if (!request_headers) {
          handle_error(request_headers.error(), std::source_location::current(), "received invalid header fields");
          co_await detail::co_close_socket(socket);
          co_return;
        }

        http::request request{
          .method = request_line->method,
          .protocol = request_line->version,
          .url = std::move(request_line->target),
          .headers = std::move(*request_headers),
        };

        // TODO: Check if there is more than 1 Host header
        auto host_header = request.headers.first_value("Host");
        if (!host_header) {
          handle_error(request_headers.error(), std::source_location::current(), "Host header missing");
          co_await detail::co_close_socket(socket);
          co_return;
        }

        co_await handle_request(conn, std::move(request));

        // TODO: Remove this, add ring_buffer or something like this to avoid
        // reallocating memory for every read. This is required for
        // async_read_until to not re-read "\r\n\r\n" from buffer
        buffer.clear();
      }
    }

    asio::awaitable<void> handle_request(std::weak_ptr<connection> weak_conn, http::request request) {
      request_handler handler;

      switch (request.method) {
      case http::method::get:
        if (auto it = get_request_handlers_.find(request.url); it != get_request_handlers_.end()) {
          handler = it->second;
        }
        break;
      case http::method::post:
        if (auto it = post_request_handlers_.find(request.url); it != post_request_handlers_.end()) {
          handler = it->second;
        }
        break;
      case http::method::put:
        if (auto it = put_request_handlers_.find(request.url); it != put_request_handlers_.end()) {
          handler = it->second;
        }
        break;
      case http::method::patch:
        if (auto it = patch_request_handlers_.find(request.url); it != patch_request_handlers_.end()) {
          handler = it->second;
        }
        break;
      case http::method::delete_:
        if (auto it = delete_request_handlers_.find(request.url); it != delete_request_handlers_.end()) {
          handler = it->second;
        }
        break;
      case http::method::options:
        if (auto it = options_request_handlers_.find(request.url); it != options_request_handlers_.end()) {
          handler = it->second;
        }
        break;
      default:
        handle_error(asio::error::operation_not_supported,
          std::source_location::current(),
          "received request method is not supported");
        if (auto conn = weak_conn.lock(); conn != nullptr) {
          co_await detail::co_close_socket(conn->socket);
        }
        co_return;
      }

      if (!handler) {
        handle_error(asio::error::service_not_found, std::source_location::current(), "received unknown request target");
        if (auto conn = weak_conn.lock(); conn != nullptr) {
          co_await detail::co_close_socket(conn->socket);
        }
        co_return;
      }

      http::response response;
      response.status_line = http::status_line{
        .protocol = std::string{http::to_string(http::version::http1_1)},
        .status_code = http::status::ok,
      };

      http::context context{&request, &response};
      handler(context);

      response.headers.replace("Content-Length", std::to_string(response.body.size()));
      response.headers.replace("Connection", "close");

      if (auto conn = weak_conn.lock(); conn != nullptr) {
        std::string serialized_response = response.serialize();

        // TODO: We should synchronize write somehow, probably without mutex.
        std::error_code write_ec;
        co_await asio::async_write(conn->socket,
          asio::const_buffer(serialized_response.data(), serialized_response.size()),
          asio::redirect_error(asio::use_awaitable, write_ec));

        if (write_ec) {
          co_await detail::co_close_socket(conn->socket);
        }
      }
    }
  };

} // namespace aero::http
