#include "aero/urls/error.hpp"
#include <ut/ut.hpp>

#include "common/error_code_test_helper.hpp"

using namespace ut;

namespace urls = aero::urls;

int main() {
  suite urls_error_code = [] {
    "all url errors have messages"_test = [] {
      test_enum_error_code_messages<urls::url_error>(urls::url_error_category());
    };
  };
}
