#include "alpaca_credential_store_windows.hpp"

#include <windows.h>
#include <wincred.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace trading {
namespace {

constexpr wchar_t credential_target[] = L"algorithmic-trading-runtime/alpaca-paper";

struct CredentialDeleter {
    void operator()(CREDENTIALW* credential) const noexcept {
        if (credential != nullptr) {
            if (credential->CredentialBlob != nullptr) {
                SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
            }
            CredFree(credential);
        }
    }
};

std::string read_api_key(const CREDENTIALW& credential) {
    if (credential.UserName == nullptr) {
        throw std::invalid_argument("saved Alpaca paper credential has no API key");
    }
    std::string key;
    for (const auto character : std::wstring_view{credential.UserName}) {
        if (static_cast<unsigned int>(character) > 0x7fU) {
            throw std::invalid_argument("saved Alpaca paper API key must be ASCII");
        }
        key.push_back(static_cast<char>(character));
    }
    return key;
}

std::string read_api_secret(const CREDENTIALW& credential) {
    if (credential.CredentialBlob == nullptr || credential.CredentialBlobSize == 0 ||
        credential.CredentialBlobSize % 2 != 0) {
        throw std::invalid_argument("saved Alpaca paper API secret must be a UTF-16LE password");
    }

    // Credential Manager's generic-password UI stores UTF-16LE bytes. Decode
    // without assuming alignment or a trailing NUL in the length-delimited blob.
    std::string secret;
    secret.reserve(credential.CredentialBlobSize / 2);
    for (DWORD index = 0; index < credential.CredentialBlobSize; index += 2) {
        const auto low = credential.CredentialBlob[index];
        const auto high = credential.CredentialBlob[index + 1];
        if (high != 0 || low > 0x7fU) {
            throw std::invalid_argument("saved Alpaca paper API secret must be ASCII");
        }
        secret.push_back(static_cast<char>(low));
    }
    return secret;
}

} // namespace

std::optional<AlpacaCredentials> load_saved_alpaca_paper_credentials() {
    CREDENTIALW* raw_credential = nullptr;
    if (!CredReadW(credential_target, CRED_TYPE_GENERIC, 0, &raw_credential)) {
        const auto code = GetLastError();
        if (code == ERROR_NOT_FOUND) {
            return std::nullopt;
        }
        throw std::system_error(static_cast<int>(code), std::system_category(),
                                "read saved Alpaca paper credential");
    }
    const std::unique_ptr<CREDENTIALW, CredentialDeleter> credential{raw_credential};
    auto key = read_api_key(*credential);
    auto secret = read_api_secret(*credential);
    return AlpacaCredentials{std::move(key), std::move(secret)};
}

} // namespace trading
