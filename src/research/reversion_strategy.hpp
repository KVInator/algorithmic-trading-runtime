#pragma once

#include "intraday_reversion.hpp"

#include <deque>
#include <string>

namespace trading {

struct ReversionParameters {
    std::size_t history_sessions{20};
    int observation_minutes{5}, hold_minutes{30};
    void validate(ResearchInputMode mode) const;
    bool operator==(const ReversionParameters &) const = default;
};
struct ReversionHistorySlot {
    HistoricalBarRequest session;
    std::optional<OpeningAnchorResidual> measurement;
};

struct ReversionPrior {
    std::chrono::sys_seconds oldest;
    std::chrono::sys_seconds latest;
    std::vector<std::vector<double>> sequences;
    bool complete;
    PooledReversionFit fit;
    std::optional<double> drift;
};

// Only completed, calendar-verified sessions can enter the bounded history.
// Unavailable anchors occupy a slot and cannot be replaced by older data.
class ReversionHistory {
  public:
    explicit ReversionHistory(ResearchInputMode mode = ResearchInputMode::legacy_complete,
                              ReversionParameters parameters = {}, PanelEvaluationAccess access = {});
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] const ReversionParameters &parameters() const noexcept { return parameters_; }
    [[nodiscard]] ReversionPrior prior(const HistoricalBarRequest &current) const;
    [[nodiscard]] std::optional<OpeningAnchorResidual> complete(const HistoricalBarRequest &session,
                                                                std::span<const MinuteBar> bars,
                                                                std::chrono::sys_seconds completed_at,
                                                                bool feature_allowed = true);

  private:
    ReversionParameters parameters_;
    std::deque<ReversionHistorySlot> slots_;
    ResearchInputMode mode_;
    std::string symbol_;
    OpeningAnchorMeasurement measurement_;
    PooledReversionEstimator estimator_;
};

enum class ReversionPolicy { candidate, sign_only, drift, always_long };
[[nodiscard]] std::string_view name(ReversionPolicy policy);

struct ReversionAction {
    bool enter;
    std::string reason;
};

struct ReversionDecision {
    std::chrono::sys_seconds at;
    ReversionPrior prior;
    std::optional<OpeningAnchorResidual> current;
    std::optional<double> expected_move;
    std::array<ReversionAction, 4> actions;
};

class ReversionStrategy {
  public:
    explicit ReversionStrategy(ResearchInputMode mode = ResearchInputMode::legacy_complete,
                               ReversionParameters parameters = {}, PanelEvaluationAccess access = {});
    [[nodiscard]] ReversionDecision decide(const HistoricalBarRequest &session,
                                           std::span<const MinuteBar> completed,
                                           const ReversionHistory &history) const;

  private:
    ReversionParameters parameters_;
    ResearchInputMode mode_;
    OpeningAnchorMeasurement measurement_;
    PooledReversionEstimator estimator_;
};
} // namespace trading
