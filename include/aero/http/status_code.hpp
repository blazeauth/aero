#pragma once

#include <array>
#include <expected>
#include <format>
#include <string_view>
#include <type_traits>

#include "aero/detail/string.hpp"
#include "aero/http/error.hpp"

namespace aero::http {

  enum class status_code : int { // NOLINT(performance-enum-size)
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

  using mapped_status_code = std::pair<status_code, std::string_view>;

  constexpr auto status_code_map = std::to_array<mapped_status_code>({
    {status_code::continue_, "Continue"},
    {status_code::switching_protocols, "Switching Protocols"},
    {status_code::processing, "Processing"},
    {status_code::early_hints, "Early Hints"},
    {status_code::ok, "OK"},
    {status_code::created, "Created"},
    {status_code::accepted, "Accepted"},
    {status_code::non_authoritative_information, "Non Authoritative Information"},
    {status_code::no_content, "No Content"},
    {status_code::reset_content, "Reset Content"},
    {status_code::partial_content, "Partial Content"},
    {status_code::multi_status, "Multi-Status"},
    {status_code::already_reported, "Already Reported"},
    {status_code::im_used, "IM Used"},
    {status_code::multiple_choices, "Multiple Choices"},
    {status_code::moved_permanently, "Moved Permanently"},
    {status_code::found, "Found"},
    {status_code::see_other, "See Other"},
    {status_code::not_modified, "Not Modified"},
    {status_code::use_proxy, "Use Proxy"},
    {status_code::temporary_redirect, "Temporary Redirect"},
    {status_code::permanent_redirect, "Permanent Redirect"},
    {status_code::bad_request, "Bad Request"},
    {status_code::unauthorized, "Unauthorized"},
    {status_code::payment_required, "Payment Required"},
    {status_code::forbidden, "Forbidden"},
    {status_code::not_found, "Not Found"},
    {status_code::method_not_allowed, "Method Not Allowed"},
    {status_code::not_acceptable, "Not Acceptable"},
    {status_code::proxy_authentication_required, "Proxy Authentication Required"},
    {status_code::request_timeout, "Request Timeout"},
    {status_code::conflict, "Conflict"},
    {status_code::gone, "Gone"},
    {status_code::length_required, "Length Required"},
    {status_code::precondition_failed, "Precondition Failed"},
    {status_code::payload_too_large, "Payload Too Large"},
    {status_code::uri_too_long, "URI Too Long"},
    {status_code::unsupported_media_type, "Unsupported Media Type"},
    {status_code::range_not_satisfiable, "Range Not Satisfiable"},
    {status_code::expectation_failed, "Expectation Failed"},
    {status_code::im_a_teapot, "I'm a teapot"},
    {status_code::misdirected_request, "Misdirected Request"},
    {status_code::unprocessable_content, "Unprocessable Content"},
    {status_code::locked, "Locked"},
    {status_code::failed_dependency, "Failed Dependency"},
    {status_code::upgrade_required, "Upgrade Required"},
    {status_code::precondition_required, "Precondition Required"},
    {status_code::too_many_requests, "Too Many Requests"},
    {status_code::request_header_fields_too_large, "Request Header Fields Too Large"},
    {status_code::unavailable_for_legal_reasons, "Unavailable For Legal Reasons"},
    {status_code::internal_server_error, "Internal Server Error"},
    {status_code::not_implemented, "Not Implemented"},
    {status_code::bad_gateway, "Bad Gateway"},
    {status_code::service_unavailable, "Service Unavailable"},
    {status_code::gateway_timeout, "Gateway Timeout"},
    {status_code::http_version_not_supported, "HTTP Version Not Supported"},
    {status_code::variant_also_negotiates, "Variant Also Negotiates"},
    {status_code::insufficient_storage, "Insufficient Storage"},
    {status_code::loop_detected, "Loop Detected"},
    {status_code::not_extended, "Not Extended"},
    {status_code::network_authentication_required, "Network Authentication Required"},
  });

  constexpr bool is_valid_status_code(status_code code) {
    const int value = std::to_underlying(code);
    const auto [min_valid_code, max_valid_code] = std::tuple{100, 599};
    return value >= min_valid_code && value <= max_valid_code;
  }

  inline std::expected<status_code, std::error_code> to_status_code(std::string_view str) {
    auto parse_result = aero::detail::to_decimal<status_code>(str);
    if (!parse_result) {
      return std::unexpected(parse_result.error());
    }
    auto code = *parse_result;
    if (!is_valid_status_code(code)) {
      return std::unexpected(http::error::protocol_error::status_code_invalid);
    }
    return code;
  }

  constexpr std::string_view to_string(status_code code) {
    if (!is_valid_status_code(code)) {
      return std::string_view{};
    }

    // NOLINTNEXTLINE(readability-qualified-auto)
    const auto it = std::ranges::find(status_code_map, code, &mapped_status_code::first);
    return (it != status_code_map.end()) ? it->second : std::string_view{};
  }

} // namespace aero::http

template <>
struct std::formatter<aero::http::status_code> : std::formatter<std::underlying_type_t<aero::http::status_code>> {
  auto format(const aero::http::status_code& status_code, std::format_context& ctx) const {
    return std::formatter<std::underlying_type_t<aero::http::status_code>>{}.format(std::to_underlying(status_code), ctx);
  }
};
