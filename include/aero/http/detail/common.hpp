#pragma once

#include <string_view>

namespace aero::http::detail {

  [[maybe_unused]] constexpr std::string_view header_name_value_separator{": "};
  [[maybe_unused]] constexpr std::string_view header_value_separator{", "};
  [[maybe_unused]] constexpr std::string_view crlf{"\r\n"};
  [[maybe_unused]] constexpr std::string_view double_crlf{"\r\n\r\n"};

} // namespace aero::http::detail
