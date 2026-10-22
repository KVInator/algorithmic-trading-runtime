#include "content_digest.hpp"

#include <openssl/evp.h>

#include <array>
#include <stdexcept>

namespace trading {

std::string sha256(std::string_view bytes) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), digest.data(), &length, EVP_sha256(), nullptr) != 1 || length != 32) {
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
