#pragma once

#include "aero/http/request.hpp"
#include "aero/http/response.hpp"
#include "aero/http/status.hpp"

namespace aero::http {

  class context {
   public:
    explicit context(http::request* request, http::response* response): request_(request), response_(response) {}

    http::request& request() noexcept {
      return *request_;
    }

    http::response& response() noexcept {
      return *response_;
    }

    void status(http::status status) {
      response_->status_line.status_code = status;
      response_->status_line.reason_phrase = http::to_string(status);
    }

   private:
    http::request* request_;
    http::response* response_;
  };

} // namespace aero::http
