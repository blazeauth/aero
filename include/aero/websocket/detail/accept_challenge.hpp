#ifndef AERO_WEBSOCKET_DETAIL_ACCEPT_CHALLENGE_HPP
#define AERO_WEBSOCKET_DETAIL_ACCEPT_CHALLENGE_HPP

#include <algorithm>
#include <random>
#include <string>

#include "aero/base64/base64.hpp"
#include "aero/detail/sha1.hpp"

namespace aero::websocket::detail {

  [[nodiscard]] inline std::string generate_random_alphanum_string(std::size_t length) {
    constexpr std::string_view alphanum{"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"};

    std::string result;
    result.resize(length);

    std::default_random_engine rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist{0, alphanum.size() - 1};

    std::ranges::generate(result, [&]() -> char { return alphanum[dist(rng)]; });

    return result;
  }

  [[nodiscard]] inline std::string generate_sec_websocket_key() {
    // RFC6455 4.2.1/5:
    // A |Sec-WebSocket-Key| header field with a base64-encoded
    // value that, when decoded, is 16 bytes in length.
    constexpr std::size_t decoded_key_length{16};

    auto key = generate_random_alphanum_string(decoded_key_length);
    return aero::base64_encode(key);
  }

  [[nodiscard]] inline std::string compute_sec_websocket_accept(std::string_view sec_websocket_key) {
    constexpr std::string_view websocket_guid{"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"};
    auto concatenated_key = std::string{sec_websocket_key}.append(websocket_guid);
    auto hashed_bytes = aero::detail::sha1(concatenated_key);

    return aero::base64_encode(std::span<const std::byte>{hashed_bytes});
  }

} // namespace aero::websocket::detail

#endif
