#pragma once

#include "aero/http/headers.hpp"
#include "aero/http/status_code.hpp"
#include "aero/http/status_line.hpp"

namespace aero::http {

  struct response {
    std::vector<std::byte> body;
    http::status_line status_line;
    http::headers headers;

    [[nodiscard]] http::status_code status_code() const {
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
  };

} // namespace aero::http
