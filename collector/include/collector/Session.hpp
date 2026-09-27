#pragma once

#include "collector/NetworkStats.hpp"
#include "spdlog/spdlog.h"
#include <boost/asio/steady_timer.hpp>
#include <collector/Auth.hpp>
#include <collector/WSClient.hpp>
#include <random>
#include <string>

struct SessionConfig {
  WSConfig ws_config;
  std::vector<std::string> markets;
};

struct RawRecord {
  uint32_t session;
  uint64_t attempt;
  uint64_t seq;
  int64_t recv_ns;
  std::string data;
};

// manages the websocket connection and subscribes to markets

template <typename Q> class Session {
public:
  Session(net::any_io_executor exec, ssl::context &ctx, SessionConfig cfg,
          Auth &auth, Q &queue, uint32_t id, NetworkStats &net_stats)
      : exec{exec}, ctx{ctx}, cfg{cfg}, auth{auth}, queue{queue}, id{id},
        stats{net_stats.add_session(id)} {}

  Session(Session const &) = delete;
  Session &operator=(Session const &) = delete;
  Session(Session &&) = delete;
  Session &operator=(Session &&) = delete;

  net::awaitable<void> run() {
    running = true;
    while (running) {
      ++attempt;
      seq = 0;

      try {
        WSClient client{cfg.ws_config, exec, ctx};
        co_await client.connect(auth.get_auth_headers(cfg.ws_config.path));

        std::string slugs;
        for (size_t i = 0; i < cfg.markets.size(); i++) {
          if (i > 0)
            slugs += ",";
          slugs += "\"" + cfg.markets[i] + "\"";
        }

        std::string sub = R"({"subscribe":{"requestId":"1","subscriptionType":)"
                          R"("SUBSCRIPTION_TYPE_MARKET_DATA","marketSlugs":[)" +
                          slugs + R"(]}})";

        spdlog::info("session {} subscribing to: {}", id, slugs);

        co_await client.send(std::move(sub));

        while (running) {
          WSMessage msg = co_await client.read();
          failures = 0; // connection delivered data, so reset the backoff
          int64_t recv_ns =
              std::chrono::duration_cast<std::chrono::nanoseconds>(
                  msg.recv_ts.time_since_epoch())
                  .count();
          stats.inc_messages();
          stats.inc_bytes_recv(msg.data.size());
          queue.push(
              RawRecord{id, attempt, seq++, recv_ns, std::move(msg.data)});
        }
      } catch (WSError const &err) {
        // bad credentials won't fix themselves, so don't retry
        if (err.http_status == 401 || err.http_status == 403)
          throw;
        spdlog::warn("session {} disconnected: {}", id, err.what());
      }

      // we exited the loop not because of error, so we just leave
      if (!running)
        break;

      // do the backoff
      auto delay = next_backoff();
      spdlog::info("session {} reconnecting in {}ms", id, delay.count());
      net::steady_timer timer{exec};
      timer.expires_after(delay);
      co_await timer.async_wait(net::use_awaitable);
    }
  }

  void stop() { running = false; }

private:
  boost::asio::any_io_executor exec;
  ssl::context &ctx;
  SessionConfig cfg;
  Auth &auth;
  Q &queue;
  bool running{false};
  uint32_t id;
  uint64_t attempt{0};
  uint64_t seq{0};
  SessionStats &stats;
  std::mt19937 rng{std::random_device{}()};
  int failures{0};

  // full jitter: random delay in [200ms, min(30s, 1s * 2^failures)]
  std::chrono::milliseconds next_backoff() {
    using namespace std::chrono;
    milliseconds base{1000}, cap{30000}, floor{200};
    auto ceiling = std::min(cap, base * (1LL << std::min(failures, 10)));
    ++failures;
    std::uniform_int_distribution<long long> dist(floor.count(),
                                                  ceiling.count());
    return milliseconds{dist(rng)};
  }
};
