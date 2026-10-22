#pragma once

#include "intraday_momentum.hpp"
#include "session_download.hpp"
#include "return_statistics.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace trading {

struct DailyMomentumReturn {
    std::string date;
    MomentumReplay replay;
};

struct MomentumCaseSummary {
    std::chrono::minutes delay;
    double cost_bps_per_side;
    std::vector<DailyMomentumReturn> days;
    std::size_t active_days;
    double strategy_mean_daily;
    double always_long_mean_daily;
    double strategy_compounded;
    double always_long_compounded;
    std::optional<double> strategy_mean_net_active;
    std::optional<double> signal_contrast_gross;
};

// Fixed protocol matrix; all descriptive aggregation uses return_statistics.
class BatchMomentumReplay {
public:
    [[nodiscard]] std::vector<MomentumCaseSummary> run(std::span<const SessionDownload> sessions) const;
};

[[nodiscard]] std::vector<DailyResearchReturn> research_returns(std::span<const DailyMomentumReturn> days);
[[nodiscard]] std::string format_momentum_daily_csv(std::span<const MomentumCaseSummary> cases);

} // namespace trading
