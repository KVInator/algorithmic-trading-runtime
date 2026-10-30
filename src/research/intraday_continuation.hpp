#pragma once

#include "intraday_momentum.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <deque>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace trading {

inline constexpr std::size_t continuation_history_sessions = 20;

struct IntradayContinuationFeatures {
    std::chrono::sys_seconds at;
    std::chrono::sys_seconds oldest_history_start;
    std::chrono::sys_seconds latest_history_start;
    double move;
    double typical_absolute_move;
    double cumulative_volume_shares;
    double historical_median_volume_shares;
    double relative_volume;
};

// Stores only complete earlier sessions. The caller supplies calendar-verified
// SPY windows and applies the registered dataset/exclusion policy.
class IntradayContinuationHistory {
  public:
    explicit IntradayContinuationHistory(ResearchInputMode mode = ResearchInputMode::legacy_complete,
                                         std::size_t history_sessions = continuation_history_sessions,
                                         PanelEvaluationAccess access = {});
    void append(const HistoricalBarRequest &session, std::span<const MinuteBar> bars);
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] IntradayContinuationFeatures measure(const HistoricalBarRequest &current_session,
                                                       std::span<const MinuteBar> completed_bars,
                                                       std::chrono::sys_seconds at) const;

  private:
    struct SessionProfile {
        std::chrono::sys_seconds start;
        std::chrono::sys_seconds end;
        std::array<double, 390> absolute_moves{};
        std::array<double, 390> cumulative_volumes{};
    };
    std::deque<SessionProfile> sessions_;
    ResearchInputMode mode_;
    PanelEvaluationAccess access_;
    std::size_t history_sessions_;
    std::string symbol_;
};

enum class ContinuationVariant { price_only, volume_conditioned };
enum class ContinuationAction { stay_cash, enter_long, hold_long, exit_long };

[[nodiscard]] std::string_view continuation_action_name(ContinuationAction action);
[[nodiscard]] std::string_view continuation_protocol_sha256() noexcept;

struct IntradayContinuationDecision {
    std::chrono::sys_seconds at;
    ContinuationAction action;
    // The terminal scheduled liquidation needs no signal feature.
    std::optional<IntradayContinuationFeatures> features;
};

class IntradayContinuationStrategy {
  public:
    IntradayContinuationStrategy(HistoricalBarRequest session, ContinuationVariant variant,
                                 ResearchInputMode mode = ResearchInputMode::legacy_complete,
                                 PanelEvaluationAccess access = {});
    [[nodiscard]] static ContinuationAction action(const IntradayContinuationFeatures &features,
                                                   ContinuationVariant variant, bool currently_long);
    [[nodiscard]] IntradayContinuationDecision decide(const IntradayContinuationHistory &history,
                                                      std::span<const MinuteBar> completed_bars,
                                                      std::chrono::sys_seconds at,
                                                      bool currently_long) const;
    [[nodiscard]] const HistoricalBarRequest &session() const noexcept;
    [[nodiscard]] ContinuationVariant variant() const noexcept;
    [[nodiscard]] std::array<std::chrono::sys_seconds, 12> decision_times() const;
    [[nodiscard]] std::chrono::sys_seconds liquidation_time() const noexcept;

  private:
    HistoricalBarRequest session_;
    ContinuationVariant variant_;
    ResearchInputMode mode_;
    PanelEvaluationAccess access_;
};

struct IntradayContinuationReplay {
    std::vector<IntradayContinuationDecision> decisions;
    std::vector<ScheduledRoundTrip> trades;
    ScheduledRoundTrip always_long_control;
    double strategy_gross_return;
    double strategy_net_return;
    std::chrono::minutes minutes_in_market;
};

// Full-session deterministic price-reference replay. Only completed prefixes
// cross the strategy boundary; no broker, queue or intraminute fill model.
class IntradayContinuationReplayEngine {
  public:
    explicit IntradayContinuationReplayEngine(std::chrono::minutes delay = std::chrono::minutes{1},
                                              double cost_bps_per_side = 1.0);
    [[nodiscard]] IntradayContinuationReplay run(const IntradayContinuationStrategy &strategy,
                                                 const IntradayContinuationHistory &history,
                                                 std::span<const MinuteBar> bars) const;

  private:
    std::chrono::minutes delay_;
    double cost_bps_per_side_;
};

} // namespace trading
