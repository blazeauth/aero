#include <cstddef>
#include <cstdint>
#include <string_view>

#include "aero/detail/ip_address_validator.hpp"

namespace aero::http::detail {

  inline bool is_ipv4_address_like(std::string_view host) noexcept {
    if (host.empty()) {
      return false;
    }

    bool has_ipv4_separator = false;

    for (char ch : host) {
      if (ch == '.') {
        has_ipv4_separator = true;
        continue;
      }

      // IPv4 address can consist only of digits and '.'
      bool is_digit = ch >= '0' && ch <= '9';
      if (!is_digit) {
        return false;
      }
    }

    return has_ipv4_separator;
  }

  inline bool validate_hostname(std::string_view host) noexcept {
    // Current implementation accepts strict ASCII DNS-like hostnames, allows
    // numeric-only DNS labels, while rejecting raw unicode and percent-encoding.

    // Before implementing percent-encoding parser, we should weigh the
    // pros and cons and accept the fact that it may create an additional
    // attack surface for HTTP server routing.

    // RFC 1035 limits a DNS name to 255 octets in DNS message form.
    // For dotted text form used in Host, the maximum length without
    // a trailing root dot is 253 characters:
    // 63 "." 63 "." 63 "." 61
    constexpr std::size_t max_host_name_length = 253;

    // RFC 1035 2.3.1:
    // Labels must be 63 characters or less.
    constexpr std::size_t max_dns_label_length = 63;

    if (host.empty()) {
      return false;
    }

    // Accept and normalize absolute hostnames that ends with '.'
    if (host.ends_with('.')) {
      host.remove_suffix(1);
      if (host.empty()) {
        return false;
      }
    }

    if (host.empty() || host.size() > max_host_name_length) {
      return false;
    }

    std::size_t dns_label_length = 0;
    char prev_char = '\0';

    for (char ch : host) {
      if (ch == '.') {
        // DNS label must not be empty
        if (dns_label_length == 0) {
          return false;
        }

        // RFC 1035 2.3.1:
        // They must ... end with a letter or digit...
        if (prev_char == '-') {
          return false;
        }

        dns_label_length = 0;
        prev_char = ch;
        continue;
      }

      // RFC 1035 2.3.1:
      // The labels must follow the rules for ARPANET host names. They must
      // start with a letter, end with a letter or digit, and have as
      // interior characters only letters, digits, and hyphen.
      bool is_dns_label_char = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-';
      if (!is_dns_label_char) {
        return false;
      }

      // RFC 1123 2.1:
      // One aspect of host name syntax is hereby changed: the restriction on the
      // first character is relaxed to allow either a letter or a digit.
      if (dns_label_length == 0 && ch == '-') {
        return false;
      }

      ++dns_label_length;

      if (dns_label_length > max_dns_label_length) {
        return false;
      }

      prev_char = ch;
    }

    if (dns_label_length == 0) {
      return false;
    }

    // RFC 1035 2.3.1:
    // They must ... end with a letter or digit...
    if (prev_char == '-') {
      return false;
    }

    return true;
  }

  inline bool validate_host_port(std::string_view port_str) noexcept {
    // RFC 3986 3.2.3:
    // port = *DIGIT
    //
    // RFC 9110 4.2.1 and 4.2.2:
    // If the port subcomponent is empty or not given, the default port is used.

    if (port_str.empty()) {
      return true;
    }

    constexpr std::uint32_t min_port = 1;
    constexpr std::uint32_t max_port = 65535;
    std::uint32_t port_value = 0;

    for (char ch : port_str) {
      if (ch < '0' || ch > '9') [[unlikely]] {
        return false;
      }

      port_value = (port_value * 10) + static_cast<std::uint32_t>(ch - '0');

      if (port_value > max_port) [[unlikely]] {
        return false;
      }
    }

    return port_value >= min_port;
  }

  inline bool validate_host_header(std::string_view host_header) noexcept {
    // RFC 9110 7.2:
    // Host = uri-host [ ":" port ]
    std::string_view uri_host = host_header;

    // RFC 9110 4.2.1 and 4.2.2:
    // A sender MUST NOT generate an "http" or "https" URI with an empty host
    // identifier. A recipient that processes such URI reference must reject it.
    if (host_header.empty()) {
      return false;
    }

    // RFC 3986 3.2.2:
    // host = IP-literal / IPv4address / reg-name
    // IP-literal = "[" ( IPv6address / IPvFuture  ) "]"

    // URI Host inside brackets must be interpreted as an IPv6 literal
    if (uri_host.starts_with('[')) {
      std::size_t closing_bracket_pos = uri_host.find(']');
      if (closing_bracket_pos == std::string_view::npos) {
        return false;
      }

      // Bracketed IPv6 literal must not be empty
      if (closing_bracket_pos == 1) {
        return false;
      }

      std::string_view bracketless_ipv6 = uri_host.substr(1, closing_bracket_pos - 1);
      if (!aero::detail::validate_ipv6_address(bracketless_ipv6)) {
        return false;
      }

      // No port is present and IPv6 is validated - host is valid
      if (closing_bracket_pos + 1 == uri_host.size()) {
        return true;
      }

      std::string_view tail_after_ipv6 = uri_host.substr(closing_bracket_pos + 1);

      // After the IPv6 brackets, only a port is allowed
      if (!tail_after_ipv6.starts_with(':')) {
        return false;
      }

      std::string_view port_str = tail_after_ipv6.substr(1);

      return validate_host_port(port_str);
    }

    // RFC 9110 4.2.3:
    // If the port is equal to the default port for a scheme, the normal
    // form is to omit the port subcomponent.
    std::size_t port_delimiter_pos = host_header.find(':');
    if (port_delimiter_pos != std::string_view::npos) {
      // Only one port delimiter is allowed
      if (host_header.find(':', port_delimiter_pos + 1) != std::string_view::npos) {
        return false;
      }

      uri_host = host_header.substr(0, port_delimiter_pos);
      std::string_view port_str = host_header.substr(port_delimiter_pos + 1);

      if (uri_host.empty()) {
        return false;
      }

      if (!validate_host_port(port_str)) {
        return false;
      }
    }

    // RFC 3986 3.2.2:
    // The syntax rule for host is ambiguous because it does not completely
    // distinguish between an IPv4address and a reg-name.
    // In order to disambiguate the syntax, we apply the "first-match-wins"
    // algorithm: If host matches the rule for IPv4address, then it should
    // be considered an IPv4 address literal and not a reg-name.
    if (aero::detail::validate_ipv4_address(uri_host)) {
      return true;
    }

    // A host that looks like an IPv4 address but failed IPv4 validation is invalid
    if (is_ipv4_address_like(uri_host)) {
      return false;
    }

    return validate_hostname(uri_host);
  }

} // namespace aero::http::detail
