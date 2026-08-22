#pragma once

#include "aero/urls/detail/authority.hpp"
#include "aero/urls/detail/hostname.hpp"
#include "aero/urls/detail/path.hpp"
#include "aero/urls/detail/query.hpp"
#include "aero/urls/error.hpp"
#include "aero/urls/url.hpp"
#include "aero/util/string.hpp"

#include <algorithm>
#include <expected>
#include <string>

namespace aero::urls {

  namespace detail {

    struct hier_part {
      rfc3986_authority authority;
      rfc3986_path path;
      bool has_authority{};
    };

    inline bool is_valid_scheme(std::string_view scheme) noexcept {
      // scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
      return std::ranges::all_of(scheme, [](char c) { return aero::is_alnum(c) || c == '+' || c == '-' || c == '.'; });
    }

    inline std::expected<std::string_view, std::error_code> parse_scheme(std::string_view text) {
      // RFC 3986, Section 3.1:
      // Scheme names consist of a sequence of characters beginning with a
      // letter and followed by any combination of letters, digits, plus ("+"),
      // period ("."), or hyphen ("-"). Although schemes are case-insensitive,
      // the canonical form is lowercase and documents that specify schemes must
      // do so with lowercase letters. An implementation should accept uppercase
      // letters as equivalent to lowercase in scheme names (e.g., allow "HTTP"
      // as well as "http") for the sake of robustness.

      // URI        = scheme ":" hier-part [ "?" query ] [ "#" fragment ]
      // scheme     = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )

      if (text.empty()) {
        return std::unexpected(url_error::scheme_invalid);
      }

      bool starts_with_alpha = aero::is_alpha(text.front());
      if (not starts_with_alpha) {
        return std::unexpected(url_error::scheme_invalid);
      }

      std::size_t scheme_end_pos = text.find(':');
      if (scheme_end_pos == std::string_view::npos) {
        return std::unexpected(url_error::scheme_invalid);
      }

      std::string_view scheme = text.substr(0, scheme_end_pos);
      if (!is_valid_scheme(scheme)) {
        return std::unexpected(url_error::scheme_invalid);
      }

      return scheme;
    }

    inline bool is_valid_fragment(std::string_view fragment, query_parser_opts opts = {}) noexcept {
      // RFC 3986, Appendix A:
      // fragment = *( pchar / "/" / "?" )
      return is_valid_query(fragment, opts);
    }

    inline std::expected<hier_part, std::error_code> parse_hier_part(std::string_view hier_part,
      authority_parser_opts authority_opts = {}, path_parser_opts path_opts = {}) {
      // RFC 3986, Section 3:
      // hier-part  = "//" authority path-abempty
      //             / path-absolute
      //             / path-rootless
      //             / path-empty
      bool has_authority = hier_part.starts_with("//");
      if (!has_authority) {
        auto path = parse_hier_part_path(hier_part, has_authority, path_opts);
        if (!path) {
          return std::unexpected(path.error());
        }

        return detail::hier_part{.path = *path};
      }

      // Remove "//" separator before authority
      hier_part.remove_prefix(2);

      std::string_view authority_str = hier_part;
      std::string_view path_str;

      // The authority is terminated by the next "/", which opens non-empty
      // path-abempty, or by the end of the hier-part (empty path-abempty)
      std::size_t path_abempty_start_pos = hier_part.find('/');
      if (path_abempty_start_pos != std::string_view::npos) {
        authority_str = hier_part.substr(0, path_abempty_start_pos);
        path_str = hier_part.substr(path_abempty_start_pos);
      }

      auto authority = parse_authority(authority_str, authority_opts);
      if (!authority) {
        return std::unexpected(authority.error());
      }

      auto path = parse_hier_part_path(path_str, has_authority, path_opts);
      if (!path) {
        return std::unexpected(path.error());
      }

      return detail::hier_part{.authority = *authority, .path = *path, .has_authority = has_authority};
    }

  } // namespace detail

  inline std::expected<urls::url, std::error_code> url::parse(std::string_view url, url_parser_opts opts) {
    // RFC 3986, Section 3:
    // The generic URI syntax consists of a hierarchical sequence of components
    // referred to as the scheme, authority, path, query, and fragment.
    //
    // URI       = scheme ":" hier-part [ "?" query ] [ "#" fragment ]
    // hier-part = "//" authority path-abempty
    //            / path-absolute
    //            / path-rootless
    //            / path-empty

    // This is the reference that we are going to parse:
    // foo://example.com:8042/over/there?name=ferret#nose

    auto parsed_scheme = detail::parse_scheme(url);
    if (!parsed_scheme) {
      return std::unexpected(parsed_scheme.error());
    }

    std::string_view scheme = *parsed_scheme;

    // +1 for ':' scheme separator
    url.remove_prefix(scheme.size() + 1);

    detail::query_parser_opts query_opts{.validate_pct_encoding = opts.validate_pct_encoding};

    // RFC 3986, Section 3.5:
    // A fragment identifier component is indicated by the presence of a
    // number sign ("#") character and terminated by the end of the URI.
    std::string_view fragment;
    std::size_t fragment_start_pos = url.find('#');
    if (fragment_start_pos != std::string_view::npos) {
      fragment = url.substr(fragment_start_pos + 1);

      // +1 for '#' fragment separator
      url.remove_suffix(fragment.size() + 1);

      if (!detail::is_valid_fragment(fragment, query_opts)) {
        return std::unexpected(url_error::fragment_invalid);
      }
    }

    // RFC 3986, Section 3.4:
    // The query component is indicated by the first question mark ("?")
    // character and terminated by a number sign ("#") character or by
    // the end of the URI.
    std::string_view query;
    std::size_t query_start_pos = url.find('?');
    if (query_start_pos != std::string_view::npos) {
      query = url.substr(query_start_pos + 1);

      // +1 for '?' query separator
      url.remove_suffix(query.size() + 1);

      if (!detail::is_valid_query(query, query_opts)) {
        return std::unexpected(url_error::query_invalid);
      }
    }

    detail::authority_parser_opts authority_opts{.allow_userinfo = opts.allow_userinfo,
      .validate_pct_encoding = opts.validate_pct_encoding};

    if (opts.validate_port_range) {
      authority_opts.allowed_port_range = detail::port_range{};
    }

    detail::path_parser_opts path_opts{.validate_pct_encoding = opts.validate_pct_encoding};

    auto hier_part = detail::parse_hier_part(url, authority_opts, path_opts);
    if (!hier_part) {
      return std::unexpected(hier_part.error());
    }

    std::string_view host = hier_part->authority.host;

    // Validate DNS hostname (which is more strict than reg-name) only when
    // specified by a parser option and host_type is not an IP literal
    if (opts.validate_dns_hostname && hier_part->authority.type == detail::host_type::reg_name) {
      if (!detail::is_valid_hostname(host, opts.extra_hostname_chars)) {
        return std::unexpected(url_error::host_invalid);
      }
    }

    urls::url parsed;
    parsed.set_scheme(std::string{scheme});
    parsed.set_userinfo(std::string{hier_part->authority.userinfo});
    parsed.set_host(std::string{host});
    parsed.set_port(std::string{hier_part->authority.port});
    parsed.set_path(std::string{hier_part->path.text});
    parsed.set_query(std::string{query});
    parsed.set_fragment(std::string{fragment});
    parsed.has_authority_ = hier_part->has_authority;

    return parsed;
  }

} // namespace aero::urls
