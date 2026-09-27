#include "collector/Discovery.hpp"
#include <atomic>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <collector/Discovery.hpp>
#include <collector/Session.hpp>
#include <collector/Writer.hpp>
#include <iostream>
#include <spdlog/spdlog.h>
#include <thread>

namespace net = boost::asio; // from <boost/asio.hpp>
using Queue = boost::lockfree::spsc_queue<RawRecord>;

int main(int argc, char **argv) {
  std::string out_dir = "raw";
  std::vector<std::string> markets;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--out-dir" && i + 1 < argc)
      out_dir = argv[++i];
    else
      markets.push_back(arg);
  }
  if (markets.empty()) {
    std::cerr << "Usage: pmsim-collector [--out-dir DIR] <market-slug>...\n";
    return EXIT_FAILURE;
  }

  spdlog::set_level(spdlog::level::info);
  spdlog::set_pattern("%Y-%m-%dT%H:%M:%S.%e %^%l%$ [%t] %v");
  spdlog::set_level(spdlog::level::debug);

  net::io_context ioc;
  ssl::context ctx{ssl::context::tls_client};
  ctx.set_default_verify_paths();
  ctx.set_verify_mode(ssl::verify_peer);

  Queue queue{1 << 16};

  std::string POLY_HOST = "api.polymarket.us";
  Auth auth;
  WSConfig ws_config{POLY_HOST, "/v1/ws/markets", "443", {}};
  SessionConfig cfg{ws_config, markets};

  Session<Queue> session{ioc.get_executor(), ctx, cfg, auth, queue, 0};
  Writer<Queue> writer{out_dir, "polymarket-us"};

  std::atomic<bool> running{true};
  std::thread writer_thread{[&] { writer.run(queue, running); }};

  net::signal_set signals{ioc, SIGINT, SIGTERM};

  // ioc.stop() so run() returns without waiting on Beast's timeout timers
  signals.async_wait([&](beast::error_code, int) {
    session.stop();
    ioc.stop();
  });

  net::co_spawn(ioc, session.run(), [&](std::exception_ptr e) {
    if (!e)
      return;
    try {
      std::rethrow_exception(e);
    } catch (std::exception const &ex) {
      spdlog::error("session crashd: {}", ex.what());
      running.store(false);
      signals.cancel();
      session.stop();
      ioc.stop();
    }
  });

  std::string POLY_GATEWAY = "gateway.polymarket.us";
  Discovery discovery{ioc.get_executor(), ctx, POLY_GATEWAY, {}};
  net::co_spawn(ioc, discovery.fetch(), [&](std::exception_ptr e) {
    if (!e)
      return;
    try {
      std::rethrow_exception(e);
    } catch (std::exception const &ex) {
      spdlog::error("req failed: {}", ex.what());
    }
  });

  ioc.run();

  running.store(false);
  writer_thread.join();
}
