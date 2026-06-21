#pragma once

#include <expected>
#include <format>
#include <string_view>
#include <type_traits>

#include "aero/detail/string.hpp"
#include "aero/http/error.hpp"

namespace aero::http {

  enum class status : int { // NOLINT(performance-enum-size)
    continue_ = 100,
    switching_protocols = 101,
    processing = 102,
    early_hints = 103,

    ok = 200,
    created = 201,
    accepted = 202,
    non_authoritative_information = 203,
    no_content = 204,
    reset_content = 205,
    partial_content = 206,
    multi_status = 207,
    already_reported = 208,
    im_used = 226,

    multiple_choices = 300,
    moved_permanently = 301,
    found = 302,
    see_other = 303,
    not_modified = 304,
    use_proxy = 305,
    switch_proxy = 306,
    temporary_redirect = 307,
    permanent_redirect = 308,

    bad_request = 400,
    unauthorized = 401,
    payment_required = 402,
    forbidden = 403,
    not_found = 404,
    method_not_allowed = 405,
    not_acceptable = 406,
    proxy_authentication_required = 407,
    request_timeout = 408,
    conflict = 409,
    gone = 410,
    length_required = 411,
    precondition_failed = 412,
    payload_too_large = 413,
    uri_too_long = 414,
    unsupported_media_type = 415,
    range_not_satisfiable = 416,
    expectation_failed = 417,
    im_a_teapot = 418,
    misdirected_request = 421,
    unprocessable_content = 422,
    locked = 423,
    failed_dependency = 424,
    too_early = 425,
    upgrade_required = 426,
    precondition_required = 428,
    too_many_requests = 429,
    request_header_fields_too_large = 431,
    unavailable_for_legal_reasons = 451,

    internal_server_error = 500,
    not_implemented = 501,
    bad_gateway = 502,
    service_unavailable = 503,
    gateway_timeout = 504,
    http_version_not_supported = 505,
    variant_also_negotiates = 506,
    insufficient_storage = 507,
    loop_detected = 508,
    not_extended = 510,
    network_authentication_required = 511
  };

  constexpr bool is_valid_status_code(status code) {
    const int value = std::to_underlying(code);
    const auto [min_valid_code, max_valid_code] = std::tuple{100, 599};
    return value >= min_valid_code && value <= max_valid_code;
  }

  inline std::expected<status, std::error_code> parse_status(std::string_view str) {
    auto parse_result = aero::detail::to_decimal<status>(str);
    if (!parse_result) {
      return std::unexpected(parse_result.error());
    }
    auto code = *parse_result;
    if (!is_valid_status_code(code)) {
      return std::unexpected(http::protocol_error::status_code_invalid);
    }
    return code;
  }

  constexpr std::string_view to_string(status code) {
    switch (code) {
    case status::continue_:
      return "Continue";
    case status::switching_protocols:
      return "Switching Protocols";
    case status::processing:
      return "Processing";
    case status::early_hints:
      return "Early Hints";

    case status::ok:
      return "OK";
    case status::created:
      return "Created";
    case status::accepted:
      return "Accepted";
    case status::non_authoritative_information:
      return "Non Authoritative Information";
    case status::no_content:
      return "No Content";
    case status::reset_content:
      return "Reset Content";
    case status::partial_content:
      return "Partial Content";
    case status::multi_status:
      return "Multi-Status";
    case status::already_reported:
      return "Already Reported";
    case status::im_used:
      return "IM Used";

    case status::multiple_choices:
      return "Multiple Choices";
    case status::moved_permanently:
      return "Moved Permanently";
    case status::found:
      return "Found";
    case status::see_other:
      return "See Other";
    case status::not_modified:
      return "Not Modified";
    case status::use_proxy:
      return "Use Proxy";
    case status::switch_proxy:
      return "Switch Proxy";
    case status::temporary_redirect:
      return "Temporary Redirect";
    case status::permanent_redirect:
      return "Permanent Redirect";

    case status::bad_request:
      return "Bad Request";
    case status::unauthorized:
      return "Unauthorized";
    case status::payment_required:
      return "Payment Required";
    case status::forbidden:
      return "Forbidden";
    case status::not_found:
      return "Not Found";
    case status::method_not_allowed:
      return "Method Not Allowed";
    case status::not_acceptable:
      return "Not Acceptable";
    case status::proxy_authentication_required:
      return "Proxy Authentication Required";
    case status::request_timeout:
      return "Request Timeout";
    case status::conflict:
      return "Conflict";
    case status::gone:
      return "Gone";
    case status::length_required:
      return "Length Required";
    case status::precondition_failed:
      return "Precondition Failed";
    case status::payload_too_large:
      return "Payload Too Large";
    case status::uri_too_long:
      return "URI Too Long";
    case status::unsupported_media_type:
      return "Unsupported Media Type";
    case status::range_not_satisfiable:
      return "Range Not Satisfiable";
    case status::expectation_failed:
      return "Expectation Failed";
    case status::im_a_teapot:
      return "I'm a teapot";
    case status::misdirected_request:
      return "Misdirected Request";
    case status::unprocessable_content:
      return "Unprocessable Content";
    case status::locked:
      return "Locked";
    case status::failed_dependency:
      return "Failed Dependency";
    case status::too_early:
      return "Too Early";
    case status::upgrade_required:
      return "Upgrade Required";
    case status::precondition_required:
      return "Precondition Required";
    case status::too_many_requests:
      return "Too Many Requests";
    case status::request_header_fields_too_large:
      return "Request Header Fields Too Large";
    case status::unavailable_for_legal_reasons:
      return "Unavailable For Legal Reasons";

    case status::internal_server_error:
      return "Internal Server Error";
    case status::not_implemented:
      return "Not Implemented";
    case status::bad_gateway:
      return "Bad Gateway";
    case status::service_unavailable:
      return "Service Unavailable";
    case status::gateway_timeout:
      return "Gateway Timeout";
    case status::http_version_not_supported:
      return "HTTP Version Not Supported";
    case status::variant_also_negotiates:
      return "Variant Also Negotiates";
    case status::insufficient_storage:
      return "Insufficient Storage";
    case status::loop_detected:
      return "Loop Detected";
    case status::not_extended:
      return "Not Extended";
    case status::network_authentication_required:
      return "Network Authentication Required";
    default:
      return {};
    }
  }

} // namespace aero::http

template <>
struct std::formatter<aero::http::status> : std::formatter<std::underlying_type_t<aero::http::status>> {
  auto format(const aero::http::status& status_code, std::format_context& ctx) const {
    return std::formatter<std::underlying_type_t<aero::http::status>>{}.format(std::to_underlying(status_code), ctx);
  }
};
