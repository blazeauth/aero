#pragma once

#include <string_view>

namespace aero::http::detail {

  [[maybe_unused]] constexpr std::string_view header_name_value_separator{": "};
  [[maybe_unused]] constexpr std::string_view header_value_separator{", "};
  [[maybe_unused]] constexpr std::string_view crlf{"\r\n"};
  [[maybe_unused]] constexpr std::string_view double_crlf{"\r\n\r\n"};
  [[maybe_unused]] constexpr std::string_view double_lf{"\n\n"};

  [[maybe_unused]] constexpr inline std::size_t max_response_body_size{1ULL * 1024 * 1024};

} // namespace aero::http::detail
