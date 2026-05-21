#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "aero/websocket/detail/utf8_validator.hpp"

#include "test_helpers.hpp"

namespace {

  using aero::tests::websocket::to_byte;
  using aero::tests::websocket::to_string;
  using aero::websocket::detail::is_valid_utf8;
  using aero::websocket::detail::utf8_validator;

  std::vector<std::byte> bytes(std::initializer_list<std::uint8_t> values) {
    std::vector<std::byte> out;
    out.reserve(values.size());
    for (std::uint8_t value : values) {
      out.push_back(to_byte(value));
    }
    return out;
  }

  bool is_complete_valid_utf8(std::span<const std::byte> input) {
    utf8_validator validator;
    return validator.write(input) && validator.finish();
  }

  struct utf8_case {
    std::string_view name;
    std::vector<std::byte> input;
  };

} // namespace

TEST(WebsocketUtf8StreamValidator, EmptyInputStartsValidAndFinished) {
  utf8_validator validator;

  EXPECT_TRUE(validator.valid_so_far());
  EXPECT_FALSE(validator.waiting_for_more_input());
  EXPECT_TRUE(validator.finish());
}

TEST(WebsocketUtf8StreamValidator, AcceptsValidCompleteUtf8AcrossImportantBoundaries) {
  const std::vector<utf8_case> cases{
    {"ascii_nul", bytes({0x00})},
    {"ascii_max", bytes({0x7F})},
    {"two_byte_min", bytes({0xC2, 0x80})},
    {"two_byte_max", bytes({0xDF, 0xBF})},
    {"three_byte_min", bytes({0xE0, 0xA0, 0x80})},
    {"last_scalar_before_surrogates", bytes({0xED, 0x9F, 0xBF})},
    {"first_scalar_after_surrogates", bytes({0xEE, 0x80, 0x80})},
    {"four_byte_min", bytes({0xF0, 0x90, 0x80, 0x80})},
    {"unicode_upper_bound", bytes({0xF4, 0x8F, 0xBF, 0xBF})},
    {"mixed_text_with_embedded_nul", bytes({0x41, 0x00, 0xC2, 0xA2, 0xE2, 0x82, 0xAC, 0xF0, 0x9F, 0x99, 0x82, 0x5A})},
  };

  for (const auto& case_data : cases) {
    SCOPED_TRACE(case_data.name);

    EXPECT_TRUE(is_complete_valid_utf8(case_data.input));
    EXPECT_TRUE(is_valid_utf8(std::span<const std::byte>{case_data.input}));

    const auto text = to_string(case_data.input);
    EXPECT_TRUE(is_valid_utf8(std::string_view{text}));
  }
}

TEST(WebsocketUtf8StreamValidator, IncompletePrefixRemainsValidButNotFinished) {
  utf8_validator validator;
  const auto prefix = bytes({0xE2, 0x82});

  EXPECT_TRUE(validator.write(prefix));
  EXPECT_TRUE(validator.valid_so_far());
  EXPECT_TRUE(validator.waiting_for_more_input());
  EXPECT_FALSE(validator.finish());
}

TEST(WebsocketUtf8StreamValidator, CompletesMultiByteSequencesAcrossChunkBoundaries) {
  const std::vector<utf8_case> cases{
    {"two_byte", bytes({0xC2, 0xA2})},
    {"three_byte", bytes({0xE2, 0x82, 0xAC})},
    {"four_byte", bytes({0xF0, 0x90, 0x8D, 0x88})},
  };

  for (const auto& case_data : cases) {
    SCOPED_TRACE(case_data.name);

    utf8_validator validator;

    for (std::size_t i{}; i < case_data.input.size(); ++i) {
      const std::array chunk{case_data.input[i]};
      EXPECT_TRUE(validator.write(chunk));
      EXPECT_TRUE(validator.valid_so_far());

      if (i + 1U == case_data.input.size()) {
        EXPECT_FALSE(validator.waiting_for_more_input());
        EXPECT_TRUE(validator.finish());
      } else {
        EXPECT_TRUE(validator.waiting_for_more_input());
        EXPECT_FALSE(validator.finish());
      }
    }
  }
}

TEST(WebsocketUtf8StreamValidator, InvalidContinuationMakesStateStickyUntilReset) {
  utf8_validator validator;

  EXPECT_TRUE(validator.write(bytes({0xE2, 0x82})));
  EXPECT_TRUE(validator.waiting_for_more_input());

  EXPECT_FALSE(validator.write(bytes({0x41})));
  EXPECT_FALSE(validator.valid_so_far());
  EXPECT_FALSE(validator.waiting_for_more_input());
  EXPECT_FALSE(validator.finish());

  EXPECT_FALSE(validator.write(bytes({0x41})));

  validator.reset();

  EXPECT_TRUE(validator.valid_so_far());
  EXPECT_FALSE(validator.waiting_for_more_input());
  EXPECT_TRUE(validator.finish());
  EXPECT_TRUE(validator.write(bytes({0x41})));
  EXPECT_TRUE(validator.finish());
}

TEST(WebsocketUtf8StreamValidator, ResetAlsoClearsPendingIncompleteSequence) {
  utf8_validator validator;

  EXPECT_TRUE(validator.write(bytes({0xF0, 0x90, 0x8D})));
  EXPECT_TRUE(validator.waiting_for_more_input());
  EXPECT_FALSE(validator.finish());

  validator.reset();

  EXPECT_TRUE(validator.valid_so_far());
  EXPECT_FALSE(validator.waiting_for_more_input());
  EXPECT_TRUE(validator.finish());
  EXPECT_TRUE(validator.write(bytes({0x42})));
  EXPECT_TRUE(validator.finish());
}

TEST(WebsocketUtf8StreamValidator, AcceptsU8StringViewInput) {
  utf8_validator validator;
  constexpr std::u8string_view text = u8"Hello \u00A2 \u20AC \U00010348";

  EXPECT_TRUE(validator.write(text));
  EXPECT_TRUE(validator.valid_so_far());
  EXPECT_FALSE(validator.waiting_for_more_input());
  EXPECT_TRUE(validator.finish());
}

TEST(WebsocketUtf8Validation, RejectsMalformedAndTruncatedCompleteInputs) {
  const std::vector<utf8_case> cases{
    {"lone_continuation", bytes({0x80})},
    {"overlong_nul_two_byte", bytes({0xC0, 0x80})},
    {"overlong_ascii_three_byte", bytes({0xE0, 0x80, 0xAF})},
    {"overlong_ascii_four_byte", bytes({0xF0, 0x80, 0x80, 0xAF})},
    {"utf16_surrogate", bytes({0xED, 0xA0, 0x80})},
    {"above_unicode_maximum", bytes({0xF4, 0x90, 0x80, 0x80})},
    {"illegal_leading_byte", bytes({0xF5, 0x80, 0x80, 0x80})},
    {"expected_continuation_got_ascii", bytes({0xE2, 0x28, 0xA1})},
    {"expected_continuation_got_new_lead", bytes({0xE2, 0x82, 0xC2})},
    {"truncated_two_byte_sequence", bytes({0xC2})},
    {"truncated_three_byte_sequence", bytes({0xE2, 0x82})},
    {"truncated_four_byte_sequence", bytes({0xF0, 0x9F, 0x92})},
  };

  for (const auto& case_data : cases) {
    SCOPED_TRACE(case_data.name);

    EXPECT_FALSE(is_complete_valid_utf8(case_data.input));
    EXPECT_FALSE(is_valid_utf8(std::span<const std::byte>{case_data.input}));

    const auto text = to_string(case_data.input);
    EXPECT_FALSE(is_valid_utf8(std::string_view{text}));
  }
}

TEST(WebsocketUtf8Validation, DistinguishesValidPrefixFromValidCompleteString) {
  const auto incomplete = bytes({0xE2, 0x82});

  utf8_validator validator;
  EXPECT_TRUE(validator.write(incomplete));
  EXPECT_TRUE(validator.valid_so_far());
  EXPECT_TRUE(validator.waiting_for_more_input());
  EXPECT_FALSE(validator.finish());

  EXPECT_FALSE(is_valid_utf8(std::span<const std::byte>{incomplete}));

  const auto text = to_string(incomplete);
  EXPECT_FALSE(is_valid_utf8(std::string_view{text}));
}
