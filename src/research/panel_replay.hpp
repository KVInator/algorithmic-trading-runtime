#pragma once
#include "intraday_continuation.hpp"
#include "reversion_strategy.hpp"
#include "daily_risk_study.hpp"
#include "panel_input_policy.hpp"
#include <deque>
#include <map>
#include <nlohmann/json.hpp>

namespace trading {
struct PanelTarget {
    std::chrono::sys_seconds at;
    std::optional<bool> target_long;
    std::string reason;
    std::optional<IntradayContinuationFeatures> continuation;
};
struct PanelPolicyReplay {
    std::string policy;
    std::vector<PanelTarget> decisions;
    std::vector<ScheduledRoundTrip> trades;
    bool execution_resolved{true};
    double gross{}, net{};
};
struct PanelRiskPrediction {
    std::string history_first, history_last, cutoff_utc, reason;
    std::optional<LaggedRiskEstimate> estimate;
    std::optional<double> intraday_weight, daily_weight, static_weight;
    std::size_t prior_returns{};
};
struct PanelReplayDay {
    std::string symbol, date;
    bool scored_period{}, common_operational{};
    PanelInputAvailability availability;
    std::vector<PanelPolicyReplay> policies;
    std::optional<MomentumDecision> momentum;
    std::optional<ReversionDecision> reversion;
    PanelRiskPrediction risk;
    std::optional<DailyRiskMeasurement> measurement;
};

// One owner per instrument. All decisions precede the commit of the current
// session to history. Execution prices never cross a feature boundary.
struct PanelReplayParameters {
    std::string family{"all"};
    int momentum_hold_minutes{25};
    std::size_t continuation_sessions{20};
    ReversionParameters reversion{};
    std::size_t risk_sessions{20};
    int risk_grid_minutes{5};
    void validate() const;
};
class PanelReplayState {
public:
    PanelReplayState(std::string symbol, PanelInputPolicy policy, PanelReplayParameters parameters = {});
    [[nodiscard]] PanelReplayDay advance(const HistoricalBarRequest& request, std::span<const MinuteBar> bars,
        double ex_dividend = 0, const std::optional<MinuteBar>& recovered_close = {});
private:
    PanelReplayParameters parameters_;
    std::string symbol_, previous_date_, previous_close_cutoff_;
    PanelInputPolicy policy_;
    IntradayContinuationHistory continuation_;
    ReversionHistory reversion_;
    std::deque<DailyRiskMeasurement> risk_history_;
    std::optional<double> previous_close_, static_weight_;
};
// Canonical record encoder; the report writer owns storage, not the engine.
[[nodiscard]] nlohmann::json panel_replay_record(const PanelReplayDay& day);
[[nodiscard]] nlohmann::json verify_panel_reference_parity(std::span<const SessionDownload> legacy,
    const RiskStudyInputs* risk = nullptr);
} // namespace trading
