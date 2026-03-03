#ifndef AERO_NET_ERRORS_HPP
#define AERO_NET_ERRORS_HPP

#include <cstdint>
#include <system_error>
#include <type_traits>

namespace aero::net::error {

  enum class connect_error : std::uint8_t {
    host_resolve_failed = 1,
    host_not_found,
    host_invalid,
  };

  namespace detail {

    class connect_error_category : public std::error_category {
     public:
      [[nodiscard]] const char* name() const noexcept override {
        return "aero.net.connect_error";
      }

      [[nodiscard]] std::string message(int value) const override {
        using aero::net::error::connect_error;
        switch (static_cast<connect_error>(value)) {
        case connect_error::host_resolve_failed:
          return "host resolve failed";
        case connect_error::host_not_found:
          return "host not found";
        case connect_error::host_invalid:
          return "host invalid";
        default:
          return "unknown connect error";
        }
      }
    };

  } // namespace detail

  const inline std::error_category& connect_error_category() noexcept {
    static const detail::connect_error_category category;
    return category;
  }

  inline std::error_code make_error_code(connect_error value) {
    return {static_cast<int>(value), connect_error_category()};
  }

} // namespace aero::net::error

template <>
struct std::is_error_code_enum<aero::net::error::connect_error> : std::true_type {};

#endif
