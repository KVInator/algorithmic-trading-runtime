#include "shared_strategy_adapter.hpp"
#include "utc_timestamp.hpp"
#include <set>
#include <stdexcept>

namespace trading::execution {
SharedTargetBatch SharedStrategyAdapter::at(std::string identity, std::chrono::sys_seconds decision,
    std::span<const PanelReplayDay> days, const SharedCapitalSnapshot& account, std::vector<SharedQuote> quotes) const {
    SharedTargetBatch batch{std::move(identity), decision.time_since_epoch().count(), std::move(quotes), {}};
    const auto date = format_utc_seconds(decision).substr(0, 10);
    if (days.size() != 5 || date >= "2024-01-01") { throw std::invalid_argument("M4 adapter requires five pre-2024 primary session records"); }
    std::set<std::string> seen;
    for (const auto& day : days) {
        if (day.date != date || !account.account.positions.contains(day.symbol) || !seen.insert(day.symbol).second) { throw std::invalid_argument("shared adapter cannot mix instruments or decision dates"); }
        for (const auto& path : day.policies) {
            std::string family;
            if (path.policy == "momentum") { family = "momentum"; }
            else if (path.policy == "continuation_volume") { family = "volume_conditioned_continuation"; }
            else if (path.policy == "reversion_candidate") { family = "reversion"; }
            else { continue; }
            const OwnerKey owner{family, day.symbol};
            for (const auto& target : path.decisions) {
                if (target.at != decision) { continue; }
                if (!target.target_long) { throw std::invalid_argument("unresolved reference path cannot authorize a shared target"); }
                bool desired = *target.target_long;
                if (family == "volume_conditioned_continuation" && target.continuation) {
                    const auto action = IntradayContinuationStrategy::action(*target.continuation, ContinuationVariant::volume_conditioned,
                        account.owners.at(owner).shares > 0);
                    desired = action == ContinuationAction::enter_long || action == ContinuationAction::hold_long;
                }
                batch.signals.push_back({owner, desired});
            }
        }
    }
    return batch;
}
} // namespace trading::execution
