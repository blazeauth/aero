#ifndef AERO_HTTP_DETAIL_COMMON_HPP
#define AERO_HTTP_DETAIL_COMMON_HPP

#include <string_view>

namespace aero::http::detail {

  [[maybe_unused]] constexpr std::string_view header_name_value_separator{": "};
  [[maybe_unused]] constexpr std::string_view header_value_separator{", "};
  [[maybe_unused]] constexpr std::string_view header_separator{"\r\n"};
  [[maybe_unused]] constexpr std::string_view headers_end_separator{"\r\n\r\n"};

} // namespace aero::http::detail

#endif
