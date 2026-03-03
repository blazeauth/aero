#include <format>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aero/http/status_line.hpp"
#include "aero/websocket/client_handshaker.hpp"
#include "aero/websocket/detail/accept_challenge.hpp"
#include "aero/websocket/error.hpp"

namespace {

  namespace http = aero::http;
  namespace websocket = aero::websocket;

  using websocket::error::handshake_error;

  std::error_code validate_server_handshake(std::string_view server_handshake, std::string_view sec_websocket_key) {
    return websocket::client_handshaker{}.parse_response(server_handshake, sec_websocket_key).error_or(std::error_code{});
  }

  std::string generate_headers_buffer(std::vector<std::pair<std::string, std::string>> headers) {
    std::string buffer;
    for (const auto& [header_key, header_value] : headers) {
      buffer += std::format("{}: {}\r\n", header_key, header_value);
    }
    buffer += "\r\n";
    return buffer;
  }

  std::string generate_server_handshake_buffer(std::string_view status_line,
    std::vector<std::pair<std::string, std::string>> headers) {
    std::string buffer;
    buffer += std::format("{}\r\n", status_line);
    buffer += generate_headers_buffer(std::move(headers));
    return buffer;
  }

  std::string generate_server_handshake_with_raw_tail(std::string_view status_line,
    std::vector<std::pair<std::string, std::string>> headers, std::string_view tail) {
    auto buffer = generate_server_handshake_buffer(status_line, std::move(headers));
    buffer.append(tail.data(), tail.size());
    return buffer;
  }

  std::string generate_valid_accept(std::string_view sec_websocket_key) {
    return websocket::detail::compute_sec_websocket_accept(sec_websocket_key);
  }

} // namespace

TEST(WebsocketServerHandshake, AcceptsValidHandshake) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
  auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

  auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
    {
      {"Upgrade", "websocket"},
      {"Connection", "Upgrade"},
      {"Sec-WebSocket-Accept", sec_websocket_accept},
    });

  auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, std::error_code{});
}

TEST(WebsocketServerHandshake, IgnoresAdditionalHeaders) {
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
  EXPECT_EQ(error_code, std::error_code{});
}

TEST(WebsocketServerHandshake, HeaderNamesAreCaseInsensitive) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
  auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

  auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
    {
      {"uPgRaDe", "websocket"},
      {"cOnNeCtIoN", "Upgrade"},
      {"sEc-WeBsOcKeT-aCcEpT", sec_websocket_accept},
    });

  auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, std::error_code{});
}

TEST(WebsocketServerHandshake, TrimsOptionalWhitespaceAroundHeaderValues) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
  auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

  auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
    {
      {"Upgrade", "   websocket   "},
      {"Connection", "\t\tUpgrade\t"},
      {"Sec-WebSocket-Accept", std::format("  {}  ", sec_websocket_accept)},
    });

  auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, std::error_code{});
}

TEST(WebsocketServerHandshake, AcceptsHandshakeEvenIfBodyFollowsHeaders) {
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
  EXPECT_EQ(error_code, std::error_code{});
}

TEST(WebsocketServerHandshake, ErrorIfStatusLineEndIsMissing) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
  constexpr std::string_view handshake{"HTTP/1.1 101 Switching Protocols"};

  auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, http::error::protocol_error::status_line_invalid);
}

TEST(WebsocketServerHandshake, PropagatesStatusLineParseError) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
  const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

  const auto handshake = generate_server_handshake_buffer("TP/1.1 101 Switching Protocols",
    {
      {"Upgrade", "websocket"},
      {"Connection", "Upgrade"},
      {"Sec-WebSocket-Accept", sec_websocket_accept},
    });

  const auto expected_ec = http::status_line::parse("TP/1.1 101 Switching Protocols").error();
  const auto error_code = validate_server_handshake(handshake, sec_websocket_key);

  EXPECT_EQ(error_code, expected_ec);
}

TEST(WebsocketServerHandshake, PropagatesHeadersParseError) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
  constexpr std::string_view handshake{"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"};

  const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, http::error::protocol_error::headers_section_incomplete);
}

TEST(WebsocketServerHandshake, ErrorIfStatusCodeIsNotSwitchingProtocols) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
  const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

  const auto handshake = generate_server_handshake_buffer("HTTP/1.1 200 OK",
    {
      {"Upgrade", "websocket"},
      {"Connection", "Upgrade"},
      {"Sec-WebSocket-Accept", sec_websocket_accept},
    });

  const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, websocket::error::handshake_error::status_code_invalid);
}

TEST(WebsocketServerHandshake, ErrorIfUpgradeHeaderIsMissing) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
  const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

  const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
    {
      {"Connection", "Upgrade"},
      {"Sec-WebSocket-Accept", sec_websocket_accept},
    });

  const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, handshake_error::upgrade_header_invalid);
}

TEST(WebsocketServerHandshake, ErrorIfUpgradeHeaderIsNotWebsocket) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
  const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

  const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
    {
      {"Upgrade", "h2c"},
      {"Connection", "Upgrade"},
      {"Sec-WebSocket-Accept", sec_websocket_accept},
    });

  const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, handshake_error::upgrade_header_invalid);
}

TEST(WebsocketServerHandshake, ErrorIfConnectionHeaderIsMissing) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
  const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

  const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
    {
      {"Upgrade", "websocket"},
      {"Sec-WebSocket-Accept", sec_websocket_accept},
    });

  const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, handshake_error::connection_header_invalid);
}

TEST(WebsocketServerHandshake, ErrorIfConnectionHeaderIsNotUpgrade) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
  const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

  const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
    {
      {"Upgrade", "websocket"},
      {"Connection", "close"},
      {"Sec-WebSocket-Accept", sec_websocket_accept},
    });

  const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, handshake_error::connection_header_invalid);
}

TEST(WebsocketServerHandshake, ErrorIfSecWebsocketAcceptHeaderIsMissing) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};

  const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
    {
      {"Upgrade", "websocket"},
      {"Connection", "Upgrade"},
    });

  const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, handshake_error::accept_header_invalid);
}

TEST(WebsocketServerHandshake, ErrorIfSecWebsocketAcceptHeaderIsEmpty) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};

  const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
    {
      {"Upgrade", "websocket"},
      {"Connection", "Upgrade"},
      {"Sec-WebSocket-Accept", ""},
    });

  const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, handshake_error::accept_header_invalid);
}

TEST(WebsocketServerHandshake, ErrorIfSecWebsocketAcceptHeaderIsWhitespaceOnly) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};

  const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
    {
      {"Upgrade", "websocket"},
      {"Connection", "Upgrade"},
      {"Sec-WebSocket-Accept", "   \t  "},
    });

  const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, handshake_error::accept_header_invalid);
}

TEST(WebsocketServerHandshake, ErrorIfAcceptChallengeDoesNotMatchKey) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};

  const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
    {
      {"Upgrade", "websocket"},
      {"Connection", "Upgrade"},
      {"Sec-WebSocket-Accept", "AAAAAAAAAAAAAAAAAAAAAAAAAAA="},
    });

  const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, handshake_error::accept_challenge_failed);
}

TEST(WebsocketServerHandshake, ErrorIfAcceptChallengeKeyIsComputedAsEmpty) {
  constexpr std::string_view sec_websocket_key;

  const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
    {
      {"Upgrade", "websocket"},
      {"Connection", "Upgrade"},
      {"Sec-WebSocket-Accept", "any"},
    });

  const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, handshake_error::accept_challenge_failed);
}

TEST(WebsocketServerHandshake, Rfc6455UpgradeTokenIsCaseInsensitive) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
  const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

  const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
    {
      {"Upgrade", "WebSocket"},
      {"Connection", "Upgrade"},
      {"Sec-WebSocket-Accept", sec_websocket_accept},
    });

  const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, std::error_code{});
}

TEST(WebsocketServerHandshake, Rfc6455ConnectionTokenIsCaseInsensitive) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
  const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

  const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
    {
      {"Upgrade", "websocket"},
      {"Connection", "upgrade"},
      {"Sec-WebSocket-Accept", sec_websocket_accept},
    });

  const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, std::error_code{});
}

TEST(WebsocketServerHandshake, Rfc6455ConnectionHeaderMayContainMultipleTokens) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
  const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

  const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
    {
      {"Upgrade", "websocket"},
      {"Connection", "keep-alive, Upgrade"},
      {"Sec-WebSocket-Accept", sec_websocket_accept},
    });

  const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, std::error_code{});
}

TEST(WebsocketServerHandshake, Rfc6455ConnectionTokenParsingMustNotMatchSubstrings) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
  const auto sec_websocket_accept = generate_valid_accept(sec_websocket_key);

  const auto handshake = generate_server_handshake_buffer("HTTP/1.1 101 Switching Protocols",
    {
      {"Upgrade", "websocket"},
      {"Connection", "Upgradex, keep-alive"},
      {"Sec-WebSocket-Accept", sec_websocket_accept},
    });

  const auto error_code = validate_server_handshake(handshake, sec_websocket_key);
  EXPECT_EQ(error_code, handshake_error::connection_header_invalid);
}

TEST(WebsocketServerHandshake, Rfc6455MultipleConnectionHeadersMayBePresent) {
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
  EXPECT_EQ(error_code, std::error_code{});
}
