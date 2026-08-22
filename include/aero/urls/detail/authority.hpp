#pragma once

#include <algorithm>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

#include "aero/detail/rfc_grammar.hpp"
#include "aero/urls/detail/pct_encoding.hpp"
#include "aero/urls/error.hpp"
#include "aero/util/ip_address_validator.hpp"
#include "aero/util/string.hpp"

namespace aero::urls::detail {

  enum class host_type : std::uint8_t {
    ipv6,
    ipv4,
    reg_name
  };

  struct port_range {
    std::uint16_t min{};
    std::uint16_t max{65535};
  };

  struct authority_parser_opts {
    bool allow_userinfo{true};
    bool allow_empty_host{true};
    bool validate_pct_encoding{true};

    // RFC 3986 allows any port as long as it consists solely of DIGITs,
    // so it's useful to provide an option for limiting the port range
    std::optional<port_range> allowed_port_range;
  };

  struct rfc3986_authority {
    std::string_view userinfo;
    std::string_view host;
    std::string_view port;
    host_type type{};
  };

  inline bool is_valid_userinfo(std::string_view userinfo, authority_parser_opts opts = {}) {
    // userinfo = *( unreserved / pct-encoded / sub-delims / ":" )
    return is_pct_encoded_sequence(
      userinfo,
      [](char c) { return aero::detail::is_unreserved(c) || aero::detail::is_sub_delim(c) || c == ':'; },
      opts.validate_pct_encoding);
  }

  inline bool is_valid_reg_name(std::string_view reg_name, authority_parser_opts opts = {}) {
    // reg-name = *( unreserved / pct-encoded / sub-delims )
    return is_pct_encoded_sequence(
      reg_name,
      [](char c) { return aero::detail::is_unreserved(c) || aero::detail::is_sub_delim(c); },
      opts.validate_pct_encoding);
  }

  inline std::expected<rfc3986_authority, std::error_code> parse_authority(std::string_view authority,
    authority_parser_opts opts = {}) noexcept {
    // RFC 3986, Appendix A:
    // authority  = [ userinfo "@" ] host [ ":" port ]
    //
    // userinfo   = *( unreserved / pct-encoded / sub-delims / ":" )
    // host       = IP-literal / IPv4address / reg-name
    // port       = *DIGIT
    //
    // IP-literal = "[" ( IPv6address / IPvFuture  ) "]"
    // reg-name   = *( unreserved / pct-encoded / sub-delims )

    std::string_view userinfo;

    std::size_t userinfo_end_pos = authority.find('@');
    if (userinfo_end_pos != std::string_view::npos) {
      if (!opts.allow_userinfo) {
        return std::unexpected(url_error::userinfo_not_allowed);
      }

      userinfo = authority.substr(0, userinfo_end_pos);

      if (!is_valid_userinfo(userinfo, opts)) {
        return std::unexpected(url_error::userinfo_invalid);
      }

      // Remove parsed userinfo and "@" separator
      authority.remove_prefix(userinfo.size() + 1);
    }

    // RFC 3986, Section 3.2.2:
    // A host identified by an IPv6 literal address is represented inside
    // the square brackets without a preceding version flag.
    std::size_t ip_literal_end_pos = 0;
    if (authority.starts_with('[')) {
      ip_literal_end_pos = authority.find(']');
      if (ip_literal_end_pos == std::string_view::npos) {
        return std::unexpected(url_error::authority_invalid);
      }
    }

    std::string_view port_str;

    // An IPv6 address carries colons of its own, so the port delimiter is the
    // first ":" after the IP literal
    std::size_t port_pos = authority.find(':', ip_literal_end_pos);
    bool has_port = port_pos != std::string_view::npos;

    if (has_port) {
      port_str = authority.substr(port_pos + 1);
      if (!std::ranges::all_of(port_str, aero::is_digit)) {
        return std::unexpected(url_error::port_invalid);
      }

      // When the port after the separator is empty, it means that the URI uses
      // the default scheme port (e.g. 80 for "http", 443 for "https").
      //
      // We do not validate the values of these ports, since knowing the default
      // ports is not the responsibility of this layer
      if (!port_str.empty() && opts.allowed_port_range) {
        std::uint32_t port_value = 0;

        for (char c : port_str) {
          port_value = (port_value * 10) + static_cast<std::uint32_t>(c - '0');

          if (port_value > opts.allowed_port_range->max) {
            return std::unexpected(url_error::port_invalid);
          }
        }

        if (port_value < opts.allowed_port_range->min) {
          return std::unexpected(url_error::port_invalid);
        }
      }
    }

    // Remove ":" in cases when port is present
    authority.remove_suffix(port_str.size() + (has_port ? 1 : 0));

    std::string_view host = authority;
    std::optional<host_type> host_type;
    bool is_ipv6_address = host.starts_with('[') && host.ends_with(']');

    if (is_ipv6_address) {
      std::string_view bracketless_ipv6 = host.substr(1, host.size() - 2);
      if (!aero::is_valid_ipv6_address(bracketless_ipv6)) {
        return std::unexpected(url_error::host_invalid);
      }
      host_type = host_type::ipv6;
    } else if (aero::is_valid_ipv4_address(host)) {
      host_type = host_type::ipv4;
    } else if (is_valid_reg_name(host, opts)) {
      host_type = host_type::reg_name;
    }

    if (!host_type) {
      return std::unexpected(url_error::host_invalid);
    }

    if (host.empty() && !opts.allow_empty_host) {
      return std::unexpected(url_error::authority_invalid);
    }

    return rfc3986_authority{.userinfo = userinfo, .host = host, .port = port_str, .type = *host_type};
  }

} // namespace aero::urls::detail
