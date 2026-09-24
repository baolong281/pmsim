#include <boost/asio/awaitable.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
namespace ssl = net::ssl;
using tcp = boost::asio::ip::tcp;

using WSHeader = std::pair<std::string, std::string>;

struct WSConfig {
  std::string host;
  std::string path;
  std::string port;
  std::vector<WSHeader> headers;

  std::chrono::seconds connect_timeout{10};
  std::chrono::seconds handshake_timeout{10};
  std::chrono::seconds idle_timeout{20};
  std::size_t max_message_size = 16 * 1024 * 1024;
};

struct WSMessage {
  std::string data;
  std::chrono::system_clock::time_point recv_ts;
};

struct WSError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

enum class State { CONNECTING, OPEN, CLOSING, CLOSED };

class WSClient {
public:
  explicit WSClient(WSConfig cfg, net::any_io_executor exec, ssl::context &ctx)
      : ex{exec}, cfg{cfg}, ws{exec, ctx} {}

  WSClient(WSClient const &) = delete;
  WSClient &operator=(WSClient const &) = delete;
  WSClient(WSClient &&) = delete;
  WSClient &operator=(WSClient &&) = delete;

  net::awaitable<void> connect(std::vector<WSHeader> extra_headers) {
    state = State::CONNECTING;
    std::string_view step = "resolve";

    try {
      tcp::resolver resolver{ex};

      // resolve the host to establish the tcp connection
      auto results = co_await resolver.async_resolve(cfg.host, cfg.port,
                                                     net::use_awaitable);

      // call on the tcp layer to connect
      step = "tcp connect";
      auto &tcp = beast::get_lowest_layer(ws);
      tcp.expires_after(cfg.connect_timeout);
      co_await tcp.async_connect(results, net::use_awaitable);

      if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(),
                                    cfg.host.c_str())) {
        throw WSError("Failed to set SNI hostname!");
      }

      // call on ssl layer to do ssl handshake
      step = "tls handshake";
      co_await ws.next_layer().async_handshake(ssl::stream_base::client,
                                               net::use_awaitable);
      tcp.expires_never();

      websocket::stream_base::timeout opt{};
      opt.handshake_timeout = cfg.handshake_timeout;
      opt.idle_timeout = cfg.idle_timeout;
      opt.keep_alive_pings = true;
      ws.set_option(opt);
      ws.read_message_max(cfg.max_message_size);

      // merge extra with config headers then set these
      auto all = cfg.headers; // copy
      all.insert(all.end(), extra_headers.begin(), extra_headers.end());

      ws.set_option(websocket::stream_base::decorator(
          [all = std::move(all)](websocket::request_type &req) {
            for (auto const &[name, value] : all)
              req.set(name, value);
          }));

      // do the websocket handshake
      step = "ws handshake";
      co_await ws.async_handshake(cfg.host, cfg.path, net::use_awaitable);
      state = State::OPEN;
    } catch (boost::system::system_error const &e) {
      state = State::CLOSED;
      throw WSError(std::string(step) + ": " + e.code().message());
    } catch (...) {
      state = State::CLOSED;
      throw;
    }
  }

  net::awaitable<void> send(std::string msg) {
    try {
      co_await ws.async_write(net::buffer(msg), net::use_awaitable);
    } catch (boost::system::system_error const &e) {
      state = State::CLOSED;
      throw WSError("send: " + e.code().message());
    } catch (...) {
      state = State::CLOSED;
      throw;
    }
  }

  net::awaitable<WSMessage> read() {
    try {
      co_await ws.async_read(buffer, net::use_awaitable);
      auto recv_ts = std::chrono::system_clock::now();

      WSMessage msg{beast::buffers_to_string(buffer.data()), recv_ts};
      buffer.consume(buffer.size());
      co_return msg;
    } catch (boost::system::system_error const &e) {
      state = State::CLOSED;
      throw WSError("read: " + e.code().message());
    } catch (...) {
      state = State::CLOSED;
      throw;
    }
  }

  // graceful close
  net::awaitable<void> close() {
    state = State::CLOSING;
    try {
      co_await ws.async_close(websocket::close_code::normal,
                              net::use_awaitable);
    } catch (boost::system::system_error const &) {
      // already dead; nothing to do
    }
    state = State::CLOSED;
  }

  // hard close
  void cancel() {
    beast::error_code ec;
    beast::get_lowest_layer(ws).socket().close(ec);
    state = State::CLOSED;
  }

private:
  net::any_io_executor ex;
  WSConfig cfg;
  websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws;
  beast::flat_buffer buffer;
  State state = State::CLOSED;
};
