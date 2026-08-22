#include "aero/urls/detail/authority.hpp"

#include <format>
#include <string_view>

#include <magic_enum/magic_enum.hpp>
#include <ut/ut.hpp>

namespace urls = aero::urls;
using namespace ut;

namespace {

  struct authority_parts {
    std::string_view userinfo;
    std::string_view host;
    std::string_view port;
    urls::detail::host_type type{urls::detail::host_type::reg_name};
  };

  void expect_parsed_as(std::string_view authority, authority_parts expected, urls::detail::authority_parser_opts opts = {}) {
    auto parsed = urls::detail::parse_authority(authority, opts);

    if (not parsed.has_value()) {
      expect(false) << std::format("'{}' was rejected: {}", authority, parsed.error().message());
      return;
    }

    expect(parsed->userinfo == expected.userinfo)
      << std::format("userinfo of '{}' is '{}', expected '{}'", authority, parsed->userinfo, expected.userinfo);
    expect(parsed->host == expected.host) << std::format("host of '{}' is '{}', expected '{}'",
      authority,
      parsed->host,
      expected.host);
    expect(parsed->port == expected.port) << std::format("port of '{}' is '{}', expected '{}'",
      authority,
      parsed->port,
      expected.port);
    expect(parsed->type == expected.type) << std::format("host type of '{}' is '{}', expected '{}'",
      authority,
      magic_enum::enum_name(parsed->type),
      magic_enum::enum_name(expected.type));
  }

  void expect_rejected_as(std::string_view authority, urls::url_error expected, urls::detail::authority_parser_opts opts = {}) {
    auto parsed = urls::detail::parse_authority(authority, opts);

    if (parsed.has_value()) {
      expect(false) << std::format("'{}' was accepted as userinfo '{}', host '{}', port '{}'",
        authority,
        parsed->userinfo,
        parsed->host,
        parsed->port);
      return;
    }

    expect(parsed.error() == urls::make_error_code(expected))
      << std::format("'{}' was rejected as: {}", authority, parsed.error().message());
  }

} // namespace

int main() {
  suite authority_parser = [] {
    "parses host without userinfo or port"_test = [] {
      expect_parsed_as("example.com", {.host = "example.com", .type = urls::detail::host_type::reg_name});
    };

    "parses host and port"_test = [] {
      expect_parsed_as("example.com:8042", {.host = "example.com", .port = "8042"});
    };

    "parses userinfo, host and port"_test = [] {
      expect_parsed_as("user:pass@example.com:8042", {.userinfo = "user:pass", .host = "example.com", .port = "8042"});
    };

    "parses userinfo without port"_test = [] {
      expect_parsed_as("user@example.com", {.userinfo = "user", .host = "example.com"});
    };

    "parses empty userinfo"_test = [] {
      expect_parsed_as("@example.com", {.host = "example.com"});
    };

    "parses empty port"_test = [] {
      expect_parsed_as("example.com:", {.host = "example.com"});
    };

    "parses IPv4 host"_test = [] {
      expect_parsed_as("127.0.0.1:80", {.host = "127.0.0.1", .port = "80", .type = urls::detail::host_type::ipv4});
    };

    "parses IPv6 literal"_test = [] {
      expect_parsed_as("[::1]", {.host = "[::1]", .type = urls::detail::host_type::ipv6});
    };

    "parses IPv6 literal with port"_test = [] {
      expect_parsed_as("[2001:db8::1]:8042", {.host = "[2001:db8::1]", .port = "8042", .type = urls::detail::host_type::ipv6});
    };

    "classifies host that is not a valid IPv4 address as reg-name"_test = [] {
      expect_parsed_as("999.1.1.1", {.host = "999.1.1.1", .type = urls::detail::host_type::reg_name});
      expect_parsed_as("1.2.3.4.5", {.host = "1.2.3.4.5", .type = urls::detail::host_type::reg_name});
    };

    "accepts pct-encoded octets in userinfo"_test = [] {
      expect_parsed_as("us%20er@example.com", {.userinfo = "us%20er", .host = "example.com"});
    };

    "accepts pct-encoded octets in reg-name"_test = [] {
      expect_parsed_as("ex%41mple.com", {.host = "ex%41mple.com"});
    };

    "accepts sub-delims in reg-name"_test = [] {
      expect_parsed_as("ex!$&'()*+,;=ample", {.host = "ex!$&'()*+,;=ample"});
    };

    "parses empty authority as empty host"_test = [] {
      expect_parsed_as("", {});
    };

    "rejects empty host when not allowed"_test = [] {
      expect_rejected_as("", urls::url_error::authority_invalid, {.allow_empty_host = false});
      expect_rejected_as(":8080", urls::url_error::authority_invalid, {.allow_empty_host = false});
    };

    "accepts port outside 16-bit range"_test = [] {
      expect_parsed_as("example.com:99999999", {.host = "example.com", .port = "99999999"});
    };

    "rejects port outside 16-bit range when the range is validated"_test = [] {
      expect_rejected_as("example.com:99999999",
        urls::url_error::port_invalid,
        {.allowed_port_range = urls::detail::port_range{}});
    };

    "accepts the highest 16-bit port when the range is validated"_test = [] {
      expect_parsed_as("example.com:65535",
        {.host = "example.com", .port = "65535"},
        {.allowed_port_range = urls::detail::port_range{}});
    };

    "rejects port below the range minimum"_test = [] {
      expect_rejected_as("example.com:0",
        urls::url_error::port_invalid,
        {.allowed_port_range = urls::detail::port_range{.min = 1}});
    };

    "accepts port at the range minimum"_test = [] {
      expect_parsed_as("example.com:1",
        {.host = "example.com", .port = "1"},
        {.allowed_port_range = urls::detail::port_range{.min = 1}});
    };

    "empty port is exempt from the range"_test = [] {
      expect_parsed_as("example.com:", {.host = "example.com"}, {.allowed_port_range = urls::detail::port_range{.min = 1}});
    };

    "rejects userinfo when not allowed"_test = [] {
      expect_rejected_as("user@example.com", urls::url_error::userinfo_not_allowed, {.allow_userinfo = false});
    };

    "rejects non-digit port"_test = [] {
      expect_rejected_as("example.com:80a", urls::url_error::port_invalid);
    };

    "rejects space in reg-name"_test = [] {
      expect_rejected_as("exa mple.com", urls::url_error::host_invalid);
    };

    "rejects unclosed IPv6 literal"_test = [] {
      expect_rejected_as("[::1", urls::url_error::authority_invalid);
    };

    "rejects malformed IPv6 address in literal"_test = [] {
      expect_rejected_as("[gg::1]", urls::url_error::host_invalid);
    };

    "rejects IPvFuture literal"_test = [] {
      expect_rejected_as("[v1.a]", urls::url_error::host_invalid);
    };

    "rejects space in userinfo"_test = [] {
      expect_rejected_as("us er@example.com", urls::url_error::userinfo_invalid);
    };

    "rejects truncated pct-encoded octet in userinfo"_test = [] {
      expect_rejected_as("user%2@example.com", urls::url_error::userinfo_invalid);
    };
  };
}
