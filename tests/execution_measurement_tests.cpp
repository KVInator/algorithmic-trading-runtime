#include "execution_measurement_report.hpp"
#include "artifact_io.hpp"
#include <iostream>

namespace {
using namespace trading::execution;
void require(bool value, std::string_view message) {
    if (!value)
        throw std::logic_error(std::string{message});
}
template <class F> void rejects(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    require(rejected, "invalid execution observation must be rejected");
}
void metrics() {
    SimulatedBroker broker{"SPY", 100000, 0};
    static_cast<void>(broker.on_quote({1, 1, 99, 100, 2, 2}));
    static_cast<void>(broker.submit({"first", "SPY", Side::buy, 4, 100, 0}, 1));
    broker.advance_to(2);
    static_cast<void>(broker.submit({"second", "SPY", Side::buy, 1, 100, 0}, 2));
    require(broker.fills().size() == 1 && broker.snapshot().position_shares == 2,
            "clock advancement never replenishes consumed quote size");
    rejects([&] { broker.advance_to(1); });

    ExecutionObservation buy{Side::buy,
                             10,
                             101,
                             99,
                             101,
                             101,
                             103,
                             0,
                             1,
                             2,
                             3,
                             20,
                             {{4, 101, 99, 101, 1, 2, 3}, {2, 100, 98, 100, 2, 4, 5}}};
    const auto a = ExecutionQuality{}.measure(buy);
    require(a.at("filled_quantity").get<int>() == 6 && a.at("unfilled_quantity").get<int>() == 4,
            "partial quantity oracle");
    require(a.at("executed_shortfall_twice_cents").get<int>() == 8 &&
                a.at("spread_component_twice_cents").get<int>() == 12 &&
                a.at("timing_component_twice_cents").get<int>() == -4 &&
                a.at("unfilled_opportunity_twice_cents").get<int>() == 16 &&
                a.at("total_shortfall_twice_cents").get<int>() == 24,
            "analytical signed cost decomposition");
    require(a.at("completion_censored").get<bool>() && a.at("completion_receipt_delay_seconds").is_null() &&
                a.at("first_fill_receipt_delay_seconds").get<int>() == 2,
            "partial completion stays censored");
    auto sell = buy;
    sell.side = Side::sell;
    sell.limit_cents = 99;
    sell.terminal_bid_cents = 97;
    sell.terminal_ask_cents = 99;
    sell.fills = {{4, 99, 99, 101, 1, 2, 3}, {2, 100, 100, 102, 2, 4, 5}};
    const auto b = ExecutionQuality{}.measure(sell);
    for (const auto key :
         {"executed_shortfall_twice_cents", "spread_component_twice_cents", "timing_component_twice_cents",
          "unfilled_opportunity_twice_cents", "total_shortfall_bps"})
        require(a.at(key) == b.at(key), "buy/sell sign symmetry");
    auto empty = buy;
    empty.fills.clear();
    const auto e = ExecutionQuality{}.measure(empty);
    require(e.at("executed_shortfall_bps").is_null() && e.at("first_fill_receipt_delay_seconds").is_null() &&
                e.at("completion_censored").get<bool>(),
            "unfilled orders do not manufacture prices or times");
    auto canceled = buy;
    canceled.cancellation_source_seconds = 5;
    canceled.cancellation_receipt_seconds = 6;
    require(ExecutionQuality{}.measure(canceled).at("completion_observation_seconds").get<int>() == 5 &&
                ExecutionQuality{}.measure(canceled).at("completion_censor_reason").get<std::string>() ==
                    "terminal_cancellation",
            "canceled observation stops at receipt rather than the common horizon");
    auto completed = buy;
    completed.fills = {{10, 101, 99, 101, 1, 2, 3}};
    require(ExecutionQuality{}.measure(completed).at("completion_receipt_delay_seconds").get<int>() == 2,
            "completed delay oracle");
    auto odd = completed;
    odd.arrival_ask_cents = 100;
    odd.limit_cents = 100;
    odd.fills = {{10, 100, 99, 100, 1, 2, 3}};
    require(ExecutionQuality{}.measure(odd).at("spread_component_twice_cents").get<int>() == 10,
            "half-cent midpoint is not rounded");
    auto bad = buy;
    bad.fills.at(0).quantity = 11;
    rejects([&] { static_cast<void>(ExecutionQuality{}.measure(bad)); });
    bad = buy;
    bad.fills.at(0).receipt_seconds = 1;
    rejects([&] { static_cast<void>(ExecutionQuality{}.measure(bad)); });
    bad = buy;
    bad.fills.at(0).price_cents = 102;
    rejects([&] { static_cast<void>(ExecutionQuality{}.measure(bad)); });
    bad = buy;
    bad.acknowledgement_receipt_seconds = 21;
    rejects([&] { static_cast<void>(ExecutionQuality{}.measure(bad)); });
}
} // namespace
int main(int argc, char **argv) {
    try {
        require(argc == 2, "measurement contracts require output directory");
        metrics();
        const auto root = trading::create_unique_directory(argv[1], "execution-measurement");
        const auto all = ExecutionMeasurementStudy::workloads();
        const std::vector<ExecutionWorkload> selected{all.at(0), all.at(3), all.at(12), all.at(18)};
        const auto a =
            ExecutionMeasurementStudy{}.run(root / "first", selected, ExecutionMeasurementStudy::policies());
        const auto b =
            ExecutionMeasurementStudy{}.run(root / "second", selected, ExecutionMeasurementStudy::policies());
        require(a.cases.size() == 16 && a.comparisons.size() == 12 && a.cases == b.cases &&
                    a.policy_summaries == b.policy_summaries && a.comparisons == b.comparisons &&
                    a.sensitivities == b.sensitivities && a.sensitivities.size() == 4,
                "repeated matched schedules reproduce all timings, metrics and ledgers");
        trading::ExecutionMeasurementReport{a}.write(root / "first");
        std::cout
            << "Execution cost, censoring, clock, buy/sell symmetry and matched replay contracts passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Execution measurement contracts failed: " << e.what() << '\n';
        return 1;
    }
}
