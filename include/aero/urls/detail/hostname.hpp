#pragma once

#include "aero/util/string.hpp"
#include <cstddef>
#include <string_view>

namespace aero::urls::detail {

  // TODO: We should add pct-encoding support for DNS hostnames later

  // Accepts strict ASCII DNS names, allows numeric-only labels, and
  // rejects raw Unicode and percent-encoding
  inline bool is_valid_hostname(std::string_view hostname, std::string_view extra_chars = {}) noexcept {
    // RFC 1035 limits a DNS name to 255 octets in DNS message form. For
    // dotted text form, the maximum length without a trailing root dot is
    // 253 characters: 63 "." 63 "." 63 "." 61
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

    for (char c : hostname) {
      if (c == '.') {
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
        prev_char = c;
        continue;
      }

      // RFC 1035, Section 2.3.1:
      // The labels must follow the rules for ARPANET host names. They must
      // ... have as interior characters only letters, digits, and hyphen.
      bool is_dns_label_char = aero::is_alpha(c) || aero::is_digit(c) || c == '-';
      if (!is_dns_label_char && !extra_chars.contains(c)) {
        return false;
      }

      // RFC 1123, Section 2.1:
      // One aspect of host name syntax is hereby changed: the restriction on the
      // first character is relaxed to allow either a letter or a digit.
      if (dns_label_length == 0 && c == '-') {
        return false;
      }

      ++dns_label_length;

      if (dns_label_length > max_dns_label_length) {
        return false;
      }

      prev_char = c;
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

} // namespace aero::urls::detail
