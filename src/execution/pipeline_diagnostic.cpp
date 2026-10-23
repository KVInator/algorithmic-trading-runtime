#include "pipeline_diagnostic.hpp"

#include <algorithm>
#include <stdexcept>

namespace trading::execution {
namespace {
using Clock = std::chrono::sys_seconds;
IntradayMomentumStrategy synthetic_strategy() {
    const Clock start = std::chrono::sys_days{std::chrono::year{2022}/2/1} + std::chrono::hours{14} + std::chrono::minutes{30};
    return IntradayMomentumStrategy{HistoricalBarRequest{"SPY", start, start + std::chrono::minutes{390}}};
}
std::vector<MinuteBar> prefix(const IntradayMomentumStrategy& strategy, bool rising) {
    std::vector<MinuteBar> bars;
    double previous = rising ? 99 : 101;
    for (int minute = 0; minute < 360; ++minute) {
        const double close = rising ? 99.0 + (minute + 1) / 360.0 : 101.0 - (minute + 1) / 360.0;
        bars.emplace_back("SPY", strategy.session().start() + std::chrono::minutes{minute},
            Ohlc{previous, std::max(previous, close), std::min(previous, close), close}, 100);
        previous = close;
    }
    return bars;
}
} // namespace

PipelineDiagnosticResult run_pipeline_diagnostic(const std::filesystem::path& directory) {
    const RuntimeConfiguration config{"SPY", 1000000, {20, 4}};
    const auto strategy = synthetic_strategy();
    const MomentumOrderAdapter adapter{strategy, 10, 10};
    const auto entry = strategy.entry_decision_time().time_since_epoch().count();
    const auto exit = strategy.exit_decision_time().time_since_epoch().count();
    PipelineDiagnosticResult result{{}, {}, prefix(strategy, true), prefix(strategy, false)};
    auto check = [&](std::string label, bool passed) {
        if (!passed) { throw std::logic_error("pipeline diagnostic failed: " + label); }
        result.checks.push_back({std::move(label), passed});
    };
    for (const bool interrupted : {false, true}) {
        SimulatedBroker broker{"SPY", 1000000, 1};
        const std::string filename = interrupted ? "interrupted.journal" : "uninterrupted.journal";
        auto runtime = std::make_unique<JournaledRuntime>(JournaledRuntime::create(create_journal_file(directory / filename), config, 2));
        auto pipeline = std::make_unique<OfflinePipeline>(*runtime, broker);
        std::vector<TopOfBook> quotes;
        auto quote = [&](TopOfBook value) { quotes.push_back(value); check("Fresh quote advances the broker", broker.on_quote(value)); };
        quote({1, entry, 9999, 10000, 20, 4});
        check("Initial broker snapshot reconciles", pipeline->synchronize());
        const auto target = adapter.entry(result.rising_prefix, *broker.quote());
        check("Existing causal strategy requests ten shares", target.target_shares == 10);
        check("Strategy target produces a durable broker order", pipeline->accept_target(target, entry).status == RouteStatus::submitted);
        check("Fill delivered before acknowledgement is accepted", pipeline->deliver_report(1) == Outcome::applied);
        pipeline->deliver_reports();
        check("Initial four-share fill is booked once", runtime->account().position_shares == 4 && runtime->account().cash_cents == 959996);
        check("Duplicate quote cannot refill its liquidity", !broker.on_quote(quotes.back()) && broker.fills().size() == 1);
        check("Duplicate target cannot create a second order", pipeline->accept_target(target, entry).status == RouteStatus::waiting && broker.order_count() == 1);
        quote({2, entry + 1, 9999, 10000, 20, 2});
        std::size_t restarts = 0;
        if (interrupted) {
            pipeline.reset(); runtime.reset();
            runtime = std::make_unique<JournaledRuntime>(JournaledRuntime::recover(open_journal_file(directory / filename), config));
            pipeline = std::make_unique<OfflinePipeline>(*runtime, broker);
            ++restarts;
            check("Recovered pipeline is gated before missing reports", pipeline->route_latest(entry + 1).status == RouteStatus::blocked);
            check("Durable strategy target survives restart", runtime->latest_target() == target);
            check("Broker replay repairs the missing execution", pipeline->synchronize());
        } else { pipeline->deliver_reports(); }
        check("Two quotes supply exactly six shares", runtime->account().position_shares == 6 && runtime->account().reserved_cash_cents == 40040);
        quote({3, exit, 9999, 10000, 3, 0});
        const auto flat = adapter.liquidation(*broker.quote());
        check("Liquidation waits for cancellation", pipeline->accept_target(flat, exit).status == RouteStatus::waiting);
        quote({4, exit + 1, 9999, 10000, 3, 1});
        pipeline->deliver_reports();
        check("Cancel race retains the seventh executed share", runtime->account().position_shares == 7 && runtime->order(target.id + "-order").status == OrderStatus::pending_cancel);
        broker.complete_cancels(exit + 1);
        pipeline->deliver_reports();
        check("Confirmed cancellation releases the unfilled buy remainder", runtime->account().reserved_cash_cents == 0);
        check("Liquidation sells actual inventory", pipeline->route_latest(exit + 1).status == RouteStatus::submitted);
        if (interrupted) {
            pipeline.reset(); runtime.reset();
            runtime = std::make_unique<JournaledRuntime>(JournaledRuntime::recover(open_journal_file(directory / filename), config));
            pipeline = std::make_unique<OfflinePipeline>(*runtime, broker);
            ++restarts;
            check("Second restart retains liquidation target", runtime->latest_target() == flat);
            check("Second broker replay recovers partial sale", pipeline->synchronize());
        } else { pipeline->deliver_reports(); }
        check("Partial sale reserves the four remaining shares", runtime->account().position_shares == 4 && runtime->account().reserved_sell_shares == 4);
        quote({5, exit + 2, 10010, 10011, 4, 0});
        pipeline->deliver_reports();
        check("Completed liquidation settles the strategy target", pipeline->route_latest(exit + 2).status == RouteStatus::settled);
        check("Old target replay cannot supersede liquidation", pipeline->accept_target(target, exit + 2).status == RouteStatus::superseded);
        check("Final broker snapshot matches the client", pipeline->synchronize());
        const auto account = runtime->account();
        check("Integer ledger matches the constructed execution identity", account.cash_cents == 1000023 && account.position_shares == 0 && account.open_orders == 0 &&
              account.reserved_cash_cents == 0 && account.reserved_sell_shares == 0 && broker.order_count() == 2 && broker.fills().size() == 5);
        result.cases.push_back({interrupted ? "Interrupted client" : "Uninterrupted client", filename, std::move(quotes),
            {target, flat}, broker.fills(), broker.reports(), runtime->trace(), account, restarts});
    }
    check("Restart leaves broker orders and fills unchanged", result.cases[0].fills == result.cases[1].fills &&
          result.cases[0].broker_reports == result.cases[1].broker_reports && result.cases[0].final_account == result.cases[1].final_account);
    {
        SimulatedBroker broker{"SPY", 1000000, 1};
        const std::string filename{"cash-signal.journal"};
        std::vector<TopOfBook> quotes{{1, entry, 9999, 10000, 20, 20}, {2, exit, 9999, 10000, 20, 20}};
        std::vector<TargetIntent> targets;
        {
            auto runtime = JournaledRuntime::create(create_journal_file(directory / filename), config, 2);
            OfflinePipeline pipeline{runtime, broker};
            check("Cash case quote accepted", broker.on_quote(quotes[0]));
            check("Cash case reconciles", pipeline.synchronize());
            targets.push_back(adapter.entry(result.falling_prefix, *broker.quote()));
            check("Falling prefix stays in cash without an order", targets.back().target_shares == 0 && pipeline.accept_target(targets.back(), entry).status == RouteStatus::settled);
        }
        auto runtime = JournaledRuntime::recover(open_journal_file(directory / filename), config);
        OfflinePipeline pipeline{runtime, broker};
        check("Cash target survives a new coordinator", runtime.latest_target() == targets[0] && pipeline.synchronize());
        check("Cash target redelivery remains settled", pipeline.accept_target(targets[0], entry).status == RouteStatus::settled);
        check("Cash liquidation quote accepted", broker.on_quote(quotes[1]));
        targets.push_back(adapter.liquidation(*broker.quote()));
        check("Cash liquidation produces no spurious sale", pipeline.accept_target(targets[1], exit).status == RouteStatus::settled);
        check("Cash path has no broker executions", broker.order_count() == 0 && broker.fills().empty() && runtime.account().cash_cents == 1000000);
        result.cases.push_back({"Cash signal", filename, std::move(quotes), std::move(targets), broker.fills(), broker.reports(), runtime.trace(), runtime.account(), 1});
    }
    return result;
}

} // namespace trading::execution
