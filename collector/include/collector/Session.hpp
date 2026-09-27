#pragma once

#include "spdlog/spdlog.h"
#include <collector/Auth.hpp>
#include <collector/WSClient.hpp>
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
          Auth &auth, Q &queue, uint32_t id)
      : client{cfg.ws_config, exec, ctx}, cfg{cfg}, auth{auth}, queue{queue},
        id{id} {}

  Session(Session const &) = delete;
  Session &operator=(Session const &) = delete;
  Session(Session &&) = delete;
  Session &operator=(Session &&) = delete;

  net::awaitable<void> run() {
    ++attempt;
    seq = 0;

    running = true;
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

    try {
      while (running) {
        WSMessage msg = co_await client.read();
        int64_t recv_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              msg.recv_ts.time_since_epoch())
                              .count();
        queue.push(RawRecord{id, attempt, seq++, recv_ns, std::move(msg.data)});
      }
    } catch (WSError const &) {
      if (running)
        throw;
    }
  }

  void stop() {
    running = false;
    client.cancel();
  }

private:
  WSClient client;
  SessionConfig cfg;
  Auth &auth;
  Q &queue;
  bool running{false};
  uint32_t id;
  uint64_t attempt{0};
  uint64_t seq{0};
};
