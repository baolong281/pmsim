#pragma once
#include "collector/NetworkStats.hpp"
#include "collector/Session.hpp"
#include <algorithm>
#include <boost/asio/co_spawn.hpp>
#include <collector/Discovery.hpp>
#include <functional>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

struct ManagerConfig {
  size_t markets_per_session;
  size_t max_markets;
};

template <typename Q> class Manager {
public:
  Manager(Discovery &discovery, Q &queue, net::any_io_executor exec,
          ssl::context &ctx, ManagerConfig cfg, NetworkStats &net_stats,
          std::function<void(std::string const &)> on_fatal)
      : discovery{discovery}, queue{queue}, exec{exec}, ctx{ctx}, cfg{cfg},
        net_stats{net_stats}, on_fatal{std::move(on_fatal)} {}

  net::awaitable<void> start() {
    auto markets = co_await discovery.fetch();
    if (markets.size() > cfg.max_markets) {
      spdlog::warn("discovery found {} markets, capping at {}", markets.size(),
                   cfg.max_markets);
      markets.resize(cfg.max_markets);
    }
    // round up so the last partial group still gets a session
    size_t n_sessions = (markets.size() + cfg.markets_per_session - 1) /
                        cfg.markets_per_session;

    WSConfig ws_config{"api.polymarket.us", "/v1/ws/markets", "443", {}};

    for (size_t i = 0; i < n_sessions; i++) {

      std::vector<std::string> markets_for_session;

      for (size_t j = i * cfg.markets_per_session;
           j < std::min((i + 1) * cfg.markets_per_session, markets.size());
           j++) {
        markets_for_session.push_back(markets[j].slug);
      }

      SessionConfig session_cfg{ws_config, markets_for_session};
      auto sesh_ptr =
          std::make_unique<Session<Q>>(exec, ctx, session_cfg, auth, queue,
                                       static_cast<uint32_t>(i), net_stats);
      sessions.push_back(std::move(sesh_ptr));
    }

    for (size_t i = 0; i < sessions.size(); i++) {
      net::co_spawn(exec, sessions[i]->run(), [this, i](std::exception_ptr e) {
        if (!e)
          return;
        try {
          std::rethrow_exception(e);
        } catch (std::exception const &ex) {
          spdlog::error("session {} crashed: {}", i, ex.what());
          on_fatal(ex.what());
        }
      });
    }
  }

  void stop() {
    for (auto &s : sessions)
      s->stop();
  }

private:
  Discovery &discovery;
  Q &queue;
  Auth auth; // sessions hold a reference, so it must outlive start()
  net::any_io_executor exec;
  ssl::context &ctx;
  ManagerConfig cfg;
  std::vector<std::unique_ptr<Session<Q>>> sessions;
  NetworkStats &net_stats;
  std::function<void(std::string const &)> on_fatal;
};
