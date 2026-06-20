#include "ut.hpp"
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "aero/http/headers.hpp"
#include "aero/http/status_line.hpp"
#include "aero/websocket/client_handshaker.hpp"
#include "aero/websocket/detail/accept_challenge.hpp"
#include "aero/websocket/error.hpp"

namespace http = aero::http;
namespace websocket = aero::websocket;

using websocket::handshake_error;

std::error_code validate_server_handshake(const http::response& server_response, std::string_view sec_websocket_key) {
  return websocket::client_handshaker{}.validate_server_handshake(server_response, sec_websocket_key);
}

http::headers make_headers(std::vector<std::pair<std::string, std::string>> headers) {
  http::headers fields;
  for (const auto& [header_key, header_value] : headers) {
    fields.add(header_key, header_value);
  }

  auto parsed = http::headers::parse(fields.serialize());
  if (!parsed.has_value()) {
    throw std::system_error{parsed.error()};
  }
  return std::move(*parsed);
}

std::vector<std::byte> make_bytes(std::string_view text) {
  std::vector<std::byte> bytes;
  bytes.reserve(text.size());
  for (unsigned char character : text) {
    bytes.push_back(static_cast<std::byte>(character));
  }
  return bytes;
}

http::response generate_server_handshake_buffer(std::string_view status_line,
  std::vector<std::pair<std::string, std::string>> headers) {
  return http::response{
    .body = {},
    .status_line = *http::status_line::parse(status_line),
    .headers = make_headers(std::move(headers)),
  };
}

http::response generate_server_handshake_with_raw_tail(std::string_view status_line,
  std::vector<std::pair<std::string, std::string>> headers, std::string_view tail) {
  auto response = generate_server_handshake_buffer(status_line, std::move(headers));
  response.body = make_bytes(tail);
  return response;
}

std::string generate_valid_accept(std::string_view sec_websocket_key) {
  return websocket::detail::compute_sec_websocket_accept(sec_websocket_key);
}

ut::suite websocket_client_handshaker = [] {
  "accepts valid handshake"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
    auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

    auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"Upgrade", "websocket"},
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Accept", sec_websocket_accept},
      });

    auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == std::error_code{});
  };

  "ignores additional headers"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
    auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

    auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"Date", "Mon, 22 Dec 2025 12:00:00 GMT"},
        {"Server", "unit-test"},
        {"Upgrade", "websocket"},
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Accept", sec_websocket_accept},
        {"X-Anything", "whatever"},
      });

    auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == std::error_code{});
  };

  "header names are case-insensitive"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
    auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

    auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"uPgRaDe", "websocket"},
        {"cOnNeCtIoN", "Upgrade"},
        {"sEc-WeBsOcKeT-aCcEpT", sec_websocket_accept},
      });

    auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == std::error_code{});
  };

  "trims optional whitespace around header values"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
    auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

    auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"Upgrade", "   websocket   "},
        {"Connection", "\t\tUpgrade\t"},
        {"Sec-WebSocket-Accept", std::format("  {}  ", sec_websocket_accept)},
      });

    auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == std::error_code{});
  };

  "accepts handshake even if body follows headers"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
    auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

    auto handshake = generate_server_handshake_with_raw_tail("HTTP/1.1 101 Switching Protocols",
      {
        {"Upgrade", "websocket"},
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Accept", sec_websocket_accept},
      },
      "Unexpected-Body: must-not-break-parser\r\n");

    auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == std::error_code{});
  };

  "returns an error if status code is not switching protocols"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
    const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

    const auto handshake = generate_server_handshake_buffer("HTTP/1.1 200 OK",
      {
        {"Upgrade", "websocket"},
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Accept", sec_websocket_accept},
      });

    const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == websocket::handshake_error::status_code_invalid);
  };

  "returns an error if upgrade header is missing"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
    const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

    const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Accept", sec_websocket_accept},
      });

    const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == handshake_error::upgrade_header_invalid);
  };

  "returns an error if upgrade header is not websocket"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
    const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

    const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"Upgrade", "h2c"},
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Accept", sec_websocket_accept},
      });

    const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == handshake_error::upgrade_header_invalid);
  };

  "returns an error if connection header is missing"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
    const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

    const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"Upgrade", "websocket"},
        {"Sec-WebSocket-Accept", sec_websocket_accept},
      });

    const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == handshake_error::connection_header_invalid);
  };

  "returns an error if connection header is not upgrade"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
    const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

    const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"Upgrade", "websocket"},
        {"Connection", "close"},
        {"Sec-WebSocket-Accept", sec_websocket_accept},
      });

    const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == handshake_error::connection_header_invalid);
  };

  "returns an error if sec-websocket-accept header is missing"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};

    const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"Upgrade", "websocket"},
        {"Connection", "Upgrade"},
      });

    const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == handshake_error::accept_header_invalid);
  };

  "returns an error if sec-websocket-accept header is empty"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};

    const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"Upgrade", "websocket"},
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Accept", ""},
      });

    const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == handshake_error::accept_header_invalid);
  };

  "returns an error if sec-websocket-accept header is whitespace only"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};

    const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"Upgrade", "websocket"},
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Accept", "   \t  "},
      });

    const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == handshake_error::accept_header_invalid);
  };

  "returns an error if accept challenge does not match key"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};

    const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"Upgrade", "websocket"},
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Accept", "AAAAAAAAAAAAAAAAAAAAAAAAAAA="},
      });

    const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == handshake_error::accept_challenge_failed);
  };

  "returns an error if accept challenge key is computed as empty"_test = [] {
    constexpr std::string_view sec_websocket_key;

    const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"Upgrade", "websocket"},
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Accept", "any"},
      });

    const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == handshake_error::accept_challenge_failed);
  };

  "rfc 6455 upgrade token is case-insensitive"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
    const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

    const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"Upgrade", "WebSocket"},
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Accept", sec_websocket_accept},
      });

    const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == std::error_code{});
  };

  "rfc 6455 connection token is case-insensitive"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
    const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

    const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"Upgrade", "websocket"},
        {"Connection", "upgrade"},
        {"Sec-WebSocket-Accept", sec_websocket_accept},
      });

    const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == std::error_code{});
  };

  "rfc 6455 connection header may contain multiple tokens"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
    const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

    const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"Upgrade", "websocket"},
        {"Connection", "keep-alive, Upgrade"},
        {"Sec-WebSocket-Accept", sec_websocket_accept},
      });

    const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == std::error_code{});
  };

  "rfc 6455 connection token parsing must not match substrings"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
    const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

    const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"Upgrade", "websocket"},
        {"Connection", "Upgradex, keep-alive"},
        {"Sec-WebSocket-Accept", sec_websocket_accept},
      });

    const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == handshake_error::connection_header_invalid);
  };

  "rfc 6455 multiple connection headers may be present"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
    const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

    const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
      {
        {"Upgrade", "websocket"},
        {"Connection", "keep-alive"},
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Accept", sec_websocket_accept},
      });

    const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
    expect(error_code == std::error_code{});
  };
};

int main() {}
