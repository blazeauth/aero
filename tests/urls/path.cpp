#include "aero/urls/detail/path.hpp"

#include <optional>
#include <string_view>

#include <ut/ut.hpp>

namespace urls = aero::urls;
using urls::detail::path_type;
using namespace ut;

namespace {
  std::optional<path_type> classify(std::string_view path, bool has_authority) {
    auto parsed = urls::detail::parse_hier_part_path(path, has_authority);
    if (!parsed) {
      return std::nullopt;
    }

    return parsed->type;
  }
} // namespace

int main() {
  suite path_parser = [] {
    "keeps the input as the parsed text"_test = [] {
      auto parsed = urls::detail::parse_hier_part_path("/over/there", true);
      expect(parsed.has_value());

      if (not parsed.has_value()) {
        return;
      }

      expect(parsed->text == "/over/there");
    };

    "reads empty path as path-abempty when authority is present"_test = [] {
      expect(classify("", true) == path_type::abempty);
    };

    "reads path beginning with / as path-abempty when authority is present"_test = [] {
      expect(classify("/over/there", true) == path_type::abempty);
    };

    "accepts empty segments"_test = [] {
      expect(classify("//", true) == path_type::abempty) << "each / opens a segment that may be empty";
    };

    "rejects path beginning with a segment when authority is present"_test = [] {
      expect(classify("over/there", true) == std::nullopt);
    };

    "reads empty path as path-empty when authority is absent"_test = [] {
      expect(classify("", false) == path_type::empty);
    };

    "reads path beginning with / as path-absolute when authority is absent"_test = [] {
      expect(classify("/over/there", false) == path_type::absolute);
    };

    "reads / as path-absolute"_test = [] {
      expect(classify("/", false) == path_type::absolute);
    };

    "rejects path beginning with // when authority is absent"_test = [] {
      expect(classify("//over/there", false) == std::nullopt) << "a leading // would be read as an authority";
    };

    "reads path beginning with a segment as path-rootless"_test = [] {
      expect(classify("over/there", false) == path_type::rootless);
    };

    "accepts : and @ in a segment"_test = [] {
      expect(classify("/over:there/@here", true) == path_type::abempty);
    };

    "accepts sub-delims in a segment"_test = [] {
      expect(classify("/!$&'()*+,;=", true) == path_type::abempty);
    };

    "accepts pct-encoded octets in a segment"_test = [] {
      expect(classify("/over%20there/%2F", true) == path_type::abempty);
    };

    "rejects truncated pct-encoded octet"_test = [] {
      expect(classify("/over%2", true) == std::nullopt);
      expect(classify("/over%", true) == std::nullopt);
    };

    "rejects non-HEXDIG in pct-encoded octet"_test = [] {
      expect(classify("/over%2Gthere", true) == std::nullopt);
    };

    "rejects space in a segment"_test = [] {
      expect(classify("/over there", true) == std::nullopt);
    };

    "rejects ? and # in a segment"_test = [] {
      expect(classify("/over?there", true) == std::nullopt) << "? starts the query, so it is not part of the path";
      expect(classify("/over#there", true) == std::nullopt) << "# starts the fragment, so it is not part of the path";
    };
  };
}
