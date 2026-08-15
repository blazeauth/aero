#include "aero/util/utf8.hpp"

#include "aero/util/string.hpp"

#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <ut/ut.hpp>

using namespace ut;
using namespace std::string_view_literals;

namespace {

  namespace codepoints {
    constexpr auto pound_sign = "\xC2\xA3"sv;           // U+00A3
    constexpr auto euro_sign = "\xE2\x82\xAC"sv;        // U+20AC
    constexpr auto hangul_han = "\xED\x95\x9C"sv;       // U+D55C
    constexpr auto gothic_hwair = "\xF0\x90\x8D\x88"sv; // U+10348
    constexpr auto poo = "\xF0\x9F\x92\xA9"sv;          // U+1F4A9
    constexpr auto byte_order_mark = "\xEF\xBB\xBF"sv;  // U+FEFF
    constexpr auto replacement = "\xEF\xBF\xBD"sv;      // U+FFFD
  } // namespace codepoints

  std::string to_hex(std::string_view sequence) {
    return aero::to_hex_string(std::as_bytes(std::span{sequence}));
  }

  void expect_ill_formed(std::string_view sequence) {
    aero::utf8_stream_validator validator;

    expect(not validator.consume(sequence)) << "consume accepted " << to_hex(sequence);
    expect(not validator.complete()) << "complete after rejecting " << to_hex(sequence);
    expect(not validator.consume("a")) << "consume recovered after rejecting " << to_hex(sequence);
    expect(not aero::is_valid_utf8(sequence)) << "is_valid_utf8 accepted " << to_hex(sequence);
  }

  void expect_stream_incomplete(std::string_view sequence) {
    aero::utf8_stream_validator validator;

    expect(validator.consume(sequence)) << "consume rejected " << to_hex(sequence);
    expect(not validator.complete()) << "complete for truncated " << to_hex(sequence);
    expect(not aero::is_valid_utf8(sequence)) << "is_valid_utf8 accepted truncated " << to_hex(sequence);
  }

  void expect_invalid_across_chunks(std::string_view first, std::string_view second) {
    aero::utf8_stream_validator validator;

    expect(validator.consume(first)) << "rejected " << to_hex(first);
    expect(not validator.complete()) << "reported complete for " << to_hex(first);
    expect(not validator.consume(second)) << "accepted " << to_hex(first) << " followed by " << to_hex(second);
  }

  void expect_valid_for_all_splits(std::string_view sequence) {
    for (std::size_t split = 0; split <= sequence.size(); ++split) {
      aero::utf8_stream_validator validator;

      expect(validator.consume(sequence.substr(0, split)))
        << "rejected the first " << split << " bytes of " << to_hex(sequence);

      if (split > 0 && split < sequence.size()) {
        expect(not validator.complete()) << "reported complete after " << split << " bytes of " << to_hex(sequence);
      }

      expect(validator.consume(sequence.substr(split))) << "rejected the tail of " << to_hex(sequence) << " split at " << split;
      expect(validator.complete()) << "reported incomplete for " << to_hex(sequence) << " split at " << split;
    }
  }

} // namespace

int main() {
  suite utf8_validation = [] {
    "accepts ascii text"_test = [] {
      expect(aero::is_valid_utf8(""));
      expect(aero::is_valid_utf8("Hello World"));
      expect(aero::is_valid_utf8(std::string(1000, 'a')));
    };

    "rejects a lone continuation byte in ascii text"_test = [] {
      std::string str = "Hello";
      str += static_cast<char>(0x80);
      str += "World";

      expect(not aero::is_valid_utf8(str));
    };

    "accepts two-byte sequences"_test = [] {
      expect(aero::is_valid_utf8(codepoints::pound_sign));
      expect(aero::is_valid_utf8("a" + std::string{codepoints::pound_sign} + "b"));
    };

    "accepts three-byte sequences"_test = [] {
      expect(aero::is_valid_utf8(codepoints::euro_sign));
      expect(aero::is_valid_utf8(codepoints::hangul_han));
    };

    "accepts four-byte sequences"_test = [] {
      expect(aero::is_valid_utf8(codepoints::gothic_hwair));
      expect(aero::is_valid_utf8(codepoints::poo));
    };

    "rejects truncated sequences"_test = [] {
      expect(not aero::is_valid_utf8("\xC2"sv));
      expect(not aero::is_valid_utf8("\xE2\x82"sv));
      expect(not aero::is_valid_utf8("\xF0\x9F\x92"sv));
    };

    "validates input around the 8-byte block boundary"_test = [] {
      expect(aero::is_valid_utf8("1234567"));
      expect(aero::is_valid_utf8("12345678"));
      expect(aero::is_valid_utf8("123456789"));

      const std::string lead_in_last_byte_of_block(7, 'a');
      expect(aero::is_valid_utf8(lead_in_last_byte_of_block + std::string{codepoints::pound_sign}));

      std::string trailing_fault = "1234567";
      trailing_fault += static_cast<char>(0xFF);
      expect(not aero::is_valid_utf8(trailing_fault));

      std::string leading_fault(1, static_cast<char>(0xFF));
      leading_fault += "1234567";
      expect(not aero::is_valid_utf8(leading_fault));
    };

    "accepts long input on the bulk path"_test = [] {
      const std::string ascii_run(200, 'x');

      expect(aero::is_valid_utf8(ascii_run));
      expect(aero::is_valid_utf8(ascii_run + std::string{codepoints::euro_sign} + std::string{codepoints::poo} + ascii_run));
    };

    "rejects ill-formed sequences on the bulk path"_test = [] {
      // The bulk path keeps its own copy of the lead byte rules, so each one needs
      // an input long enough to reach it
      constexpr std::string_view faults[] = {
        "\xC0\x80"sv,         // overlong two-byte
        "\xE0\x80\x80"sv,     // overlong three-byte
        "\xED\xA0\x80"sv,     // UTF-16 surrogate
        "\xF0\x8F\xBF\xBF"sv, // overlong four-byte
        "\xF4\x90\x80\x80"sv, // above U+10FFFF
        "\xF5\x80\x80\x80"sv, // never a valid lead
      };

      for (std::string_view fault : faults) {
        for (std::size_t prefix : {31U, 32U, 33U, 63U, 64U, 65U, 127U, 128U}) {
          std::string str(prefix, 'x');
          str += fault;
          str += std::string(100, 'y');

          expect(not aero::is_valid_utf8(str)) << "accepted " << to_hex(fault) << " after " << prefix << " ascii bytes";
        }
      }
    };
  };

  suite utf8_stream_validation = [] {
    "completes a sequence split across consume calls"_test = [] {
      aero::utf8_stream_validator validator;

      expect(validator.consume("\xF0\x9F"sv));
      expect(not validator.complete());
      expect(validator.consume("\x92\xA9"sv));
      expect(validator.complete());

      validator.reset();
      expect(validator.consume("\xE2\x82"sv));
      expect(not validator.complete());
      expect(validator.consume("\xAC hello"sv));
      expect(validator.complete());
    };

    "accepts boundary sequences split at every offset"_test = [] {
      expect_valid_for_all_splits("\x7F"sv);
      expect_valid_for_all_splits("\xC2\x80"sv);
      expect_valid_for_all_splits("\xDF\xBF"sv);
      expect_valid_for_all_splits("\xE0\xA0\x80"sv);
      expect_valid_for_all_splits("\xED\x9F\xBF"sv);
      expect_valid_for_all_splits("\xEE\x80\x80"sv);
      expect_valid_for_all_splits("\xEF\xBF\xBF"sv);
      expect_valid_for_all_splits("\xF0\x90\x80\x80"sv);
      expect_valid_for_all_splits("\xF4\x8F\xBF\xBF"sv);
    };

    "rejects sequences outside the valid ranges"_test = [] {
      expect_ill_formed("\x80"sv);
      expect_ill_formed("\xBF"sv);

      expect_ill_formed("\xC0\x80"sv);
      expect_ill_formed("\xC1\xBF"sv);

      expect_ill_formed("\xE0\x80\x80"sv);
      expect_ill_formed("\xE0\x9F\xBF"sv);

      expect_ill_formed("\xED\xA0\x80"sv);
      expect_ill_formed("\xED\xBF\xBF"sv);

      expect_ill_formed("\xF0\x80\x80\x80"sv);
      expect_ill_formed("\xF0\x8F\xBF\xBF"sv);

      expect_ill_formed("\xF4\x90\x80\x80"sv);
      expect_ill_formed("\xF4\xBF\xBF\xBF"sv);

      expect_ill_formed("\xF5\x80\x80\x80"sv);
      expect_ill_formed("\xFE"sv);
      expect_ill_formed("\xFF"sv);
    };

    "rejects a non-continuation byte in every continuation position"_test = [] {
      expect_ill_formed("\xC2\x20"sv);

      expect_ill_formed("\xE2\x20\x80"sv);
      expect_ill_formed("\xE2\x82\x20"sv);

      expect_ill_formed("\xF0\x20\x80\x80"sv);
      expect_ill_formed("\xF0\x90\x20\x80"sv);
      expect_ill_formed("\xF0\x90\x80\x20"sv);
    };

    "narrowed continuation range survives a chunk boundary"_test = [] {
      // E0, ED, F0 and F4 restrict their first continuation byte below the usual 80-BF
      expect_invalid_across_chunks("\xE0"sv, "\x9F\x80"sv);
      expect_invalid_across_chunks("\xED"sv, "\xA0\x80"sv);

      expect_invalid_across_chunks("\xF0"sv, "\x8F\xBF\xBF"sv);
      expect_invalid_across_chunks("\xF4"sv, "\x90\x80\x80"sv);
    };

    "rejects a non-continuation byte in a later chunk"_test = [] {
      // Past the first continuation byte the bounds are back to 80-BF
      expect_invalid_across_chunks("\xC2"sv, "\x20"sv);
      expect_invalid_across_chunks("\xE2\x82"sv, "\x20"sv);
      expect_invalid_across_chunks("\xF0\x90\x80"sv, "\x20"sv);
    };

    "reports a truncated sequence as incomplete, not invalid"_test = [] {
      expect_stream_incomplete("\xC2"sv);

      expect_stream_incomplete("\xE0"sv);
      expect_stream_incomplete("\xE0\xA0"sv);

      expect_stream_incomplete("\xF0"sv);
      expect_stream_incomplete("\xF0\x90"sv);
      expect_stream_incomplete("\xF0\x90\x80"sv);
    };

    "reset clears both pending and invalid state"_test = [] {
      aero::utf8_stream_validator validator;

      expect(validator.consume(""));
      expect(validator.complete());

      expect(validator.consume("hello"));
      expect(validator.complete());

      expect(validator.consume("\xE2"sv));
      expect(not validator.complete());

      validator.reset();
      expect(validator.complete());
      expect(validator.consume("ok"));
      expect(validator.complete());

      expect(not validator.consume("\x80"sv));
      expect(not validator.complete());

      validator.reset();
      expect(validator.complete());
      expect(validator.consume("ok"));
      expect(validator.complete());
    };

    "carries pending state across bulk-path chunks"_test = [] {
      aero::utf8_stream_validator validator;

      std::string first(100, 'x');
      first += codepoints::poo.substr(0, 2);

      std::string second{codepoints::poo.substr(2)};
      second += std::string(100, 'y');

      expect(validator.consume(first));
      expect(not validator.complete());
      expect(validator.consume(second));
      expect(validator.complete());

      validator.reset();
      std::string bad_second{"\x92\x20"sv};
      bad_second += std::string(100, 'y');

      expect(validator.consume(first));
      expect(not validator.consume(bad_second));
    };
  };

  suite utf8_ill_formed_replacement = [] {
    // Follows WHATWG "UTF-8 decode without BOM"
    // https://encoding.spec.whatwg.org/#utf-8-decode-without-bom

    "leaves well-formed input unchanged"_test = [] {
      const std::string samples[] = {
        "",
        "Hello World",
        "a" + std::string{codepoints::pound_sign} + "b",
        std::string{codepoints::euro_sign},
        std::string{codepoints::poo},
        std::string(200, 'a') + std::string{codepoints::euro_sign} + std::string{codepoints::poo} + std::string(200, 'b'),
      };

      for (const std::string& sample : samples) {
        expect(aero::utf8_replace_ill_formed(sample) == sample) << "rewrote " << to_hex(sample);
      }
    };

    "does not strip a leading BOM"_test = [] {
      // "without BOM" means no BOM handling at all, so U+FEFF stays as ordinary text
      const std::string with_bom = std::string{codepoints::byte_order_mark} + "text";
      expect(aero::utf8_replace_ill_formed(with_bom) == with_bom);
    };

    "replaces each ill-formed subpart with one U+FFFD"_test = [] {
      auto replacements = [](std::size_t count) {
        std::string result;
        for (std::size_t i{}; i < count; ++i) {
          result += codepoints::replacement;
        }
        return result;
      };

      // Bytes that can never lead
      expect(aero::utf8_replace_ill_formed("\x80"sv) == replacements(1));
      expect(aero::utf8_replace_ill_formed("\xFF"sv) == replacements(1));

      // A sequence truncated by the end of input is one subpart, not one per byte
      expect(aero::utf8_replace_ill_formed("\xE2\x82"sv) == replacements(1));
      expect(aero::utf8_replace_ill_formed("\xF0\x9F\x92"sv) == replacements(1));

      // C0 cannot lead and AF cannot stand alone, so two subparts
      expect(aero::utf8_replace_ill_formed("\xC0\xAF"sv) == replacements(2));

      // E0 requires A0..BF, so <E0> ends there and 80, BF each stand alone
      expect(aero::utf8_replace_ill_formed("\xE0\x80\xBF"sv) == replacements(3));

      // ED requires 80..9F, above that is a UTF-16 surrogate
      expect(aero::utf8_replace_ill_formed("\xED\xA0\x80"sv) == replacements(3));

      // F4 requires 80..8F, above that exceeds U+10FFFF
      expect(aero::utf8_replace_ill_formed("\xF4\x91\x92\x93"sv) == replacements(4));

      // The worked example from the standard: eight replacements, then 'A'
      expect(aero::utf8_replace_ill_formed("\xC0\xAF\xE0\x80\xBF\xF0\x81\x82\x41"sv) == replacements(8) + "A");

      const std::string surrounded = "a\xFF" + std::string{codepoints::euro_sign};
      expect(aero::utf8_replace_ill_formed(surrounded) == "a" + replacements(1) + std::string{codepoints::euro_sign});
    };

    "leaves input unchanged if and only if is_valid_utf8 accepts it"_test = [] {
      // Callers use is_valid_utf8 as a fast path and skip the rewrite when it says
      // yes, which is only sound while the two accept exactly the same byte strings.
      // A lead byte the validator rejects but the rewrite's table copies through
      // would let ill-formed output past both
      std::size_t mismatches = 0;
      std::size_t well_formed_count = 0;
      std::size_t ill_formed_count = 0;

      auto check = [&](std::string_view sequence) {
        const bool valid = aero::is_valid_utf8(sequence);
        const bool unchanged = aero::utf8_replace_ill_formed(sequence) == sequence;

        if (valid) {
          ++well_formed_count;
        } else {
          ++ill_formed_count;
        }

        if (valid != unchanged) {
          ++mismatches;
          if (mismatches == 1) {
            expect(false) << "first mismatch at " << to_hex(sequence);
          }
        }
      };

      std::string buffer;
      for (int byte0 = 0; byte0 <= 0xFF; ++byte0) {
        buffer.assign(1, static_cast<char>(byte0));
        check(buffer);

        for (int byte1 = 0; byte1 <= 0xFF; ++byte1) {
          buffer.assign({static_cast<char>(byte0), static_cast<char>(byte1)});
          check(buffer);
        }
      }

      for (int byte0 = 0xC2; byte0 <= 0xF4; ++byte0) {
        for (int byte1 = 0; byte1 <= 0xFF; ++byte1) {
          for (int byte2 = 0; byte2 <= 0xFF; ++byte2) {
            buffer.assign({static_cast<char>(byte0), static_cast<char>(byte1), static_cast<char>(byte2)});
            check(buffer);

            for (int byte3 : {0x80, 0xBF, 0x41}) {
              buffer.assign(
                {static_cast<char>(byte0), static_cast<char>(byte1), static_cast<char>(byte2), static_cast<char>(byte3)});
              check(buffer);
            }
          }
        }
      }

      expect(mismatches == 0);
      // Without these, a build where every input came out ill-formed would still pass
      expect(well_formed_count > 0);
      expect(ill_formed_count > 0);
    };
  };
}
