#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <openssl/evp.h>
#include <openssl/tls1.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// handles signatures and auth stuff
class Auth {
public:
  std::vector<std::pair<std::string, std::string>>
  get_auth_headers(std::string path) {
    std::string access_key = env("PM_ACCESS_KEY");
    std::string secret_key = env("PM_SECRET_KEY");

    std::string timestamp =
        std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count());

    auto signature = get_signature(secret_key, path, timestamp);

    return {{"X-PM-Access-Key", access_key},
            {"X-PM-Timestamp", timestamp},
            {"X-PM-Signature", signature}};
  }

private:
  std::string get_signature(std::string secret_key, std::string path,
                            std::string timestamp) {

    return sign(secret_key, timestamp + "GET" + path);
  }

  std::string sign(std::string const &secret_b64, std::string const &message) {
    std::vector<unsigned char> secret(secret_b64.size());
    int secret_len = EVP_DecodeBlock(
        secret.data(),
        reinterpret_cast<unsigned char const *>(secret_b64.data()),
        static_cast<int>(secret_b64.size()));
    if (secret_len < 32)
      throw std::runtime_error("PM_SECRET_KEY is not base64 of >= 32 bytes");

    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{
        EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, secret.data(),
                                     32),
        EVP_PKEY_free};
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx{
        EVP_MD_CTX_new(), EVP_MD_CTX_free};

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

  std::string env(char const *name) {
    char const *value = std::getenv(name);
    if (!value || !*value)
      throw std::runtime_error(std::string(name) + " is not set");
    return value;
  }
};
