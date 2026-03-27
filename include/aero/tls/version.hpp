#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace aero::tls {

  enum class version : std::uint8_t {
    sslv2,
    sslv3,
    tlsv1,
    tlsv1_1,
    tlsv1_2,
    tlsv1_3,
  };

  [[maybe_unused]] constexpr static std::array deprecated_versions{version::sslv2,
    version::sslv3,
    version::tlsv1,
    version::tlsv1_1};

  inline bool is_deprecated(tls::version version) noexcept {
    return std::ranges::contains(deprecated_versions, version);
  }

} // namespace aero::tls
