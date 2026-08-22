#pragma once

#include <charconv>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace aero::urls {

  struct url_parser_opts {
    bool validate_pct_encoding{true};
    bool allow_userinfo{true};
    bool validate_port_range{false};
    bool validate_dns_hostname{false};

    // Characters accepted in a hostname even where RFC 1035 does not allow them
    std::string_view extra_hostname_chars;
  };

  class url {
   public:
    url() = default;

    [[nodiscard]] static std::expected<url, std::error_code> parse(std::string_view url, url_parser_opts opts = {});

    [[nodiscard]] std::string_view scheme() const noexcept {
      return scheme_;
    }

    [[nodiscard]] std::string_view userinfo() const noexcept {
      return userinfo_;
    }

    [[nodiscard]] std::string_view host() const noexcept {
      return host_;
    }

    [[nodiscard]] std::string_view port() const noexcept {
      return port_;
    }

    [[nodiscard]] std::optional<std::uint16_t> port_number() const noexcept {
      if (port_.empty()) {
        return std::nullopt;
      }

      std::uint16_t value = 0;
      auto conversion = std::from_chars(port_.data(), port_.data() + port_.size(), value);
      if (conversion.ec != std::errc{}) {
        return std::nullopt;
      }

      return value;
    }

    [[nodiscard]] std::string_view path() const noexcept {
      return path_;
    }

    [[nodiscard]] std::string_view query() const noexcept {
      return query_;
    }

    [[nodiscard]] std::string_view fragment() const noexcept {
      return fragment_;
    }

    [[nodiscard]] bool has_scheme() const noexcept {
      return !scheme_.empty();
    }

    [[nodiscard]] bool has_authority() const noexcept {
      return has_authority_;
    }

    [[nodiscard]] bool has_userinfo() const noexcept {
      return !userinfo_.empty();
    }

    [[nodiscard]] bool has_port() const noexcept {
      return !port_.empty();
    }

    [[nodiscard]] bool has_query() const noexcept {
      return !query_.empty();
    }

    [[nodiscard]] bool has_fragment() const noexcept {
      return !fragment_.empty();
    }

    void set_scheme(std::string scheme) {
      scheme_ = std::move(scheme);
    }

    void set_userinfo(std::string userinfo) {
      userinfo_ = std::move(userinfo);
    }

    void set_host(std::string host) {
      host_ = std::move(host);
    }

    void set_port(std::string port) {
      port_ = std::move(port);
    }

    void set_path(std::string path) {
      path_ = std::move(path);
    }

    void set_query(std::string query) {
      query_ = std::move(query);
    }

    void set_fragment(std::string fragment) {
      fragment_ = std::move(fragment);
    }

    [[nodiscard]] bool empty() const noexcept {
      return scheme_.empty() && !has_authority_ && userinfo_.empty() && host_.empty() && port_.empty() && path_.empty() &&
             query_.empty() && fragment_.empty();
    }

   private:
    std::string scheme_;
    std::string userinfo_;
    std::string host_;
    std::string port_;
    std::string path_;
    std::string query_;
    std::string fragment_;
    bool has_authority_{};
  };

} // namespace aero::urls

#include "aero/urls/impl/url_parser.ipp"
