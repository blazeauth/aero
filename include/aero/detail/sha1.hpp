#pragma once

#include <array>
#include <span>
#include <string_view>

#ifdef AERO_USE_TLS
#include "aero/tls/detail/sha1.hpp"
#else
#include "aero/detail/impl/sha1.hpp"
#endif

namespace aero::detail {

#ifdef AERO_USE_TLS
  using sha_result = std::array<std::byte, tls::detail::sha1_digest_size>;
#else
  using sha_result = std::array<std::byte, sha1::digest_size>;
#endif

  inline sha_result sha1(std::span<const std::byte> input) {
#ifdef AERO_USE_TLS
    return aero::tls::detail::sha1(input);
#else
    return aero::detail::sha1::hash(input);
#endif
  }

  inline sha_result sha1(std::string_view input) {
    return sha1(std::span{reinterpret_cast<const std::byte*>(input.data()), input.size()});
  }

} // namespace aero::detail
