#pragma once
#include "collector/NetworkStats.hpp"
#include "collector/Session.hpp"
#include <algorithm>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <collector/Discovery.hpp>
#include <functional>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_set>
#include <vector>

namespace asio = boost::asio;

struct ManagerConfig {
  size_t markets_per_session;
  size_t max_markets;
  std::chrono::minutes interval;
};

template <typename Q> class Manager {
public:
  Manager(Discovery &discovery, Q &queue, net::any_io_executor exec,
          ssl::context &ctx, ManagerConfig cfg, NetworkStats &net_stats,
          std::function<void(std::string const &)> on_fatal)
      : discovery{discovery}, queue{queue}, exec{exec}, ctx{ctx}, cfg{cfg},
        net_stats{net_stats}, on_fatal{std::move(on_fatal)}, timer{exec} {}

  net::awaitable<void> start() {
    std::unordered_set<std::string> seen_markets{};

    running = true;

    while (running) {

      std::vector<MarketInfo> unfiltered_markets;
      // if we fail none of the code or sessions should be run, we should just
      // hit the interval timer
      try {
        unfiltered_markets = co_await discovery.fetch();
      } catch (std::exception const &e) {
        spdlog::warn("discovery failed, retrying next interval: {}", e.what());
      }

      // markets we have not seen
      std::vector<std::string> markets;
      for (auto &m : unfiltered_markets) {
        if (!seen_markets.contains(m.slug)) {
          markets.push_back(m.slug);
        }
      }

      // how many markets do we have left to put?
      // if we have room take the difference, otherwise set zero
      size_t room = cfg.max_markets > seen_markets.size()
                        ? cfg.max_markets - seen_markets.size()
                        : 0;
      if (markets.size() > room) {
        spdlog::warn("discovery found {} new markets, room for {}",
                     markets.size(), room);
        markets.resize(room);
      }

      for (auto const &m : markets)
        seen_markets.insert(m);

      // round up so the last partial group still gets a session
      size_t n_sessions = (markets.size() + cfg.markets_per_session - 1) /
                          cfg.markets_per_session;

      WSConfig ws_config{"api.polymarket.us", "/v1/ws/markets", "443", {}};

      auto first_new = sessions.size();
      for (size_t i = 0; i < n_sessions; i++) {
        std::vector<std::string> markets_for_session;

        for (size_t j = i * cfg.markets_per_session;
             j < std::min((i + 1) * cfg.markets_per_session, markets.size());
             j++) {
          markets_for_session.push_back(markets[j]);
        }

        SessionConfig session_cfg{ws_config, markets_for_session};
        auto id = sessions.size();
        auto sesh_ptr =
            std::make_unique<Session<Q>>(exec, ctx, session_cfg, auth, queue,
                                         static_cast<uint32_t>(id), net_stats);
        sessions.push_back(std::move(sesh_ptr));
      }

      for (size_t i = first_new; i < sessions.size(); i++) {
        net::co_spawn(exec, sessions[i]->run(),
                      [this, i](std::exception_ptr e) {
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

      timer.expires_after(cfg.interval);
      co_await timer.async_wait();
    }
  }

  void stop() {
    for (auto &s : sessions)
      s->stop();
  }

private:
  Discovery &discovery;
  asio::steady_timer timer;
  Q &queue;
  Auth auth; // sessions hold a reference, so it must outlive start()
  net::any_io_executor exec;
  ssl::context &ctx;
  ManagerConfig cfg;
  std::vector<std::unique_ptr<Session<Q>>> sessions;
  NetworkStats &net_stats;
  std::function<void(std::string const &)> on_fatal;
  bool running = false;
};
