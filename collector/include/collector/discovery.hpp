#pragma once

#include "simdjson.h"
#include "spdlog/spdlog.h"
#include <arm_neon.h>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <spdlog/fmt/ranges.h>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace beast = boost::beast; // from <boost/beast.hpp>
namespace http = beast::http;
namespace net = boost::asio; // from <boost/asio.hpp>
namespace ssl = net::ssl;
namespace json = simdjson;
using tcp = boost::asio::ip::tcp;

// empty = every category
struct MarketFilter {
  std::vector<std::string> categories;
};

struct MarketInfo {
  std::string slug;
  std::string category; /* status, gameStartTime… */
  bool active;
};

inline std::string format_as(MarketInfo const &m) {
  return fmt::format("{{slug={}, category={}, active={}}}", m.slug, m.category,
                     m.active);
}

class Discovery {
public:
  Discovery(net::any_io_executor exec, ssl::context &ctx, std::string host,
            MarketFilter filters)
      : exec{exec}, ctx{ctx}, host{host}, filters{filters} {}

  net::awaitable<std::vector<MarketInfo>> fetch() {
    // the gateway returns at most 500 markets per request, so page with
    // offset until a short page comes back
    constexpr size_t page_size = 500;

    std::vector<MarketInfo> res;
    // the list can shift while paging, so the same slug may appear twice
    std::unordered_set<std::string> seen;
    json::ondemand::parser parser;

    for (size_t offset = 0;; offset += page_size) {
      std::string target = "/v1/markets?limit=" + std::to_string(page_size) +
                           "&offset=" + std::to_string(offset) +
                           "&active=true&closed=false";
      for (auto const &c : filters.categories)
        target += "&categories=" + c;
      std::string body = co_await https_get(target);

      json::padded_string json{body};
      json::ondemand::document obj = parser.iterate(json);

      size_t n = 0;
      for (auto e : obj.find_field("markets").get_array()) {
        n++;
        MarketInfo m{std::string(e["slug"].get_string().value()),
                     std::string(e["category"].get_string().value()),
                     e["active"].get_bool().value()};
        if (seen.insert(m.slug).second)
          res.push_back(std::move(m));
      }

      if (n < page_size)
        break;
    }

    spdlog::info("discovery found {} markets", res.size());
    spdlog::debug("found markets: {}", res);
    co_return res;
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
