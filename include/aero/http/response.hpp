#pragma once

#include <cstddef>
#include <vector>

#include "aero/http/headers.hpp"
#include "aero/http/status.hpp"
#include "aero/http/status_line.hpp"

namespace aero::http {

  struct response {
    std::vector<std::byte> body;
    http::status_line status_line;
    http::headers headers;

    [[nodiscard]] http::status status_code() const {
      return status_line.status_code;
    }

    [[nodiscard]] std::string_view text() const noexcept {
      return {reinterpret_cast<const char*>(body.data()), body.size()};
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
      return body;
    }

    [[nodiscard]] bool empty() const noexcept {
      return body.empty() && status_line.empty() && headers.empty();
    }

    [[nodiscard]] std::expected<std::string_view, std::error_code> content_type() const noexcept {
      return http::content_type(headers);
    }

    [[nodiscard]] std::string serialize() const {
      auto status_line_str = status_line.serialize();
      if (status_line_str.empty()) {
        return {};
      }

      auto headers_str = headers.serialize();
      if (headers_str.empty()) {
        return {};
      }

      std::string buffer;
      buffer.reserve(status_line_str.size() + headers_str.size() + body.size());
      buffer.append(status_line_str).append(headers_str).append(text());

      return buffer;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
      return not empty();
    }
  };

} // namespace aero::http
