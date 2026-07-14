#include "aero/detail/ip_address_validator.hpp"
#include "aero/http/detail/request_target_validator.hpp"

#include <string_view>
#include <ut/ut.hpp>

using namespace ut;

using aero::detail::is_valid_ipv4_address;

int main() {
  suite ipv4_address_validation = [] {
    "rejects invalid IPv4 literals"_test = [] {
      expect(not is_valid_ipv4_address(""));
      expect(not is_valid_ipv4_address("256.0.0.1"));
      expect(not is_valid_ipv4_address("1.2.3.256"));
      expect(not is_valid_ipv4_address("999.0.0.1"));
      expect(not is_valid_ipv4_address("1.2.3.4:80"));
      expect(not is_valid_ipv4_address("1.2.3"));
      expect(not is_valid_ipv4_address("1.2.3.4.5"));
      expect(not is_valid_ipv4_address("1..2.3"));
      expect(not is_valid_ipv4_address("1.2..4"));
      expect(not is_valid_ipv4_address(".1.2.3.4"));
      expect(not is_valid_ipv4_address("1.2.3.4."));
      expect(not is_valid_ipv4_address("01.2.3.4"));
      expect(not is_valid_ipv4_address("00.2.3.4"));
      expect(not is_valid_ipv4_address("1.02.3.4"));
      expect(not is_valid_ipv4_address("1.2.003.4"));
      expect(not is_valid_ipv4_address("001.2.3.4"));
      expect(not is_valid_ipv4_address("1.2.3.04"));
      expect(not is_valid_ipv4_address("1.2.3.004"));
      expect(not is_valid_ipv4_address("1.2.3.1234"));
      expect(not is_valid_ipv4_address("1.2.3.-4"));
      expect(not is_valid_ipv4_address("+1.2.3.4"));
      expect(not is_valid_ipv4_address("1.+2.3.4"));
      expect(not is_valid_ipv4_address(" 1.2.3.4"));
      expect(not is_valid_ipv4_address("1.2.3.4 "));
      expect(not is_valid_ipv4_address("\t1.2.3.4"));
      expect(not is_valid_ipv4_address("1.2.3.4\t"));
      expect(not is_valid_ipv4_address("1.2.3.4\n"));
      expect(not is_valid_ipv4_address("b1.2.3.4"));
      expect(not is_valid_ipv4_address("a.e.r.o"));
      expect(not is_valid_ipv4_address("1.2.a.4"));
      expect(not is_valid_ipv4_address("1.2.3.4/32"));
      expect(not is_valid_ipv4_address("[1.2.3.4]"));
      expect(not is_valid_ipv4_address(std::string_view{"1.2.3.4\0", 8}));
    };

    "rejects legacy IPv4 forms"_test = [] {
      expect(not is_valid_ipv4_address("127.1"));
      expect(not is_valid_ipv4_address("127.0.1"));
      expect(not is_valid_ipv4_address("2130706433"));
      expect(not is_valid_ipv4_address("0177.0.0.1"));
      expect(not is_valid_ipv4_address("127.000.000.001"));
      expect(not is_valid_ipv4_address("0x7f.0.0.1"));
      expect(not is_valid_ipv4_address("0x7f000001"));
    };

    "accepts valid IPv4 literals"_test = [] {
      expect(is_valid_ipv4_address("0.0.0.0"));
      expect(is_valid_ipv4_address("9.10.99.100"));
      expect(is_valid_ipv4_address("127.0.0.1"));
      expect(is_valid_ipv4_address("1.2.3.4"));
      expect(is_valid_ipv4_address("192.168.0.1"));
      expect(is_valid_ipv4_address("199.200.249.250"));
      expect(is_valid_ipv4_address("255.255.255.255"));
    };
  };
}
