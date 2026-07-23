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
#include <ut/ut.hpp>
#include <vector>

#include <asio/error.hpp>

#include "aero/util/deadline.hpp"
#include "aero/websocket/client.hpp"
#include "aero/websocket/close_code.hpp"
#include "aero/websocket/error.hpp"
#include "aero/websocket/message.hpp"

#include "websocket/test_helpers.hpp"

using namespace ut;

namespace websocket = aero::websocket;

using namespace std::chrono_literals;

using std::chrono::steady_clock;
using websocket::client;
using websocket::close_code;
using websocket::message;
using websocket::protocol_error;

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
  suite websocket_network_tcp_client = [] {
    "connect succeeds and is open for writing"_test = [] {
      client websocket_client{};

      auto connect_ec = connect_to_echo(websocket_client);
      expect[not connect_ec];

      expect(websocket_client.is_open_for_writing());

      auto close_ec = websocket_client.close(close_code::normal, "bye");
      expect(not close_ec);

      expect(websocket_client.is_closed());
      expect(not websocket_client.is_open_for_writing());
    };

    "text and ping roundtrip returns text echo and pong"_test = [] {
      client websocket_client{};

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

    "binary roundtrip returns binary echo"_test = [] {
      client websocket_client{};

      auto connect_ec = connect_to_echo(websocket_client);
      expect[not connect_ec];
      expect[websocket_client.is_open_for_writing()];

      const auto binary_payload = unique_binary_payload();

      auto send_ec = websocket_client.send_binary(binary_payload);
      expect[not send_ec];

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

      expect[not read_loop_ec];
      expect(got_binary_echo);

      auto close_ec = websocket_client.close(close_code::normal, "done");
      expect(not close_ec);
    };

    "read after close returns connection closed"_test = [] {
      client websocket_client{};

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
      client websocket_client{};

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
      client websocket_client{};

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

    "parallel read and close completes without deadlock"_test = [] {
      client websocket_client{};

      auto connect_ec = connect_to_echo(websocket_client);
      expect[not connect_ec];
      expect[websocket_client.is_open_for_writing()];

      auto read_future = std::async(std::launch::async, [&] { return websocket_client.read(); });

      std::this_thread::sleep_for(50ms);

      auto close_future =
        std::async(std::launch::async, [&] { return websocket_client.close(close_code::normal, "parallel-close"); });

      expect[close_future.wait_for(6s) == std::future_status::ready];
      auto close_ec = close_future.get();
      expect(not close_ec);

      expect[read_future.wait_for(6s) == std::future_status::ready];
      auto read_result = read_future.get();

      if (read_result) {
        expect(read_result->is_close() or read_result->is_control() or read_result->is_text() or read_result->is_binary());
      } else {
        expect(
          read_result.error() == protocol_error::connection_closed or read_result.error() == asio::error::operation_aborted);
      }

      expect(websocket_client.is_closed());
      expect(not websocket_client.is_open_for_writing());
    };

    "parallel read and concurrent sends return all echoes"_test = [] {
      client websocket_client{};

      auto connect_ec = connect_to_echo(websocket_client);
      expect[not connect_ec];
      expect[websocket_client.is_open_for_writing()];

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

      expect[not read_loop_ec];
      expect(send_errors.load() == 0U);

      {
        std::scoped_lock lock(received_mutex);
        expect(received_payloads.size() == expected_payloads.size());
        expect(
          std::ranges::all_of(expected_payloads, [&](const std::string& text) { return received_payloads.contains(text); }));
      }

      auto close_ec = websocket_client.close(close_code::normal, "done");
      expect(not close_ec);
    };
  };
}
