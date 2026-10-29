#include "content_digest.hpp"

#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>

namespace trading {
namespace {

void check(NTSTATUS status, const char *operation) {
    if (status < 0) {
        throw std::runtime_error(std::string{operation} + " failed with NTSTATUS " + std::to_string(status));
    }
}

struct AlgorithmCloser {
    void operator()(void *handle) const noexcept {
        static_cast<void>(BCryptCloseAlgorithmProvider(handle, 0));
    }
};
struct HashCloser {
    void operator()(void *handle) const noexcept { static_cast<void>(BCryptDestroyHash(handle)); }
};

} // namespace

class Sha256::State {
  public:
    State() {
        BCRYPT_ALG_HANDLE raw_algorithm = nullptr;
        check(BCryptOpenAlgorithmProvider(&raw_algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0),
              "BCryptOpenAlgorithmProvider");
        algorithm.reset(raw_algorithm);
        BCRYPT_HASH_HANDLE raw_hash = nullptr;
        check(BCryptCreateHash(algorithm.get(), &raw_hash, nullptr, 0, nullptr, 0, 0), "BCryptCreateHash");
        hash.reset(raw_hash);
    }
    std::unique_ptr<void, AlgorithmCloser> algorithm;
    std::unique_ptr<void, HashCloser> hash;
    bool finished{};
};

Sha256::Sha256() : state_(std::make_unique<State>()) {}
Sha256::~Sha256() = default;
void Sha256::update(std::string_view bytes) {
    if (state_->finished) {
        throw std::logic_error("SHA-256 is already finalized");
    }
    while (!bytes.empty()) {
        const auto count = std::min<std::size_t>(bytes.size(), 1024U * 1024U);
        check(BCryptHashData(state_->hash.get(), reinterpret_cast<PUCHAR>(const_cast<char *>(bytes.data())),
                             static_cast<ULONG>(count), 0),
              "BCryptHashData");
        bytes.remove_prefix(count);
    }
}
std::string Sha256::finish() {
    if (state_->finished) {
        throw std::logic_error("SHA-256 is already finalized");
    }
    state_->finished = true;
    std::array<unsigned char, 32> digest{};
    check(BCryptFinishHash(state_->hash.get(), digest.data(), static_cast<ULONG>(digest.size()), 0),
          "BCryptFinishHash");
    constexpr std::string_view hex = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto value : digest) {
        result.push_back(hex[value >> 4]);
        result.push_back(hex[value & 15U]);
    }
    return result;
}

} // namespace trading
