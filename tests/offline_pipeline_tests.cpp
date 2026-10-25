#include "artifact_io.hpp"
#include "momentum_order_adapter.hpp"
#include "offline_pipeline.hpp"
#include "pipeline_diagnostic.hpp"
#include "runtime_json.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string_view>

namespace {
using namespace trading::execution;
const RuntimeConfiguration config{"SPY", 1000000, {20, 4}};
const TargetIntent target{"D1", "SPY", 1000, 1030, 10, 10000, 9999, 10};
void require(bool condition, std::string_view message) { if (!condition) { throw std::runtime_error(std::string{message}); } }
template<class F> void rejects(F operation, const std::source_location& location = std::source_location::current()) {
    bool failed = false;
    try { operation(); } catch (const std::exception&) { failed = true; }
    if (!failed) { throw std::runtime_error("invalid operation must fail at test line " + std::to_string(location.line())); }
}
void test_broker_liquidity_and_identity() {
    SimulatedBroker broker{"SPY", 1000000, 1};
    const TopOfBook first{1, 1000, 9999, 10000, 10, 3};
    require(broker.on_quote(first), "initial quote");
    const OrderRequest a{"A", "SPY", Side::buy, 2, 10000, 10};
    auto b = a; b.id = "B";
    require(broker.submit(a, 1000) && broker.submit(b, 1000), "two independent broker orders");
    require(broker.fills().size() == 2 && broker.fills()[0].shares == 2 && broker.fills()[1].shares == 1,
            "FIFO orders share one quote's three-share budget");
    require(broker.snapshot().cash_units == 969997 && broker.snapshot().position_shares == 3, "independent broker cash identity");
    require(!broker.on_quote(first) && !broker.submit(a, 1000) && broker.fills().size() == 2, "duplicate quote and command cannot manufacture fills");
    require(broker.on_quote({2, 1001, 9998, 9999, 10, 1}), "fresh quote replenishes one share");
    require(broker.fills().back().price_cents == 9999 && broker.snapshot().cash_units == 959997, "execution uses actual improved ask");
    auto conflict = first; conflict.ask_shares = 9;
    rejects([&] { static_cast<void>(broker.on_quote(conflict)); });
    auto bad_order = a; bad_order.shares = 3;
    rejects([&] { static_cast<void>(broker.submit(bad_order, 1001)); });
    rejects([&] { static_cast<void>(broker.on_quote({3, 1002, 10001, 10000, 1, 1})); });
    const OrderRequest sale{"S", "SPY", Side::sell, 4, 9999, 10};
    require(broker.submit(sale, 1001), "resting sale above current bid");
    require(broker.cancel("S", "C", 1001), "pending cancel");
    require(broker.on_quote({3, 1002, 10001, 10002, 2, 0}), "quote arrives before cancel completion");
    broker.complete_cancels(1002);
    require(broker.reports().back().kind == ReportKind::canceled && broker.reports().back().cumulative_filled_shares == 2,
            "cancel confirms raced execution quantity");
    require(broker.snapshot().cash_units == 979997 && broker.snapshot().position_shares == 2 && broker.snapshot().open_orders.empty(),
            "partial sale and cancellation conserve executed inventory");
    require(!broker.cancel("S", "C", 1002), "cancel command idempotence");
    rejects([&] { static_cast<void>(broker.cancel("missing", "C2", 1002)); });
    require(broker.on_quote({4, 1003, 10001, 10002, 3, 0}), "oversell has executable liquidity");
    const auto before = broker.snapshot();
    rejects([&] { static_cast<void>(broker.submit({"oversell", "SPY", Side::sell, 3, 10000, 10}, 1003)); });
    require(broker.snapshot().cash_units == before.cash_units && broker.order_count() == 3, "failed broker transaction leaves its ledger and order set unchanged");
}
void test_schema_and_target_recovery(const std::filesystem::path& root) {
    {
        auto runtime = JournaledRuntime::create(create_journal_file(root / "legacy.journal"), config);
        const auto records = runtime.inspection().input_records;
        rejects([&] { static_cast<void>(runtime.execute(target)); });
        require(runtime.healthy() && runtime.inspection().input_records == records, "schema 1 rejects targets before mutation or I/O");
    }
    {
        auto old = JournaledRuntime::recover(open_journal_file(root / "legacy.journal"), config);
        require(old.inspection().schema_version == 1 && !old.latest_target(), "legacy journal still recovers without migration");
    }
    SimulatedBroker broker{"SPY", 1000000, 1};
    require(broker.on_quote({1, 1000, 9999, 10000, 10, 4}), "recovery quote");
    const auto file = root / "target-before-order.journal";
    {
        auto runtime = JournaledRuntime::create(create_journal_file(file), config, 2);
        OfflinePipeline pipeline{runtime, broker};
        require(pipeline.synchronize(), "initial sync");
        require(runtime.execute(target) == Outcome::applied, "durable decision before its order");
        require(runtime.execute(target) == Outcome::duplicate, "identical decision deduplicated");
    }
    auto runtime = JournaledRuntime::recover(open_journal_file(file), config);
    SimulatedBroker wrong_symbol{"QQQ", 1000000, 1};
    rejects([&] { static_cast<void>(OfflinePipeline{runtime, wrong_symbol}); });
    OfflinePipeline pipeline{runtime, broker};
    require(runtime.latest_target() == target && pipeline.route_latest(1000).status == RouteStatus::blocked, "decision restored behind restart gate");
    require(pipeline.synchronize() && pipeline.route_latest(1000).status == RouteStatus::submitted, "unrouted decision resumes only after broker agreement");
    pipeline.deliver_reports();
    require(pipeline.accept_target(target, 1000).status == RouteStatus::waiting && broker.order_count() == 1, "decision replay cannot create a duplicate order");
    auto conflict = target; conflict.target_shares = 2;
    require(pipeline.accept_target(conflict, 1000).status == RouteStatus::rejected && runtime.account().reconciliation_required, "conflicting durable decision is quarantined");
    require(runtime.latest_target() == target && broker.order_count() == 1, "conflict cannot replace the original target");
    require(input_json(parse_runtime_input(input_json(target))) == input_json(target), "target JSON round trip");
}
void test_unsent_durable_order(const std::filesystem::path& root) {
    SimulatedBroker broker{"SPY", 1000000, 1};
    require(broker.on_quote({1, 1000, 9999, 10000, 10, 10}), "unsent quote");
    const auto file = root / "order-before-dispatch.journal";
    {
        auto runtime = JournaledRuntime::create(create_journal_file(file), config, 2);
        OfflinePipeline pipeline{runtime, broker};
        require(pipeline.synchronize() && runtime.execute(target) == Outcome::applied, "unsent decision");
        require(runtime.execute(SubmitInput{{target.id + "-order", "SPY", Side::buy, 10, 10000, 10}}) == Outcome::applied,
                "durable command exists before transport receives it");
    }
    auto runtime = JournaledRuntime::recover(open_journal_file(file), config);
    OfflinePipeline pipeline{runtime, broker};
    require(!pipeline.synchronize() && pipeline.route_latest(1000).status == RouteStatus::blocked,
            "missing broker order cannot be repaired by blind resend");
    require(runtime.account().reserved_cash_units == 100100 && broker.order_count() == 0, "ambiguous dispatch retains resources and sends no duplicate");
}
void test_freshness_limits_and_rejection(const std::filesystem::path& root) {
    {
        SimulatedBroker broker{"SPY", 1000000, 1};
        require(broker.on_quote({1, 1000, 9999, 10000, 200, 200}), "dispatch failure quote");
        auto runtime = JournaledRuntime::create(create_journal_file(root / "dispatch-error.journal"), config, 2);
        OfflinePipeline pipeline{runtime, broker}; require(pipeline.synchronize(), "dispatch failure sync");
        require(broker.submit({"external", "SPY", Side::buy, 99, 10000, 10}, 1000), "unreported external broker activity");
        rejects([&] { static_cast<void>(pipeline.accept_target(target, 1000)); });
        require(!runtime.account().connected && runtime.account().reconciliation_required && runtime.account().reserved_cash_units == 100100,
                "broker exception after durable command closes the client gate and retains reservation");
        require(!pipeline.synchronize() && broker.order_count() == 1, "external broker discrepancy cannot trigger automatic resend");
    }
    {
        SimulatedBroker broker{"SPY", 1000000, 1};
        require(broker.on_quote({1, 1000, 9999, 10000, 10, 10}), "stale quote fixture");
        auto runtime = JournaledRuntime::create(create_journal_file(root / "freshness.journal"), config, 2);
        OfflinePipeline pipeline{runtime, broker}; require(pipeline.synchronize(), "freshness sync");
        require(pipeline.accept_target(target, 1003).status == RouteStatus::blocked && broker.order_count() == 0, "stale quote blocks dispatch");
        require(broker.on_quote({2, 1003, 9999, 10000, 10, 10}), "new causal quote");
        require(pipeline.route_latest(1003).status == RouteStatus::submitted, "same durable target can await a fresh quote");
        pipeline.deliver_reports();
        rejects([&] { static_cast<void>(pipeline.route_latest(1002)); });
        require(pipeline.route_latest(1003).status == RouteStatus::settled, "full fill settles without extra commands");
    }
    {
        SimulatedBroker broker{"SPY", 1000000, 1};
        require(broker.on_quote({1, 1040, 9999, 10000, 10, 10}), "expired quote");
        auto runtime = JournaledRuntime::create(create_journal_file(root / "expiry.journal"), config, 2);
        OfflinePipeline pipeline{runtime, broker}; require(pipeline.synchronize(), "expiry sync");
        require(pipeline.accept_target(target, 1040).status == RouteStatus::expired && broker.order_count() == 0, "expired target cannot dispatch a new order");
        auto future = target; future.id = "future"; future.decision_at_seconds = 1041; future.expires_at_seconds = 1050;
        rejects([&] { static_cast<void>(pipeline.accept_target(future, 1040)); });
    }
    for (const bool position_limit : {true, false}) {
        auto limited = config;
        if (position_limit) { limited.limits.maximum_position_shares = 5; } else { limited.initial_cash_units = 50000; }
        SimulatedBroker broker{"SPY", limited.initial_cash_units, 1};
        require(broker.on_quote({1, 1000, 9999, 10000, 10, 10}), "risk quote");
        auto runtime = JournaledRuntime::create(create_journal_file(root / (position_limit ? "position.journal" : "cash.journal")), limited, 2);
        OfflinePipeline pipeline{runtime, broker}; require(pipeline.synchronize(), "limited sync");
        require(pipeline.accept_target(target, 1000).status == RouteStatus::rejected && broker.order_count() == 0,
                "position and cash limits prevent broker dispatch");
    }
    {
        SimulatedBroker broker{"SPY", 1000000, 1}; require(broker.on_quote({1, 1000, 9999, 10000, 10, 10}), "fee quote");
        auto runtime = JournaledRuntime::create(create_journal_file(root / "broker-reject.journal"), config, 2);
        OfflinePipeline pipeline{runtime, broker}; require(pipeline.synchronize(), "fee sync");
        auto no_fee = target; no_fee.fee_per_share_ceiling_units = 0;
        require(pipeline.accept_target(no_fee, 1000).status == RouteStatus::submitted, "valid local order reaches broker");
        pipeline.deliver_reports();
        require(pipeline.route_latest(1000).status == RouteStatus::rejected && broker.order_count() == 1 && runtime.account().reserved_cash_units == 0,
                "broker rejection releases resources and is not automatically retried");
    }
}
void test_diagnostic_and_causality(const std::filesystem::path& root) {
    const auto result = PipelineDiagnostic{trading::create_unique_directory(root, "pipeline")}.run();
    require(result.cases.size() == 3 && result.cases[1].client_restarts == 2 && result.cases[2].client_restarts == 1,
            "paired execution and cash cases retain fresh-client restarts");
    const auto start = result.rising_prefix.front().start();
    MomentumOrderAdapter adapter{trading::IntradayMomentumStrategy{trading::HistoricalBarRequest{"SPY", start, start + std::chrono::minutes{390}}}, 10, 10};
    const auto quote = result.cases[0].quotes.front();
    require(adapter.entry(result.rising_prefix, quote).target_shares == 10 && adapter.entry(result.falling_prefix, quote).target_shares == 0,
            "existing strategy computes both signal branches");
    auto incomplete = result.rising_prefix; incomplete.pop_back();
    rejects([&] { static_cast<void>(adapter.entry(incomplete, quote)); });
    auto future = quote; ++future.at_seconds;
    rejects([&] { static_cast<void>(adapter.entry(result.rising_prefix, future)); });
    auto stale = quote; stale.at_seconds -= 3;
    rejects([&] { static_cast<void>(adapter.entry(result.rising_prefix, stale)); });
    auto ahead = result.rising_prefix;
    ahead.emplace_back("SPY", adapter.strategy().entry_decision_time(), trading::Ohlc{1, 100000, 1, 100000}, 1);
    rejects([&] { static_cast<void>(adapter.entry(ahead, quote)); });
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) { throw std::invalid_argument("offline pipeline contracts require an output directory"); }
        const auto root = trading::create_unique_directory(argv[1], "offline-pipeline-contracts");
        test_broker_liquidity_and_identity();
        test_schema_and_target_recovery(root);
        test_unsent_durable_order(root);
        test_freshness_limits_and_rejection(root);
        test_diagnostic_and_causality(root);
        std::cout << "Offline pipeline contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Offline pipeline contract failure: " << error.what() << '\n';
        return 1;
    }
}
