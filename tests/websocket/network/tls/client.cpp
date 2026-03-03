#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

#include "aero/deadline.hpp"

#include "aero/tls/system_context.hpp"
#include "aero/tls/version.hpp"
#include "aero/websocket/close_code.hpp"
#include "aero/websocket/error.hpp"
#include "aero/websocket/message.hpp"
#include "aero/websocket/tls/client.hpp"

#include "websocket/test_helpers.hpp"

namespace websocket = aero::websocket;

namespace {

  using namespace std::chrono_literals;

  using std::chrono::steady_clock;
  using websocket::close_code;
  using websocket::message;
  using websocket::error::protocol_error;
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
    return ec == aero::error::errc::canceled;
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

    auto connect_result = websocket_client.connect(echo_endpoint);
    if (!connect_result) {
      result.connect_ec = connect_result.error();
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

} // namespace

TEST(WebsocketNetworkTlsClient, ConnectSucceedsAndIsOpenForWriting) {
  client websocket_client{system_tls_ctx};

  auto connect_result = websocket_client.connect(echo_endpoint);
  ASSERT_TRUE(connect_result) << connect_result.error().category().name() << " : " << connect_result.error().message();

  EXPECT_TRUE(websocket_client.is_open_for_writing());

  auto close_ec = websocket_client.close(close_code::normal, "bye");
  EXPECT_FALSE(close_ec);

  EXPECT_TRUE(websocket_client.is_closed());
  EXPECT_FALSE(websocket_client.is_open_for_writing());
}

TEST(WebsocketNetworkTlsClient, TextAndPingRoundtripReturnsTextEchoAndPong) {
  client websocket_client{system_tls_ctx};

  auto connect_result = websocket_client.connect(echo_endpoint);
  ASSERT_TRUE(connect_result) << connect_result.error().category().name() << " : " << connect_result.error().message();
  ASSERT_TRUE(websocket_client.is_open_for_writing());

  const auto text_payload = unique_text("aero-text-");
  const auto ping_payload = unique_text("aero-ping-");

  auto send_text_ec = websocket_client.send_text(text_payload);
  ASSERT_FALSE(send_text_ec);

  auto ping_ec = websocket_client.ping(ping_payload);
  ASSERT_FALSE(ping_ec);

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

  ASSERT_FALSE(read_loop_ec);

  EXPECT_TRUE(got_text_echo);
  EXPECT_TRUE(got_expected_pong);

  auto close_ec = websocket_client.close(close_code::normal, "done");
  EXPECT_FALSE(close_ec);
}

TEST(WebsocketNetworkTlsClient, ReadAfterCloseReturnsConnectionClosed) {
  client websocket_client{system_tls_ctx};

  auto connect_result = websocket_client.connect(echo_endpoint);
  ASSERT_TRUE(connect_result) << connect_result.error().category().name() << " : " << connect_result.error().message();
  ASSERT_TRUE(websocket_client.is_open_for_writing());

  auto close_ec = websocket_client.close(close_code::normal, "closing");
  ASSERT_FALSE(close_ec);

  EXPECT_FALSE(websocket_client.is_open_for_writing());
  EXPECT_TRUE(websocket_client.is_closed());

  auto read_result = websocket_client.read(50ms);
  ASSERT_FALSE(read_result);
  EXPECT_EQ(read_result.error(), protocol_error::connection_closed);
}

TEST(WebsocketNetworkTlsClient, SendAfterCloseReturnsConnectionClosed) {
  client websocket_client{system_tls_ctx};

  auto connect_result = websocket_client.connect(echo_endpoint);
  ASSERT_TRUE(connect_result) << connect_result.error().category().name() << " : " << connect_result.error().message();
  ASSERT_TRUE(websocket_client.is_open_for_writing());

  auto close_ec = websocket_client.close(close_code::normal, "closing");
  ASSERT_FALSE(close_ec);

  EXPECT_TRUE(websocket_client.is_closed());

  auto send_ec = websocket_client.send_text("x");
  EXPECT_EQ(send_ec, protocol_error::connection_closed);
}

TEST(WebsocketNetworkTlsClient, SyncApiRoundtripWorks) {
  client websocket_client{system_tls_ctx};

  auto result = sync_roundtrip(websocket_client, 3s);

  EXPECT_FALSE(result.connect_ec) << result.connect_ec.category().name() << " : " << result.connect_ec.message();
  EXPECT_TRUE(result.open_for_writing_after_connect);

  EXPECT_FALSE(result.send_text_ec);
  EXPECT_FALSE(result.ping_ec);

  EXPECT_FALSE(result.read_loop_ec);

  EXPECT_TRUE(result.got_text);
  EXPECT_TRUE(result.got_pong);

  EXPECT_FALSE(result.close_ec);
  EXPECT_FALSE(result.open_for_writing_after_close);

  auto read_after_close = websocket_client.read(50ms);
  ASSERT_FALSE(read_after_close);
  EXPECT_EQ(read_after_close.error(), protocol_error::connection_closed);
}
