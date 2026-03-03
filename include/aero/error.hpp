#ifndef AERO_ERROR_HPP
#define AERO_ERROR_HPP

#include <cstdint>
#include <string>
#include <system_error>

#include <asio/error.hpp>

#ifdef AERO_USE_TLS
#include <asio/ssl/error.hpp>
#endif

namespace aero::error {

  enum class basic_error : std::uint8_t {
    not_enough_memory = 1,
    deadlock_would_occur,
  };

  enum class errc : std::uint8_t {
    canceled = 1,
    timeout,
    end_of_stream,
    connection_refused,
    not_connected,
    temporary
  };

  namespace detail {
    class basic_error_category : public std::error_category {
     public:
      [[nodiscard]] const char* name() const noexcept override {
        return "aero.basic_error";
      }

      [[nodiscard]] std::string message(int value) const override {
        switch (static_cast<aero::error::basic_error>(value)) {
        case basic_error::not_enough_memory:
          return "not enough memory";
        case basic_error::deadlock_would_occur:
          return "deadlock would occur";
        default:
          return "unknown basic error";
        }
      }
    };

    class error_condition_category final : public std::error_category {
     public:
      [[nodiscard]] const char* name() const noexcept override {
        return "aero.error_condition";
      }

      [[nodiscard]] std::string message(int value) const override {
        using aero::error::errc;
        switch (static_cast<errc>(value)) {
        case errc::canceled:
          return "operation canceled";
        case errc::timeout:
          return "operation timed out";
        case errc::end_of_stream:
          return "end of stream";
        case errc::connection_refused:
          return "connection refused";
        case errc::not_connected:
          return "not connected";
        case errc::temporary:
          return "temporary failure";
        default:
          return "unknown error condition";
        }
      }

      [[nodiscard]] std::error_condition default_error_condition(int value) const noexcept override {
        return {value, *this};
      }

      [[nodiscard]] bool equivalent(const std::error_code& ec, int value) const noexcept override;
    };
  } // namespace detail

  const inline std::error_category& basic_error_category() noexcept {
    static const detail::basic_error_category category;
    return category;
  }

  const inline std::error_category& error_condition_category() noexcept {
    static const detail::error_condition_category category;
    return category;
  }

  inline std::error_code make_error_code(basic_error code) {
    return {static_cast<int>(code), basic_error_category()};
  }

  inline std::error_condition make_error_condition(aero::error::errc value) noexcept {
    return {static_cast<int>(value), error_condition_category()};
  }

  [[nodiscard]] inline bool detail::error_condition_category::equivalent(const std::error_code& ec, int value) const noexcept {
    using aero::error::errc;

    auto is_any_of = [&ec](auto... values) {
      return ((ec == values) || ...);
    };

    switch (static_cast<errc>(value)) {
    case errc::canceled:
      return is_any_of(asio::error::operation_aborted, std::make_error_condition(std::errc::operation_canceled));
    case errc::timeout:
      return is_any_of(asio::error::timed_out, std::make_error_condition(std::errc::timed_out));
    case errc::end_of_stream:
      return is_any_of(asio::error::eof,
#ifdef AERO_USE_TLS
        asio::ssl::error::stream_truncated,
#endif
        std::make_error_condition(std::errc::broken_pipe),
        std::make_error_condition(std::errc::connection_reset));
    case errc::connection_refused:
      return is_any_of(asio::error::connection_refused, std::make_error_condition(std::errc::connection_refused));
    case errc::not_connected:
      return is_any_of(asio::error::not_connected, std::make_error_condition(std::errc::not_connected));
    case errc::temporary:
      return is_any_of(asio::error::would_block,
        asio::error::try_again,
        std::make_error_condition(std::errc::operation_would_block),
        std::make_error_condition(std::errc::resource_unavailable_try_again),
        std::make_error_condition(std::errc::interrupted));
    default:
      return false;
    }
  }

} // namespace aero::error

template <>
struct std::is_error_code_enum<aero::error::basic_error> : std::true_type {};
template <>
struct std::is_error_condition_enum<aero::error::errc> : std::true_type {};

#endif
