#ifndef AERO_WEBSOCKET_CONCEPTS_MASKING_KEY_SOURCE_HPP
#define AERO_WEBSOCKET_CONCEPTS_MASKING_KEY_SOURCE_HPP

#include <array>
#include <expected>
#include <system_error>

namespace aero::websocket::concepts {

  using masking_key = std::array<std::byte, 4>;

  template <typename T>
  concept masking_key_source = requires(T obj) {
    { obj.next() } -> std::same_as<std::expected<masking_key, std::error_code>>;
  };

} // namespace aero::websocket::concepts

#endif
