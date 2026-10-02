#include "collector/Discovery.hpp"
#include "collector/Manager.hpp"
#include "collector/NetworkStats.hpp"
#include "spdlog/common.h"
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
  Discovery discovery{
      ioc.get_executor(), ctx, "gateway.polymarket.us", {{"politics"}}};

  NetworkStats net_stats{};

  // any unrecoverable error (e.g. bad credentials) stops the whole collector
  int exit_code = EXIT_SUCCESS;
  auto fatal = [&](std::string const &) {
    exit_code = EXIT_FAILURE;
    ioc.stop();
  };

  ManagerConfig cfg{100, 500}; // 500 markets = 5 connections
  Manager<Queue> manager{discovery, queue, ioc.get_executor(), ctx, cfg,
                         net_stats, fatal};

  Writer<Queue> writer{out_dir, "polymarket-us"};

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
      spdlog::error("manager crashed: {}", ex.what());
      fatal(ex.what());
    }
  });

  ioc.run();

  writer.stop();
  writer_thread.join();

  spdlog::info("network stats={}", net_stats);
  return exit_code;
}
