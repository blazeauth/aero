#pragma once

#include <cstdint>
#include <system_error>
#include <type_traits>

namespace aero::urls {

  enum class url_error : std::uint8_t {
    scheme_invalid = 1,
    authority_invalid,
    host_invalid,
    port_invalid,
    userinfo_invalid,
    userinfo_not_allowed,
    path_invalid,
    query_invalid,
    fragment_invalid,
  };

  namespace detail {

    class url_error_category final : public std::error_category {
     public:
      [[nodiscard]] const char* name() const noexcept override {
        return "aero.urls.url_error";
      }

      [[nodiscard]] std::string message(int code) const override {
        switch (static_cast<url_error>(code)) {
        case url_error::scheme_invalid:
          return "scheme is invalid";
        case url_error::authority_invalid:
          return "authority is invalid";
        case url_error::host_invalid:
          return "host is invalid";
        case url_error::port_invalid:
          return "port is invalid";
        case url_error::userinfo_invalid:
          return "userinfo is invalid";
        case url_error::userinfo_not_allowed:
          return "userinfo not allowed";
        case url_error::path_invalid:
          return "path is invalid";
        case url_error::query_invalid:
          return "query is invalid";
        case url_error::fragment_invalid:
          return "fragment is invalid";
        default:
          return "unknown url error";
        }
      }
    };

  } // namespace detail

  [[nodiscard]] const inline std::error_category& url_error_category() noexcept {
    static const detail::url_error_category instance{};
    return instance;
  }

  [[nodiscard]] inline std::error_code make_error_code(url_error code) noexcept {
    return std::error_code{static_cast<int>(code), url_error_category()};
  }

} // namespace aero::urls

template <>
struct std::is_error_code_enum<aero::urls::url_error> : std::true_type {};
