#include "aero/urls/query_params.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <iomanip>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ut/ut.hpp>

namespace urls = aero::urls;
using namespace ut;

namespace {

  void serializes_to(std::initializer_list<urls::query_param> params, std::string_view expected) {
    std::string serialized = urls::query_params{params}.to_string();
    expect(serialized == expected) << "expected" << std::quoted(expected) << "got" << std::quoted(serialized);
  }

  void params_are(const urls::query_params& params, std::string_view expected) {
    std::string serialized = params.to_string();
    expect(serialized == expected) << "expected" << std::quoted(expected) << "got" << std::quoted(serialized);
  }

  // The checks are templated because 'requires' reports invalid expressions
  // only during template substitution
  template <class T = urls::query_params>
  consteval bool rvalue_overloads_are_deleted() {
    static_assert(not requires(T params) { std::move(params).begin(); });
    static_assert(not requires(T params) { std::move(params).end(); });
    static_assert(not requires(T params) { std::move(params).cbegin(); });
    static_assert(not requires(T params) { std::move(params).cend(); });
    static_assert(not requires(T params) { std::move(params).rbegin(); });
    static_assert(not requires(T params) { std::move(params).rend(); });
    static_assert(not requires(T params) { std::move(params).front(); });
    static_assert(not requires(T params) { std::move(params).back(); });
    static_assert(not requires(T params) { std::move(params).find("a"); });
    static_assert(not requires(T params) { std::move(params).entries("a"); });
    static_assert(not requires(T params) { std::move(params).names(); });
    static_assert(not requires(T params) { std::move(params).values(); });
    static_assert(not requires(T params) { std::move(params).values("a"); });
    static_assert(not requires(T params) { std::move(params).first_value("a"); });
    static_assert(not requires(T params) { std::move(params).add("a"); });
    static_assert(not requires(T params) { std::move(params).add("a", "1"); });
    static_assert(not requires(T params) { std::move(params).set("a"); });
    static_assert(not requires(T params) { std::move(params).set("a", "1"); });

    static_assert(not requires(const T params) { std::move(params).begin(); });
    static_assert(not requires(const T params) { std::move(params).end(); });
    static_assert(not requires(const T params) { std::move(params).rbegin(); });
    static_assert(not requires(const T params) { std::move(params).rend(); });
    static_assert(not requires(const T params) { std::move(params).front(); });
    static_assert(not requires(const T params) { std::move(params).back(); });
    static_assert(not requires(const T params) { std::move(params).find("a"); });
    static_assert(not requires(const T params) { std::move(params).entries("a"); });

    return true;
  }
  static_assert(rvalue_overloads_are_deleted());

} // namespace

int main() {
  suite query_params_serialization = [] {
    "serializes empty params as an empty string"_test = [] {
      serializes_to({}, "");
    };

    "joins params with &"_test = [] {
      serializes_to({{"a", "1"}, {"b", "2"}}, "a=1&b=2");
    };

    "omits = for params without a value"_test = [] {
      serializes_to({{"key", std::nullopt}}, "key");
    };

    "keeps = for an empty value"_test = [] {
      serializes_to({{"key", ""}}, "key=");
    };

    "separates a value-less param from the one after it"_test = [] {
      serializes_to({{"a", std::nullopt}, {"b", "1"}}, "a&b=1");
    };

    "emits a leading = for an empty name"_test = [] {
      serializes_to({{"", "value"}}, "=value");
    };

    "keeps duplicate names in insertion order"_test = [] {
      serializes_to({{"k", "1"}, {"k", "2"}, {"k", std::nullopt}}, "k=1&k=2&k");
    };

    "leaves alphanumerics and *-._ unencoded"_test = [] {
      serializes_to({{"n4me*-._", "v4lue*-._"}}, "n4me*-._=v4lue*-._");
    };

    "encodes space as +"_test = [] {
      serializes_to({{"na me", "va lue"}}, "na+me=va+lue");
    };

    "percent-encodes + so it survives a round trip"_test = [] {
      serializes_to({{"key", "a+b"}}, "key=a%2Bb");
    };

    "percent-encodes & and = in names and values"_test = [] {
      serializes_to({{"a&b", "c=d"}}, "a%26b=c%3Dd");
    };

    "percent-encodes % as %25"_test = [] {
      serializes_to({{"key", "100%"}}, "key=100%25");
    };

    "percent-encodes control characters"_test = [] {
      serializes_to({{"key", "line\nfeed\x7F"}}, "key=line%0Afeed%7F");
    };

    "percent-encodes non-ascii bytes with uppercase hex digits"_test = [] {
      serializes_to({{"greeting", "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"}},
        "greeting=%D0%BF%D1%80%D0%B8%D0%B2%D0%B5%D1%82");
    };

    "round-trips a parsed query string"_test = [] {
      constexpr std::string_view query = "key1=value1&key2=value2&key3&key4=";

      auto parsed = urls::query_params::parse(query);
      require(parsed.has_value());

      std::string serialized = parsed->to_string();
      expect(serialized == query) << "expected" << std::quoted(query) << "got" << std::quoted(serialized);
    };

    "re-encodes lowercase percent escapes in uppercase"_test = [] {
      auto parsed = urls::query_params::parse("k=%d0%bf");
      require(parsed.has_value());

      params_are(*parsed, "k=%D0%BF");
    };
  };

  suite query_params_mutation = [] {
    "add appends to the end"_test = [] {
      urls::query_params params;
      params.add("a", "1");
      params.add("b", "2");

      params_are(params, "a=1&b=2");
    };

    "add keeps duplicate names"_test = [] {
      urls::query_params params{{"k", "1"}};
      params.add("k", "2");

      params_are(params, "k=1&k=2");
      expect(params.count("k") == 2U);
    };

    "add without a value stores nullopt"_test = [] {
      urls::query_params params;
      auto added = params.add("k");

      expect(added->value == std::nullopt);
      params_are(params, "k");
    };

    "add returns an iterator to the new param"_test = [] {
      urls::query_params params{{"a", "1"}};
      auto added = params.add("b", "2");

      expect(added == std::prev(params.end()));
      expect(added->name == "b");
    };

    "set appends when the name is absent"_test = [] {
      urls::query_params params{{"a", "1"}};
      params.set("b", "2");

      params_are(params, "a=1&b=2");
    };

    "set replaces the value in place"_test = [] {
      urls::query_params params{{"a", "1"}, {"b", "2"}};
      params.set("a", "9");

      params_are(params, "a=9&b=2");
    };

    "set removes trailing duplicates of the name"_test = [] {
      urls::query_params params{{"k", "1"}, {"a", "x"}, {"k", "2"}, {"k", "3"}};
      params.set("k", "9");

      params_are(params, "k=9&a=x");
      expect(params.count("k") == 1U);
    };

    "set without a value clears the existing value"_test = [] {
      urls::query_params params{{"k", "1"}};
      params.set("k");

      params_are(params, "k");
    };

    "set without a value removes trailing duplicates"_test = [] {
      urls::query_params params{{"k", "1"}, {"k", "2"}};
      params.set("k");

      params_are(params, "k");
    };

    "set returns an iterator to the updated param"_test = [] {
      urls::query_params params{{"a", "1"}, {"b", "2"}};
      auto updated = params.set("b", "9");

      expect(updated == std::next(params.begin()));
      expect(updated->value == "9");
    };

    "erase removes every param with the name"_test = [] {
      urls::query_params params{{"k", "1"}, {"a", "x"}, {"k", "2"}};
      params.erase("k");

      params_are(params, "a=x");
    };

    "erase ignores an absent name"_test = [] {
      urls::query_params params{{"a", "1"}};
      params.erase("zzz");

      params_are(params, "a=1");
    };

    "clear removes every param"_test = [] {
      urls::query_params params{{"a", "1"}, {"b", "2"}};
      params.clear();

      expect(params.empty());
      params_are(params, "");
    };

    "append concatenates another instance"_test = [] {
      urls::query_params params{{"a", "1"}};
      const urls::query_params other{{"b", "2"}, {"c", std::nullopt}};
      params.append(other);

      params_are(params, "a=1&b=2&c");

      std::string source = other.to_string();
      expect(source == "b=2&c") << "append(const&) must leave the source untouched, got" << std::quoted(source);
    };

    "append moves out of an rvalue and clears it"_test = [] {
      urls::query_params params{{"a", "1"}};
      urls::query_params other{{"b", "2"}};
      params.append(std::move(other));

      params_are(params, "a=1&b=2");
      expect(other.empty()) << "append(query_params&&) clears the source";
    };

    "append on itself is a no-op"_test = [] {
      urls::query_params params{{"a", "1"}, {"b", "2"}};
      params.append(params);

      params_are(params, "a=1&b=2");
    };
  };

  suite query_params_lookup = [] {
    "find returns end for an absent name"_test = [] {
      urls::query_params params{{"a", "1"}};

      expect(params.find("zzz") == params.end());
      expect(not params.contains("zzz"));
    };

    "count includes every param with the name"_test = [] {
      urls::query_params params{{"k", "1"}, {"a", "x"}, {"k", std::nullopt}};

      expect(params.count("k") == 2U);
      expect(params.count("a") == 1U);
      expect(params.count("zzz") == 0U);
    };

    "first_value returns the first of duplicate names"_test = [] {
      urls::query_params params{{"k", "1"}, {"k", "2"}};

      expect(params.first_value("k") == "1");
    };

    "entries yields every param with the name"_test = [] {
      urls::query_params params{{"k", "1"}, {"a", "x"}, {"k", "2"}};

      std::string joined;
      for (const urls::query_param& param : params.entries("k")) {
        joined += param.value_or("-");
      }

      expect(joined == "12");
    };

    "names yields every name in insertion order"_test = [] {
      urls::query_params params{{"a", "1"}, {"b", "2"}, {"a", "3"}};
      std::array<std::string_view, 3> expected{"a", "b", "a"};

      expect(std::ranges::equal(params.names(), expected));
    };

    "values yields nullopt for params without a value"_test = [] {
      urls::query_params params{{"a", "1"}, {"b", std::nullopt}};

      std::vector<std::optional<std::string_view>> values;
      std::ranges::copy(params.values(), std::back_inserter(values));

      require(values.size() == 2U);

      expect(values.at(0) == "1");
      expect(values.at(1) == std::nullopt);
    };

    "values by name skips other params"_test = [] {
      urls::query_params params{{"k", "1"}, {"a", "x"}, {"k", std::nullopt}};

      std::string joined;
      for (std::optional<std::string_view> value : params.values("k")) {
        joined += value.value_or("-");
      }

      expect(joined == "1-");
    };
  };
}
