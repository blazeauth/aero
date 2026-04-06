#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef AERO_USE_TLS
#include <asio/ssl/context.hpp>
#endif

#include "aero/detail/string.hpp"
#include "aero/error.hpp"
#include "aero/http/detail/common.hpp"
#include "aero/http/error.hpp"
#include "aero/http/port.hpp"
#include "aero/http/request.hpp"
#include "aero/http/response.hpp"
#include "aero/http/status_code.hpp"
#include "aero/http/version.hpp"
#include "aero/net/concepts/transport.hpp"
#include "aero/net/detail/basic_transport.hpp"

#ifdef AERO_USE_TLS
#include "aero/net/tls_transport.hpp"
#include "aero/tls/system_context.hpp"
#include "aero/tls/version.hpp"
#endif

namespace aero::http::detail {

  constexpr inline std::size_t default_max_idle_connections_per_endpoint{10};

#ifdef AERO_USE_TLS
  constexpr inline aero::tls::version default_system_tls_version{aero::tls::version::tlsv1_2};
#endif

  struct pool_options {
    std::size_t max_idle_connections_per_endpoint{default_max_idle_connections_per_endpoint};
    std::size_t transport_buffer_size{aero::net::detail::default_buffer_size};
  };

  template <typename Transport>
  struct secure_transport_trait : std::false_type {};

#ifdef AERO_USE_TLS
  template <typename... TransportArgs>
  struct secure_transport_trait<aero::net::tls_transport<TransportArgs...>> : std::true_type {};
#endif

  template <net::concepts::transport Transport>
  class connection_pool final {
   public:
    using executor_type = asio::any_io_executor;
    using transport_type = Transport;

    constexpr static int informational_status_code_min = std::to_underlying(http::status_code::continue_);
    constexpr static int informational_status_code_max = std::to_underlying(http::status_code::ok);
    constexpr static int succesfull_status_code_min = std::to_underlying(http::status_code::ok);
    constexpr static int succesfull_status_code_max = std::to_underlying(http::status_code::multiple_choices);

    [[nodiscard]] constexpr static bool is_secure_transport() noexcept {
      return secure_transport_trait<transport_type>::value;
    }

    struct connection_key {
      std::string host;
      std::uint16_t port{http::default_port};

      [[nodiscard]] friend bool operator==(const connection_key&, const connection_key&) = default;
    };

    class leased_connection;

   private:
    enum class transfer_encoding_framing : std::uint8_t {
      none = 0,
      chunked,
      close_delimited,
      invalid,
    };

    struct transfer_encoding_info final {
      bool present{false};
      bool invalid{false};
      bool final_chunked{false};
    };

    struct connection_key_hash {
      [[nodiscard]] std::size_t operator()(const connection_key& key) const noexcept {
        return std::hash<std::string>{}(key.host) ^ (std::hash<std::uint16_t>{}(key.port) << 1U);
      }
    };

    struct pooled_connection {
      pooled_connection(std::uint64_t id, connection_key key, transport_type transport)
        : id(id), key(std::move(key)), transport(std::move(transport)) {}

      pooled_connection(pooled_connection&&) noexcept = default;
      pooled_connection& operator=(pooled_connection&&) noexcept = default;
      pooled_connection(const pooled_connection&) = delete;
      pooled_connection& operator=(const pooled_connection&) = delete;
      ~pooled_connection() = default;

      [[nodiscard]] bool is_open() {
        return transport.lowest_layer().is_open();
      }

      std::uint64_t id{0};
      connection_key key;
      transport_type transport;
    };

    struct state final {
      explicit state(executor_type executor, pool_options options)
        : executor_(std::move(executor)),
          options_(sanitize_options(options))
#ifdef AERO_USE_TLS
          ,
          owned_tls_context_(make_default_system_tls_context_if_needed())
#endif
      {
#ifdef AERO_USE_TLS
        if constexpr (connection_pool::is_secure_transport()) {
          tls_context_ = owned_tls_context_ ? &owned_tls_context_->context() : nullptr;
        }
#endif
      }

#ifdef AERO_USE_TLS
      explicit state(executor_type executor, pool_options options, asio::ssl::context& tls_context)
        requires(connection_pool::is_secure_transport())
        : executor_(std::move(executor)), options_(sanitize_options(options)), tls_context_(&tls_context) {}
#endif

      [[nodiscard]] std::expected<pooled_connection, std::error_code> acquire_connection(connection_key key) {
        std::scoped_lock lock{mutex_};

        if (auto idle_connection = take_idle_connection_locked(key)) {
          return std::move(*idle_connection);
        }

        return make_connection_locked(std::move(key));
      }

      void recycle_connection(pooled_connection connection) {
        if (!connection.is_open()) {
          return;
        }

        std::scoped_lock lock{mutex_};
        auto& idle_connections = idle_connections_[connection.key];
        if (idle_connections.size() >= options_.max_idle_connections_per_endpoint) {
          return;
        }

        idle_connections.push_back(std::move(connection));
      }

      void discard_connection([[maybe_unused]] pooled_connection connection) {}

      [[nodiscard]] executor_type get_executor() const noexcept {
        return executor_;
      }

     private:
#ifdef AERO_USE_TLS
      [[nodiscard]] static std::shared_ptr<aero::tls::system_context> make_default_system_tls_context_if_needed() {
        if constexpr (connection_pool::is_secure_transport()) {
          return std::make_shared<aero::tls::system_context>(default_system_tls_version);
        }

        return {};
      }
#endif

      [[nodiscard]] std::expected<pooled_connection, std::error_code> make_connection_locked(connection_key key) {
        try {
          auto id = next_connection_id_++;

          if constexpr (connection_pool::is_secure_transport()) {
#ifdef AERO_USE_TLS
            if (tls_context_ == nullptr) {
              return std::unexpected(http::error::connection_error::tls_context_missing);
            }

            return pooled_connection{
              id,
              std::move(key),
              transport_type{executor_, *tls_context_, options_.transport_buffer_size},
            };
#else
            return std::unexpected(aero::error::basic_error::tls_support_unavailable);
#endif
          } else {
            return pooled_connection{
              id,
              std::move(key),
              transport_type{executor_, options_.transport_buffer_size},
            };
          }
        } catch (const std::bad_alloc&) {
          return std::unexpected(aero::error::basic_error::not_enough_memory);
        }
      }

      [[nodiscard]] std::optional<pooled_connection> take_idle_connection_locked(const connection_key& key) {
        auto endpoint_it = idle_connections_.find(key);
        if (endpoint_it == idle_connections_.end()) {
          return std::nullopt;
        }

        auto& idle_connections = endpoint_it->second;
        while (!idle_connections.empty()) {
          auto connection = std::move(idle_connections.back());
          idle_connections.pop_back();
          if (connection.is_open()) {
            cleanup_endpoint_locked(endpoint_it);
            return connection;
          }
        }

        cleanup_endpoint_locked(endpoint_it);
        return std::nullopt;
      }

      void cleanup_endpoint_locked(
        typename std::unordered_map<connection_key, std::vector<pooled_connection>, connection_key_hash>::iterator
          endpoint_it) {
        if (endpoint_it != idle_connections_.end() && endpoint_it->second.empty()) {
          idle_connections_.erase(endpoint_it);
        }
      }

      [[nodiscard]] static pool_options sanitize_options(pool_options options) {
        if (options.max_idle_connections_per_endpoint == 0U) {
          options.max_idle_connections_per_endpoint = 1U;
        }

        if (options.transport_buffer_size == 0U) {
          options.transport_buffer_size = aero::net::detail::default_buffer_size;
        }

        return options;
      }

      executor_type executor_;
      pool_options options_;
      std::mutex mutex_;
      std::unordered_map<connection_key, std::vector<pooled_connection>, connection_key_hash> idle_connections_;
      std::uint64_t next_connection_id_{1};

#ifdef AERO_USE_TLS
      std::shared_ptr<aero::tls::system_context> owned_tls_context_;
      asio::ssl::context* tls_context_{nullptr};
#endif
    };

   public:
    class leased_connection final {
     public:
      leased_connection() = default;
      leased_connection(const leased_connection&) = delete;
      leased_connection(leased_connection&&) noexcept = default;

      leased_connection& operator=(const leased_connection&) = delete;

      leased_connection& operator=(leased_connection&& other) noexcept {
        if (this == &other) {
          return *this;
        }

        discard();
        state_ = std::move(other.state_);
        connection_ = std::move(other.connection_);
        return *this;
      }

      ~leased_connection() {
        discard();
      }

      [[nodiscard]] explicit operator bool() const noexcept {
        return connection_.has_value();
      }

      [[nodiscard]] std::uint64_t id() const noexcept {
        return connection_ ? connection_->id : 0;
      }

      [[nodiscard]] connection_key key() const {
        return connection_ ? connection_->key : connection_key{};
      }

      [[nodiscard]] bool is_open() {
        return connection_ && connection_->is_open();
      }

      [[nodiscard]] transport_type& transport() & {
        return connection_->transport;
      }

      [[nodiscard]] const transport_type& transport() const& {
        return connection_->transport;
      }

      void release(const http::request& request, const http::response& response) {
        if (connection_pool::can_reuse(request, response)) {
          recycle();
          return;
        }

        discard();
      }

      void recycle() {
        auto state = std::move(state_);
        auto connection = std::move(connection_);
        if (!state || !connection) {
          return;
        }

        state->recycle_connection(std::move(*connection));
      }

      void discard() {
        auto state = std::move(state_);
        auto connection = std::move(connection_);
        if (!state || !connection) {
          return;
        }

        state->discard_connection(std::move(*connection));
      }

      [[nodiscard]] transport_type& transport() && = delete;
      [[nodiscard]] const transport_type& transport() const&& = delete;

     private:
      leased_connection(std::shared_ptr<state> state, pooled_connection connection)
        : state_(std::move(state)), connection_(std::move(connection)) {}

      std::shared_ptr<state> state_;
      std::optional<pooled_connection> connection_;

      friend class connection_pool;
    };

    explicit connection_pool(executor_type executor): connection_pool(std::move(executor), pool_options{}) {}

    connection_pool(executor_type executor, pool_options options)
      : state_(std::make_shared<state>(std::move(executor), options)) {}

#ifdef AERO_USE_TLS
    explicit connection_pool(executor_type executor, asio::ssl::context& tls_context)
      requires(is_secure_transport())
      : connection_pool(std::move(executor), tls_context, pool_options{}) {}

    connection_pool(executor_type executor, asio::ssl::context& tls_context, pool_options options)
      requires(is_secure_transport())
      : state_(std::make_shared<state>(std::move(executor), options, tls_context)) {}
#endif

    [[nodiscard]] std::expected<leased_connection, std::error_code> acquire(std::string host, std::uint16_t port) {
      if (!state_) {
        return std::unexpected(http::error::connection_error::pool_unavailable);
      }

      auto key = make_key(std::move(host), port);
      if (key.host.empty()) {
        return std::unexpected(http::error::connection_error::endpoint_host_empty);
      }

      if (key.port == 0U) {
        return std::unexpected(http::error::connection_error::endpoint_port_invalid);
      }

      auto connection = state_->acquire_connection(std::move(key));
      if (!connection.has_value()) {
        return std::unexpected(connection.error());
      }

      return leased_connection{state_, std::move(*connection)};
    }

    [[nodiscard]] executor_type get_executor() const noexcept {
      return state_ ? state_->get_executor() : executor_type{};
    }

    [[nodiscard]] static bool can_reuse(const http::request& request, const http::response& response) {
      return can_reuse(request.method, request.protocol, request.headers, response);
    }

    [[nodiscard]] static bool can_reuse(http::method request_method, http::version request_version,
      const http::headers& request_headers, const http::response& response) {
      auto response_version = response.status_line.version();

      if (!request_enables_keep_alive(request_version, request_headers)) {
        return false;
      }

      if (!response_enables_keep_alive(response_version, response.headers)) {
        return false;
      }

      if (is_upgrade_response(request_headers, response)) {
        return false;
      }

      return !response_requires_eof(request_method, response);
    }

   private:
    [[nodiscard]] static connection_key make_key(std::string host, std::uint16_t port) {
      return connection_key{
        .host = aero::detail::to_lowercase(host),
        .port = port,
      };
    }

    [[nodiscard]] static bool request_enables_keep_alive(http::version version, const http::headers& headers) {
      if (headers.contains_token("connection", "close")) {
        return false;
      }

      switch (version) {
      case http::version::http1_1:
        return true;
      case http::version::http1_0:
        return headers.contains_token("connection", "keep-alive");
      default:
        return false;
      }
    }

    [[nodiscard]] static bool response_enables_keep_alive(http::version version, const http::headers& headers) {
      if (headers.contains_token("connection", "close")) {
        return false;
      }

      switch (version) {
      case http::version::http1_1:
        return true;
      case http::version::http1_0:
        return headers.contains_token("connection", "keep-alive");
      default:
        return false;
      }
    }

    [[nodiscard]] static bool is_bodyless_response(http::method request_method, http::status_code status_code) noexcept {
      auto status_value = std::to_underlying(status_code);
      bool is_informational_status_code =
        (status_value >= informational_status_code_min && status_value < informational_status_code_max);

      return request_method == http::method::head || is_informational_status_code ||
             status_code == http::status_code::no_content || status_code == http::status_code::reset_content ||
             status_code == http::status_code::not_modified;
    }

    static void trim_http_whitespace(std::string_view& value) noexcept {
      while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
      }

      while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
      }
    }

    [[nodiscard]] static transfer_encoding_info inspect_transfer_encoding(const http::headers& headers) {
      auto serialized_headers = headers.serialize();
      auto remaining = std::string_view{serialized_headers};

      transfer_encoding_info info;
      std::vector<std::string> codings;

      while (!remaining.empty()) {
        auto line_end = remaining.find(http::detail::crlf);
        if (line_end == std::string_view::npos) {
          break;
        }

        auto line = remaining.substr(0, line_end);
        remaining.remove_prefix(line_end + http::detail::crlf.size());

        if (line.empty()) {
          break;
        }

        auto value_separator = line.find(':');
        if (value_separator == std::string_view::npos) {
          continue;
        }

        auto header_name = aero::detail::to_lowercase(std::string{line.substr(0, value_separator)});
        if (header_name != "transfer-encoding") {
          continue;
        }

        info.present = true;

        auto value = line.substr(value_separator + 1U);
        for (;;) {
          auto token_separator = value.find(',');
          auto token = token_separator == std::string_view::npos ? value : value.substr(0, token_separator);
          trim_http_whitespace(token);

          if (token.empty()) {
            info.invalid = true;
            return info;
          }

          codings.push_back(aero::detail::to_lowercase(std::string{token}));

          if (token_separator == std::string_view::npos) {
            break;
          }

          value.remove_prefix(token_separator + 1U);
        }
      }

      if (!info.present) {
        return info;
      }

      if (codings.empty()) {
        info.invalid = true;
        return info;
      }

      auto chunked_count = std::ranges::count(codings, std::string{"chunked"});
      info.final_chunked = codings.back() == "chunked";
      info.invalid = chunked_count > 1U || (chunked_count == 1U && !info.final_chunked);

      return info;
    }

    [[nodiscard]] static transfer_encoding_framing classify_transfer_encoding(http::version version,
      const http::headers& headers) {
      auto info = inspect_transfer_encoding(headers);
      if (!info.present) {
        return transfer_encoding_framing::none;
      }

      if (info.invalid) {
        return transfer_encoding_framing::invalid;
      }

      if (version != http::version::http1_1) {
        return transfer_encoding_framing::invalid;
      }

      if (info.final_chunked) {
        return transfer_encoding_framing::chunked;
      }

      return transfer_encoding_framing::close_delimited;
    }

    [[nodiscard]] static bool is_successful_connect_response(http::method request_method,
      http::status_code status_code) noexcept {
      auto status_value = std::to_underlying(status_code);
      bool is_succesfull_status_code =
        (status_value >= succesfull_status_code_min && status_value < succesfull_status_code_max);

      return request_method == http::method::connect and is_succesfull_status_code;
    }

    [[nodiscard]] static bool response_requires_eof(http::method request_method, const http::response& response) {
      if (is_successful_connect_response(request_method, response.status_code())) {
        return true;
      }

      if (is_bodyless_response(request_method, response.status_code())) {
        return false;
      }

      auto framing = classify_transfer_encoding(response.status_line.version(), response.headers);

      if (framing == transfer_encoding_framing::chunked) {
        return false;
      }

      if (framing == transfer_encoding_framing::close_delimited || framing == transfer_encoding_framing::invalid) {
        return true;
      }

      if (response.headers.contains("content-length")) {
        return false;
      }

      return true;
    }

    [[nodiscard]] static bool is_upgrade_response(const http::headers& request_headers, const http::response& response) {
      if (response.status_code() == http::status_code::switching_protocols) {
        return true;
      }

      if (request_headers.contains_token("connection", "upgrade") || response.headers.contains_token("connection", "upgrade")) {
        return true;
      }

      return request_headers.contains("upgrade") || response.headers.contains("upgrade");
    }

    std::shared_ptr<state> state_;
  };

} // namespace aero::http::detail
