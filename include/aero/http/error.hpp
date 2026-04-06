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

  enum class uri_error : std::uint8_t {
    missing_scheme_delimiter = 1,
    invalid_scheme,
    empty_authority,
    empty_host,
    userinfo_not_allowed,
    fragment_not_allowed,
    invalid_authority,
    invalid_host,
    invalid_ipv6_literal,
    empty_port,
    invalid_port,
    port_out_of_range,
    invalid_path,
    invalid_character,
  };

  enum class connection_error : std::uint8_t {
    pool_unavailable = 1,
    endpoint_host_empty,
    endpoint_port_invalid,
    tls_context_missing,
  };

  enum class client_error : std::uint8_t {
    client_unavailable = 1,
    request_encoding_unsupported,
    response_encoding_unsupported,
    content_length_mismatch,
    chunked_encoding_invalid,
    response_body_too_large,
    connect_tunnel_unsupported,
    unexpected_failure,
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
        case header_error::lf_field_endings_not_supported:
          return "header field contains LF-only endings which are not supported";
        case header_error::content_length_missing:
          return "content-length header is missing";
        case header_error::content_type_missing:
          return "content-type header is missing";
        default:
          return "unknown http header error";
        }
      }
    };

    class uri_error_category final : public std::error_category {
     public:
      [[nodiscard]] const char* name() const noexcept override {
        return "aero.http.uri_error";
      }

      [[nodiscard]] std::string message(int value) const override {
        switch (static_cast<uri_error>(value)) {
        case uri_error::missing_scheme_delimiter:
          return "uri scheme delimiter is missing";
        case uri_error::invalid_scheme:
          return "uri scheme is invalid";
        case uri_error::empty_authority:
          return "uri authority is empty";
        case uri_error::empty_host:
          return "uri host is empty";
        case uri_error::userinfo_not_allowed:
          return "uri userinfo is not allowed";
        case uri_error::fragment_not_allowed:
          return "uri fragment is not allowed";
        case uri_error::invalid_authority:
          return "uri authority is invalid";
        case uri_error::invalid_host:
          return "uri host is invalid";
        case uri_error::invalid_ipv6_literal:
          return "uri ipv6 literal is invalid";
        case uri_error::empty_port:
          return "uri port is empty";
        case uri_error::invalid_port:
          return "uri port is invalid";
        case uri_error::port_out_of_range:
          return "uri port is out of range";
        case uri_error::invalid_path:
          return "uri path is invalid";
        case uri_error::invalid_character:
          return "uri contains invalid character";
        default:
          return "unknown http uri error";
        }
      }
    };

    class connection_error_category final : public std::error_category {
     public:
      [[nodiscard]] const char* name() const noexcept override {
        return "aero.http.connection_error";
      }

      [[nodiscard]] std::string message(int value) const override {
        switch (static_cast<connection_error>(value)) {
        case connection_error::pool_unavailable:
          return "http connection pool is unavailable";
        case connection_error::endpoint_host_empty:
          return "http endpoint host is empty";
        case connection_error::endpoint_port_invalid:
          return "http endpoint port is invalid";
        case connection_error::tls_context_missing:
          return "tls context is missing";
        default:
          return "unknown http connection error";
        }
      }
    };

    class client_error_category final : public std::error_category {
     public:
      [[nodiscard]] const char* name() const noexcept override {
        return "aero.http.client_error";
      }

      [[nodiscard]] std::string message(int value) const override {
        switch (static_cast<client_error>(value)) {
        case client_error::client_unavailable:
          return "http client is unavailable";
        case client_error::request_encoding_unsupported:
          return "request transfer-encoding is not supported";
        case client_error::response_encoding_unsupported:
          return "response transfer-encoding is not supported";
        case client_error::content_length_mismatch:
          return "response body exceeds declared content-length";
        case client_error::chunked_encoding_invalid:
          return "response chunked encoding is invalid";
        case client_error::response_body_too_large:
          return "response body is too large";
        case client_error::connect_tunnel_unsupported:
          return "CONNECT tunnel mode is unsupported";
        case client_error::unexpected_failure:
          return "http client failed unexpectedly";
        default:
          return "unknown http client error";
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

  [[nodiscard]] const inline std::error_category& uri_error_category() noexcept {
    static const detail::uri_error_category instance{};
    return instance;
  }

  [[nodiscard]] const inline std::error_category& connection_error_category() noexcept {
    static const detail::connection_error_category instance{};
    return instance;
  }

  [[nodiscard]] const inline std::error_category& client_error_category() noexcept {
    static const detail::client_error_category instance{};
    return instance;
  }

  [[nodiscard]] inline std::error_code make_error_code(protocol_error error) noexcept {
    return std::error_code{static_cast<int>(error), protocol_error_category()};
  }

  [[nodiscard]] inline std::error_code make_error_code(header_error error) noexcept {
    return std::error_code{static_cast<int>(error), header_error_category()};
  }

  [[nodiscard]] inline std::error_code make_error_code(uri_error error) noexcept {
    return std::error_code{static_cast<int>(error), uri_error_category()};
  }

  [[nodiscard]] inline std::error_code make_error_code(connection_error error) noexcept {
    return std::error_code{static_cast<int>(error), connection_error_category()};
  }

  [[nodiscard]] inline std::error_code make_error_code(client_error error) noexcept {
    return std::error_code{static_cast<int>(error), client_error_category()};
  }

} // namespace aero::http::error

template <>
struct std::is_error_code_enum<aero::http::error::protocol_error> : std::true_type {};

template <>
struct std::is_error_code_enum<aero::http::error::header_error> : std::true_type {};

template <>
struct std::is_error_code_enum<aero::http::error::uri_error> : std::true_type {};

template <>
struct std::is_error_code_enum<aero::http::error::connection_error> : std::true_type {};

template <>
struct std::is_error_code_enum<aero::http::error::client_error> : std::true_type {};
