#pragma once

#include "journaled_runtime.hpp"
#include "simulated_broker.hpp"

namespace trading::execution {

enum class RouteStatus { submitted, waiting, settled, blocked, expired, rejected, superseded };
struct RouteResult { RouteStatus status; std::string order_id; std::string reason; };

// One strategy owns the single-symbol account. References must outlive this
// coordinator. A new coordinator may attach to a recovered runtime and the same
// external simulator; report delivery cursors are deliberately reconstructible.
class OfflinePipeline {
public:
    OfflinePipeline(JournaledRuntime& runtime, SimulatedBroker& broker, std::int64_t maximum_quote_age_seconds = 2);
    [[nodiscard]] bool synchronize();
    void deliver_reports();
    [[nodiscard]] Outcome deliver_report(std::size_t index);
    [[nodiscard]] RouteResult accept_target(const TargetIntent& target, std::int64_t now_seconds);
    [[nodiscard]] RouteResult route_latest(std::int64_t now_seconds);
private:
    JournaledRuntime& runtime_;
    SimulatedBroker& broker_;
    std::int64_t quote_age_;
    std::size_t next_report_{0};
};
[[nodiscard]] std::string_view name(RouteStatus status);

} // namespace trading::execution
