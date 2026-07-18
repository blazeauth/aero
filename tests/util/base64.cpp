#include "aero/util/base64.hpp"
#include <ut/ut.hpp>

using namespace ut;

int main() {
  suite base64 = [] {
    "encodes to base64"_test = [] {
      constexpr std::string_view string{"one, two, three, four, five"};
      constexpr std::string_view string_b64{"b25lLCB0d28sIHRocmVlLCBmb3VyLCBmaXZl"};

      expect(aero::base64_encode(string) == string_b64);
      expect(aero::base64_encode(std::as_bytes(std::span{string})) == string_b64);
    };

    "decodes from base64"_test = [] {
      constexpr std::string_view string_b64{"b25lLCB0d28sIHRocmVlLCBmb3VyLCBmaXZl"};
      constexpr std::string_view string{"one, two, three, four, five"};

      expect(aero::base64_decode(string_b64) == string);
      expect(aero::base64_decode(std::as_bytes(std::span{string_b64})) == string);
    };

    "round-trips every padding case"_test = [] {
      constexpr std::string_view input{"aero!"};

      for (std::size_t length = 0; length <= input.size(); ++length) {
        const std::string_view slice = input.substr(0, length);
        expect(aero::base64_decode(aero::base64_encode(slice)) == slice) << "failed for length " << length;
      }
    };

    "encodes and decodes empty input to empty string"_test = [] {
      expect(aero::base64_encode(std::string_view{}).empty());
      expect(aero::base64_decode(std::string_view{}).empty());
    };
  };
}
