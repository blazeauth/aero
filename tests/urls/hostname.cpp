#include "aero/urls/detail/hostname.hpp"

#include <string>
#include <string_view>

#include <ut/ut.hpp>

namespace urls = aero::urls;
using namespace ut;

int main() {
  suite hostname_validation = [] {
    "accepts dotted name"_test = [] {
      expect(urls::detail::is_valid_hostname("example.com"));
    };

    "accepts numeric-only label"_test = [] {
      expect(urls::detail::is_valid_hostname("123.example"));
    };

    "accepts trailing root dot"_test = [] {
      expect(urls::detail::is_valid_hostname("example.com."));
    };

    "accepts hyphen inside label"_test = [] {
      expect(urls::detail::is_valid_hostname("my-service.example"));
    };

    "rejects hostname without a label"_test = [] {
      expect(not urls::detail::is_valid_hostname(""));
      expect(not urls::detail::is_valid_hostname("."));
    };

    "rejects empty label"_test = [] {
      expect(not urls::detail::is_valid_hostname("example..com"));
      expect(not urls::detail::is_valid_hostname(".example.com"));
    };

    "label must not exceed 63 characters"_test = [] {
      expect(urls::detail::is_valid_hostname(std::string(63, 'a') + ".com"));
      expect(not urls::detail::is_valid_hostname(std::string(64, 'a') + ".com"));
    };

    "name must not exceed 253 characters"_test = [] {
      std::string name;
      while (name.size() < 250) {
        name += "ab.";
      }
      name += std::string(253 - name.size(), 'a');

      expect(urls::detail::is_valid_hostname(name));
      expect(not urls::detail::is_valid_hostname(name + "a"));
    };

    "rejects leading hyphen in label"_test = [] {
      expect(not urls::detail::is_valid_hostname("-example.com"));
      expect(not urls::detail::is_valid_hostname("example.-com"));
    };

    "rejects trailing hyphen in label"_test = [] {
      expect(not urls::detail::is_valid_hostname("example-.com"));
      expect(not urls::detail::is_valid_hostname("example.com-"));
    };

    "rejects underscore"_test = [] {
      expect(not urls::detail::is_valid_hostname("my_service.example"));
    };

    "accepts underscore listed in extra chars"_test = [] {
      expect(urls::detail::is_valid_hostname("my_service.example", "_"));
    };

    "accepts extra char at both ends of a label"_test = [] {
      expect(urls::detail::is_valid_hostname("_dmarc.example", "_"));
      expect(urls::detail::is_valid_hostname("example_.com", "_"));
    };

    "accepts several extra chars"_test = [] {
      expect(urls::detail::is_valid_hostname("a_b~c.example", "_~"));
    };

    "rejects character absent from extra chars"_test = [] {
      expect(not urls::detail::is_valid_hostname("a~b.example", "_"));
    };

    "keeps hyphen position rules when extra chars are listed"_test = [] {
      expect(not urls::detail::is_valid_hostname("-example.com", "_"));
    };

    // TODO: We should add pct-encoding validation support for DNS
    // hostnames later and this test should be removed after that
    "rejects pct-encoded octet"_test = [] {
      expect(not urls::detail::is_valid_hostname("ex%41mple.com"));
    };

    "rejects space"_test = [] {
      expect(not urls::detail::is_valid_hostname("exa mple.com"));
    };
  };
}
