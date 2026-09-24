#include <boost/asio/co_spawn.hpp>
#include <collector/Session.hpp>

namespace net = boost::asio; // from <boost/asio.hpp>

int main() {
  net::io_context ioc;
  ssl::context ctx{ssl::context::tls_client};
  ctx.set_default_verify_paths();
  ctx.set_verify_mode(ssl::verify_peer);

  Auth auth;

  WSConfig ws_config{"api.polymarket.us", "/v1/ws/markets", "443", {}};

  SessionConfig cfg{ws_config, {"aec-mlb-hou-sea-2026-09-23"}};

  Session session{ioc.get_executor(), ctx, cfg, auth};

  net::co_spawn(ioc, session.run(), [](std::exception_ptr e) {
    if (!e)
      return;
    try {
      std::rethrow_exception(e);
    } catch (std::exception const &ex) {
      std::cerr << "session crashed: " << ex.what() << "\n";
    }
  });

  ioc.run();
}
