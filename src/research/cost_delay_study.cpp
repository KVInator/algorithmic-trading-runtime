#include "cost_delay_study.hpp"
#include "long_cash_account.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace trading {
namespace {
void equal(double a, double b) {
    if (!std::isfinite(a) || !std::isfinite(b) ||
        std::abs(a - b) > 1e-12 * std::max({1.0, std::abs(a), std::abs(b)})) {
        throw std::logic_error("cost/delay primary reference parity failed");
    }
}
} // namespace
CostDelayStudy::CostDelayStudy(const ExtensionStudyContract &contract, PanelEvaluationAccess access)
    : access_(std::move(access)), costs_(contract.document()
                                             .at("reference_costs")
                                             .at("per_side_basis_points")
                                             .get<std::vector<double>>()),
      delays_(contract.document().at("reference_costs").at("delays_minutes").get<std::vector<int>>()) {
    if (costs_ != std::vector<double>{0, .5, 1, 2, 5} || delays_ != std::vector<int>{1, 2}) {
        throw std::invalid_argument("cost/delay grid differs from the registered comparison");
    }
}
const std::vector<double> &CostDelayStudy::costs() const noexcept { return costs_; }
const std::vector<int> &CostDelayStudy::delays() const noexcept { return delays_; }
CostDelayPath CostDelayStudy::replay(const PanelPolicyReplay &policy,
                                     const HistoricalBarRequest &request,
                                     std::span<const MinuteBar> bars, int delay) const {
    require_panel_regular_session(request, access_);
    if (std::find(delays_.begin(), delays_.end(), delay) == delays_.end() || policy.decisions.empty()) {
        throw std::invalid_argument("cost/delay replay requires registered delay and frozen decisions");
    }
    const ResearchSessionView observed{request, bars};
    const auto lag = std::chrono::minutes{delay};
    CostDelayPath out{policy.policy, "resolved", delay, true, {}, {}, 0};
    std::optional<std::pair<std::chrono::sys_seconds, double>> entry;
    std::optional<std::chrono::sys_seconds> previous;
    for (const auto &decision : policy.decisions) {
        if (decision.at < request.start() || decision.at + lag >= request.end_exclusive() ||
            (decision.at - request.start()) % std::chrono::minutes{1} != std::chrono::seconds{0} ||
            (previous && decision.at <= *previous)) {
            throw std::invalid_argument("frozen decision clocks are unordered or outside the session");
        }
        previous = decision.at;
        if (!decision.target_long) {
            out.resolved = false;
            out.reason = "primary_target_unavailable";
            break;
        }
        if (*decision.target_long == entry.has_value()) {
            continue;
        }
        const auto minute =
            std::chrono::duration_cast<std::chrono::minutes>(decision.at + lag - request.start())
                .count();
        const auto *price = observed.at(static_cast<std::size_t>(minute));
        if (!price) {
            out.resolved = false;
            out.reason = "execution_open_unavailable";
            break;
        }
        if (*decision.target_long) {
            entry = std::pair{decision.at, price->prices().open};
        } else {
            out.trades.push_back({entry->first, entry->first + lag, decision.at, decision.at + lag,
                                  entry->second, price->prices().open,
                                  evaluate_long_prices(entry->second, price->prices().open, 0)});
            out.minutes_held +=
                std::chrono::duration_cast<std::chrono::minutes>(decision.at - entry->first).count();
            entry.reset();
        }
    }
    if (out.resolved && entry) {
        throw std::invalid_argument("frozen intraday decisions lack terminal liquidation");
    }
    if (!out.resolved) {
        return out;
    }
    for (double cost : costs_) {
        LongCashAccount account{1};
        long double gross = 1, net = 1;
        for (const auto &trade : out.trades) {
            account.rebalance(trade.entry_reference_time, trade.entry_reference_price, 1, cost);
            account.rebalance(trade.exit_reference_time, trade.exit_reference_price, 0, cost);
            const auto returns =
                evaluate_long_prices(trade.entry_reference_price, trade.exit_reference_price, cost);
            gross *= 1 + returns.gross_return;
            net *= 1 + returns.net_return;
        }
        long double notional = 0, paid = 0;
        for (const auto &event : account.ledger()) {
            notional += event.traded_notional_usd;
            paid += event.cost_usd;
        }
        const auto terminal =
            account.value(out.trades.empty() ? 1 : out.trades.back().exit_reference_price);
        equal(terminal.equity_usd, static_cast<double>(net));
        if (terminal.shares != 0 || terminal.receivable_usd != 0) {
            throw std::logic_error("intraday cost reference retained overnight assets");
        }
        equal(static_cast<double>(paid), static_cast<double>(notional) * cost / 10000);
        out.outcomes.push_back({cost, static_cast<double>(gross - 1), terminal.equity_usd - 1,
                                static_cast<double>(notional), static_cast<double>(paid)});
    }
    return out;
}
std::vector<CostDelayPath> CostDelayStudy::evaluate(const PanelReplayDay &day,
                                                    const SessionDownload &session) const {
    access_.require_price(day.date);
    if (day.symbol != session.request.symbol() ||
        day.date != format_utc_seconds(session.request.start()).substr(0, 10)) {
        throw std::invalid_argument(
            "cost/delay replay cannot mix instruments, dates or reserved inputs");
    }
    const std::set<std::string> expected{"momentum",
                                         "momentum_always_long",
                                         "continuation_price",
                                         "continuation_volume",
                                         "reversion_candidate",
                                         "reversion_sign_only",
                                         "reversion_drift",
                                         "reversion_always_long"};
    std::set<std::string> names;
    for (const auto &policy : day.policies) {
        names.insert(policy.policy);
    }
    if (day.availability.regular_session ? (names != expected || day.policies.size() != 8)
                                         : !day.policies.empty()) {
        throw std::invalid_argument("cost/delay primary policy matrix differs");
    }
    std::vector<CostDelayPath> out;
    for (const auto &policy : day.policies) {
        for (int delay : delays_) {
            auto path = replay(policy, session.request, session.bars, delay);
            if (day.common_operational && !path.resolved) {
                throw std::logic_error("common operational mask admitted an unresolved execution path");
            }
            if (delay == 1) {
                if (path.resolved != policy.execution_resolved ||
                    path.trades.size() != policy.trades.size()) {
                    throw std::logic_error("cost/delay primary trades or availability changed");
                }
                for (std::size_t i = 0; i < path.trades.size(); ++i) {
                    const auto &a = path.trades[i];
                    const auto &b = policy.trades[i];
                    if (a.entry_decision != b.entry_decision || a.exit_decision != b.exit_decision) {
                        throw std::logic_error("cost/delay primary trade clocks changed");
                    }
                    equal(a.entry_reference_price, b.entry_reference_price);
                    equal(a.exit_reference_price, b.exit_reference_price);
                }
                if (path.resolved) {
                    equal(path.outcomes.at(2).gross_return, policy.gross);
                    equal(path.outcomes.at(2).net_return, policy.net);
                }
            }
            out.push_back(std::move(path));
        }
    }
    return out;
}
} // namespace trading
