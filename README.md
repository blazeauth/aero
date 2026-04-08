# Aero: Modern C++ Networking Library

<img width="1920" height="302" alt="Github Aero Image" src="https://github.com/user-attachments/assets/d05b01e9-8259-46f5-ae27-c81bf98027b5" />


## TLDR

Aero is a lightweight, header-only networking library for modern C++.

Library gives you a clean client-side API for **WebSocket** and **HTTP/1.0 / HTTP/1.1**, with optional **TLS** support. The project is built around a pretty simple idea: networking code should be easy to write, fast in practice, and not turn into a heavyweight dependency just because you needed a client.

The library compiles with both OpenSSL and wolfSSL. The asynchronous model is built on asio completion tokens and feels like an extension of asio rather than part of a different library, in other words, the library is designed to feel like a pleasant addition to asio, but it doesn't adhere to the asio style as strictly as, say, Boost-Beast.

Here's an example of GET request:
```cpp
#include <print>

#include "aero/http.hpp"

namespace http = aero::http;

int main() {
  std::expected<http::response, std::error_code> response = http::get("https://example.com/");
  if (!response) {
    std::println("Request failed: {}", response.error().message());
    return 1;
  }

  std::println("Received response from example.com:");

  std::println("Response Headers:");
  for (const auto& [name, value] : response->headers) {
    std::println("{}: {}", name, value);
  }
  
  std::println("Status: {} ({})", response->status_line.reason_phrase, response->status_code());

  if (response->content_type() == "text/html") {
    std::println("Body (first 100 bytes): {}", response->text().substr(0, 100));
  } else {
    std::println("Body: {}", response->text());
  }
}
```

### More examples:
#### WebSocket Client
<details>
<summary>WebSocket Secure Binance Stream (<strong>Sync</strong>)</summary>

```cpp
#include <print>

#include "aero/deadline.hpp"
#include "aero/error.hpp"
#include "aero/tls/system_context.hpp"
#include "aero/tls/version.hpp"
#include "aero/websocket/close_code.hpp"
#include "aero/websocket/tls/client.hpp"

namespace websocket = aero::websocket;
namespace tls = aero::tls;
namespace http = aero::http;

void print_error(std::string_view message, const std::error_code& ec) {
  std::println("{}: {} ({} - {})", message, ec.message(), ec.value(), ec.category().name());
}

void print_headers(const http::headers& headers) {
  std::println("[HEADERS] Printing:");
  for (const auto& [name, value] : headers) {
    std::println("{}: {}", name, value);
  }
  std::println("[HEADERS] Done");
}

int main() {
  using namespace std::chrono_literals;

  // System context simply wraps asio::ssl::context and implements
  // AIA fetching for Win32, otherwise sets default verify paths
  tls::system_context tls_ctx{tls::version::tlsv1_2};
  tls_ctx.disable_deprecated_versions();

  websocket::tls::client client{tls_ctx.context()};

  auto handshake_headers = client.connect("wss://stream.binance.com:9443/ws/btcusdt@trade", 5s);
  if (!handshake_headers) {
    print_error("Connect to binance stream failed", handshake_headers.error());
    return 1;
  }

  std::println("Succesfully connected");
  print_headers(*handshake_headers);

  aero::deadline deadline{5min};

  for (;;) {
    if (deadline.expired()) {
      break;
    }

    auto message = client.read(deadline.remaining());
    if (!message) {
      if (message.error() == aero::error::errc::timeout && deadline.expired()) {
        std::println("Read deadline expired, breaking from read-loop");
        break;
      }
      print_error("Failed to receive message from binance stream", message.error());
      break;
    }

    if (!message->is_text()) {
      std::println("Received non-text message type ({}), skipping", message->kind);
      continue;
    }

    std::println("Received message from binance stream: {}", message->text());
  }

  auto close_ec = client.close(websocket::close_code::normal, "thank you, we are leaving.");
  if (close_ec) {
    print_error("Close handshake failed", close_ec);
    std::ignore = client.force_close();
  }

  return 0;
}
```
</details>

<details>
<summary>WebSocket Secure Postman Echo (<strong>Coro</strong>)</summary>

```cpp
#include <chrono>
#include <future>
#include <print>
#include <system_error>

#include <asio/awaitable.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include "aero/http/headers.hpp"
#include "aero/io_runtime.hpp"
#include "aero/tls/initialize.hpp"
#include "aero/tls/system_context.hpp"
#include "aero/tls/version.hpp"
#include "aero/wait_threads.hpp"
#include "aero/websocket/close_code.hpp"
#include "aero/websocket/tls/client.hpp"

using namespace std::chrono_literals;
namespace websocket = aero::websocket;

void print_error(std::string_view message, const std::error_code& ec) {
  std::println("{}: {} ({} - {})", message, ec.message(), ec.value(), ec.category().name());
}

void print_headers(const aero::http::headers& headers) {
  std::println("[HEADERS] Printing:");
  for (const auto& [name, value] : headers) {
    std::println("{}: {}", name, value);
  }
  std::println("[HEADERS] Done");
}

asio::awaitable<std::error_code> async_run_echo_client(websocket::tls::client& client) {
  // https://blog.postman.com/introducing-postman-websocket-echo-service/
  auto [connect_ec, headers] =
    co_await client.async_connect("wss://ws.postman-echo.com/raw", asio::as_tuple(asio::use_awaitable));
  if (connect_ec) {
    co_return connect_ec;
  }

  print_headers(headers);

  auto [write_ec] = co_await client.async_send_text("hello from aero client!!!", asio::as_tuple(asio::use_awaitable));
  if (write_ec) {
    co_return write_ec;
  }

  auto [read_ec, message] = co_await client.async_read(asio::cancel_after(1500ms, asio::as_tuple(asio::use_awaitable)));
  if (read_ec) {
    co_return read_ec;
  }

  std::println("Received message from postman echo server. Kind: {}. Text: {}", message.kind, message.text());

  auto [close_ec] = co_await client.async_close(websocket::close_code::normal, asio::as_tuple(asio::use_awaitable));
  if (close_ec) {
    if (close_ec == aero::error::errc::timeout) {
      co_await client.async_force_close(asio::use_awaitable);
      co_return std::error_code{};
    }
    co_return close_ec;
  }

  co_return std::error_code{};
}

int main() {
  // If using wolfSSL, this step is required
  aero::tls::initialize_library();

  aero::io_runtime runtime(1, aero::wait_threads);

  aero::tls::system_context tls_context{aero::tls::version::tlsv1_2};
  tls_context.disable_deprecated_versions();

  websocket::tls::client client{runtime.get_executor(), tls_context};

  try {
    // All coroutines should use client executor to serialize all
    // of the operations correctly & prevent any race conditions
    auto echo_ec = asio::co_spawn(client.get_executor(), async_run_echo_client(client), asio::use_future).get();
    if (echo_ec) {
      print_error("Postman echo client failed", echo_ec);
    }
  } catch (const std::system_error& e) {
    print_error("System error exception catched", e.code());
  } catch (const std::future_error& e) {
    print_error("Future error exception catched", e.code());
  }
}
```
</details>

<details>
<summary>WebSocket Chilkat Echo (<strong>Sync</strong>)</summary>

```cpp
#include <print>
#include <string_view>

#include "aero/websocket/client.hpp"

namespace websocket = aero::websocket;

void print_error(std::string_view message, const std::error_code& ec) {
  std::println("{}: {} ({} - {})", message, ec.message(), ec.value(), ec.category().name());
}

void print_message(const websocket::message& message) {
  switch (message.kind) {
  case websocket::message_kind::text:
    std::println("Received text: {}", message.text());
    break;
  case websocket::message_kind::binary:
    std::println("Received binary of size {}", message.payload.size());
    break;
  case websocket::message_kind::pong:
    if (message.has_payload()) {
      // Assume that the ping content was valid UTF-8 text, so we expect the same payload to be echoed
      std::println("Received pong with payload: {}", message.text());
    } else {
      std::println("Received pong");
    }
    break;
  case websocket::message_kind::close:
    std::println("Received close with code {} and reason {}",
      message.close_code().value_or(websocket::close_code::no_status_received),
      message.close_reason().value_or("no reason"));
    break;
  default:
    std::println("Received message of kind {}", message.kind);
  }
}

int main() {
  using namespace std::chrono_literals;
  websocket::client client;

  auto connect_result = client.connect("ws://websockets.chilkat.io/wsChilkatEcho.ashx", 5s);
  if (!connect_result) {
    if (connect_result.error() == aero::error::errc::timeout) {
      print_error("Connect to echo server timed out", connect_result.error());
      return 1;
    }

    print_error("Connect to echo server failed", connect_result.error());
    return 1;
  }

  auto text_ec = client.send_text("hello from aero client");
  if (text_ec) {
    print_error("Text send failed", text_ec);
    return 1;
  }

  auto read_result = client.read(1500ms);
  if (!read_result.has_value()) {
    print_error("Read failed", read_result.error());
    return 1;
  }

  print_message(read_result.value());

  std::println("Initiating connection close");

  auto close_ec = client.close(websocket::close_code::normal, "aero client is leaving, byye!");
  if (close_ec) {
    print_error("Closing connection failed", close_ec);
    return 1;
  }

  std::println("Connection succesfully closed. Done.");
}
```
</details>

#### HTTP

<details>
<summary>HTTP httpforever.com GET (<strong>Sync</strong>)</summary>

```cpp
#include <print>

#include "aero/http/client.hpp"

namespace http = aero::http;

int main() {
  auto response = http::get("http://httpforever.com/");
  if (!response) {
    std::println("Request failed: {}", response.error().message());
    return 1;
  }

  std::println("Received response from example.com:");
  std::println("Response Headers:");
  for (const auto& [name, value] : response->headers) {
    std::println("{}: {}", name, value);
  }
  std::println("Status: {} ({})", response->status_line.reason_phrase, response->status_code());
  if (response->content_type() == "text/html") {
    std::println("Body (first 100 bytes): {}", response->text().substr(0, 100));
  } else {
    std::println("Body: {}", response->text());
  }
}
```
</details>

<details>
<summary>HTTPS example.com GET (<strong>Async</strong>)</summary>

```cpp
#include <latch>
#include <print>

#include "aero/http/client.hpp"
#include "aero/http/response.hpp"
#include "aero/io_runtime.hpp"
#include "aero/tls/system_context.hpp"

namespace http = aero::http;
namespace tls = aero::tls;

int main() {
  aero::io_runtime io_runtime{1};

  tls::system_context tls_context{tls::version::tlsv1_3};
  tls_context.disable_deprecated_versions();

  http::client client{io_runtime.get_executor(),
    http::client_options{
      .max_response_body_size = 32768,
      .tls_context = std::ref(tls_context.context()),
    }};

  std::latch latch{1};

  client.async_get("https://example.com/", [&](std::error_code ec, http::response response) {
    if (ec) {
      std::println("Request failed: {}", ec.message());
      latch.count_down();
      return;
    }

    std::println("Received response from example.com:");
    std::println("Response Headers:");
    for (const auto& [name, value] : response.headers) {
      std::println("{}: {}", name, value);
    }
    std::println("Status: {} ({})", response.status_line.reason_phrase, response.status_code());

    if (response.content_type() == "text/html") {
      std::println("Body (first 100 bytes): {}", response.text().substr(0, 100));
    } else {
      std::println("Body: {}", response.text());
    }

    latch.count_down();
  });

  latch.wait();

  std::println("Request completed");
}
```
</details>

<details>
<summary>HTTPS example.com GET (<strong>Coro</strong>)</summary>

```cpp
#include <print>

#include <asio/awaitable.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <system_error>

#include "aero/default_executor.hpp"
#include "aero/http/client.hpp"
#include "aero/tls/system_context.hpp"

namespace http = aero::http;
namespace tls = aero::tls;

asio::awaitable<std::error_code> do_request(http::client& client) {
  auto [ec, response] = co_await client.async_get("https://example.com/", asio::as_tuple(asio::use_awaitable));
  if (ec) {
    co_return ec;
  }

  std::println("Received response from example.com:");
  std::println("Response Headers:");
  for (const auto& [name, value] : response.headers) {
    std::println("{}: {}", name, value);
  }
  std::println("Status: {} ({})", response.status_line.reason_phrase, response.status_code());

  if (response.content_type() == "text/html") {
    std::println("Body (first 100 bytes): {}", response.text().substr(0, 100));
  } else {
    std::println("Body: {}", response.text());
  }

  co_return std::error_code{};
}

int main() {
  tls::system_context tls_context{tls::version::tlsv1_3};
  tls_context.disable_deprecated_versions();

  http::client client{aero::get_default_executor(),
    http::client_options{
      .max_response_body_size = 32768,
      .tls_context = std::ref(tls_context.context()),
    }};

  auto fut = asio::co_spawn(client.get_executor(), do_request(client), asio::use_future);

  try {
    auto request_ec = fut.get();
    if (request_ec) {
      std::println("HTTPS request failed with error: {} ({})", request_ec.message(), request_ec.category().name());
    }
  } catch (const std::exception& e) {
    std::println("Exception: {}", e.what());
  }

  std::println("Request completed");
}
```
</details>

<details>
<summary>HTTPS example.com GET (<strong>Sync</strong>)</summary>

```cpp
#include <print>

#include "aero/http/client.hpp"
#include "aero/tls/system_context.hpp"

namespace http = aero::http;
namespace tls = aero::tls;

int main() {
  tls::system_context tls_context{tls::version::tlsv1_3};
  tls_context.disable_deprecated_versions();

  http::client client{http::client_options{
    .max_response_body_size = 32768,
    .tls_context = std::ref(tls_context.context()),
  }};

  auto response = client.get("https://example.com/");
  if (!response) {
    std::println("Request failed: {}", response.error().message());
    return 1;
  }

  std::println("Received response from example.com:");
  std::println("Response Headers:");
  for (const auto& [name, value] : response->headers) {
    std::println("{}: {}", name, value);
  }
  std::println("Status: {} ({})", response->status_line.reason_phrase, response->status_code());
  if (response->content_type() == "text/html") {
    std::println("Body (first 100 bytes): {}", response->text().substr(0, 100));
  } else {
    std::println("Body: {}", response->text());
  }
}
```
</details>

---

### Why Aero?
We created Aero as a hobby project because existing solutions either depended on Boost.Asio or other heavyweight interfaces. Don't get us wrong, Boost.Asio is an excellent library. The only issue we see with using it as a client-side library is its dependency on Boost.Asio and the Boost ecosystem as a whole, as well as the lack of support for standalone-asio, which makes the library feel rather heavy for potentially simple projects. Aero uses standalone Asio and exposes operations via the standard Asio completion-token model. When built with WolfSSL it can produce pretty small binaries, making it suitable for resource‑constrained applications.

### Async model
Aero uses Asio-style completion tokens instead of inventing its own async model.

That means you can use the style that already fits your codebase:

- callbacks
- `asio::use_future`
- `asio::use_awaitable`
- other compatible completion token patterns

## TLS support

TLS is optional.

Aero can be built without TLS when you want the lightest possible setup, or with a TLS backend when you need secure HTTP / WebSocket connections.

Supported configurations include:

- OpenSSL
- WolfSSL[asio]
- no TLS

## Important API contract notes

These details matter and are worth reading before using the async API heavily.

### Non-copying async write contract

Async write-side operations use a non-copying contract.

If you pass data into operations such as:

- `async_send_text`
- `async_send_binary`
- `async_ping`
- `async_pong`
- close reason strings for async close paths

that data must stay alive until the completion handler is called.

### Synchronous API behavior

The synchronous API is implemented as a convenience layer on top of the asynchronous operations using `std::future`.

Because of that, you must not call a blocking synchronous Aero operation from the same thread that is responsible for progressing the executor work needed to complete that operation.

If that situation is detected, Aero reports:

```cpp
aero::basic_error::deadlock_would_occur
```

## WebSocket interface

Websocket client `aero::websocket::basic_client`:
```cpp
template <net::concepts::transport Transport>
class basic_client {
 public:
  using transport_type = Transport;
  using duration = std::chrono::steady_clock::duration;
  using executor_type = typename transport_type::executor_type;

  ...

  auto async_connect(websocket::uri uri, CompletionToken&& token);
  auto async_connect(std::expected<websocket::uri, std::error_code> parsed_uri, CompletionToken&& token);
  auto async_connect(std::string_view uri, CompletionToken&& token);

  auto async_connect(websocket::uri uri, http::headers headers, CompletionToken&& token);
  auto async_connect(std::expected<websocket::uri, std::error_code> parsed_uri, http::headers headers, CompletionToken&& token);
  auto async_connect(std::string_view uri, http::headers headers, CompletionToken&& token);

  auto async_send_text(std::string_view text, CompletionToken&& token);
  auto async_send_binary(std::span<const std::byte> data, CompletionToken&& token);

  auto async_ping(std::string_view text, CompletionToken&& token);
  auto async_ping(CompletionToken&& token);
  auto async_ping(std::span<const std::byte> data, CompletionToken&& token);

  auto async_pong(std::span<const std::byte> data, CompletionToken&& token);
  auto async_pong(std::string_view text, CompletionToken&& token);
  auto async_pong(CompletionToken&& token);

  auto async_close(websocket::close_code code, std::string reason, CompletionToken&& token);
  auto async_close(websocket::close_code code, CompletionToken&& token);
  auto async_force_close(CompletionToken&& token);

  auto async_read(CompletionToken&& token);

  std::expected<http::headers, std::error_code> connect(websocket::uri uri);
  std::expected<http::headers, std::error_code> connect(websocket::uri uri, duration timeout);
  std::expected<http::headers, std::error_code> connect(std::expected<websocket::uri, std::error_code> parsed_uri);
  std::expected<http::headers, std::error_code> connect(std::expected<websocket::uri, std::error_code> parsed_uri, duration timeout);
  std::expected<http::headers, std::error_code> connect(std::string_view uri_string);
  std::expected<http::headers, std::error_code> connect(std::string_view uri_string, duration timeout);

  std::expected<http::headers, std::error_code> connect(websocket::uri uri, http::headers headers);
  std::expected<http::headers, std::error_code> connect(websocket::uri uri, http::headers headers, duration timeout);
  std::expected<http::headers, std::error_code> connect(std::expected<websocket::uri, std::error_code> parsed_uri, http::headers headers);
  std::expected<http::headers, std::error_code> connect(std::expected<websocket::uri, std::error_code> parsed_uri, http::headers headers, duration timeout);
  std::expected<http::headers, std::error_code> connect(std::string_view uri_string, http::headers headers);
  std::expected<http::headers, std::error_code> connect(std::string_view uri_string, http::headers headers, duration timeout);

  std::error_code send_text(std::string_view text);
  std::error_code send_binary(std::span<const std::byte> data);

  std::error_code ping();
  std::error_code ping(std::string_view text);
  std::error_code ping(std::span<const std::byte> data);

  std::error_code pong();
  std::error_code pong(std::string_view text);
  std::error_code pong(std::span<const std::byte> data);

  std::error_code close(websocket::close_code code);
  std::error_code close(websocket::close_code code, std::string reason);
  std::error_code force_close();

  std::expected<websocket::message, std::error_code> read();
  std::expected<websocket::message, std::error_code> read(duration timeout);

  [[nodiscard]] bool is_open_for_writing() const noexcept;
  [[nodiscard]] bool is_connecting() const noexcept;
  [[nodiscard]] bool is_closed() const noexcept;
  [[nodiscard]] bool is_closing() const noexcept;
  [[nodiscard]] executor_type get_executor() const noexcept;
  [[nodiscard]] transport_type& transport();
};
```

The synchronous API wraps multiple return values in `std::expected`. By design, the API cannot throw exceptions (although exceptions possibly can be thrown by asio).

- `aero::websocket::client` is simply an alias to `aero::websocket::basic_client<aero::net::tcp_transport>`
- `aero::websocket::tls::client` is a class (not an alias since it requires storing and processing the TLS context) that wraps `aero::websocket::basic_client<aero::net::tls_transport>`, it has identical interface to `aero::websocket::client`


Websocket message `aero::websocket::message`:
```cpp
struct message {
  websocket::message_kind kind{};
  std::vector<std::byte> payload;

  [[nodiscard]] bool is_text() const noexcept;
  [[nodiscard]] bool is_binary() const noexcept;
  [[nodiscard]] bool is_close() const noexcept;
  [[nodiscard]] bool is_ping() const noexcept;
  [[nodiscard]] bool is_pong() const noexcept;
  [[nodiscard]] bool is_control() const noexcept;

  [[nodiscard]] bool has_payload() const noexcept;
  [[nodiscard]] std::string_view text() const noexcept;
  [[nodiscard]] std::span<const std::byte> bytes() const noexcept;

  [[nodiscard]] bool has_close_code() const noexcept;
  [[nodiscard]] std::optional<websocket::close_code> close_code() const noexcept;
  [[nodiscard]] bool has_close_reason() const noexcept;
  [[nodiscard]] std::optional<std::string_view> close_reason() const noexcept;
};
```

## Asynchronous API
Any asynchronous operation accepts a completion token from the asio world. This means you can use many different tokens from asio for any operation, and you are not limited to any form of asynchrony. Example of using completion tokens from asio:
```cpp
using namespace std::chrono_literals;

// Return result as an awaitable tuple (asio::awaitable<std::tuple>)
auto [connect_ec, headers] = co_await client.async_connect("ws://example.com/", asio::as_tuple(asio::use_awaitable));

// Return result as an awaitable tuple with timeout of 1500ms
auto [read_ec, message] = co_await client.async_read(asio::cancel_after(1500ms, asio::as_tuple(asio::use_awaitable)));

// Ignore 'async_connect' return-value and only care about error in awaitable context
std::error_code connect_ec;
co_await client.async_connect("ws://example.com/", asio::redirect_error(asio::use_awaitable, connect_ec));

// Use functor with correct completion signature instead of coroutines
client.async_connect("ws://example.com/", [](std::error_code ec, aero::http::headers headers) {});

// Return 'std::future' from async operation
auto completion_future = client.async_connect("ws://example.com/", asio::use_future)
```

|Function|Completion Signature|
|-|-|
|`async_connect(...)`|void(std::error_code, aero::http::headers)|
|`async_send_text(...)`|void(std::error_code)|
|`async_send_binary(...)`|void(std::error_code)|
|`async_ping(...)`|void(std::error_code)|
|`async_pong(...)`|void(std::error_code)|
|`async_close(...)`|void(std::error_code)|
|`async_force_close(...)`|void(std::error_code)|
|`async_read(...)`|void(std::error_code, aero::websocket::message)|

> [!NOTE]
>
> Aero implements a non-copying API, which means that the caller must ensure that the buffer passed to `async_send_text(text)`, `async_send_binary(data)`, `async_ping(data)`, `async_pong(data)`, `async_close(..., close_reason)` remains valid until the operation is complete.

## Synchronous API
> [!IMPORTANT]
>
> Any synchronous function is implemented using `std::future` from an asynchronous function. If you try to call a synchronous function on the same thread on which the executor passed to the client is running, you will get the `aero::basic_error::deadlock_would_occur` error.

## Threadsafety
Please note that all references to functions apply to both synchronous and asynchronous variants. For example, if `async_connect` is mentioned, this implies all overloads of this function and its synchronous variant `connect`.

|Operation|Contract|
|-|-|
|`async_connect`|No other `async_connect` should be outstanding and no read/close is active. Exclusive phase, concurrent usage with `async_connect`, `async_read`, `async_close` is forbidden.|
|`async_send_text`, `async_send_binary`, `async_ping`, `async_pong`|Can be called concurrently with any operation except `async_connect`. Transport layer should serialize all write operations using strand/mutex/etc. Not meaningful concurrently with `async_connect` before open, because it returns `protocol_error::connection_closed`. Can be called concurenntly with `async_close`, but the result depends on strand ordering - once in closing, it will return `protocol_error::connection_closed`.|
|`async_close`|Threadsafe, but correct usage is only one close at a time. A second concurrent call returns `protocol_error::already_closing`. Forbidden concurrently with `async_connect` (possible competing reads). Allowed to use with `async_read` concurrently, and if `async_close` starts first, it may start reading and an external `async_read` will return `protocol_error::already_reading`|
|`async_force_close`|Threadsafe, cancels all running operations|
|`is_open_for_writing`, `is_connecting`, `is_closed`, `is_closing`, `get_executor`|Threadsafe getters|

## HTTP

### What you get
- HTTP/1.0 and HTTP/1.1 support
- plain HTTP and HTTPS client support
- one-shot helper functions for common requests
- a reusable HTTP client type for when you want connection reuse and more control
- request and response types
- header utilities that are actually pleasant to work with
- common request helpers for things like `GET`, `HEAD`, `POST`, `PUT`, `PATCH`, `DELETE`, and `OPTIONS`
- client options for connection reuse, idle connection limits, transport buffer sizing, response body size caps, and `Expect: 100-continue` timing

# Build and integration
Aero is header‑only and requires a C++23 compiler. The primary dependency is standalone Asio (header‑only), and optional TLS support depends on WolfSSL or OpenSSL. Currently, Aero does not support package managers due to time constraints and because the current version is an MVP. PRs are very welcome.

You can include Aero in your project with CMake using `add_subdirectory` or `FetchContent`:
```cmake
cmake_minimum_required(VERSION 3.24)
project(my_app LANGUAGES CXX)

# Add aero as a subdirectory
add_subdirectory(path/to/aero)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE aero)

# Optional settings (configure before the first configure step)
# You can find all of the options in 'cmake/AeroOptions.cmake'

# Use installed wolfssl target. Supported backends are: wolfssl|openssl|none.
# Default is 'none'
# set(AERO_TLS_BACKEND wolfssl)

# set(AERO_USE_BUNDLED_ASIO ON) # "Fetch ASIO using FetchContent if not found on system or in targets"
# set(AERO_USE_BUNDLED_UTFCPP ON) # "Fetch utfcpp using FetchContent if not found on system or in targets"
```

## Tests
Actions build and test Aero on:
|Compiler|Platform|Modes|
|-|-|-|
|Clang (20+)|Linux|With TLS & No TLS|
|GCC (15.2+)|Linux|With TLS & No TLS|
|MSVC|Windows|With TLS & No TLS|
|Clang-CL|Windows|With TLS & No TLS|
|AppleClang|macOS|With TLS & No TLS|

Aero contains ~300+ unit tests of implementation details

The Websocket protocol is tested using [autobahn](https://github.com/crossbario/autobahn-testsuite) (a set of tests for RFC6455 compliance used by industry giants).

### RFC6455 compliance
Aero passes all autobahn tests (excluding cases '9', '12', '13' because they are testing extensions that are not implemented in Aero) except section 6.4 with NON-STRICT results (not an error, but not an ideal implementation also). The decision not to implement 6.4 (Fail-fast on invalid UTF-8) was made based on several factors:
- Time: implementing a streaming UTF-8 validator is a difficult task that will take a lot of time, so we decided to abandon this idea and leave NON-STRICT behavior in this category due to a severe lack of time for implementation.
- Optimization: 6.4 requires fail-fast behavior when invalid UTF-8 is detected in a text continuation frame. Current behavior - bytes are not validated for each continuation frame, as this would require a streaming UTF-8 validator, which could significantly impact optimization when receiving large text messages.

If you require strict RFC 6455 conformance or enterprise‑level support, you should consider using Boost.Beast. Aero strives to implement the protocol as faithfully as possible, but it is developed in the author's free time and may potentially contain discrepancies with the RFC.

## License
Aero is distributed under the MIT License.
