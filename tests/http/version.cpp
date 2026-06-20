#include "aero/http/version.hpp"
#include "aero/http/error.hpp"
#include <ut/ut.hpp>

using namespace ut;

namespace http = aero::http;
using http::protocol_error;
using http::version;

int main() {
  suite http_version = [] {
    "std::format formats version as string"_test = [] {
      expect(std::format("Version {}", static_cast<version>(100)) == "Version unknown_version");
      expect(std::format("Version {}", version::http1_0) == "Version HTTP/1.0");
      expect(std::format("Version {}", version::http1_1) == "Version HTTP/1.1");
    };

    "to_string returns canonical tokens"_test = [] {
      expect(http::to_string(http::version::http1_0) == "HTTP/1.0");
      expect(http::to_string(http::version::http1_1) == "HTTP/1.1");
    };

    "parses canonical tokens"_test = [] {
      expect(http::parse_version("HTTP/1.0") == http::version::http1_0);
      expect(http::parse_version("HTTP/1.1") == http::version::http1_1);
    };

    "rejects wrong prefix"_test = [] {
      auto parsed = http::parse_version("HTP/1.1");
      expect[not parsed.has_value()];
      expect(parsed.error() == protocol_error::version_invalid);
    };

    "rejects wrong separator"_test = [] {
      auto parsed = http::parse_version("HTTP-1.1");
      expect[not parsed.has_value()];
      expect(parsed.error() == protocol_error::version_invalid);
    };

    "rejects unsupported minor or major"_test = [] {
      for (std::string_view text : {"HTTP/0.9", "HTTP/1.2", "HTTP/2.0", "HTTP/1.01"}) {
        auto parsed = http::parse_version(text);
        expect[not parsed.has_value()];
        expect(parsed.error() == protocol_error::version_invalid);
      }
    };

    "rejects case differences and whitespace"_test = [] {
      for (std::string_view text : {"http/1.1", "HTTP/1.1 ", " HTTP/1.1", "HTTP/ 1.1", "HTTP/1.1\r\n"}) {
        auto parsed = http::parse_version(text);
        expect[not parsed.has_value()];
        expect(parsed.error() == protocol_error::version_invalid);
      }
    };
  };
}
