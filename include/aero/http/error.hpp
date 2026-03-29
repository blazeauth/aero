#pragma once

#include <cstdint>
#include <string>
#include <system_error>
#include <type_traits>

namespace aero::http::error {

  enum class protocol_error : std::uint8_t {
    status_line_invalid = 1,
    request_line_invalid,
    status_code_invalid,
    version_invalid,
    method_invalid,
    reason_phrase_invalid,
  };

  enum class header_error : std::uint8_t {
    section_incomplete = 1,
    field_invalid,
    crlf_separator_missing,
    value_separator_missing,
    name_invalid,
    obs_fold_not_supported,
    lf_field_endings_not_supported,
    content_length_missing,
    content_type_missing,
  };

  namespace detail {
    class protocol_error_category final : public std::error_category {
     public:
      [[nodiscard]] const char* name() const noexcept override {
        return "aero.http.protocol_error";
      }

      [[nodiscard]] std::string message(int value) const override {
        switch (static_cast<protocol_error>(value)) {
        case protocol_error::status_line_invalid:
          return "status line is invalid";
        case protocol_error::request_line_invalid:
          return "request line is invalid";
        case protocol_error::status_code_invalid:
          return "status code is invalid";
        case protocol_error::version_invalid:
          return "http version is invalid";
        case protocol_error::method_invalid:
          return "http method is invalid";
        case protocol_error::reason_phrase_invalid:
          return "reason phrase is invalid";
        default:
          return "unknown http protocol error";
        }
      }
    };

    class header_error_category final : public std::error_category {
     public:
      [[nodiscard]] const char* name() const noexcept override {
        return "aero.http.header_error";
      }

      [[nodiscard]] std::string message(int value) const override {
        switch (static_cast<header_error>(value)) {
        case header_error::section_incomplete:
          return "headers section incomplete";
        case header_error::field_invalid:
          return "header field is invalid";
        case header_error::crlf_separator_missing:
          return "header crlf separator is missing";
        case header_error::value_separator_missing:
          return "header value separator is missing";
        case header_error::name_invalid:
          return "header name is invalid";
        case header_error::obs_fold_not_supported:
          return "obsolete header line folding is not supported";
        case header_error::content_length_missing:
          return "content-length header is missing";
        case header_error::content_type_missing:
          return "content-type header is missing";
        default:
          return "unknown http header error";
        }
      }
    };
  } // namespace detail

  [[nodiscard]] const inline std::error_category& protocol_error_category() noexcept {
    static const detail::protocol_error_category instance{};
    return instance;
  }

  [[nodiscard]] const inline std::error_category& header_error_category() noexcept {
    static const detail::header_error_category instance{};
    return instance;
  }

  [[nodiscard]] inline std::error_code make_error_code(protocol_error error) noexcept {
    return std::error_code{static_cast<int>(error), protocol_error_category()};
  }

  [[nodiscard]] inline std::error_code make_error_code(header_error error) noexcept {
    return std::error_code{static_cast<int>(error), header_error_category()};
  }

} // namespace aero::http::error

template <>
struct std::is_error_code_enum<aero::http::error::protocol_error> : std::true_type {};

template <>
struct std::is_error_code_enum<aero::http::error::header_error> : std::true_type {};
