#include "aero/urls/url.hpp"

#include <format>
#include <optional>
#include <string_view>

#include <ut/ut.hpp>

namespace urls = aero::urls;
using namespace ut;

struct url_parts {
  std::string_view scheme;
  std::string_view userinfo;
  std::string_view host;
  std::string_view port;
  std::string_view path;
  std::string_view query;
  std::string_view fragment;
};

void expect_parsed_as(std::string_view text, url_parts expected, urls::url_parser_opts opts = {}) {
  auto parsed = urls::url::parse(text, opts);

  if (not parsed.has_value()) {
    expect(false) << std::format("'{}' was rejected: {}", text, parsed.error().message());
    return;
  }

  expect(parsed->scheme() == expected.scheme)
    << std::format("scheme of '{}' is '{}', expected '{}'", text, parsed->scheme(), expected.scheme);
  expect(parsed->userinfo() == expected.userinfo)
    << std::format("userinfo of '{}' is '{}', expected '{}'", text, parsed->userinfo(), expected.userinfo);
  expect(parsed->host() == expected.host) << std::format("host of '{}' is '{}', expected '{}'",
    text,
    parsed->host(),
    expected.host);
  expect(parsed->port() == expected.port) << std::format("port of '{}' is '{}', expected '{}'",
    text,
    parsed->port(),
    expected.port);
  expect(parsed->path() == expected.path) << std::format("path of '{}' is '{}', expected '{}'",
    text,
    parsed->path(),
    expected.path);
  expect(parsed->query() == expected.query)
    << std::format("query of '{}' is '{}', expected '{}'", text, parsed->query(), expected.query);
  expect(parsed->fragment() == expected.fragment)
    << std::format("fragment of '{}' is '{}', expected '{}'", text, parsed->fragment(), expected.fragment);
}

void expect_rejected_as(std::string_view text, urls::url_error expected, urls::url_parser_opts opts = {}) {
  auto parsed = urls::url::parse(text, opts);

  if (parsed.has_value()) {
    expect(false) << std::format("'{}' was accepted as scheme '{}', host '{}', path '{}'",
      text,
      parsed->scheme(),
      parsed->host(),
      parsed->path());
    return;
  }

  expect(parsed.error() == urls::make_error_code(expected))
    << std::format("'{}' was rejected as: {}", text, parsed.error().message());
}

int main() {
  suite url_parser = [] {
    "parses every component of a full URI"_test = [] {
      expect_parsed_as("foo://example.com:8042/over/there?name=ferret#nose",
        {
          .scheme = "foo",
          .host = "example.com",
          .port = "8042",
          .path = "/over/there",
          .query = "name=ferret",
          .fragment = "nose",
        });
    };

    "parses URI without path, query and fragment"_test = [] {
      expect_parsed_as("http://example.com", {.scheme = "http", .host = "example.com"});
    };

    "parses URI without authority as path-rootless"_test = [] {
      expect_parsed_as("mailto:user@example.com", {.scheme = "mailto", .path = "user@example.com"});
    };

    "parses URI without authority as path-absolute"_test = [] {
      expect_parsed_as("file:/etc/hosts", {.scheme = "file", .path = "/etc/hosts"});
    };

    "parses empty authority"_test = [] {
      expect_parsed_as("file:///etc/hosts", {.scheme = "file", .path = "/etc/hosts"});
    };

    "has_authority distinguishes empty authority from no authority"_test = [] {
      auto with_authority = urls::url::parse("file:///etc/hosts");
      expect(with_authority.has_value() && with_authority->has_authority());

      auto without_authority = urls::url::parse("file:/etc/hosts");
      expect(without_authority.has_value() && not without_authority->has_authority());
    };

    "parses IPv6 literal host with brackets"_test = [] {
      expect_parsed_as("http://[::1]:8042/", {.scheme = "http", .host = "[::1]", .port = "8042", .path = "/"});
    };

    "parses fragment containing a question mark"_test = [] {
      expect_parsed_as("http://example.com/#a?b", {.scheme = "http", .host = "example.com", .path = "/", .fragment = "a?b"});
    };

    "parses userinfo"_test = [] {
      expect_parsed_as("http://user:pass@example.com/",
        {.scheme = "http", .userinfo = "user:pass", .host = "example.com", .path = "/"});
    };

    "parses pct-encoded octets in userinfo, host, path, query and fragment"_test = [] {
      expect_parsed_as("http://us%20er@ex%41mple.com/a%2Fb?q=%7E#f%20g",
        {
          .scheme = "http",
          .userinfo = "us%20er",
          .host = "ex%41mple.com",
          .path = "/a%2Fb",
          .query = "q=%7E",
          .fragment = "f%20g",
        });
    };

    "parses empty port as no port"_test = [] {
      expect_parsed_as("http://example.com:/", {.scheme = "http", .host = "example.com", .path = "/"});
    };

    "rejects missing scheme"_test = [] {
      expect_rejected_as("//example.com/over", urls::url_error::scheme_invalid);
      expect_rejected_as("", urls::url_error::scheme_invalid);
    };

    "rejects scheme not starting with ALPHA"_test = [] {
      expect_rejected_as("1foo://example.com", urls::url_error::scheme_invalid);
    };

    "rejects space in path"_test = [] {
      expect_rejected_as("http://example.com/over there", urls::url_error::path_invalid);
    };

    "rejects space in query"_test = [] {
      expect_rejected_as("http://example.com/?name=fer ret", urls::url_error::query_invalid);
    };

    "rejects space in fragment"_test = [] {
      expect_rejected_as("http://example.com/#no se", urls::url_error::fragment_invalid);
    };

    "rejects truncated pct-encoded octet"_test = [] {
      expect_rejected_as("http://example.com/a%2", urls::url_error::path_invalid);
    };

    "accepts malformed pct-encoding when it is not validated"_test = [] {
      expect_parsed_as("http://example.com/a%zz?b%=c#d%",
        {.scheme = "http", .host = "example.com", .path = "/a%zz", .query = "b%=c", .fragment = "d%"},
        {.validate_pct_encoding = false});
    };

    "rejects userinfo when not allowed"_test = [] {
      expect_rejected_as("http://user@example.com/", urls::url_error::userinfo_not_allowed, {.allow_userinfo = false});
    };

    "rejects hostname outside DNS rules only when the DNS hostname is validated"_test = [] {
      expect_parsed_as("http://my_service.internal/", {.scheme = "http", .host = "my_service.internal", .path = "/"});
      expect_rejected_as("http://my_service.internal/", urls::url_error::host_invalid, {.validate_dns_hostname = true});
    };

    "accepts hostname character listed in extra hostname chars"_test = [] {
      expect_parsed_as("http://my_service.internal/",
        {.scheme = "http", .host = "my_service.internal", .path = "/"},
        {.validate_dns_hostname = true, .extra_hostname_chars = "_"});
    };

    "validates DNS hostname only for a reg-name host"_test = [] {
      expect_parsed_as("http://127.0.0.1/",
        {.scheme = "http", .host = "127.0.0.1", .path = "/"},
        {.validate_dns_hostname = true});
      expect_parsed_as("http://[::1]/", {.scheme = "http", .host = "[::1]", .path = "/"}, {.validate_dns_hostname = true});
    };

    "accepts port outside 16-bit range"_test = [] {
      expect_parsed_as("http://example.com:99999999/",
        {.scheme = "http", .host = "example.com", .port = "99999999", .path = "/"});
    };

    "rejects port outside 16-bit range when the range is validated"_test = [] {
      expect_rejected_as("http://example.com:99999999/", urls::url_error::port_invalid, {.validate_port_range = true});
    };

    "port_number converts the port text to a 16-bit number"_test = [] {
      auto parsed = urls::url::parse("http://example.com:8042/");
      expect(parsed.has_value() && parsed->port_number() == 8042);
    };

    "port_number is empty for absent port"_test = [] {
      auto parsed = urls::url::parse("http://example.com/");
      expect(parsed.has_value() && parsed->port_number() == std::nullopt);
    };

    "port_number is empty for port outside 16-bit range"_test = [] {
      auto parsed = urls::url::parse("http://example.com:99999999/");
      expect(parsed.has_value() && parsed->port_number() == std::nullopt);
    };
  };
}
