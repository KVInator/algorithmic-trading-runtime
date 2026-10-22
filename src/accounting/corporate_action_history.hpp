#pragma once

#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trading {

struct CashDistribution {
    std::string id;
    std::string ex_date;
    std::string record_date;
    std::string payable_date;
    std::string process_date;
    double usd_per_share;
};

// Present-day historical records, not evidence of announcement availability.
// Unsupported action types are retained as blockers rather than discarded.
class CorporateActionHistory {
public:
    [[nodiscard]] std::optional<std::string> append_page(std::string_view payload,
        const std::string& process_first, const std::string& process_last);
    [[nodiscard]] const std::vector<CashDistribution>& distributions() const noexcept;
    [[nodiscard]] const std::vector<std::string>& unsupported_types() const noexcept;
    // Compare the full ex-date horizon against independently sourced records.
    // USD/share tolerance is 1e-6, matching the retained six-decimal rates.
    [[nodiscard]] std::vector<std::string> reconcile(std::span<const CashDistribution> reference) const;
private:
    std::set<std::string> ids_;
    std::vector<CashDistribution> distributions_;
    std::vector<std::string> unsupported_;
};

} // namespace trading
