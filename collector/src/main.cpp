#include "collector/Discovery.hpp"
#include "collector/Manager.hpp"
#include "collector/NetworkStats.hpp"
#include "spdlog/common.h"
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
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--out-dir" && i + 1 < argc)
      out_dir = argv[++i];
  }

  // setup loggin
  spdlog::set_level(spdlog::level::info);
  spdlog::set_pattern("%Y-%m-%dT%H:%M:%S.%e %^%l%$ [%t] %v");
  spdlog::set_level(spdlog::level::debug);

  // async executor + ssl context
  net::io_context ioc;
  ssl::context ctx{ssl::context::tls_client};
  ctx.set_default_verify_paths();
  ctx.set_verify_mode(ssl::verify_peer);

  Queue queue{1 << 16};
  Discovery discovery{ioc.get_executor(), ctx, "gateway.polymarket.us", {}};

  NetworkStats net_stats{};

  ManagerConfig cfg{100};
  Manager<Queue> manager{discovery, queue, ioc.get_executor(),
                         ctx,       cfg,   net_stats};

  Writer<Queue> writer{out_dir, "polymarket-us"};

  std::atomic<bool> running{true};
  std::thread writer_thread{[&] { writer.run(queue); }};

  // ctrl + c cleanup
  net::signal_set signals{ioc, SIGINT, SIGTERM};
  signals.async_wait([&](beast::error_code, int) {
    manager.stop();
    ioc.stop();
  });

  // start the manager
  net::co_spawn(ioc, manager.start(), [&](std::exception_ptr e) {
    if (!e)
      return;
    try {
      std::rethrow_exception(e);
    } catch (std::exception const &ex) {
      spdlog::error("session crashd: {}", ex.what());
      running.store(false);
      signals.cancel();
      manager.stop();
      ioc.stop();
    }
  });

  ioc.run();

  writer.stop();
  writer_thread.join();

  spdlog::info("network stats={}", net_stats);
}
