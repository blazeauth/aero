#include <system_error>

#include <asio/io_context.hpp>
#include <ut/ut.hpp>

#include "aero/http/detail/connection_pool.hpp"
#include "aero/net/tcp_transport.hpp"

using namespace ut;

namespace http = aero::http;

using connection_pool = http::detail::connection_pool<aero::net::tcp_transport<>>;

http::request make_request(http::method method = http::method::GET, http::version version = http::version::http1_1) {
  return http::request{
    .method = method,
    .protocol = version,
    .url = "/",
    .body = {},
    .headers = {{"Host", "example.com"}},
    .content_length = 0,
  };
}

http::response make_response(http::headers headers, http::status status_code = http::status::ok) {
  return http::response{
    .body = {},
    .status_line = http::status_line{.protocol = "HTTP/1.1", .status_code = status_code, .reason_phrase = "OK"},
    .headers = std::move(headers),
  };
}

void open_socket(connection_pool::leased_connection& lease) {
  std::error_code ec;
  std::ignore = lease.transport().lowest_layer().open(asio::ip::tcp::v4(), ec);
  expect[not ec];
}

void close_socket(connection_pool::leased_connection& lease) {
  std::error_code ec;
  std::ignore = lease.transport().lowest_layer().close(ec);
  expect[not ec];
}

int main() {
  suite http_connection_pool = [] {
    "acquire normalizes host name for endpoint key"_test = [] {
      asio::io_context io;
      connection_pool pool(io.get_executor());

      auto lease = pool.acquire("Example.COM", 80);

      expect[lease.has_value()];
      expect(lease->key().host == "example.com");
      expect(lease->key().port == 80);
      expect(not pool.is_secure_transport());
    };

    "recycle reuses keep alive ready connection"_test = [] {
      asio::io_context io;
      connection_pool pool(io.get_executor());

      auto first = pool.acquire("example.com", 80);
      expect[first.has_value()];

      auto first_id = first->id();
      open_socket(*first);

      auto request = make_request();
      auto response = make_response({{"Content-Length", "0"}});
      first->release(request, response);

      auto second = pool.acquire("example.com", 80);
      expect[second.has_value()];
      expect(second->id() == first_id);
    };

    "closed idle connection is dropped on next acquire"_test = [] {
      asio::io_context io;
      connection_pool pool(io.get_executor());

      auto first = pool.acquire("example.com", 80);
      expect[first.has_value()];

      auto first_id = first->id();
      open_socket(*first);

      auto request = make_request();
      auto response = make_response({{"Content-Length", "0"}});
      first->release(request, response);

      auto reopened = pool.acquire("example.com", 80);
      expect[reopened.has_value()];
      expect(reopened->id() == first_id);

      close_socket(*reopened);
      reopened->recycle();

      auto fresh = pool.acquire("example.com", 80);
      expect[fresh.has_value()];
      expect(fresh->id() != first_id);
    };

    "release discards close-delimited response connection"_test = [] {
      asio::io_context io;
      connection_pool pool(io.get_executor());

      auto first = pool.acquire("example.com", 80);
      expect[first.has_value()];

      auto first_id = first->id();
      open_socket(*first);

      auto request = make_request();
      auto response = make_response({});
      first->release(request, response);

      auto second = pool.acquire("example.com", 80);
      expect[second.has_value()];
      expect(second->id() != first_id);
    };

    "max idle connections per endpoint limits reuse cache"_test = [] {
      asio::io_context io;
      connection_pool pool(io.get_executor(), http::detail::pool_options{.max_idle_connections_per_endpoint = 1});

      auto first = pool.acquire("example.com", 80);
      auto second = pool.acquire("example.com", 80);
      expect[first.has_value()];
      expect[second.has_value()];

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
      expect[reused.has_value()];
      expect[fresh.has_value()];

      expect(reused->id() == first_id or reused->id() == second_id);
      expect(fresh->id() != first_id and fresh->id() != second_id);
    };

    "can reuse matches http/1.1 body delimitation rules"_test = [] {
      auto head_request = make_request(http::method::HEAD);
      auto get_request = make_request(http::method::GET);

      auto head_response = make_response({});
      auto close_delimited_response = make_response({});
      auto length_delimited_response = make_response({{"Content-Length", "0"}});

      expect(connection_pool::can_reuse(head_request, head_response));
      expect(not connection_pool::can_reuse(get_request, close_delimited_response));
      expect(connection_pool::can_reuse(get_request, length_delimited_response));
    };
  };
}
