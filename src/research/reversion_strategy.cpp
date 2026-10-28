#include "reversion_strategy.hpp"

#include <cmath>
#include <stdexcept>

namespace trading {
namespace {
bool positive_anchor_volume(std::span<const MinuteBar> bars) {
    for (std::size_t i = 0; i < 30; ++i) {
        if (bars[i].volume() > 0) {
            return true;
        }
    }
    return false;
}
} // namespace

ReversionHistory::ReversionHistory(ResearchInputMode mode) : mode_{mode}, measurement_{mode} {}

bool ReversionHistory::ready() const noexcept {
    return slots_.size() == reversion_history_sessions;
}

ReversionPrior ReversionHistory::prior(const HistoricalBarRequest& current) const {
    static_cast<void>(measurement_.decision_time(current));
    if ((!symbol_.empty() && current.symbol() != symbol_) || !ready() || current.start() < slots_.back().session.end_exclusive()) {
        throw std::invalid_argument("reversion requires twenty strictly earlier completed session slots");
    }
    ReversionPrior result{
        slots_.front().session.start(), slots_.back().session.start(), {}, true, estimator_.fit({}), {}};
    for (const auto& slot : slots_) {
        if (!slot.measurement) {
            result.complete = false;
        } else {
            result.sequences.emplace_back(slot.measurement->values.begin(), slot.measurement->values.end());
        }
    }
    if (result.complete) {
        result.fit = estimator_.fit(result.sequences);
        long double increments = 0;
        for (const auto& sequence : result.sequences) {
            for (std::size_t i = 1; i < sequence.size(); ++i) {
                increments += static_cast<long double>(sequence[i]) - sequence[i - 1];
            }
        }
        result.drift = static_cast<double>(6 * increments / 480);
        if (!std::isfinite(*result.drift)) {
            throw std::overflow_error("nonfinite prior drift");
        }
    }
    return result;
}

std::optional<OpeningAnchorResidual> ReversionHistory::complete(const HistoricalBarRequest& session,
                                                                std::span<const MinuteBar> bars,
                                                                std::chrono::sys_seconds completed_at, bool feature_allowed) {
    static_cast<void>(measurement_.decision_time(session));
    const auto audit = audit_session(session, bars);
    if ((!symbol_.empty() && symbol_ != session.symbol()) || completed_at != session.end_exclusive() ||
        (mode_ == ResearchInputMode::legacy_complete && (bars.size() != 390 || !audit.complete())) ||
        (!slots_.empty() && session.start() < slots_.back().session.end_exclusive())) {
        throw std::invalid_argument("history append requires an ordered completed full session");
    }
    std::optional<OpeningAnchorResidual> measured;
    if (feature_allowed) {
        if (mode_ == ResearchInputMode::registered_panel) {
            measured = measurement_.observe(session, ResearchSessionView{session, bars}.before(measurement_.decision_time(session)));
        } else if (positive_anchor_volume(bars)) { measured = measurement_.measure(session, bars.first(reversion_prefix_minutes)); }
    }
    symbol_ = session.symbol();
    slots_.push_back({session, measured});
    if (slots_.size() > reversion_history_sessions) {
        slots_.pop_front();
    }
    return measured;
}

std::string_view name(ReversionPolicy policy) {
    switch (policy) {
    case ReversionPolicy::candidate:
        return "candidate";
    case ReversionPolicy::sign_only:
        return "sign_only";
    case ReversionPolicy::drift:
        return "drift";
    case ReversionPolicy::always_long:
        return "always_long";
    }
    throw std::invalid_argument("unknown reversion policy");
}

ReversionStrategy::ReversionStrategy(ResearchInputMode mode) : mode_{mode}, measurement_{mode} {}

ReversionDecision ReversionStrategy::decide(const HistoricalBarRequest& session,
                                            std::span<const MinuteBar> completed,
                                            const ReversionHistory& history) const {
    const auto at = measurement_.decision_time(session);
    if (mode_ == ResearchInputMode::legacy_complete && (completed.size() != reversion_prefix_minutes ||
        !audit_session(HistoricalBarRequest{session.symbol(), session.start(), at}, completed).complete())) {
        throw std::invalid_argument("noon decision requires exactly its completed prefix");
    }
    ReversionDecision result{at,
                             history.prior(session),
                             {},
                             {},
                             {{{false, "current_anchor_unavailable"},
                               {false, "current_anchor_unavailable"},
                               {false, "history_anchor_unavailable"},
                               {true, "scheduled_long"}}}};
    if (mode_ == ResearchInputMode::registered_panel) {
        result.current = measurement_.observe(session, completed);
        if (!result.current || !result.prior.complete) {
            for (auto& action : result.actions) { action = {false, !result.current ? "current_feature_unavailable" : "history_feature_unavailable"}; }
            return result;
        }
    } else if (positive_anchor_volume(completed)) { result.current = measurement_.measure(session, completed); }
    const double hurdle = std::log1p(0.0001) - std::log1p(-0.0001);
    if (result.prior.complete) {
        result.actions[2] = {*result.prior.drift > hurdle, *result.prior.drift > hurdle
                                                               ? "drift_above_cost_hurdle"
                                                               : "drift_below_cost_hurdle"};
    }
    if (result.current) {
        const double x = result.current->values.back();
        result.actions[1] = {x < 0, x < 0 ? "negative_residual" : "nonnegative_residual"};
        if (!result.prior.complete) {
            result.actions[0] = {false, "history_anchor_unavailable"};
        } else if (result.prior.fit.status != ReversionFitStatus::admissible) {
            result.actions[0] = {false, "model_" + std::string{name(result.prior.fit.status)}};
        } else {
            result.expected_move = estimator_.expected_log_move(result.prior.fit, x, 1, 30);
            const bool below = x < *result.prior.fit.equilibrium;
            const bool enter = below && *result.expected_move > hurdle;
            result.actions[0] = {enter, enter    ? "expected_move_above_cost_hurdle"
                                        : !below ? "not_below_equilibrium"
                                                 : "expected_move_below_cost_hurdle"};
        }
    }
    return result;
}
} // namespace trading
