#include <collector/Auth.hpp>
#include <collector/WSClient.hpp>
#include <iostream>
#include <sstream>
#include <string>

struct SessionConfig {
  WSConfig ws_config;
  std::vector<std::string> markets;
};

// manages the websocket connection and subscribes to markets
class Session {

public:
  Session(net::any_io_executor exec, ssl::context &ctx, SessionConfig cfg,
          Auth &auth)
      : client{cfg.ws_config, exec, ctx}, cfg{cfg}, exec{exec}, auth{auth} {}

  net::awaitable<void> run() {
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

    co_await client.send(std::move(sub));

    while (true) {
      WSMessage msg = co_await client.read();
      std::cout << msg.data << std::endl;
    }
  }

private:
  WSClient client;
  SessionConfig cfg;
  net::any_io_executor exec;
  Auth &auth;
};
