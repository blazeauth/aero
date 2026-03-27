#pragma once

#include <cstdint>
#include <string>
#include <system_error>
#include <type_traits>

namespace aero::http::error {

  enum class protocol_error : std::uint8_t {
    headers_section_incomplete = 1,
    obs_fold_without_previous_header,
    header_line_invalid,
    header_separator_missing,
    header_value_separator_missing,
    header_name_invalid,
    status_line_invalid,
    request_line_invalid,
    status_code_invalid,
    version_invalid,
    method_invalid,
    reason_phrase_invalid,
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
        case protocol_error::headers_section_incomplete:
          return "headers section incomplete";
        case protocol_error::obs_fold_without_previous_header:
          return "obs-fold without previous header";
        case protocol_error::header_line_invalid:
          return "header line is invalid";
        case protocol_error::header_separator_missing:
          return "header separator is missing";
        case protocol_error::header_value_separator_missing:
          return "header value separator is missing";
        case protocol_error::header_name_invalid:
          return "header name is invalid";
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
        case protocol_error::content_length_missing:
          return "content-length header is missing";
        case protocol_error::content_type_missing:
          return "content-type header is missing";
        default:
          return "unknown http protocol error";
        }
      }
    };
  } // namespace detail

  [[nodiscard]] const inline std::error_category& protocol_error_category() noexcept {
    static const detail::protocol_error_category instance{};
    return instance;
  }

  [[nodiscard]] inline std::error_code make_error_code(protocol_error error) noexcept {
    return std::error_code{static_cast<int>(error), protocol_error_category()};
  }

} // namespace aero::http::error

template <>
struct std::is_error_code_enum<aero::http::error::protocol_error> : std::true_type {};
