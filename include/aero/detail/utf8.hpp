#ifndef AERO_DETAIL_UTF8_HPP
#define AERO_DETAIL_UTF8_HPP

#include <span>
#include <string_view>

#include <utf8/checked.h>

namespace aero::detail {

  [[nodiscard]] inline bool is_valid_utf8(std::string_view str) {
    return utf8::is_valid(str.begin(), str.end());
  }

  [[nodiscard]] inline bool is_valid_utf8(std::span<const std::byte> str) {
    const auto* bytes = reinterpret_cast<const char*>(str.data());
    return utf8::is_valid(bytes, bytes + str.size());
  }

} // namespace aero::detail

#endif
