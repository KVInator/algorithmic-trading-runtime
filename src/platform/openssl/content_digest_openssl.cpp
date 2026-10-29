#include "content_digest.hpp"

#include <openssl/evp.h>

#include <array>
#include <stdexcept>

namespace trading {

class Sha256::State {
  public:
    State() {
        if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
            throw std::runtime_error("OpenSSL SHA-256 initialization failed");
        }
    }
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context{EVP_MD_CTX_new(), &EVP_MD_CTX_free};
    bool finished{};
};

Sha256::Sha256() : state_(std::make_unique<State>()) {}
Sha256::~Sha256() = default;
void Sha256::update(std::string_view bytes) {
    if (state_->finished) {
        throw std::logic_error("SHA-256 is already finalized");
    }
    if (!bytes.empty() && EVP_DigestUpdate(state_->context.get(), bytes.data(), bytes.size()) != 1) {
        throw std::runtime_error("OpenSSL SHA-256 update failed");
    }
}
std::string Sha256::finish() {
    if (state_->finished) {
        throw std::logic_error("SHA-256 is already finalized");
    }
    state_->finished = true;
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(state_->context.get(), digest.data(), &length) != 1 || length != 32) {
        throw std::runtime_error("OpenSSL SHA-256 calculation failed");
    }
    constexpr std::string_view hex = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (unsigned int index = 0; index < length; ++index) {
        result.push_back(hex[digest[index] >> 4]);
        result.push_back(hex[digest[index] & 15U]);
    }
    return result;
}

} // namespace trading
