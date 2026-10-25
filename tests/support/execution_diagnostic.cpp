#include "execution_diagnostic.hpp"

#include <stdexcept>
#include <utility>

namespace trading::execution {
namespace {

OrderRequest buy(std::string id, Shares shares = 10) {
    return {std::move(id), "SPY", Side::buy, shares, 10000, 10};
}
OrderRequest sell(std::string id, Shares shares) {
    return {std::move(id), "SPY", Side::sell, shares, 10100, 10};
}
BrokerReport ack(std::string id, std::string order) {
    return {std::move(id), std::move(order), ReportKind::accepted, "", 0, 0, 0, 0, ""};
}
BrokerReport fill(std::string id, std::string order, std::string execution, Shares shares, Cents price,
                  Cents fee) {
    return {
        std::move(id), std::move(order), ReportKind::fill, std::move(execution), shares, price, fee, 0, ""};
}
BrokerReport cancel_ack(std::string id, std::string order, Shares cumulative, std::string cancel) {
    return {std::move(id), std::move(order), ReportKind::canceled, "", 0, 0, 0,
            cumulative,    std::move(cancel)};
}
BrokerReport cancel_reject(std::string id, std::string order, std::string cancel) {
    return {std::move(id), std::move(order), ReportKind::cancel_rejected, "", 0, 0, 0, 0, std::move(cancel)};
}
BrokerReport reject(std::string id, std::string order) {
    return {std::move(id), std::move(order), ReportKind::rejected, "", 0, 0, 0, 0, ""};
}
std::vector<ExecutionScenario> scenarios() {
    using enum Outcome;
    return {{"Partial fills, duplicate deliveries and delayed acknowledgement",
             {{SubmitInput{buy("B")}, applied},
              {fill("F1", "B", "X1", 4, 9900, 4), applied},
              {fill("F1", "B", "X1", 4, 9900, 4), duplicate},
              {fill("F1-copy", "B", "X1", 4, 9900, 4), duplicate},
              {ack("A", "B"), obsolete},
              {fill("F2", "B", "X2", 6, 10000, 6), applied},
              {ack("A-late", "B"), obsolete},
              {BrokerSnapshot{900390, 10, {}}, applied}},
             900390,
             10},
            {"Cancel and fill race with a stale cancel rejection",
             {{SubmitInput{buy("B")}, applied},
              {ack("A", "B"), applied},
              {CancelInput{"B", "C1"}, applied},
              {fill("F", "B", "X", 3, 10000, 3), applied},
              {cancel_reject("CR1", "B", "C1"), applied},
              {CancelInput{"B", "C2"}, applied},
              {cancel_reject("CR1-late", "B", "C1"), obsolete},
              {cancel_ack("CA", "B", 3, "C2"), applied},
              {cancel_ack("CA", "B", 3, "C2"), duplicate},
              {BrokerSnapshot{969997, 3, {}}, applied}},
             969997,
             3},
            {"Terminal message precedes a missing execution",
             {{SubmitInput{buy("B")}, applied},
              {ack("A", "B"), applied},
              {fill("F1", "B", "X1", 4, 10000, 4), applied},
              {CancelInput{"B", "C"}, applied},
              {cancel_ack("CA", "B", 10, "C"), quarantined},
              {SubmitInput{buy("blocked", 1)}, denied},
              {fill("F2", "B", "X2", 6, 10000, 6), applied},
              {cancel_ack("CA", "B", 10, "C"), obsolete},
              {BrokerSnapshot{899991, 10, {}}, quarantined},
              {BrokerSnapshot{899990, 10, {}}, applied},
              {SubmitInput{buy("recovered", 1)}, applied},
              {reject("R", "recovered"), applied}},
             899990,
             10},
            {"Disconnect, continued deliveries and exact reconciliation",
             {{SubmitInput{buy("B")}, applied},
              {ack("A", "B"), applied},
              {ConnectionInput{false}, applied},
              {SubmitInput{buy("blocked", 1)}, denied},
              {fill("F", "B", "X", 2, 10000, 2), applied},
              {ConnectionInput{true}, applied},
              {SubmitInput{buy("still-blocked", 1)}, denied},
              {BrokerSnapshot{979998, 2, {}}, quarantined},
              {BrokerSnapshot{979998, 2, {{buy("B"), 2}}}, applied},
              {CancelInput{"B", "C"}, applied},
              {cancel_ack("CA", "B", 2, "C"), applied}},
             979998,
             2},
            {"Resource reservations, order rejection and partial sales",
             {{SubmitInput{buy("large", 15)}, applied},
              {SubmitInput{buy("too-many", 6)}, denied},
              {SubmitInput{sell("short", 1)}, denied},
              {CancelInput{"large", "C0"}, applied},
              {SubmitInput{buy("still-too-many", 6)}, denied},
              {reject("R", "large"), applied},
              {SubmitInput{buy("B")}, applied},
              {fill("BF", "B", "BX", 10, 10000, 10), applied},
              {SubmitInput{sell("S", 8)}, applied},
              {SubmitInput{sell("oversell", 3)}, denied},
              {fill("SF1", "S", "SX1", 3, 10200, 3), applied},
              {CancelInput{"S", "C1"}, applied},
              {cancel_ack("CA", "S", 3, "C1"), applied},
              {SubmitInput{sell("S2", 7)}, applied},
              {fill("SF2", "S2", "SX2", 7, 10100, 7), applied}},
             1001280,
             0},
            {"Conflicting identities, unknown orders and invalid executions",
             {{SubmitInput{buy("B")}, applied},
              {fill("F", "B", "X", 4, 10000, 4), applied},
              {fill("F", "B", "X", 4, 10000, 5), quarantined},
              {fill("F-copy", "B", "X", 3, 10000, 3), quarantined},
              {ack("unknown", "missing"), quarantined},
              {fill("overfill", "B", "over", 7, 10000, 7), quarantined},
              {fill("bad-fee", "B", "fee", 1, 10000, 11), quarantined},
              {fill("bad-price", "B", "price", 1, 10001, 1), quarantined},
              {BrokerSnapshot{959996, 4, {{buy("B"), 4}}}, applied},
              {CancelInput{"B", "C"}, applied},
              {cancel_ack("CA", "B", 4, "C"), applied},
              {fill("after-cancel", "B", "late", 1, 10000, 1), quarantined},
              {BrokerSnapshot{959996, 4, {}}, applied}},
             959996,
             4}};
}
OrderRuntime replay(const ExecutionScenario &scenario) {
    OrderRuntime runtime{"SPY", 1000000, {20, 4}};
    for (const auto &step : scenario.steps) {
        const auto actual = apply_input(runtime, step.input);
        if (actual != step.expected_outcome) {
            throw std::logic_error("execution scenario outcome differs at step " +
                                   std::to_string(runtime.trace().size()) + ": " + scenario.name + "; " +
                                   runtime.trace().back().reason);
        }
    }
    const auto final = runtime.account();
    if (final.cash_units != scenario.expected_cash_cents ||
        final.position_shares != scenario.expected_position_shares || final.open_orders != 0 ||
        final.reserved_cash_units != 0 || final.reserved_sell_shares != 0 ||
        final.outstanding_buy_shares != 0 || !final.connected || final.reconciliation_required) {
        throw std::logic_error("execution scenario final balance or gate differs: " + scenario.name);
    }
    return runtime;
}

} // namespace

std::vector<ExecutionDiagnosticResult> ExecutionDiagnostic::run() const {
    std::vector<ExecutionDiagnosticResult> results;
    for (auto scenario : scenarios()) {
        const auto first = replay(scenario);
        const auto second = replay(scenario);
        if (first.trace() != second.trace() || first.account() != second.account()) {
            throw std::logic_error("execution replay is not deterministic: " + scenario.name);
        }
        results.push_back({std::move(scenario), first.trace(), first.account()});
    }
    return results;
}

} // namespace trading::execution
