#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace trading {

// Complete paired day record for one declared cost/delay case. Strategy
// returns equal the window return on active days and zero on cash days.
struct DailyResearchReturn {
    std::string date;
    bool active;
    double window_gross_return;
    double window_net_return;
};

struct ReturnStatistics {
    double mean;
    double compounded;
    double maximum_drawdown; // Nonnegative loss fraction, including initial wealth.
    std::optional<double> sample_standard_deviation;
    std::optional<double> annualized_daily_sharpe;
};

struct WinningContribution {
    std::string date;
    double net_return;
};

struct PeriodStatistics {
    std::size_t sessions;
    std::size_t active_days;
    double fraction_active;
    ReturnStatistics strategy_net;
    ReturnStatistics always_long_net;
    ReturnStatistics cash;
    std::optional<double> mean_gross_active;
    std::optional<double> mean_net_active;
    std::optional<double> mean_gross_nonpositive_signal;
    std::optional<double> signal_contrast_gross;
    std::optional<double> break_even_cost_bps_per_side;
    std::vector<WinningContribution> largest_five_winners;
    double arithmetic_net_sum;
    double largest_five_net_sum;
    std::optional<double> largest_five_share_of_arithmetic_net;
    std::optional<double> largest_five_share_of_positive_returns;
};

[[nodiscard]] PeriodStatistics describe_returns(std::span<const DailyResearchReturn> days);

struct PercentileInterval {
    double lower;
    double upper;
};

struct BootstrapStatistics {
    std::size_t block_length;
    std::size_t replicates;
    std::uint64_t seed;
    PercentileInterval mean_net_daily_95;
    std::optional<PercentileInterval> signal_contrast_gross_95;
    std::size_t undefined_contrast_replicates;
};

// Non-circular moving blocks; starts uniform on [0, N-L]. Each invocation
// resets mt19937_64 to the protocol seed. All statistics share sampled days.
class MovingBlockBootstrap {
public:
    explicit MovingBlockBootstrap(std::size_t block_length);
    [[nodiscard]] BootstrapStatistics run(std::span<const DailyResearchReturn> days) const;
private:
    std::size_t block_length_;
};

} // namespace trading
