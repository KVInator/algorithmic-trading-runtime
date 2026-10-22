#pragma once

#include <string>

namespace trading {

class AlpacaCredentials {
public:
    AlpacaCredentials(std::string key, std::string secret);

    [[nodiscard]] const std::string& key() const noexcept;
    [[nodiscard]] const std::string& secret() const noexcept;

private:
    std::string key_;
    std::string secret_;
};

} // namespace trading
