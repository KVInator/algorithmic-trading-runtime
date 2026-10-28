#pragma once
#include "panel_replay.hpp"
#include "shared_capital.hpp"

namespace trading::execution {
// M3 primary features feed the shared account. Continuation's entry/hold
// distinction uses this owner's actual inventory, through the same action rule.
class SharedStrategyAdapter {
public:
    [[nodiscard]] SharedTargetBatch at(std::string identity, std::chrono::sys_seconds decision,
        std::span<const PanelReplayDay> days, const SharedCapitalSnapshot& account,
        std::vector<SharedQuote> quotes) const;
};
} // namespace trading::execution
