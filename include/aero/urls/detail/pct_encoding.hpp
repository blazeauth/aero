#pragma once

#include <algorithm>
#include <concepts>
#include <string_view>

#include "aero/detail/rfc_grammar.hpp"

namespace aero::urls::detail {

  // If pct-encoding is not validated, the "%" character is treated as a regular
  // character, and a correctly formed pct-encoding in this case consists simply
  // of the "%" character followed by two HEXDIG characters
  template <std::predicate<char> Pred>
  [[nodiscard]] constexpr bool is_pct_encoded_sequence(std::string_view str, Pred is_allowed,
    bool validate_pct_encoding) noexcept {
    if (!validate_pct_encoding) {
      return std::ranges::all_of(str, [&is_allowed](char c) { return is_allowed(c) || c == '%'; });
    }

    return aero::detail::is_pct_encoded_sequence(str, is_allowed);
  }

} // namespace aero::urls::detail
