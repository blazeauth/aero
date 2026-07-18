#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <ut/ut.hpp>

#include "aero/util/deadline.hpp"

#include "aero/tls/system_context.hpp"
#include "aero/tls/version.hpp"
#include "aero/websocket/close_code.hpp"
#include "aero/websocket/error.hpp"
#include "aero/websocket/message.hpp"
#include "aero/websocket/tls/client.hpp"

#include "websocket/test_helpers.hpp"

using namespace ut;

namespace websocket = aero::websocket;

using namespace std::chrono_literals;

using std::chrono::steady_clock;
using websocket::close_code;
using websocket::message;
using websocket::protocol_error;
using websocket::tls::client;

using aero::tests::websocket::to_bytes;

aero::tls::system_context system_tls_ctx{aero::tls::version::tlsv1_2};

// Note that Postman websocket is text-only endpoint that does not support binary frames
constexpr std::string_view echo_endpoint = "wss://ws.postman-echo.com/raw";

std::string unique_text(std::string_view prefix) {
  const auto ticks = static_cast<std::uint64_t>(steady_clock::now().time_since_epoch().count());
  return std::string(prefix) + std::to_string(ticks);
}

bool is_timeout_error(const std::error_code& ec) {
  return ec == aero::errc::canceled;
}

std::error_code connect_to_echo(client& websocket_client) {
  [[maybe_unused]] auto [connect_ec, server_response] = websocket_client.connect(echo_endpoint);
  return connect_ec;
}

template <typename MessageHandler, typename CompletionPredicate>
std::error_code read_until(client& websocket_client, std::chrono::milliseconds overall_timeout,
  std::size_t max_successful_messages, MessageHandler message_handler, CompletionPredicate completion_predicate) {
  aero::deadline overall_deadline{overall_timeout};
  std::size_t successful_messages = 0;

  if (completion_predicate()) {
    return {};
  }

  for (;;) {
    if (overall_deadline.expired() || successful_messages >= max_successful_messages) {
      break;
    }

    const auto poll_timeout = overall_deadline.clamp(200ms);
    if (poll_timeout <= 0ms) {
      break;
    }

    auto read_result = websocket_client.read(poll_timeout);
    if (!read_result) {
      if (is_timeout_error(read_result.error())) {
        continue;
      }
      return read_result.error();
    }

    ++successful_messages;
    message_handler(*read_result);
  }

  return {};
}

struct sync_roundtrip_result {
  std::error_code connect_ec;
  std::error_code send_text_ec;
  std::error_code ping_ec;
  bool got_text{};
  bool got_pong{};
  std::error_code close_ec;
  bool open_for_writing_after_connect{};
  bool open_for_writing_after_close{};
  std::error_code read_loop_ec;
};

sync_roundtrip_result sync_roundtrip(client& websocket_client, std::chrono::milliseconds overall_timeout) {
  sync_roundtrip_result result{};

  auto connect_ec = connect_to_echo(websocket_client);
  if (connect_ec) {
    result.connect_ec = connect_ec;
    result.open_for_writing_after_connect = websocket_client.is_open_for_writing();
    return result;
  }

  result.open_for_writing_after_connect = websocket_client.is_open_for_writing();

  const auto text_payload = unique_text("aero-sync-text-");
  result.send_text_ec = websocket_client.send_text(text_payload);
  if (result.send_text_ec) {
    return result;
  }

  const auto ping_payload = unique_text("aero-sync-ping-");
  result.ping_ec = websocket_client.ping(ping_payload);
  if (result.ping_ec) {
    return result;
  }

  const auto expected_pong_bytes = to_bytes(ping_payload);

  result.read_loop_ec = read_until(
    websocket_client,
    overall_timeout,
    64,
    [&](const message& received_message) {
      if (received_message.is_text() && received_message.text() == text_payload) {
        result.got_text = true;
      }
      if (received_message.is_pong() && std::ranges::equal(received_message.bytes(), expected_pong_bytes)) {
        result.got_pong = true;
      }
    },
    [&]() { return result.got_text && result.got_pong; });

  result.close_ec = websocket_client.close(close_code::normal, "aero is leaving, bye-bye!");
  result.open_for_writing_after_close = websocket_client.is_open_for_writing();

  return result;
}

int main() {
  suite websocket_network_tls_client = [] {
    "connect succeeds and is open for writing"_test = [] {
      client websocket_client{system_tls_ctx};

      auto connect_ec = connect_to_echo(websocket_client);
      expect[not connect_ec];

      expect(websocket_client.is_open_for_writing());

      auto close_ec = websocket_client.close(close_code::normal, "bye");
      expect(not close_ec);

      expect(websocket_client.is_closed());
      expect(not websocket_client.is_open_for_writing());
    };

    "text and ping roundtrip returns text echo and pong"_test = [] {
      client websocket_client{system_tls_ctx};

      auto connect_ec = connect_to_echo(websocket_client);
      expect[not connect_ec];
      expect[websocket_client.is_open_for_writing()];

      const auto text_payload = unique_text("aero-text-");
      const auto ping_payload = unique_text("aero-ping-");

      auto send_text_ec = websocket_client.send_text(text_payload);
      expect[not send_text_ec];

      auto ping_ec = websocket_client.ping(ping_payload);
      expect[not ping_ec];

      const auto expected_pong_bytes = to_bytes(ping_payload);

      bool got_text_echo = false;
      bool got_expected_pong = false;

      auto read_loop_ec = read_until(
        websocket_client,
        2s,
        64,
        [&](const message& received_message) {
          if (received_message.is_text() && received_message.text() == text_payload) {
            got_text_echo = true;
          }
          if (received_message.is_pong() && std::ranges::equal(received_message.bytes(), expected_pong_bytes)) {
            got_expected_pong = true;
          }
        },
        [&]() { return got_text_echo && got_expected_pong; });

      expect[not read_loop_ec];

      expect(got_text_echo);
      expect(got_expected_pong);

      auto close_ec = websocket_client.close(close_code::normal, "done");
      expect(not close_ec);
    };

    "read after close returns connection closed"_test = [] {
      client websocket_client{system_tls_ctx};

      auto connect_ec = connect_to_echo(websocket_client);
      expect[not connect_ec];
      expect[websocket_client.is_open_for_writing()];

      auto close_ec = websocket_client.close(close_code::normal, "closing");
      expect[not close_ec];

      expect(not websocket_client.is_open_for_writing());
      expect(websocket_client.is_closed());

      auto read_result = websocket_client.read(50ms);
      expect[not read_result.has_value()];
      expect(read_result.error() == protocol_error::connection_closed);
    };

    "send after close returns connection closed"_test = [] {
      client websocket_client{system_tls_ctx};

      auto connect_ec = connect_to_echo(websocket_client);
      expect[not connect_ec];
      expect[websocket_client.is_open_for_writing()];

      auto close_ec = websocket_client.close(close_code::normal, "closing");
      expect[not close_ec];

      expect(websocket_client.is_closed());

      auto send_ec = websocket_client.send_text("x");
      expect(send_ec == protocol_error::connection_closed);
    };

    "sync api roundtrip works"_test = [] {
      client websocket_client{system_tls_ctx};

      auto result = sync_roundtrip(websocket_client, 3s);

      expect(not result.connect_ec);
      expect(result.open_for_writing_after_connect);

      expect(not result.send_text_ec);
      expect(not result.ping_ec);

      expect(not result.read_loop_ec);

      expect(result.got_text);
      expect(result.got_pong);

      expect(not result.close_ec);
      expect(not result.open_for_writing_after_close);

      auto read_after_close = websocket_client.read(50ms);
      expect[not read_after_close.has_value()];
      expect(read_after_close.error() == protocol_error::connection_closed);
    };
  };
}
