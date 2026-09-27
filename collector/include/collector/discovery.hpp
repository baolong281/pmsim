#pragma once

#include "spdlog/spdlog.h"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

namespace beast = boost::beast; // from <boost/beast.hpp>
namespace http = beast::http;
namespace net = boost::asio; // from <boost/asio.hpp>
namespace ssl = net::ssl;
using tcp = boost::asio::ip::tcp;

struct MarketFilter { //
  std::vector<std::string> categories, include_slugs, exclude_slugs,
      slug_prefixes;
};

struct MarketInfo {
  std::string slug;
  std::string category; /* status, gameStartTime… */
};

class Discovery {
public:
  Discovery(net::any_io_executor exec, ssl::context &ctx, std::string host,
            MarketFilter filters)
      : exec{exec}, ctx{ctx}, host{host}, filters{filters} {}

  net::awaitable<void> fetch() {
    std::string res = co_await https_get("/v1/markets");
    spdlog::info("{}", res);
  }

private:
  net::any_io_executor exec;
  ssl::context &ctx;
  std::string host;
  MarketFilter filters;

  net::awaitable<std::string> https_get(std::string target) {
    tcp::resolver resolver{exec};
    beast::ssl_stream<beast::tcp_stream> stream{exec, ctx};
    SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()); // SNI

    // resolve the host
    auto results =
        co_await resolver.async_resolve(host, "443", net::use_awaitable);

    beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(10));
    //
    // socket connect
    co_await beast::get_lowest_layer(stream).async_connect(results,
                                                           net::use_awaitable);
    // do ssl handshake
    co_await stream.async_handshake(ssl::stream_base::client,
                                    net::use_awaitable);

    // build http request
    http::request<http::empty_body> req{http::verb::get, target, 11};
    req.set(http::field::host, host);

    // send the request
    co_await http::async_write(stream, req, net::use_awaitable);

    beast::flat_buffer buf;
    http::response<http::string_body> res;
    // read the response
    co_await http::async_read(stream, buf, res, net::use_awaitable);

    if (res.result() != http::status::ok)
      throw std::runtime_error("GET " + target + ": " +
                               std::to_string(res.result_int()));

    co_return res.body(); // parse with simdjson
  }
};
