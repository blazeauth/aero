#include "aero/urls/query_params.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <ut/ut.hpp>

namespace urls = aero::urls;
using namespace ut;

int main() {
  suite query_params_parser = [] {
    "parses valid query params"_test = [] {
      auto parsed = urls::query_params::parse("key1=value1&key2=value2&key3&key4=");
      require(parsed.has_value());

      expect(parsed->contains("key1"));
      expect(parsed->contains("key2"));
      expect(parsed->contains("key3"));
      expect(parsed->contains("key4"));

      expect(parsed->first_value("key1") == "value1");
      expect(parsed->first_value("key2") == "value2");
      expect(parsed->first_value("key3") == std::nullopt);
      expect(parsed->first_value("key4") == "");
    };

    "parses empty input as no params"_test = [] {
      auto parsed = urls::query_params::parse("");
      require(parsed.has_value());

      expect(parsed->empty()) << "empty input yields no params, got" << parsed->size();
    };

    "skips empty sequences"_test = [] {
      auto parsed = urls::query_params::parse("&a=1&&b=2&");
      require(parsed.has_value());

      expect(parsed->size() == 2U) << "leading, repeated and trailing & contribute no params, got" << parsed->size();
      expect(parsed->first_value("a") == "1");
      expect(parsed->first_value("b") == "2");
    };

    "parses input of only & as no params"_test = [] {
      auto parsed = urls::query_params::parse("&&&");
      require(parsed.has_value());

      expect(parsed->empty()) << "every sequence is empty, so none of them becomes a param, got" << parsed->size();
    };

    "splits name and value on the first ="_test = [] {
      auto parsed = urls::query_params::parse("a=1=2");
      require(parsed.has_value());

      expect(parsed->size() == 1U);
      expect(parsed->first_value("a") == "1=2") << "every = after the first belongs to the value";
    };

    "parses an empty name from a leading ="_test = [] {
      auto parsed = urls::query_params::parse("=value&a=1");
      require(parsed.has_value());

      expect(parsed->contains("")) << "a sequence starting with = has the empty name, not the name \"value\"";
      expect(parsed->first_value("") == "value");
      expect(parsed->size() == 2U);
    };

    "distinguishes an empty value from an absent one"_test = [] {
      auto parsed = urls::query_params::parse("trailing=&bare");
      require(parsed.has_value());

      expect(parsed->first_value("trailing") == "") << "a trailing = gives an empty value, not an absent one";
      expect(parsed->first_value("bare") == std::nullopt) << "a sequence without = has no value at all";
    };

    "keeps duplicate names in input order"_test = [] {
      auto parsed = urls::query_params::parse("k=1&a=x&k=2&k");
      require(parsed.has_value());

      expect(parsed->count("k") == 3U) << "duplicates are preserved rather than collapsed";
      expect(parsed->first_value("k") == "1");

      std::string joined;
      for (const urls::query_param& param : parsed->entries("k")) {
        joined += param.value_or("-");
      }
      expect(joined == "12-");
    };

    "percent-decodes names and values"_test = [] {
      auto parsed = urls::query_params::parse("na%3Dme=va%26lue&plus+space=a+b%2Bc");
      require(parsed.has_value());

      expect(parsed->first_value("na=me") == "va&lue");
      // '+' becomes SP before pct-decoding, so %2B survives as a literal '+'
      expect(parsed->first_value("plus space") == "a b+c");
    };

    "decodes percent escapes with lowercase hex digits"_test = [] {
      auto parsed = urls::query_params::parse("%6bey=%d0%bf&mixed=%eF%bB%Bf&plus=%2b");
      require(parsed.has_value());

      expect(parsed->first_value("key") == "\xD0\xBF") << "lowercase hex digits decode in the name as well as the value";
      expect(parsed->first_value("mixed") == "\xEF\xBB\xBF") << "the two hex digits are independently cased";
      expect(parsed->first_value("plus") == "+") << "%2b decodes after + has already become SP";
    };

    "keeps malformed percent sequences as-is"_test = [] {
      auto parsed = urls::query_params::parse("key=%ZZ%2&trail=100%");
      require(parsed.has_value());

      expect(parsed->first_value("key") == "%ZZ%2");
      expect(parsed->first_value("trail") == "100%");
    };

    "decodes utf8 sequences"_test = [] {
      auto parsed = urls::query_params::parse("greeting=%D0%BF%D1%80%D0%B8%D0%B2%D0%B5%D1%82&raw=caf\xC3\xA9");
      require(parsed.has_value());

      expect(parsed->first_value("greeting") == "\xD0\xBf\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
      expect(parsed->first_value("raw") == "caf\xC3\xA9");
    };

    "replaces invalid utf8 with U+FFFD"_test = [] {
      constexpr std::string_view replacement = "\xEF\xBF\xBD";

      auto parsed =
        urls::query_params::parse("lone=%FF&truncated=%E2%82&overlong=%C0%AF&surrogate=%ED%A0%80&above_max=%F4%91%92%93");
      require(parsed.has_value());

      auto fffd_count = [&](std::size_t count) {
        std::string result;
        for (std::size_t i{}; i < count; ++i) {
          result += replacement;
        }
        return result;
      };

      expect(parsed->first_value("lone") == fffd_count(1));
      // truncated 3-byte sequence at end of input collapses into a single U+FFFD
      expect(parsed->first_value("truncated") == fffd_count(1));
      // each maximal invalid subpart becomes its own U+FFFD (Unicode Table 3-8/3-9)
      expect(parsed->first_value("overlong") == fffd_count(2));
      expect(parsed->first_value("surrogate") == fffd_count(3));
      expect(parsed->first_value("above_max") == fffd_count(4));
    };

    "does not strip a leading BOM"_test = [] {
      auto parsed = urls::query_params::parse("key=%EF%BB%BFvalue");
      require(parsed.has_value());

      expect(parsed->first_value("key") == "\xEF\xBB\xBFvalue");
    };

    "parse accepts a byte span"_test = [] {
      constexpr std::string_view query = "a=1&b=2";

      auto parsed = urls::query_params::parse(std::as_bytes(std::span{query}));
      require(parsed.has_value());

      expect(parsed->first_value("a") == "1");
      expect(parsed->first_value("b") == "2");
    };
  };
}
