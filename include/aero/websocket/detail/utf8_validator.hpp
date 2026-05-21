#pragma once

#include <algorithm>
#include <span>
#include <string_view>

namespace aero::websocket::detail {

  class utf8_validator {
   public:
    bool write(std::span<const std::byte> bytes) noexcept {
      if (!valid_) {
        return false;
      }

      return std::ranges::all_of(bytes, [this](std::byte byte) {
        if (!consume(std::to_integer<std::uint8_t>(byte))) {
          valid_ = false;
          return false;
        }

        return true;
      });
    }

    bool write(std::string_view text) noexcept {
      return write(std::as_bytes(std::span{text.data(), text.size()}));
    }

    bool write(std::u8string_view text) noexcept {
      return write(std::as_bytes(std::span{text.data(), text.size()}));
    }

    [[nodiscard]] bool finish() const noexcept {
      return valid_ && expected_continuations_ == 0;
    }

    [[nodiscard]] bool valid_so_far() const noexcept {
      return valid_;
    }

    [[nodiscard]] bool waiting_for_more_input() const noexcept {
      return valid_ && expected_continuations_ != 0;
    }

    void reset() noexcept {
      expected_continuations_ = 0;
      minimum_next_ = 0x80;
      maximum_next_ = 0xBF;
      valid_ = true;
    }

   private:
    bool consume(std::uint8_t value) noexcept {
      if (expected_continuations_ == 0) {
        return consume_leading_byte(value);
      }

      if (value < minimum_next_ || value > maximum_next_) {
        return false;
      }

      --expected_continuations_;
      minimum_next_ = 0x80;
      maximum_next_ = 0xBF;

      return true;
    }

    bool consume_leading_byte(std::uint8_t value) noexcept {
      if (value <= 0x7F) {
        return true;
      }

      if (value >= 0xC2 && value <= 0xDF) {
        expect_continuations(1, 0x80, 0xBF);
        return true;
      }

      if (value == 0xE0) {
        expect_continuations(2, 0xA0, 0xBF);
        return true;
      }

      if (value >= 0xE1 && value <= 0xEC) {
        expect_continuations(2, 0x80, 0xBF);
        return true;
      }

      if (value == 0xED) {
        expect_continuations(2, 0x80, 0x9F);
        return true;
      }

      if (value >= 0xEE && value <= 0xEF) {
        expect_continuations(2, 0x80, 0xBF);
        return true;
      }

      if (value == 0xF0) {
        expect_continuations(3, 0x90, 0xBF);
        return true;
      }

      if (value >= 0xF1 && value <= 0xF3) {
        expect_continuations(3, 0x80, 0xBF);
        return true;
      }

      if (value == 0xF4) {
        expect_continuations(3, 0x80, 0x8F);
        return true;
      }

      return false;
    }

    void expect_continuations(std::uint8_t count, std::uint8_t minimum_next, std::uint8_t maximum_next) noexcept {
      expected_continuations_ = count;
      minimum_next_ = minimum_next;
      maximum_next_ = maximum_next;
    }

    std::uint8_t expected_continuations_{};
    std::uint8_t minimum_next_{0x80};
    std::uint8_t maximum_next_{0xBF};
    bool valid_{true};
  };

  [[nodiscard]] inline bool is_valid_utf8(std::string_view str) {
    utf8_validator validator;
    return validator.write(str) && validator.finish();
  }

  [[nodiscard]] inline bool is_valid_utf8(std::span<const std::byte> bytes) {
    utf8_validator validator;
    return validator.write(bytes) && validator.finish();
  }

} // namespace aero::websocket::detail
