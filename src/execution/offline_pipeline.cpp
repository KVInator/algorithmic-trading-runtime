#include "offline_pipeline.hpp"

#include <stdexcept>

namespace trading::execution {
namespace {
bool open(const OrderView& order) {
    return order.status != OrderStatus::filled && order.status != OrderStatus::canceled && order.status != OrderStatus::rejected;
}
} // namespace
std::string_view name(RouteStatus status) {
    switch (status) {
    case RouteStatus::submitted: return "submitted";
    case RouteStatus::waiting: return "waiting";
    case RouteStatus::settled: return "settled";
    case RouteStatus::blocked: return "blocked";
    case RouteStatus::expired: return "expired";
    case RouteStatus::rejected: return "rejected";
    case RouteStatus::superseded: return "superseded";
    }
    throw std::invalid_argument("unknown pipeline routing status");
}
OfflinePipeline::OfflinePipeline(JournaledRuntime& runtime, SimulatedBroker& broker, std::int64_t age)
    : runtime_{runtime}, broker_{broker}, quote_age_{age} {
    if (age < 0 || runtime_.inspection().schema_version != 2 || broker_.symbol() != runtime_.symbol()) {
        throw std::invalid_argument("pipeline requires matching symbols, schema 2 and nonnegative quote age");
    }
}
Outcome OfflinePipeline::deliver_report(std::size_t index) { return runtime_.execute(broker_.reports().at(index)); }
void OfflinePipeline::deliver_reports() {
    while (next_report_ < broker_.reports().size()) {
        static_cast<void>(deliver_report(next_report_));
        ++next_report_;
    }
}
bool OfflinePipeline::synchronize() {
    static_cast<void>(runtime_.execute(ConnectionInput{true}));
    next_report_ = 0;
    deliver_reports();
    return runtime_.execute(broker_.snapshot()) == Outcome::applied;
}
RouteResult OfflinePipeline::accept_target(const TargetIntent& target, std::int64_t now) {
    if (now < broker_.now_seconds() || now < target.decision_at_seconds) {
        throw std::invalid_argument("strategy target cannot precede the broker clock or its decision time");
    }
    const auto outcome = runtime_.execute(target);
    if (outcome != Outcome::applied && outcome != Outcome::duplicate) {
        return {RouteStatus::rejected, "", "target was not accepted by the runtime"};
    }
    if (runtime_.latest_target()->id != target.id) { return {RouteStatus::superseded, "", "a later durable target already exists"}; }
    return route_latest(now);
}
RouteResult OfflinePipeline::route_latest(std::int64_t now) {
    if (now < broker_.now_seconds()) { throw std::invalid_argument("pipeline clock cannot regress"); }
    const auto target = runtime_.latest_target();
    if (!target) { return {RouteStatus::settled, "", "no strategy target is registered"}; }
    const auto account = runtime_.account();
    if (!account.connected || account.reconciliation_required) { return {RouteStatus::blocked, "", "connection and exact reconciliation are required"}; }
    if (now < target->decision_at_seconds) { return {RouteStatus::expired, "", "strategy decision is not yet complete"}; }
    const auto orders = runtime_.orders();
    const auto id = target->id + "-order";
    bool waiting = false;
    for (const auto& order : orders) {
        if (!open(order)) { continue; }
        waiting = true;
        if (order.request.id != id) {
            const auto cancel_id = target->id + "-C-" + order.request.id;
            if (runtime_.execute(CancelInput{order.request.id, cancel_id}) == Outcome::applied) {
                try { static_cast<void>(broker_.cancel(order.request.id, cancel_id, now)); }
                catch (...) { static_cast<void>(runtime_.execute(ConnectionInput{false})); throw; }
            }
        }
    }
    if (waiting) { return {RouteStatus::waiting, id, "existing order or cancellation retains its reservation"}; }
    if (account.position_shares == target->target_shares) { return {RouteStatus::settled, id, "executed inventory equals the target"}; }
    for (const auto& order : orders) {
        if (order.request.id == id) { return {RouteStatus::rejected, id, "terminal target order cannot be automatically retried"}; }
    }
    if (now > target->expires_at_seconds) { return {RouteStatus::expired, id, "strategy target is outside its new-order dispatch window"}; }
    const auto quote = broker_.quote();
    if (!quote || now < quote->at_seconds || now - quote->at_seconds > quote_age_) {
        return {RouteStatus::blocked, id, "a current causal quote is required for dispatch"};
    }
    const bool buy = target->target_shares > account.position_shares;
    const auto quantity = buy ? target->target_shares - account.position_shares : account.position_shares - target->target_shares;
    const OrderRequest request{id, target->symbol, buy ? Side::buy : Side::sell, quantity,
        buy ? target->buy_limit_cents : target->sell_limit_cents, target->fee_per_share_ceiling_cents};
    if (runtime_.execute(SubmitInput{request}) != Outcome::applied) { return {RouteStatus::rejected, id, "pre-trade resource checks rejected the order"}; }
    try { static_cast<void>(broker_.submit(request, now)); }
    catch (...) { static_cast<void>(runtime_.execute(ConnectionInput{false})); throw; }
    return {RouteStatus::submitted, id, "durable order dispatched once to the simulated broker"};
}

} // namespace trading::execution
