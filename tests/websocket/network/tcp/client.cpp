#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <future>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <vector>

#include <asio/error.hpp>

#include "aero/deadline.hpp"

#include "aero/websocket/client.hpp"
#include "aero/websocket/close_code.hpp"
#include "aero/websocket/error.hpp"
#include "aero/websocket/message.hpp"

#include "websocket/test_helpers.hpp"

namespace websocket = aero::websocket;

namespace {

  using namespace std::chrono_literals;

  using std::chrono::steady_clock;
  using websocket::client;
  using websocket::close_code;
  using websocket::message;
  using websocket::error::protocol_error;

  using aero::tests::websocket::to_bytes;

  constexpr std::string_view echo_endpoint = "ws://websockets.chilkat.io/wsChilkatEcho.ashx";

  std::string unique_text(std::string_view prefix) {
    const auto ticks = static_cast<std::uint64_t>(steady_clock::now().time_since_epoch().count());
    return std::string(prefix) + std::to_string(ticks);
  }

  std::vector<std::byte> unique_binary_payload() {
    const auto ticks = static_cast<std::uint64_t>(steady_clock::now().time_since_epoch().count());
    std::vector<std::byte> payload;
    for (std::size_t i{}; i < sizeof(ticks); ++i) {
      payload.push_back(std::byte{static_cast<unsigned char>((ticks >> (i * 8U)) & 0xFFU)});
    }

    return payload;
  }

  bool is_timeout_error(const std::error_code& ec) {
    // asio::cancel_after returns asio::error::operation_aborted
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

TEST(WebsocketNetworkTcpClient, ConnectSucceedsAndIsOpenForWriting) {
  client websocket_client{};

  auto connect_result = websocket_client.connect(echo_endpoint);
  ASSERT_TRUE(connect_result);

  EXPECT_TRUE(websocket_client.is_open_for_writing());

  auto close_ec = websocket_client.close(close_code::normal, "bye");
  EXPECT_FALSE(close_ec);

  EXPECT_TRUE(websocket_client.is_closed());
  EXPECT_FALSE(websocket_client.is_open_for_writing());
}

TEST(WebsocketNetworkTcpClient, TextAndPingRoundtripReturnsTextEchoAndPong) {
  client websocket_client{};

  auto connect_result = websocket_client.connect(echo_endpoint);
  ASSERT_TRUE(connect_result);
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

TEST(WebsocketNetworkTcpClient, BinaryRoundtripReturnsBinaryEcho) {
  client websocket_client{};

  auto connect_result = websocket_client.connect(echo_endpoint);
  ASSERT_TRUE(connect_result);
  ASSERT_TRUE(websocket_client.is_open_for_writing());

  const auto binary_payload = unique_binary_payload();

  auto send_ec = websocket_client.send_binary(binary_payload);
  ASSERT_FALSE(send_ec);

  bool got_binary_echo = false;

  auto read_loop_ec = read_until(
    websocket_client,
    2s,
    64,
    [&](const message& received_message) {
      if (!received_message.is_binary()) {
        return;
      }
      if (std::ranges::equal(received_message.bytes(), binary_payload)) {
        got_binary_echo = true;
      }
    },
    [&]() { return got_binary_echo; });

  ASSERT_FALSE(read_loop_ec);
  EXPECT_TRUE(got_binary_echo);

  auto close_ec = websocket_client.close(close_code::normal, "done");
  EXPECT_FALSE(close_ec);
}

TEST(WebsocketNetworkTcpClient, ReadAfterCloseReturnsConnectionClosed) {
  client websocket_client{};

  auto connect_result = websocket_client.connect(echo_endpoint);
  ASSERT_TRUE(connect_result);
  ASSERT_TRUE(websocket_client.is_open_for_writing());

  auto close_ec = websocket_client.close(close_code::normal, "closing");
  ASSERT_FALSE(close_ec);

  EXPECT_FALSE(websocket_client.is_open_for_writing());
  EXPECT_TRUE(websocket_client.is_closed());

  auto read_result = websocket_client.read(50ms);
  ASSERT_FALSE(read_result);
  EXPECT_EQ(read_result.error(), protocol_error::connection_closed);
}

TEST(WebsocketNetworkTcpClient, SendAfterCloseReturnsConnectionClosed) {
  client websocket_client{};

  auto connect_result = websocket_client.connect(echo_endpoint);
  ASSERT_TRUE(connect_result);
  ASSERT_TRUE(websocket_client.is_open_for_writing());

  auto close_ec = websocket_client.close(close_code::normal, "closing");
  ASSERT_FALSE(close_ec);

  EXPECT_TRUE(websocket_client.is_closed());

  auto send_ec = websocket_client.send_text("x");
  EXPECT_EQ(send_ec, protocol_error::connection_closed);
}

TEST(WebsocketNetworkTcpClient, SyncApiRoundtripWorks) {
  client websocket_client{};

  auto result = sync_roundtrip(websocket_client, 3s);

  EXPECT_FALSE(result.connect_ec);
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

TEST(WebsocketNetworkTcpClient, ParallelReadAndCloseCompletesWithoutDeadlock) {
  client websocket_client{};

  auto connect_result = websocket_client.connect(echo_endpoint);
  ASSERT_TRUE(connect_result);
  ASSERT_TRUE(websocket_client.is_open_for_writing());

  auto read_future = std::async(std::launch::async, [&] { return websocket_client.read(); });

  std::this_thread::sleep_for(50ms);

  auto close_future =
    std::async(std::launch::async, [&] { return websocket_client.close(close_code::normal, "parallel-close"); });

  ASSERT_EQ(close_future.wait_for(6s), std::future_status::ready);
  auto close_ec = close_future.get();
  EXPECT_FALSE(close_ec);

  ASSERT_EQ(read_future.wait_for(6s), std::future_status::ready);
  auto read_result = read_future.get();

  if (read_result) {
    EXPECT_TRUE(read_result->is_close() || read_result->is_control() || read_result->is_text() || read_result->is_binary());
  } else {
    EXPECT_TRUE(
      read_result.error() == protocol_error::connection_closed || read_result.error() == asio::error::operation_aborted);
  }

  EXPECT_TRUE(websocket_client.is_closed());
  EXPECT_FALSE(websocket_client.is_open_for_writing());
}

TEST(WebsocketNetworkTcpClient, ParallelReadAndConcurrentSendsReturnAllEchoes) {
  client websocket_client{};

  auto connect_result = websocket_client.connect(echo_endpoint);
  ASSERT_TRUE(connect_result);
  ASSERT_TRUE(websocket_client.is_open_for_writing());

  constexpr std::size_t messages_total = 12;
  constexpr std::size_t threads_total = 4;

  std::vector<std::string> payloads;
  payloads.reserve(messages_total);

  std::unordered_set<std::string> expected_payloads;
  expected_payloads.reserve(messages_total);

  for (std::size_t i = 0; i < messages_total; ++i) {
    auto payload = unique_text("aero-parallel-text-");
    expected_payloads.insert(payload);
    payloads.push_back(std::move(payload));
  }

  std::atomic<std::size_t> send_errors{0};

  auto send_worker = [&](std::size_t start_index, std::size_t end_index) {
    for (std::size_t index = start_index; index < end_index; ++index) {
      auto send_ec = websocket_client.send_text(payloads[index]);
      if (send_ec) {
        ++send_errors;
      }
    }
  };

  std::vector<std::jthread> sender_threads;
  sender_threads.reserve(threads_total);

  const std::size_t chunk = (messages_total + threads_total - 1) / threads_total;
  for (std::size_t thread_index = 0; thread_index < threads_total; ++thread_index) {
    const std::size_t start_index = thread_index * chunk;
    const std::size_t end_index = std::min(messages_total, start_index + chunk);
    sender_threads.emplace_back(send_worker, start_index, end_index);
  }

  std::mutex received_mutex;
  std::unordered_set<std::string> received_payloads;
  received_payloads.reserve(messages_total);

  auto read_loop_ec = read_until(
    websocket_client,
    4s,
    256,
    [&](const message& received_message) {
      if (!received_message.is_text()) {
        return;
      }
      std::string payload(received_message.text());
      std::scoped_lock lock(received_mutex);
      received_payloads.insert(std::move(payload));
    },
    [&]() {
      std::scoped_lock lock(received_mutex);
      return received_payloads.size() >= expected_payloads.size();
    });

  for (auto& thread : sender_threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  ASSERT_FALSE(read_loop_ec);
  EXPECT_EQ(send_errors.load(), 0U);

  {
    std::scoped_lock lock(received_mutex);
    EXPECT_EQ(received_payloads.size(), expected_payloads.size());
    EXPECT_TRUE(
      std::ranges::all_of(expected_payloads, [&](const std::string& text) { return received_payloads.contains(text); }));
  }

  auto close_ec = websocket_client.close(close_code::normal, "done");
  EXPECT_FALSE(close_ec);
}
