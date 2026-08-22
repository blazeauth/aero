#pragma once

#include <string_view>

#include "aero/detail/rfc_grammar.hpp"
#include "aero/http/method.hpp"
#include "aero/urls/detail/authority.hpp"
#include "aero/urls/detail/hostname.hpp"
#include "aero/util/ip_address_validator.hpp"
#include "aero/util/string.hpp"

namespace aero::http::detail {

  constexpr inline urls::detail::authority_parser_opts authority_parser_opts{
    // RFC 9110, Section 4.2.4:
    // Before making use of an "http" or "https" URI reference received from
    // an untrusted source, a recipient SHOULD parse for userinfo and treat
    // its presence as an error; ...
    .allow_userinfo = false,

    // RFC 9110, Section 4.2.1:
    // A sender MUST NOT generate an "http" URI with an empty host
    // identifier.  A recipient that processes such a URI reference MUST
    // reject it as invalid.
    .allow_empty_host = false,

    // Stricter than the RFCs require: port 0 and IPvFuture literals are
    // rejected by internal policy
    .allowed_port_range = urls::detail::port_range{.min = 1},
  };

  inline bool is_valid_authority(std::string_view authority) noexcept {
    auto parsed = urls::detail::parse_authority(authority, authority_parser_opts);
    if (!parsed) {
      return false;
    }

    if (parsed->type != urls::detail::host_type::reg_name) {
      return true;
    }

    // Reject hosts that look like an IPv4 address but failed IPv4 validation
    if (aero::detail::is_ipv4_address_like(parsed->host)) {
      return false;
    }

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
    constexpr static std::string_view extra_hostname_chars = "_";

    return urls::detail::is_valid_hostname(parsed->host, extra_hostname_chars);
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
        if (!aero::detail::is_pct_encoded(path.substr(pos, 3))) {
          return false;
        }

        pos += 3;
        continue;
      }

      // Percent encoding has already been validated above, so all that
      // remains is to check whether the character is one of the allowed
      // pchar, unreserved, or sub-delimiter characters.
      if (ch != ':' && ch != '@' && !aero::detail::is_unreserved(ch) && !aero::detail::is_sub_delim(ch)) {
        return false;
      }

      pos++;
    }

    return true;
  }

  inline bool is_valid_uri_query(std::string_view query) noexcept {
    if (query.empty()) {
      return true;
    }

    return aero::detail::is_pct_encoded_sequence(query, [](char c) {
      // RFC 3986, Appendix A:
      // query       = *( pchar / "/" / "?" )
      // pchar       = unreserved / pct-encoded / sub-delims / ":" / "@"
      return aero::detail::is_unencoded_pchar(c) || c == '/' || c == '?';
    });
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
    bool starts_with_http = aero::striequal(target.substr(0, http_scheme.size()), http_scheme);
    bool starts_with_https = false;
    if (!starts_with_http) {
      starts_with_https = aero::striequal(target.substr(0, https_scheme.size()), https_scheme);
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
