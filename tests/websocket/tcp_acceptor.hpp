#pragma once

#include <charconv>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <asio/io_context.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/read_until.hpp>
#include <asio/write.hpp>

#include "aero/http/detail/line_endings.hpp"

namespace test::http {

  using tcp = asio::ip::tcp;

  class tcp_acceptor final {
   public:
    template <typename Handler>
    explicit tcp_acceptor(Handler&& handler)
      : acceptor_{io_context_, tcp::endpoint{asio::ip::make_address("127.0.0.1"), 0}},
        thread_{[this, handler = std::forward<Handler>(handler)]() mutable noexcept {
          try {
            handler(*this);
          } catch (...) {
            exception_ = std::current_exception();
          }
        }} {}

    tcp_acceptor(const tcp_acceptor&) = delete;
    tcp_acceptor& operator=(const tcp_acceptor&) = delete;
    tcp_acceptor(tcp_acceptor&&) = delete;
    tcp_acceptor& operator=(tcp_acceptor&&) = delete;

    ~tcp_acceptor() {
      join();
    }

    [[nodiscard]] std::uint16_t port() const {
      return acceptor_.local_endpoint().port();
    }

    [[nodiscard]] tcp::socket accept() {
      return acceptor_.accept();
    }

    void join() {
      if (thread_.joinable()) {
        thread_.join();
      }
    }

    [[nodiscard]] std::exception_ptr exception() const noexcept {
      return exception_;
    }

   private:
    asio::io_context io_context_;
    tcp::acceptor acceptor_;
    std::exception_ptr exception_;
    std::jthread thread_;
  };

  [[nodiscard]] inline std::size_t parse_content_length(std::string_view headers) {
    constexpr std::string_view content_length_name{"Content-Length:"};
    auto header_position = headers.find(content_length_name);
    if (header_position == std::string_view::npos) {
      return 0;
    }

    auto value_begin = header_position + content_length_name.size();
    while (value_begin < headers.size() && (headers[value_begin] == ' ' || headers[value_begin] == '\t')) {
      ++value_begin;
    }

    auto value_end = headers.find(aero::http::detail::crlf, value_begin);
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

  [[nodiscard]] inline std::string read_http_request_head(tcp::socket& socket, std::string& buffer) {
    std::error_code read_error;
    auto bytes_read = asio::read_until(socket, asio::dynamic_buffer(buffer), aero::http::detail::double_crlf, read_error);
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

  [[nodiscard]] inline std::string read_http_request_body(tcp::socket& socket, std::string& buffer,
    std::string_view request_head) {
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

  [[nodiscard]] inline std::string read_http_request(tcp::socket& socket, std::string& buffer) {
    auto request_head = read_http_request_head(socket, buffer);
    auto request_body = read_http_request_body(socket, buffer, request_head);
    return request_head + request_body;
  }

  inline void write_http_response(tcp::socket& socket, std::string_view response_text) {
    std::error_code write_error;
    auto bytes_written = asio::write(socket, asio::buffer(response_text.data(), response_text.size()), write_error);
    if (write_error) {
      throw std::system_error{write_error};
    }
    if (bytes_written != response_text.size()) {
      throw std::runtime_error{"http response was not written completely"};
    }
  }

} // namespace test::http
