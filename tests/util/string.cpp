#include "aero/util/string.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <ut/ut.hpp>

using namespace ut;

using aero::striequal;

int main() {
  suite string_striequal = [] {
    "compares equal strings ignoring ascii case"_test = [] {
      expect(striequal("", ""));
      expect(striequal("a", "a"));
      expect(striequal("a", "A"));
      expect(striequal("GET", "get"));
      expect(striequal("HTTP://", "http://"));
      expect(striequal("hTtP/1.1", "Http/1.1"));
      expect(striequal("content-length", "Content-Length"));
      expect(striequal("0123456789", "0123456789"));
      expect(striequal("!#$%&'*+-.^_`|~", "!#$%&'*+-.^_`|~"));
    };

    "rejects different strings"_test = [] {
      expect(not striequal("a", "b"));
      expect(not striequal("GET", "PUT"));
      expect(not striequal("http://", "https:/"));
      expect(not striequal("content-length", "content-range!"));
    };

    "rejects strings of different lengths"_test = [] {
      expect(not striequal("", "a"));
      expect(not striequal("a", ""));
      expect(not striequal("GET", "GETS"));
      expect(not striequal("content-length", "content-length "));
    };

    // The runtime path folds full 8-byte blocks and the remainder separately
    "compares strings around the 8-byte block boundary"_test = [] {
      for (std::size_t length : {1U, 7U, 8U, 9U, 15U, 16U, 17U, 64U}) {
        std::string upper(length, 'A');
        std::string lower(length, 'a');
        expect(striequal(upper, lower)) << "failed for length " << length;

        std::string different = lower;
        different.back() = 'b';
        expect(not striequal(upper, different)) << "failed for length " << length;

        different = lower;
        different.front() = 'b';
        expect(not striequal(upper, different)) << "failed for length " << length;
      }
    };

    "does not fold non-letter characters that differ by the case bit"_test = [] {
      expect(not striequal("@", "`"));
      expect(not striequal("[", "{"));
      expect(not striequal("\\", "|"));
      expect(not striequal("]", "}"));
      expect(not striequal("^", "~"));
      expect(not striequal("@@@@@@@@", "````````"));
      expect(not striequal("[[[[[[[[[", "{{{{{{{{{"));
    };

    "does not fold non-ascii bytes"_test = [] {
      // 0xC3 and 0xE3 also differ only by the case bit, but the high bit
      // must exclude them from folding
      std::string upper_like{static_cast<char>(0xC3)};
      std::string lower_like{static_cast<char>(0xE3)};
      expect(striequal(upper_like, upper_like));
      expect(not striequal(upper_like, lower_like));

      // "é" vs "É" in UTF-8, an ascii-only comparison must not equate them
      expect(not striequal("caf\xC3\xA9", "caf\xC3\x89"));
    };

    "compares embedded nul bytes exactly"_test = [] {
      expect(striequal(std::string_view{"a\0b", 3}, std::string_view{"A\0B", 3}));
      expect(not striequal(std::string_view{"a\0b", 3}, std::string_view{"a\0c", 3}));
    };

    "compares at compile time"_test = [] {
      constexpr bool folds_case = striequal("Content-Length", "content-length");
      constexpr bool detects_difference = striequal("GET", "PUT");
      expect(folds_case);
      expect(not detects_difference);
    };
  };
}
