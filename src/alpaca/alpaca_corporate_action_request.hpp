#pragma once

#include <string>
#include <optional>

namespace trading {

// Process-date bounds include the early-2024 tail of 2023 entitlements.
// Ex-date completeness still requires reconciliation against an issuer source.
class CorporateActionRequest {
public:
    CorporateActionRequest(std::string first, std::string last);
    CorporateActionRequest(std::string symbol, std::string first, std::string last);
    [[nodiscard]] std::string path(const std::optional<std::string>& token = std::nullopt) const;
    [[nodiscard]] const std::string& symbol() const noexcept { return symbol_; }
    [[nodiscard]] const std::string& first() const noexcept { return first_; }
    [[nodiscard]] const std::string& last() const noexcept { return last_; }
private:
    std::string symbol_;
    std::string first_;
    std::string last_;
};

// One-page, all-type SPY capability request. Not an ex-date coverage contract.
class CorporateActionProbeRequest {
public:
    explicit CorporateActionProbeRequest(int process_date_year);
    [[nodiscard]] std::string path() const;
private:
    int year_;
};

} // namespace trading
