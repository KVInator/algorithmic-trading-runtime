#include "content_digest.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <limits>
#include <memory>
#include <stdexcept>

namespace trading {
namespace {

void check(NTSTATUS status, const char* operation) {
    if (status < 0) {
        throw std::runtime_error(std::string{operation} + " failed with NTSTATUS " + std::to_string(status));
    }
}

struct AlgorithmCloser {
    void operator()(void* handle) const noexcept { static_cast<void>(BCryptCloseAlgorithmProvider(handle, 0)); }
};
struct HashCloser {
    void operator()(void* handle) const noexcept { static_cast<void>(BCryptDestroyHash(handle)); }
};

} // namespace

std::string sha256(std::string_view bytes) {
    if (bytes.size() > std::numeric_limits<ULONG>::max()) {
        throw std::length_error("SHA-256 input exceeds the bounded provider call size");
    }
    BCRYPT_ALG_HANDLE raw_algorithm = nullptr;
    check(BCryptOpenAlgorithmProvider(&raw_algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0),
          "BCryptOpenAlgorithmProvider");
    const std::unique_ptr<void, AlgorithmCloser> algorithm{raw_algorithm};
    BCRYPT_HASH_HANDLE raw_hash = nullptr;
    check(BCryptCreateHash(algorithm.get(), &raw_hash, nullptr, 0, nullptr, 0, 0), "BCryptCreateHash");
    const std::unique_ptr<void, HashCloser> hash{raw_hash};
    if (!bytes.empty()) {
        check(BCryptHashData(hash.get(), reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                             static_cast<ULONG>(bytes.size()), 0), "BCryptHashData");
    }
    std::array<unsigned char, 32> digest{};
    check(BCryptFinishHash(hash.get(), digest.data(), static_cast<ULONG>(digest.size()), 0), "BCryptFinishHash");
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
