#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace aero::detail {

  inline bool is_valid_ipv4_address(std::string_view address) noexcept {
    // RFC 3986 3.2.2:
    // A host identified by an IPv4 literal address is represented in
    // dotted-decimal notation (a sequence of four decimal numbers in
    // the range 0 to 255, separated by ".")
    // IPv4address = dec-octet "." dec-octet "." dec-octet "." dec-octet

    constexpr std::size_t min_ipv4_length = 7;  // "x.x.x.x"
    constexpr std::size_t max_ipv4_length = 15; // "xxx.xxx.xxx.xxx"

    if (address.size() < min_ipv4_length || address.size() > max_ipv4_length) {
      return false;
    }

    std::size_t pos = 0;

    // An IPv4 address contains exactly 4 octets separated by '.' (ignoring legacy forms)
    for (int octet_index = 0; octet_index < 4; ++octet_index) {
      if (pos == address.size()) {
        return false;
      }

      std::size_t octet_start = pos;
      std::uint32_t octet_value = 0;
      std::uint32_t digit_count = 0;

      while (pos < address.size() && address[pos] != '.') {
        char ch = address[pos];

        // IPv4 allows only numeric (0-9) and '.' chars
        if (ch < '0' || ch > '9') [[unlikely]] {
          return false;
        }

        octet_value = (octet_value * 10) + static_cast<std::uint32_t>(ch - '0');
        ++digit_count;
        ++pos;

        // An octet cannot be longer than 3 digits (0-255)
        if (digit_count > 3) {
          return false;
        }
      }

      // An octet should contain at least 1 digit
      if (digit_count == 0) {
        return false;
      }

      // IPv4 multi-digit octet cannot have leading zeros
      if (digit_count > 1 && address[octet_start] == '0') {
        return false;
      }

      // An octet must be in the range 0-255
      if (octet_value > 255) {
        return false;
      }

      if (octet_index < 3) {
        // Parsed fewer than 4 octets, but the address already ended
        if (pos == address.size()) {
          return false;
        }

        if (address[pos] != '.') {
          return false;
        }

        ++pos;
      }
      // All 4 octets have been parsed, so any remaining tail is invalid
      else if (pos != address.size()) {
        return false;
      }
    }

    return true;
  }

  inline bool is_ipv6_hex_digit(char ch) noexcept {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
  }

  inline bool count_ipv6_pieces(std::string_view text, std::size_t& piece_count) noexcept {
    piece_count = 0;

    // Empty side is valid after splitting around "::".
    // Examples: "::1", "1::", "::".
    if (text.empty()) {
      return true;
    }

    std::size_t pos = 0;

    while (pos < text.size()) {
      std::size_t digit_count = 0;

      // Parse one explicit IPv6 piece.
      // RFC 3986 grammar name:
      // h16 = 1*4HEXDIG
      while (pos < text.size() && text[pos] != ':') {
        char ch = text[pos];

        if (!is_ipv6_hex_digit(ch)) [[unlikely]] {
          return false;
        }

        ++digit_count;
        ++pos;

        if (digit_count > 4) [[unlikely]] {
          return false;
        }
      }

      if (digit_count == 0) [[unlikely]] {
        return false;
      }

      ++piece_count;

      if (pos == text.size()) {
        return true;
      }

      // Parser stopped before the end, so it must be at ':'
      if (text[pos] != ':') [[unlikely]] {
        return false;
      }

      ++pos;

      // After a single ':' there must be another explicit piece.
      // Valid "::" compression is handled before this function is called
      if (pos == text.size()) [[unlikely]] {
        return false;
      }
    }

    return true;
  }

  inline bool is_valid_ipv6_address(std::string_view address) noexcept {
    constexpr std::size_t min_ipv6_length = 2;  // "::"
    constexpr std::size_t max_ipv6_length = 45; // "xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:255.255.255.255"

    if (address.size() < min_ipv6_length || address.size() > max_ipv6_length) {
      return false;
    }

    std::string_view ipv6_part = address;
    std::size_t ipv4_tail_piece_count = 0;

    // RFC 3986 describes IPv6 format in 3.2.2, stating that the last
    // 32 bits of an IPv6 address (2 last parts) can be an IPv4 address
    bool has_ipv4_tail = address.find('.') != std::string_view::npos;
    if (has_ipv4_tail) {
      // Find the last IPv6 separator before the IPv4 tail
      std::size_t tail_separator_pos = address.rfind(':');
      if (tail_separator_pos == std::string_view::npos) {
        return false;
      }

      // Assume that everything after the last IPv6 separator is the
      // IPv4 tail, the IPv4 validator will catch any issues
      std::string_view ipv4_tail = address.substr(tail_separator_pos + 1); // +1 to skip ':'
      if (!is_valid_ipv4_address(ipv4_tail)) {
        return false;
      }

      std::size_t ipv6_part_size = tail_separator_pos;

      // For cases such as "::xxx.x.x.x" we check if there is a compression
      // token and do not count it as a part of IPv6
      if (tail_separator_pos > 0 && address[tail_separator_pos - 1] == ':') {
        ipv6_part_size = tail_separator_pos + 1;
      }

      // When an IPv4 tail is present, it takes 2 last pieces of IPv6 address (32 bits)
      ipv4_tail_piece_count = 2;
      ipv6_part = address.substr(0, ipv6_part_size);
    }

    std::size_t compression_pos = ipv6_part.find("::");
    if (compression_pos != std::string_view::npos) {
      std::string_view left_part = ipv6_part.substr(0, compression_pos);
      std::string_view right_part = ipv6_part.substr(compression_pos + 2);

      // Only one compression token is allowed in an IPv6 literal.
      // Only the right side needs to be scanned because the first
      // compression token has already been found
      if (right_part.find("::") != std::string_view::npos) {
        return false;
      }

      std::size_t left_piece_count = 0;
      std::size_t right_piece_count = 0;

      if (!count_ipv6_pieces(left_part, left_piece_count)) {
        return false;
      }

      if (!count_ipv6_pieces(right_part, right_piece_count)) {
        return false;
      }

      std::size_t piece_count = left_piece_count + right_piece_count + ipv4_tail_piece_count;

      // Address must not have 8 pieces if compression token is present
      return piece_count < 8;
    }

    // No compression token present, just count all of the IPv6 pieces
    std::size_t piece_count = 0;
    if (!count_ipv6_pieces(ipv6_part, piece_count)) {
      return false;
    }

    // IPv6 pieces + optional 2 pieces from IPv4 tail
    return piece_count + ipv4_tail_piece_count == 8;
  }

} // namespace aero::detail
