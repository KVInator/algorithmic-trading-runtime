#pragma once
#include "daily_risk_study.hpp"

namespace trading {
[[nodiscard]] std::filesystem::path save_daily_risk_report(const DailyRiskResult& result,
    const std::filesystem::path& output_root);
} // namespace trading
