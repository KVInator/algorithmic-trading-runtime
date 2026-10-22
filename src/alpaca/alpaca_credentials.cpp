#include "alpaca_credentials.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {

void validate_header_value(const std::string& value, const char* field) {
    if (value.empty() ||
        std::any_of(value.begin(), value.end(), [](unsigned char character) {
            return character <= 0x20 || character >= 0x7f;
        })) {
        throw std::invalid_argument(std::string{field} +
                                    " must contain printable ASCII without whitespace");
    }
}

} // namespace

AlpacaCredentials::AlpacaCredentials(std::string key, std::string secret)
    : key_{std::move(key)}, secret_{std::move(secret)} {
    validate_header_value(key_, "API key");
    validate_header_value(secret_, "API secret");
}

const std::string& AlpacaCredentials::key() const noexcept {
    return key_;
}

const std::string& AlpacaCredentials::secret() const noexcept {
    return secret_;
}

} // namespace trading
