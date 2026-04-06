#include <system_error>

#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include "aero/http/detail/connection_pool.hpp"
#include "aero/net/tcp_transport.hpp"

namespace {

  namespace http = aero::http;

  using connection_pool = http::detail::connection_pool<aero::net::tcp_transport<>>;

  http::request make_request(http::method method = http::method::get, http::version version = http::version::http1_1) {
    return http::request{
      .method = method,
      .protocol = version,
      .url = "/",
      .body = {},
      .headers = {{"Host", "example.com"}},
      .content_length = 0,
    };
  }

  http::response make_response(http::headers headers, http::status_code status_code = http::status_code::ok) {
    return http::response{
      .body = {},
      .status_line = http::status_line{.protocol = "HTTP/1.1", .status_code = status_code, .reason_phrase = "OK"},
      .headers = std::move(headers),
    };
  }

  void open_socket(connection_pool::leased_connection& lease) {
    std::error_code ec;
    std::ignore = lease.transport().lowest_layer().open(asio::ip::tcp::v4(), ec);
    ASSERT_FALSE(ec);
  }

  void close_socket(connection_pool::leased_connection& lease) {
    std::error_code ec;
    std::ignore = lease.transport().lowest_layer().close(ec);
    ASSERT_FALSE(ec);
  }

} // namespace

TEST(HttpConnectionPool, AcquireNormalizesHostNameForEndpointKey) {
  asio::io_context io;
  connection_pool pool(io.get_executor());

  auto lease = pool.acquire("Example.COM", 80);

  ASSERT_TRUE(lease.has_value());
  EXPECT_EQ(lease->key().host, "example.com");
  EXPECT_EQ(lease->key().port, 80);
  EXPECT_FALSE(pool.is_secure_transport());
}

TEST(HttpConnectionPool, RecycleReusesKeepAliveReadyConnection) {
  asio::io_context io;
  connection_pool pool(io.get_executor());

  auto first = pool.acquire("example.com", 80);
  ASSERT_TRUE(first.has_value());

  auto first_id = first->id();
  open_socket(*first);

  auto request = make_request();
  auto response = make_response({{"Content-Length", "0"}});
  first->release(request, response);

  auto second = pool.acquire("example.com", 80);
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->id(), first_id);
}

TEST(HttpConnectionPool, ClosedIdleConnectionIsDroppedOnNextAcquire) {
  asio::io_context io;
  connection_pool pool(io.get_executor());

  auto first = pool.acquire("example.com", 80);
  ASSERT_TRUE(first.has_value());

  auto first_id = first->id();
  open_socket(*first);

  auto request = make_request();
  auto response = make_response({{"Content-Length", "0"}});
  first->release(request, response);

  auto reopened = pool.acquire("example.com", 80);
  ASSERT_TRUE(reopened.has_value());
  EXPECT_EQ(reopened->id(), first_id);

  close_socket(*reopened);
  reopened->recycle();

  auto fresh = pool.acquire("example.com", 80);
  ASSERT_TRUE(fresh.has_value());
  EXPECT_NE(fresh->id(), first_id);
}

TEST(HttpConnectionPool, ReleaseDiscardsCloseDelimitedResponseConnection) {
  asio::io_context io;
  connection_pool pool(io.get_executor());

  auto first = pool.acquire("example.com", 80);
  ASSERT_TRUE(first.has_value());

  auto first_id = first->id();
  open_socket(*first);

  auto request = make_request();
  auto response = make_response({});
  first->release(request, response);

  auto second = pool.acquire("example.com", 80);
  ASSERT_TRUE(second.has_value());
  EXPECT_NE(second->id(), first_id);
}

TEST(HttpConnectionPool, MaxIdleConnectionsPerEndpointLimitsReuseCache) {
  asio::io_context io;
  connection_pool pool(io.get_executor(), http::detail::pool_options{.max_idle_connections_per_endpoint = 1});

  auto first = pool.acquire("example.com", 80);
  auto second = pool.acquire("example.com", 80);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());

  auto first_id = first->id();
  auto second_id = second->id();
  open_socket(*first);
  open_socket(*second);

  auto request = make_request();
  auto response = make_response({{"Content-Length", "0"}});

  first->release(request, response);
  second->release(request, response);

  auto reused = pool.acquire("example.com", 80);
  auto fresh = pool.acquire("example.com", 80);
  ASSERT_TRUE(reused.has_value());
  ASSERT_TRUE(fresh.has_value());

  EXPECT_TRUE(reused->id() == first_id || reused->id() == second_id);
  EXPECT_TRUE(fresh->id() != first_id && fresh->id() != second_id);
}

TEST(HttpConnectionPool, CanReuseMatchesHttp11BodyDelimitationRules) {
  auto head_request = make_request(http::method::head);
  auto get_request = make_request(http::method::get);

  auto head_response = make_response({});
  auto close_delimited_response = make_response({});
  auto length_delimited_response = make_response({{"Content-Length", "0"}});

  EXPECT_TRUE(connection_pool::can_reuse(head_request, head_response));
  EXPECT_FALSE(connection_pool::can_reuse(get_request, close_delimited_response));
  EXPECT_TRUE(connection_pool::can_reuse(get_request, length_delimited_response));
}
