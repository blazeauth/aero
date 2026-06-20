#include "aero/base64/base64.hpp"
#include "ut.hpp"

ut::suite base64_encode = [] {
  "encodes to base64"_test = [] {
    constexpr std::string_view string{"one, two, three, four, five"};
    constexpr std::string_view string_b64{"b25lLCB0d28sIHRocmVlLCBmb3VyLCBmaXZl"};

    expect(aero::base64_encode(string) == string_b64);
    expect(aero::base64_encode(std::as_bytes(std::span{string})) == string_b64);
  };
};

int main() {}
