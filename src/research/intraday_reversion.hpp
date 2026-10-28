#pragma once

#include "session_download.hpp"
#include "research_session.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace trading {

inline constexpr std::size_t reversion_history_sessions = 20;
inline constexpr std::size_t reversion_prefix_minutes = 150;
inline constexpr std::size_t reversion_observations = 25;

struct OpeningAnchorResidual {
    std::chrono::sys_seconds decision_at;
    double anchor_price;
    // Fixed arithmetic volume-weighted minute closes, not transaction VWAP.
    std::array<double, reversion_observations> values;
};

// The opening anchor ends at 10:00. Five-minute closing observations end at
// 10:00, 10:05, ..., 12:00. Only the exact completed 150-minute prefix is accepted.
class OpeningAnchorMeasurement {
public:
    explicit OpeningAnchorMeasurement(ResearchInputMode mode = ResearchInputMode::legacy_complete);
    [[nodiscard]] std::chrono::sys_seconds decision_time(const HistoricalBarRequest& session) const;
    [[nodiscard]] OpeningAnchorResidual measure(const HistoricalBarRequest& session,
        std::span<const MinuteBar> completed) const;
    [[nodiscard]] std::optional<OpeningAnchorResidual> observe(const HistoricalBarRequest& session,
        std::span<const MinuteBar> completed) const;
private:
    ResearchInputMode mode_;
};

enum class ReversionFitStatus {
    admissible, insufficient_pairs, singular_design, nonpositive_slope,
    unit_root_boundary, nonstationary_slope, zero_innovation_variance
};
[[nodiscard]] std::string_view name(ReversionFitStatus status);

struct PooledReversionFit {
    ReversionFitStatus status;
    std::size_t sequences;
    std::size_t pairs;
    std::optional<double> intercept;
    std::optional<double> slope;
    std::optional<double> innovation_variance; // SSE / (pairs - 2).
    std::optional<double> equilibrium;
    std::optional<double> speed_per_minute;
    std::optional<double> half_life_minutes;
};

// Unrestricted AR(1) OLS with an intercept. Each span is one separate session;
// no transition is constructed between sessions. OU interpretation is conditional
// on an admissible positive sub-unit slope and nonzero innovation variance.
class PooledReversionEstimator {
public:
    [[nodiscard]] PooledReversionFit fit(std::span<const std::vector<double>> sequences) const;
    [[nodiscard]] double expected_log_move(const PooledReversionFit& fit, double current,
        double entry_delay_minutes, double holding_minutes) const;
};

} // namespace trading
