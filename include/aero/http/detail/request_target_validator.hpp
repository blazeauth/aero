#pragma once

#include <string_view>

#include "aero/detail/ip_address_validator.hpp"
#include "aero/detail/rfc_grammar.hpp"
#include "aero/detail/string.hpp"
#include "aero/http/method.hpp"

namespace aero::http::detail {

  inline bool is_ipv4_address_like(std::string_view text) noexcept {
    if (text.empty()) {
      return false;
    }

    bool has_ipv4_separator = false;

    for (char ch : text) {
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

  inline bool is_valid_hostname(std::string_view hostname) noexcept {
    // The current implementation accepts strict ASCII DNS-like hostnames, allows
    // numeric-only DNS labels, and additionally permits the underscore '_',
    // while rejecting raw Unicode and percent-encoding.

    // The underscore is not a valid DNS label character under RFC 1035, but it
    // appears routinely in real-world Host values: internal service names
    // (e.g. "my_service.internal") and underscore-prefixed labels such as "_dmarc"
    // or the SRV form "_sip._tcp". Common servers (nginx, Go net/http, Node.js)
    // tolerate it, and since this server does not route on the Host value, rejecting
    // '_' is pure interop cost with no security benefit (it is inert as a delimiter).
    // It is therefore treated as an ordinary label character valid at any position,
    // including leading and trailing.
    //
    // We deliberately do NOT extend this to '~', the other character in RFC 3986's
    // reg-name "unreserved" set that is likewise not a DNS label character: unlike
    // '_', tilde does not occur in practice as a hostname character, so accepting it
    // would widen the input surface with no interop gain. Unreserved-set membership
    // alone is thus not the criterion here; real-world usage is.

    // Before implementing a percent-encoding parser, we should weigh the
    // pros and cons and accept the fact that it may create an additional
    // attack surface for HTTP server routing.

    // RFC 1035 limits a DNS name to 255 octets in DNS message form.
    // For dotted text form used in Host, the maximum length without
    // a trailing root dot is 253 characters:
    // 63 "." 63 "." 63 "." 61
    constexpr std::size_t max_host_name_length = 253;

    // RFC 1035, Section 2.3.1:
    // Labels must be 63 characters or less.
    constexpr std::size_t max_dns_label_length = 63;

    if (hostname.empty()) {
      return false;
    }

    // Accept and normalize absolute hostnames that end with '.'
    if (hostname.ends_with('.')) {
      hostname.remove_suffix(1);
      if (hostname.empty()) {
        return false;
      }
    }

    if (hostname.empty() || hostname.size() > max_host_name_length) {
      return false;
    }

    std::size_t dns_label_length = 0;
    char prev_char = '\0';

    for (char ch : hostname) {
      if (ch == '.') {
        // DNS label must not be empty
        if (dns_label_length == 0) {
          return false;
        }

        // RFC 1035, Section 2.3.1:
        // They must ... end with a letter or digit...
        if (prev_char == '-') {
          return false;
        }

        dns_label_length = 0;
        prev_char = ch;
        continue;
      }

      // RFC 1035, Section 2.3.1:
      // The labels must follow the rules for ARPANET host names. They must
      // start with a letter, end with a letter or digit, and have as
      // interior characters only letters, digits, and hyphen.
      // The underscore is permitted as a pragmatic superset (see note above);
      // unlike hyphen it carries no start/end position restriction.
      bool is_dns_label_char =
        (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
      if (!is_dns_label_char) {
        return false;
      }

      // RFC 1123, Section 2.1:
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

    // RFC 1035, Section 2.3.1:
    // They must ... end with a letter or digit...
    if (prev_char == '-') {
      return false;
    }

    return true;
  }

  inline bool is_valid_port(std::string_view port_str) noexcept {
    // RFC 3986, Section 3.2.3:
    // port = *DIGIT
    //
    // RFC 9110, Section 4.2.1 and Section 4.2.2:
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

  // Deliberately stricter than RFC 3986 authority:
  //  - userinfo is rejected per RFC 9110, Section 4.2.4 (deprecated in http/https URIs)
  //  - IPvFuture literals are rejected by internal policy
  inline bool is_valid_authority(std::string_view authority) noexcept {
    // RFC 9110, Section 7.2:
    // Host = uri-host [ ":" port ]
    std::string_view uri_host = authority;

    // RFC 9110, Section 4.2.1 and Section 4.2.2:
    // A sender MUST NOT generate an "http" or "https" URI with an empty host
    // identifier. A recipient that processes such a URI reference MUST reject
    // it as invalid.
    if (authority.empty()) {
      return false;
    }

    // RFC 3986, Section 3.2.2:
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
      if (!aero::detail::is_valid_ipv6_address(bracketless_ipv6)) {
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

      return is_valid_port(port_str);
    }

    // RFC 9110, Section 4.2.3:
    // If the port is equal to the default port for a scheme, the normal
    // form is to omit the port subcomponent.
    std::size_t port_delimiter_pos = authority.find(':');
    if (port_delimiter_pos != std::string_view::npos) {
      // Only one port delimiter is allowed
      if (authority.find(':', port_delimiter_pos + 1) != std::string_view::npos) {
        return false;
      }

      uri_host = authority.substr(0, port_delimiter_pos);
      std::string_view port_str = authority.substr(port_delimiter_pos + 1);

      if (uri_host.empty()) {
        return false;
      }

      if (!is_valid_port(port_str)) {
        return false;
      }
    }

    // RFC 3986, Section 3.2.2:
    // The syntax rule for host is ambiguous because it does not completely
    // distinguish between an IPv4address and a reg-name.
    // In order to disambiguate the syntax, we apply the "first-match-wins"
    // algorithm: If host matches the rule for IPv4address, then it should
    // be considered an IPv4 address literal and not a reg-name.
    if (aero::detail::is_valid_ipv4_address(uri_host)) {
      return true;
    }

    // A host that looks like an IPv4 address but failed IPv4 validation is invalid
    if (is_ipv4_address_like(uri_host)) {
      return false;
    }

    return is_valid_hostname(uri_host);
  }

  inline bool is_valid_absolute_path(std::string_view path) noexcept {
    // RFC 9112, Section 3.2.1:
    // absolute-path  = 1*( "/" segment )
    //
    // RFC 3986, Appendix A:
    // segment     = *pchar
    // pchar       = unreserved / pct-encoded / sub-delims / ":" / "@"
    // unreserved  = ALPHA / DIGIT / "-" / "." / "_" / "~"
    // pct-encoded = "%" HEXDIG HEXDIG
    // sub-delims  = "!" / "$" / "&" / "'" / "(" / ")"
    //             / "*" / "+" / "," / ";" / "="

    if (path.empty() || !path.starts_with('/')) {
      return false;
    }

    std::size_t pos = 0;
    while (pos != path.size()) {
      char ch = path[pos];

      // Skip segment delimiters
      if (ch == '/') {
        pos++;
        continue;
      }

      // Handle a percent-encoded token, which must be exactly 3 bytes
      if (ch == '%') {
        if (!is_pct_encoded(path.substr(pos, 3))) {
          return false;
        }

        pos += 3;
        continue;
      }

      // Percent encoding has already been validated above, so all that
      // remains is to check whether the character is one of the allowed
      // pchar, unreserved, or sub-delimiter characters.
      if (ch != ':' && ch != '@' && !is_unreserved(ch) && !is_sub_delim(ch)) {
        return false;
      }

      pos++;
    }

    return true;
  }

  inline bool is_valid_uri_query(std::string_view query) noexcept {
    // RFC 3986, Appendix A:
    // query       = *( pchar / "/" / "?" )
    // pchar       = unreserved / pct-encoded / sub-delims / ":" / "@"
    // unreserved  = ALPHA / DIGIT / "-" / "." / "_" / "~"
    // pct-encoded = "%" HEXDIG HEXDIG
    // sub-delims  = "!" / "$" / "&" / "'" / "(" / ")"
    //             / "*" / "+" / "," / ";" / "="

    if (query.empty()) {
      return true;
    }

    std::size_t pos = 0;
    while (pos < query.size()) {
      char ch = query[pos];

      // Handle a percent-encoded token, which must be exactly 3 bytes
      if (ch == '%') {
        if (!is_pct_encoded(query.substr(pos, 3))) {
          return false;
        }

        pos += 3;
        continue;
      }

      // Percent encoding has already been validated above, so all that
      // remains is to check whether the character is one of the allowed
      // query, pchar, unreserved, or sub-delimiter characters.
      if (ch != '/' && ch != '?' && ch != ':' && ch != '@' && !is_unreserved(ch) && !is_sub_delim(ch)) [[unlikely]] {
        return false;
      }

      pos++;
    }

    return true;
  }

  inline bool is_valid_origin_form(std::string_view origin) noexcept {
    if (!origin.starts_with('/')) {
      return false;
    }

    if (origin.size() == 1) {
      return true; // "/" is a valid origin-form
    }

    std::string_view absolute_path = origin;
    std::string_view query;

    std::size_t query_separator_pos = origin.find('?', 1);
    bool has_query_params = query_separator_pos != std::string_view::npos;

    if (has_query_params) {
      absolute_path = origin.substr(0, query_separator_pos);
      query = origin.substr(query_separator_pos + 1);

      if (!is_valid_uri_query(query)) {
        return false;
      }
    }

    return is_valid_absolute_path(absolute_path);
  }

  inline bool is_valid_request_target(http::method method, std::string_view target) noexcept {
    // RFC 9112, Section 3.2:
    // There are four distinct formats for the request-target, depending on both
    // the method being requested and whether the request is to a proxy.
    // request-target = origin-form
    //                / absolute-form
    //                / authority-form
    //                / asterisk-form

    // We do not validate the authority-form, since this form is intended
    // exclusively for CONNECT requests, and aero does not currently
    // support this method. It is expected that the CONNECT method has been
    // rejected before this function is called.

    // RFC 3986 "scheme" + "://" separator
    constexpr static std::string_view http_scheme = "http://";
    constexpr static std::string_view https_scheme = "https://";

    // RFC 9112, Section 3.2.4:
    // When a client wishes to request OPTIONS for the server as a whole, as
    // opposed to a specific named resource of that server, the client MUST
    // send only "*" (%x2A) as the request-target.
    if (target == "*") {
      return method == http::method::OPTIONS;
    }

    // RFC 9112, 9110 and 3986:
    // origin-form    = absolute-path [ "?" query ]
    // absolute-path  = 1*( "/" segment )
    // segment        = *pchar
    bool is_origin_form = target.starts_with('/');
    if (is_origin_form) {
      return is_valid_origin_form(target);
    }

    // RFC 9112, Section 3.2.2:
    // absolute-form = absolute-URI
    // ...
    // A server MUST accept the absolute-form in requests even though most
    // HTTP/1.1 clients will only send the absolute-form to a proxy.
    //
    // RFC 9110, Section 4.2.1 and Section 4.2.2:
    // http-URI  = "http"  "://" authority path-abempty [ "?" query ]
    // https-URI = "https" "://" authority path-abempty [ "?" query ]

    // RFC 3986, Section 3.1: An implementation should accept uppercase
    // letters as equivalent to lowercase in scheme names (e.g., allow
    // "HTTP" as well as "http") for the sake of robustness but should only
    // produce lowercase scheme names for consistency.

    // We need to determine exactly which prefix the string has if the
    // request target is specified in absolute-form, but we don't want to
    // validate the prefix again unnecessarily
    bool starts_with_http = aero::detail::striequal(target.substr(0, http_scheme.size()), http_scheme);
    bool starts_with_https = false;
    if (!starts_with_http) {
      starts_with_https = aero::detail::striequal(target.substr(0, https_scheme.size()), https_scheme);
    }

    bool is_absolute_form = starts_with_http || starts_with_https;
    if (is_absolute_form) {
      std::string_view scheme_prefix = starts_with_http ? http_scheme : https_scheme;
      target.remove_prefix(scheme_prefix.size());

      std::string_view authority = target;

      // RFC 3986, Section 3.2:
      // The authority component is preceded by a double slash ("//") and is
      // terminated by the next slash ("/"), question mark ("?"), or number
      // sign ("#") character, or by the end of the URI.
      //
      // '#' is deliberately not searched for here. RFC 3986, Section 4.3
      // defines absolute-URI = scheme ":" hier-part [ "?" query ], with
      // no fragment allowed, so a '#' anywhere in the target must fail
      // validation, which it does in the authority/path/query checks
      std::size_t authority_end = target.find_first_of("/?");
      bool ends_with_authority = authority_end == std::string_view::npos;

      // RFC 3986, Section 3.3:
      // path-abempty ... begins with "/" or is empty
      //
      // In cases such as "http://example.com" no path or query is present
      // and the entire remainder is the authority
      if (ends_with_authority) {
        return is_valid_authority(authority);
      }

      // Otherwise, a path or query is present, and we validate the authority first
      authority = target.substr(0, authority_end);
      if (!is_valid_authority(authority)) {
        return false;
      }

      target.remove_prefix(authority.size());

      // After the authority is validated and removed from the string, all
      // that should be left is the origin-form, but the authority can be
      // followed either by a path or a query, so we need to decide which
      // one to validate
      bool is_path_after_authority = target.starts_with('/');
      if (is_path_after_authority) {
        return is_valid_origin_form(target);
      }

      // If the authority is not followed by a path, it MUST be followed by a query
      return is_valid_uri_query(target);
    }

    return false;
  }

} // namespace aero::http::detail
