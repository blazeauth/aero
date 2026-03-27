#pragma once

#include <vector>

#include "aero/http/headers.hpp"
#include "aero/http/method.hpp"
#include "aero/http/version.hpp"

namespace aero::http {

  struct request {
    http::method method;
    http::version protocol;
    std::string url;

    std::vector<std::byte> body;
    http::headers headers;

    std::int64_t content_length;
  };

} // namespace aero::http
