#include "aero/detail/ip_address_validator.hpp"

#include <string_view>
#include <ut/ut.hpp>

using namespace ut;

using aero::detail::validate_ipv6_address;

int main() {
  suite ipv6_address_validation = [] {
    "rejects invalid IPv6 literals"_test = [] {
      expect(not validate_ipv6_address(""));
      expect(not validate_ipv6_address(":"));
      expect(not validate_ipv6_address(":::"));
      expect(not validate_ipv6_address(":1"));
      expect(not validate_ipv6_address("1:"));
      expect(not validate_ipv6_address("1:::2"));
      expect(not validate_ipv6_address("1::2::3"));
      expect(not validate_ipv6_address("1:2:3:4:5:6"));
      expect(not validate_ipv6_address("1:2:3:4:5:6:7"));
      expect(not validate_ipv6_address("1:2:3:4:5:6:7:8:9"));
      expect(not validate_ipv6_address("1:2:3:4:5:6:7:8::"));
      expect(not validate_ipv6_address("::1:2:3:4:5:6:7:8"));
      expect(not validate_ipv6_address("1:2:3:4::5:6:7:8"));
      expect(not validate_ipv6_address("1:2:3:4:5::6:7:8"));
      expect(not validate_ipv6_address("1:2:3:4:5:6:7::8"));
      expect(not validate_ipv6_address("12345::1"));
      expect(not validate_ipv6_address("2001:db8:0:0:0:0:0:00000"));
      expect(not validate_ipv6_address("gggg::1"));
      expect(not validate_ipv6_address("2001:db8::-1"));
      expect(not validate_ipv6_address("2001:db8::+1"));
      expect(not validate_ipv6_address("+2001:db8::1"));
      expect(not validate_ipv6_address("2001:db8::0x1"));
      expect(not validate_ipv6_address("2001:db8::1/64"));
      expect(not validate_ipv6_address("2001:db8::1%eth0"));
      expect(not validate_ipv6_address("2001:db8::1%25eth0"));
      expect(not validate_ipv6_address("2001:db8::1?x"));
      expect(not validate_ipv6_address("2001:db8::1#x"));
      expect(not validate_ipv6_address(" 2001:db8::1"));
      expect(not validate_ipv6_address("2001:db8::1 "));
      expect(not validate_ipv6_address("\t2001:db8::1"));
      expect(not validate_ipv6_address("2001:db8::1\t"));
      expect(not validate_ipv6_address("2001:db8::1\n"));
      expect(not validate_ipv6_address("[2001:db8::1]"));
      expect(not validate_ipv6_address("[2001:db8::1]:443"));
      expect(not validate_ipv6_address(std::string_view{"2001:db8::1\0", 12}));
    };

    "accepts valid IPv6 literals without compression"_test = [] {
      expect(validate_ipv6_address("0:0:0:0:0:0:0:0"));
      expect(validate_ipv6_address("0:0:0:0:0:0:0:1"));
      expect(validate_ipv6_address("1:2:3:4:5:6:7:8"));
      expect(validate_ipv6_address("2001:db8:0:0:0:0:0:1"));
      expect(validate_ipv6_address("2001:0DB8:0000:0000:0000:0000:0000:0001"));
      expect(validate_ipv6_address("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
    };

    "accepts valid IPv6 literals with compression"_test = [] {
      expect(validate_ipv6_address("::"));
      expect(validate_ipv6_address("::1"));
      expect(validate_ipv6_address("1::"));
      expect(validate_ipv6_address("FE80::1"));
      expect(validate_ipv6_address("2001::0"));
      expect(validate_ipv6_address("2001:db8::"));
      expect(validate_ipv6_address("2001:db8::1"));
      expect(validate_ipv6_address("2001:db8::ff00:42:8329"));
      expect(validate_ipv6_address("1:2:3:4::6:7:8"));
      expect(validate_ipv6_address("1:2:3:4:5:6:7::"));
      expect(validate_ipv6_address("2001:db8::1:80"));
    };

    "rejects invalid IPv6 literals with IPv4 tail"_test = [] {
      expect(not validate_ipv6_address("192.0.2.1"));
      expect(not validate_ipv6_address("::ffff:999.0.2.1"));
      expect(not validate_ipv6_address("::ffff:192.0.2"));
      expect(not validate_ipv6_address("::ffff:192.0.2.1.5"));
      expect(not validate_ipv6_address("::ffff:192.168.001.1"));
      expect(not validate_ipv6_address("::ffff:192.0.2.01"));
      expect(not validate_ipv6_address("::ffff:0x7f.0.0.1"));
      expect(not validate_ipv6_address("::ffff:192.0.2.1."));
      expect(not validate_ipv6_address("::ffff:192.0.2.1:80"));
      expect(not validate_ipv6_address("::ffff:192.0.2.-1"));
      expect(not validate_ipv6_address("::ffff:+192.0.2.1"));
      expect(not validate_ipv6_address("::ffff:192.0.2.1 "));
      expect(not validate_ipv6_address("192.0.2.1::"));
      expect(not validate_ipv6_address("2001:db8:192.0.2.1:1"));
      expect(not validate_ipv6_address("1:2:3:4:5:192.0.2.1"));
      expect(not validate_ipv6_address("0:0:0:0:0:0:0:192.0.2.1"));
      expect(not validate_ipv6_address("1:2:3:4:5:6::192.0.2.1"));
      expect(not validate_ipv6_address("::1:2:3:4:5:6:192.0.2.1"));
      expect(not validate_ipv6_address("1:2:3:4:5::6:192.0.2.1"));
    };

    "accepts valid IPv6 literals with IPv4 tail"_test = [] {
      expect(validate_ipv6_address("::0.0.0.0"));
      expect(validate_ipv6_address("::192.0.2.1"));
      expect(validate_ipv6_address("::255.255.255.255"));
      expect(validate_ipv6_address("::ffff:192.0.2.1"));
      expect(validate_ipv6_address("2001:db8::192.0.2.1"));
      expect(validate_ipv6_address("0:0:0:0:0:0:192.0.2.1"));
      expect(validate_ipv6_address("1:2:3:4:5:6:192.0.2.1"));
      expect(validate_ipv6_address("1:2:3:4:5::192.0.2.1"));
      expect(validate_ipv6_address("ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255"));
    };
  };
}
