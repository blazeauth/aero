#pragma once

#include <algorithm>
#include <system_error>
#include <utility>

#include <asio/ssl/context.hpp>

#include "aero/tls/aia_fetching_verify_callback.hpp"
#include "aero/tls/error.hpp"
#include "aero/tls/version.hpp"

namespace aero::tls {

  // This class will definitely need improvements in the future
  // Current implementation is not much different from vanilla
  // asio::ssl::context on any platform except Windows

  class system_context {
   public:
    using context_type = asio::ssl::context;
    using tls_options = asio::ssl::context::options;

    explicit system_context(tls::version version): ctx_(version_to_method(version)), ctx_version_(version) {
      ctx_.set_verify_mode(asio::ssl::verify_peer);

#if AERO_AIA_FETCHING_CALLBACK_SUPPORTED
      ctx_.set_verify_callback(aia_fetching_verify_callback);
#else
#ifdef AERO_USE_WOLFSSL
      wolfSSL_CTX_load_system_CA_certs(ctx_.native_handle());
#endif
      ctx_.set_default_verify_paths();
#endif
    }

    system_context(const system_context&) = delete;
    system_context& operator=(const system_context&) = delete;
    system_context(system_context&&) noexcept = default;
    system_context& operator=(system_context&&) noexcept = default;
    ~system_context() noexcept = default;

    [[nodiscard]] context_type& context() {
      return static_cast<context_type&>(*this);
    }

    template <std::same_as<tls::version>... Versions>
      requires(sizeof...(Versions) != 0)
    [[nodiscard]] std::error_code disable_version(Versions... versions) {
      if (current_version_either(versions...)) {
        return tls::error::context_error::cannot_disable_active_tls_version;
      }
      tls_options options = (version_to_options(versions) | ...);
      ctx_.set_options(options);
      return std::error_code{};
    }

    void disable_deprecated_versions() {
      if (tls::is_deprecated(ctx_version_)) {
        return;
      }

      std::ranges::for_each(tls::deprecated_versions, [this](tls::version version) { std::ignore = disable_version(version); });
    }

    [[nodiscard]] explicit operator context_type&() {
      return ctx_;
    }

   private:
    [[nodiscard]] tls_options version_to_options(tls::version version) const {
      using asio::ssl::context;
      switch (version) {
      case tls::version::sslv2:
        return context::no_sslv2;
      case tls::version::sslv3:
        return context::no_sslv3;
      case tls::version::tlsv1:
        return context::no_tlsv1;
      case tls::version::tlsv1_1:
        return context::no_tlsv1_1;
      case tls::version::tlsv1_2:
        return context::no_tlsv1_2;
      case tls::version::tlsv1_3:
        return context::no_tlsv1_3;
      }
      std::unreachable();
    }

    [[nodiscard]] context_type::method version_to_method(tls::version version) const {
      using method = asio::ssl::context::method;
      switch (version) {
      case version::sslv2:
        return method::sslv2_client;
      case version::sslv3:
        return method::sslv3_client;
      case version::tlsv1:
        return method::tlsv1_client;
      case version::tlsv1_1:
        return method::tlsv11_client;
      case version::tlsv1_2:
        return method::tlsv12_client;
      case version::tlsv1_3:
        return method::tlsv13_client;
      }
      std::unreachable();
    }

    [[nodiscard]] bool current_version_either(auto... values) {
      return ((ctx_version_ == values) || ...);
    }

    context_type ctx_;
    tls::version ctx_version_;
  };

} // namespace aero::tls
