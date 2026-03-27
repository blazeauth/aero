#pragma once

#include <algorithm>
#include <string>
#include <system_error>
#include <vector>

#include "aero/detail/string.hpp"
#include "aero/http/detail/common.hpp"
#include "aero/http/headers.hpp"
#include "aero/http/method.hpp"
#include "aero/http/request_line.hpp"
#include "aero/http/status_line.hpp"
#include "aero/http/version.hpp"
#include "aero/websocket/detail/accept_challenge.hpp"
#include "aero/websocket/error.hpp"
#include "aero/websocket/uri.hpp"

namespace aero::websocket {

  struct handshake_options {
    std::vector<std::string> subprotocols;
    std::string origin;
  };

  struct handshake_request {
    std::string request;
    std::string sec_websocket_key;

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
      return {reinterpret_cast<const std::byte*>(request.data()), request.size()};
    }
  };

  class client_handshaker {
    constexpr static std::array<std::string_view, 9> protocol_reserved_headers{
      "host",
      "origin",
      "upgrade",
      "connection",
      "sec-websocket-key",
      "sec-websocket-version",
      "sec-websocket-protocol",
      "sec-websocket-extensions",
      "sec-websocket-accept",
    };

    using handshake_error = websocket::error::handshake_error;

   public:
    client_handshaker() = default;

    explicit client_handshaker(handshake_options options)
      : subprotocols_(std::move(options.subprotocols)), origin_(std::move(options.origin)) {}

    [[nodiscard]] std::expected<handshake_request, std::error_code> build_request(const aero::websocket::uri& uri,
      http::headers headers, std::optional<std::string> sec_websocket_key = std::nullopt) {
      if (contains_reserved_header(headers)) {
        return std::unexpected(handshake_error::header_name_reserved);
      }

      std::string request_target = build_request_target(uri);
      std::string host_header_value = build_request_host(uri);

      auto request_line = http::request_line{
        .method = http::method::get,
        .target = request_target,
        .version = http::version::http1_1,
      };

      std::string websocket_key = sec_websocket_key.value_or(detail::generate_sec_websocket_key());

      // If passed `sec_websocket_key` is empty - we generate it
      if (websocket_key.empty()) {
        websocket_key = detail::generate_sec_websocket_key();
      }

      http::headers handshake_headers{
        {"Host", host_header_value},
        {"Upgrade", "websocket"},
        {"Connection", "Upgrade"},
        {"Sec-Websocket-Key", websocket_key},
        {"Sec-Websocket-Version", "13"},
      };

      if (!headers.empty()) {
        handshake_headers.append(std::move(headers));
      }

      if (this->headers_.has_value()) {
        handshake_headers.append(*this->headers_);
      }

      if (!origin_.empty()) {
        handshake_headers.add("Origin", origin_);
      }

      if (!subprotocols_.empty()) {
        auto subprotocols_str = aero::detail::join_strings(subprotocols_, http::detail::header_value_separator);
        handshake_headers.add("Sec-Websocket-Protocol", subprotocols_str);
      }

      return handshake_request{
        .request = request_line.to_string().append(handshake_headers.serialize()),
        .sec_websocket_key = std::move(websocket_key),
      };
    }

    [[nodiscard]] std::expected<http::headers, std::error_code> parse_response(std::string_view payload,
      std::string_view sec_websocket_key) const {
      using websocket::error::handshake_error;

      if (sec_websocket_key.empty()) {
        return std::unexpected(handshake_error::accept_challenge_failed);
      }

      auto status_line_end = payload.find(http::detail::crlf);
      if (status_line_end == std::string_view::npos) {
        return std::unexpected(http::error::protocol_error::status_line_invalid);
      }

      auto status_line = http::status_line::parse(payload.substr(0, status_line_end));
      if (!status_line) {
        return std::unexpected(status_line.error());
      }

      if (status_line->status_code != http::status_code::switching_protocols) {
        return std::unexpected(handshake_error::status_code_invalid);
      }

      auto headers_section_start = status_line_end + http::detail::crlf.size();
      auto headers = http::headers::parse(payload.substr(headers_section_start));
      if (!headers) {
        return std::unexpected(headers.error());
      }

      // 5.2: An |Upgrade| header field with value "websocket"
      auto upgrade_header = headers->first_value("upgrade");
      if (!upgrade_header.has_value() || !aero::detail::ascii_iequal(*upgrade_header, "websocket")) {
        return std::unexpected(handshake_error::upgrade_header_invalid);
      }

      // 5.3: A |Connection| header field with value "Upgrade"
      auto has_connection_upgrade_header = std::ranges::any_of(headers->fields("connection"),
        [](const http::headers::field_type& fields) { return aero::detail::ascii_iequal(fields.value, "Upgrade"); });

      if (!has_connection_upgrade_header) {
        return std::unexpected(handshake_error::connection_header_invalid);
      }

      auto sec_websocket_accept = headers->first_value("sec-websocket-accept");
      if (!sec_websocket_accept.has_value() || sec_websocket_accept->empty()) {
        return std::unexpected(handshake_error::accept_header_invalid);
      }

      auto generated_accept_key = detail::compute_sec_websocket_accept(sec_websocket_key);
      if (generated_accept_key != sec_websocket_accept) {
        return std::unexpected(handshake_error::accept_challenge_failed);
      }

      return headers;
    }

    void set_subprotocols(std::vector<std::string> subprotocols) {
      subprotocols_ = std::move(subprotocols);
    }

    void set_subprotocols(std::span<const std::string> subprotocols) {
      subprotocols_.assign_range(subprotocols);
    }

    void set_subprotocols(std::initializer_list<std::string> subprotocols) {
      subprotocols_ = subprotocols;
    }

    std::error_code set_headers(http::headers headers) {
      if (contains_reserved_header(headers)) {
        return handshake_error::header_name_reserved;
      }

      headers_ = std::move(headers);
      return {};
    }

    void clear_headers() noexcept {
      headers_.reset();
    }

   private:
    [[nodiscard]] bool contains_reserved_header(const http::headers& headers) const noexcept {
      if (headers.empty()) {
        return false;
      }

      return std::ranges::any_of(headers.names(), [this](std::string_view name) { return is_reserved_header(name); });
    }

    [[nodiscard]] bool is_reserved_header(std::string_view name) const noexcept {
      return std::ranges::any_of(protocol_reserved_headers,
        [name](std::string_view header) { return aero::detail::ascii_iequal(name, header); });
    }

    [[nodiscard]] std::string build_request_target(const aero::websocket::uri& uri) const {
      std::string target;

      std::string_view path = uri.path();
      if (path.empty()) {
        target.push_back('/');
      } else {
        if (path.front() != '/') {
          target.push_back('/');
        }
        target.append(path);
      }

      std::span query = uri.query();
      if (!query.empty()) {
        target.push_back('?');
        for (std::size_t i{}; i < query.size(); ++i) {
          if (i != 0) {
            target.push_back('&');
          }
          target.append(query[i].first);
          if (!query[i].second.empty()) {
            target.push_back('=');
            target.append(query[i].second);
          }
        }
      }

      return target;
    }

    [[nodiscard]] std::string build_request_host(const aero::websocket::uri& uri) const {
      std::string host_str{uri.host()};
      std::uint16_t port = uri.port();
      std::uint16_t default_port = uri.default_port();

      if (port != 0 && port != default_port) {
        host_str += ':' + std::to_string(port);
      }
      return host_str;
    }

    std::optional<http::headers> headers_;
    std::vector<std::string> subprotocols_;
    std::string origin_;
  };

} // namespace aero::websocket
