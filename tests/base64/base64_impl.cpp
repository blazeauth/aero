#include "aero/base64/impl/base64.hpp"
#include <ut/ut.hpp>

using namespace ut;

int main() {
  suite base64_impl = [] {
    "encodes to base64"_test = [] {
      constexpr std::string_view string{"one, two, three, four, five"};
      constexpr std::string_view string_b64{"b25lLCB0d28sIHRocmVlLCBmb3VyLCBmaXZl"};

      expect(aero::detail::base64_encode(string) == string_b64);
      expect(aero::detail::base64_encode(std::as_bytes(std::span{string})) == string_b64);
    };
  };
}
