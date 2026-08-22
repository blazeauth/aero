#pragma once

#include <string_view>

#include "aero/detail/rfc_grammar.hpp"
#include "aero/urls/detail/pct_encoding.hpp"

namespace aero::urls::detail {

  struct query_parser_opts {
    bool validate_pct_encoding{true};
  };

  inline bool is_valid_query(std::string_view query, query_parser_opts opts = {}) noexcept {
    // RFC 3986, Appendix A:
    // query = *( pchar / "/" / "?" )
    return is_pct_encoded_sequence(
      query,
      [](char c) { return aero::detail::is_unencoded_pchar(c) || c == '/' || c == '?'; },
      opts.validate_pct_encoding);
  }

} // namespace aero::urls::detail
