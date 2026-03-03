#ifndef AERO_TESTS_ERROR_CODE_TEST_HELPER_HPP
#define AERO_TESTS_ERROR_CODE_TEST_HELPER_HPP

#include <magic_enum/magic_enum.hpp>

#include <format>
#include <system_error>

namespace aero::tests {

  template <typename ErrorEnum>
    requires(std::is_enum_v<ErrorEnum>)
  inline void test_enum_error_code_messages(const std::error_category& cat) {
    auto unknown_error_message = std::error_code{-1, cat}.message();

    for (auto [status, name] : magic_enum::enum_entries<ErrorEnum>()) {
      auto error_message = std::error_code{status}.message();
      auto is_unknown_error = error_message == unknown_error_message;

      EXPECT_FALSE(is_unknown_error) << std::format("Error code '{}' ({}) does not have an error message",
        name,
        std::to_underlying(status));
    }
  }

  template <typename ErrorEnum>
    requires(std::is_enum_v<ErrorEnum>)
  inline void test_enum_error_condition_messages(const std::error_category& cat) {
    auto unknown_error_message = std::error_code{-1, cat}.message();

    for (auto [status, name] : magic_enum::enum_entries<ErrorEnum>()) {
      auto error_message = std::error_condition{status}.message();
      auto is_unknown_error = error_message == unknown_error_message;

      EXPECT_FALSE(is_unknown_error) << std::format("Error condition '{}' ({}) does not have an error message",
        name,
        std::to_underlying(status));
    }
  }

} // namespace aero::tests

#endif
