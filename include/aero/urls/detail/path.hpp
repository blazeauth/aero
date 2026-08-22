#pragma once

#include <cstdint>
#include <expected>
#include <string_view>
#include <system_error>

#include "aero/detail/rfc_grammar.hpp"
#include "aero/urls/detail/pct_encoding.hpp"
#include "aero/urls/error.hpp"

namespace aero::urls::detail {

  // Every single ABNF/text citation here comes from "RFC 3986, Section 3.3",
  // this comment is created to avoid restating this fact over and over again

  namespace grammar = aero::detail;

  struct path_parser_opts {
    bool validate_pct_encoding{true};
  };

  inline bool is_valid_segment(std::string_view segment, path_parser_opts opts = {}) noexcept {
    return is_pct_encoded_sequence(
      segment,
      [](char c) {
        // segment = *pchar
        return grammar::is_unencoded_pchar(c);
      },
      opts.validate_pct_encoding);
  }

  inline bool is_valid_segments(std::string_view segment, path_parser_opts opts = {}) noexcept {
    return is_pct_encoded_sequence(
      segment,
      [](char c) {
        // *( "/" segment )
        // segment = *pchar
        return grammar::is_unencoded_pchar(c) || c == '/';
      },
      opts.validate_pct_encoding);
  }

  inline bool is_valid_segment_nz(std::string_view segment_nz, path_parser_opts opts = {}) noexcept {
    // segment-nz = 1*pchar
    if (segment_nz.empty()) {
      return false;
    }
    return is_valid_segment(segment_nz, opts);
  }

  inline bool is_valid_segment_nz_nc(std::string_view segment_nz_nc, path_parser_opts opts = {}) noexcept {
    if (segment_nz_nc.empty()) {
      return false;
    }

    return is_pct_encoded_sequence(
      segment_nz_nc,
      [](char c) {
        // segment-nz-nc = 1*( unreserved / pct-encoded / sub-delims / "@" )
        return grammar::is_unreserved(c) || grammar::is_sub_delim(c) || c == '@';
      },
      opts.validate_pct_encoding);
  }

  inline bool is_path_abempty(std::string_view path, path_parser_opts opts = {}) noexcept {
    // path         = path-abempty    ; begins with "/" or is empty
    // path-abempty = *( "/" segment )
    // segment      = *pchar
    if (path.empty()) {
      return true;
    }

    if (!path.starts_with('/')) {
      return false;
    }

    return is_valid_segments(path, opts);
  }

  inline bool is_path_absolute(std::string_view path, path_parser_opts opts = {}) noexcept {
    // path          = path-absolute   ; begins with "/" but not "//"
    // path-absolute = "/" [ segment-nz *( "/" segment ) ]
    // segment       = *pchar
    // segment-nz    = 1*pchar
    if (!path.starts_with('/') || path.starts_with("//")) {
      return false;
    }

    if (path == "/") {
      return true;
    }

    // Remove "/" prefix
    path.remove_prefix(1);
    if (path.empty()) {
      return false;
    }

    std::string_view segment_nz = path;
    std::string_view segments;

    std::size_t segments_start_pos = path.find('/');
    if (segments_start_pos != std::string_view::npos) {
      segment_nz = path.substr(0, segments_start_pos);
      segments = path.substr(segments_start_pos);
    }

    if (!is_valid_segment_nz(segment_nz, opts)) {
      return false;
    }

    return is_valid_segments(segments, opts);
  }

  inline bool is_path_noscheme(std::string_view path, path_parser_opts opts = {}) noexcept {
    // path          = path-noscheme   ; begins with a non-colon segment
    // path-noscheme = segment-nz-nc *( "/" segment )
    // segment       = *pchar
    // segment-nz-nc = 1*( unreserved / pct-encoded / sub-delims / "@" )
    //               ; non-zero-length segment without any colon ":"
    if (path.empty()) {
      return false;
    }

    std::string_view segment_nz_nc = path;
    std::string_view segments;

    std::size_t segments_start_pos = path.find('/');
    if (segments_start_pos != std::string_view::npos) {
      segment_nz_nc = path.substr(0, segments_start_pos);
      segments = path.substr(segments_start_pos);
    }

    if (!is_valid_segment_nz_nc(segment_nz_nc, opts)) {
      return false;
    }

    return is_valid_segments(segments, opts);
  }

  inline bool is_path_rootless(std::string_view path, path_parser_opts opts = {}) noexcept {
    // path          = path-rootless   ; begins with a segment
    // path-rootless = segment-nz *( "/" segment )
    // segment-nz    = 1*pchar
    // segment       = *pchar
    std::string_view segment_nz = path;
    std::string_view segments;

    std::size_t segment_start_pos = path.find('/');
    if (segment_start_pos != std::string_view::npos) {
      segment_nz = path.substr(0, segment_start_pos);
      segments = path.substr(segment_start_pos);
    }

    if (!is_valid_segment_nz(segment_nz, opts)) {
      return false;
    }

    return is_valid_segments(segments, opts);
  }

  inline bool is_path_empty(std::string_view path) noexcept {
    // path          = path-empty ; zero characters
    // path-empty    = 0<pchar>
    return path.empty();
  }

  enum class path_type : std::uint8_t {
    abempty,
    absolute,
    noscheme,
    rootless,
    empty
  };

  struct rfc3986_path {
    std::string_view text;
    path_type type{};
  };

  // RFC 3986, Section 3:
  // hier-part = "//" authority path-abempty
  //           / path-absolute
  //           / path-rootless
  //           / path-empty
  inline std::expected<rfc3986_path, std::error_code> parse_hier_part_path(std::string_view path, bool has_authority,
    path_parser_opts opts = {}) noexcept {
    // According to the hier-part ABNF, when an authority is present, the path
    // component must be of type abempty
    if (has_authority) {
      if (!is_path_abempty(path, opts)) {
        return std::unexpected(url_error::path_invalid);
      }

      return rfc3986_path{.text = path, .type = path_type::abempty};
    }

    if (is_path_empty(path)) {
      return rfc3986_path{.text = path, .type = path_type::empty};
    }

    // If a URI does not contain an authority component, then the path cannot
    // begin with "//", which is_path_absolute rejects.
    if (path.starts_with('/')) {
      if (!is_path_absolute(path, opts)) {
        return std::unexpected(url_error::path_invalid);
      }

      return rfc3986_path{.text = path, .type = path_type::absolute};
    }

    if (!is_path_rootless(path, opts)) {
      return std::unexpected(url_error::path_invalid);
    }

    return rfc3986_path{.text = path, .type = path_type::rootless};
  }

} // namespace aero::urls::detail
