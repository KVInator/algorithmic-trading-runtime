#pragma once

#include "daily_risk_study.hpp"

#include <filesystem>

namespace trading {

// The result must outlive the report; export does not modify its inputs.
class DailyRiskReport {
public:
    explicit DailyRiskReport(const DailyRiskResult& result) : result_{result} {}
    [[nodiscard]] std::filesystem::path write(const std::filesystem::path& root) const;

private:
    const DailyRiskResult& result_;
};

} // namespace trading
