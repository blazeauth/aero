#pragma once

#include <algorithm>
#include <expected>
#include <optional>
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
    using tls_options = asio::ssl::context::options;

    explicit system_context(): system_context(defer_trust_store_t{}, std::nullopt) {
      std::ignore = use_system_trust_store();
    }

    explicit system_context(tls::version pinned_version): system_context(defer_trust_store_t{}, pinned_version) {
      std::ignore = use_system_trust_store();
    }

    friend std::expected<system_context, std::error_code> make_system_context();
    friend std::expected<system_context, std::error_code> make_system_context(tls::version pinned_version);

    system_context(const system_context&) = delete;
    system_context& operator=(const system_context&) = delete;
    system_context(system_context&&) noexcept = default;
    system_context& operator=(system_context&&) noexcept = default;
    ~system_context() noexcept = default;

    [[nodiscard]] asio::ssl::context& context() {
      return static_cast<asio::ssl::context&>(*this);
    }

    template <std::same_as<tls::version>... Versions>
      requires(sizeof...(Versions) != 0)
    [[nodiscard]] std::error_code disable_version(Versions... versions) {
      if (current_version_either(versions...)) {
        return tls::context_error::cannot_disable_active_tls_version;
      }
      tls_options options = (version_to_options(versions) | ...);
      ctx_.set_options(options);
      return std::error_code{};
    }

    void disable_deprecated_versions() {
      if (ctx_version_ && tls::is_deprecated(*ctx_version_)) {
        return;
      }

      std::ranges::for_each(tls::deprecated_versions, [this](tls::version version) { std::ignore = disable_version(version); });
    }

    [[nodiscard]] explicit operator asio::ssl::context&() {
      return ctx_;
    }

   private:
    struct defer_trust_store_t {};

    explicit system_context(defer_trust_store_t, std::optional<tls::version> version)
      : ctx_(version.has_value() ? version_to_method(*version) : asio::ssl::context::tls_client), ctx_version_(version) {
      if (!ctx_version_) {
        ctx_.set_options(asio::ssl::context::no_sslv2 | asio::ssl::context::no_sslv3 | asio::ssl::context::no_tlsv1 |
                         asio::ssl::context::no_tlsv1_1);
      }
      ctx_.set_verify_mode(asio::ssl::verify_peer);
    }

    [[nodiscard]] std::error_code use_system_trust_store() {
#if AERO_AIA_FETCHING_CALLBACK_SUPPORTED
      ctx_.set_verify_callback(aia_fetching_verify_callback);
      return {};
#elif defined(AERO_USE_WOLFSSL)
      if (wolfSSL_CTX_load_system_CA_certs(ctx_.native_handle()) != WOLFSSL_SUCCESS) {
        return tls::context_error::system_trust_store_unavailable;
      }
      return {};
#else
      std::error_code ec;
      ctx_.set_default_verify_paths(ec);
      return ec;
#endif
    }

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

    [[nodiscard]] asio::ssl::context::method version_to_method(tls::version version) const {
      using enum asio::ssl::context::method;
      switch (version) {
      case version::sslv2:
        return sslv2_client;
      case version::sslv3:
        return sslv3_client;
      case version::tlsv1:
        return tlsv1_client;
      case version::tlsv1_1:
        return tlsv11_client;
      case version::tlsv1_2:
        return tlsv12_client;
      case version::tlsv1_3:
        return tlsv13_client;
      }
      std::unreachable();
    }

    [[nodiscard]] bool current_version_either(auto... values) {
      return ((ctx_version_ == values) || ...);
    }

    asio::ssl::context ctx_;
    std::optional<tls::version> ctx_version_;
  };

  [[nodiscard]] inline std::expected<system_context, std::error_code> make_system_context() {
    system_context system_ctx{system_context::defer_trust_store_t{}, std::nullopt};
    if (std::error_code ec = system_ctx.use_system_trust_store()) {
      return std::unexpected(ec);
    }
    return system_ctx;
  }

  [[nodiscard]] inline std::expected<system_context, std::error_code> make_system_context(tls::version pinned_version) {
    system_context system_ctx{system_context::defer_trust_store_t{}, pinned_version};
    if (std::error_code ec = system_ctx.use_system_trust_store()) {
      return std::unexpected(ec);
    }
    return system_ctx;
  }

} // namespace aero::tls
