#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <openssl/evp.h>
#include <openssl/tls1.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
namespace ssl = net::ssl;
using tcp = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>

static std::string env(char const *name) {
  char const *value = std::getenv(name);
  if (!value || !*value)
    throw std::runtime_error(std::string(name) + " is not set");
  return value;
}

// base64(Ed25519(first 32 bytes of base64-decoded secret, message)), the
// X-PM-Signature scheme from https://docs.polymarket.us/api/authentication
static std::string sign(std::string const &secret_b64,
                        std::string const &message) {
  std::vector<unsigned char> secret(secret_b64.size());
  int secret_len = EVP_DecodeBlock(
      secret.data(), reinterpret_cast<unsigned char const *>(secret_b64.data()),
      static_cast<int>(secret_b64.size()));
  if (secret_len < 32)
    throw std::runtime_error("PM_SECRET_KEY is not base64 of >= 32 bytes");

  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{
      EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, secret.data(),
                                   32),
      EVP_PKEY_free};
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx{EVP_MD_CTX_new(),
                                                              EVP_MD_CTX_free};

  unsigned char sig[64];
  size_t sig_len = sizeof sig;
  if (!key || !ctx ||
      EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) !=
          1 ||
      EVP_DigestSign(ctx.get(), sig, &sig_len,
                     reinterpret_cast<unsigned char const *>(message.data()),
                     message.size()) != 1)
    throw std::runtime_error("Ed25519 signing failed");

  // +1 for the NUL EVP_EncodeBlock writes
  std::string out(4 * ((sig_len + 2) / 3) + 1, '\0');
  int out_len = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(out.data()),
                                sig, static_cast<int>(sig_len));
  out.resize(static_cast<size_t>(out_len));
  return out;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: pmsim-collector <market-slug>\n"
              << "Needs PM_ACCESS_KEY and PM_SECRET_KEY in the environment.\n";
    return EXIT_FAILURE;
  }

  try {
    std::string host = "api.polymarket.us";
    std::string port = "443";
    std::string path = "/v1/ws/markets";
    std::string slug = argv[1];

    std::string access_key = env("PM_ACCESS_KEY");
    std::string secret_key = env("PM_SECRET_KEY");

    net::io_context ioc;
    ssl::context ctx{ssl::context::tlsv12_client};

    tcp::resolver resolver{ioc};
    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws{ioc, ctx};

    std::string sub = R"({"subscribe":{"requestId":"1","subscriptionType":)"
                      R"("SUBSCRIPTION_TYPE_MARKET_DATA","marketSlugs":[")" +
                      slug + R"("]}})";

    beast::flat_buffer buffer;
    std::function<void()> read_next;
    read_next = [&] {
      ws.async_read(buffer, [&](beast::error_code ec, std::size_t) {
        if (ec) {
          std::cerr << "read: " << ec.message() << "\n";
          return;
        }
        std::cout << beast::make_printable(buffer.data()) << "\n";
        buffer.consume(buffer.size());
        read_next();
      });
    };

    resolver.async_resolve(
        host, port,
        [&](beast::error_code ec, tcp::resolver::results_type results) {
          if (ec) {
            std::cerr << "resolve: " << ec.message() << "\n";
            return;
          }

          beast::get_lowest_layer(ws).async_connect(
              results, [&](beast::error_code ec, tcp::endpoint) {
                if (ec) {
                  std::cerr << "connect: " << ec.message() << "\n";
                  return;
                }

                if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(),
                                              host.c_str())) {
                  std::cerr << "failed to set SNI hostname\n";
                  return;
                }

                ws.next_layer().async_handshake(
                    ssl::stream_base::client, [&](beast::error_code ec) {
                      if (ec) {
                        std::cerr << "ssl handshake: " << ec.message() << "\n";
                        return;
                      }

                      std::string timestamp = std::to_string(
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now()
                                  .time_since_epoch())
                              .count());
                      std::string signature =
                          sign(secret_key, timestamp + "GET" + path);

                      ws.set_option(websocket::stream_base::decorator(
                          [access_key, timestamp,
                           signature](websocket::request_type &req) {
                            req.set(http::field::user_agent,
                                    std::string(BOOST_BEAST_VERSION_STRING) +
                                        " pmsim-collector");
                            req.set("X-PM-Access-Key", access_key);
                            req.set("X-PM-Timestamp", timestamp);
                            req.set("X-PM-Signature", signature);
                          }));

                      ws.async_handshake(host, path, [&](beast::error_code ec) {
                        if (ec) {
                          std::cerr << "ws handshake: " << ec.message() << "\n";
                          return;
                        }

                        ws.async_write(net::buffer(sub),
                                       [&](beast::error_code ec, std::size_t) {
                                         if (ec) {
                                           std::cerr
                                               << "write: " << ec.message()
                                               << "\n";
                                           return;
                                         }
                                         read_next();
                                       });
                      });
                    });
              });
        });

    ioc.run();
  } catch (std::exception const &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
