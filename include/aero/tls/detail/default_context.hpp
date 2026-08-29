#pragma once

#include "aero/tls/system_context.hpp"
#include <expected>

namespace aero::tls::detail {

  [[nodiscard]] inline std::expected<tls::system_context, std::error_code>& default_context() {
    static auto& ctx{*new auto{tls::make_system_context()}};
    return ctx;
  }

} // namespace aero::tls::detail
