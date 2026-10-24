#include "strategy_execution_report.hpp"

#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "runtime_json.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace trading;
using namespace trading::execution;

void require(bool value, std::string_view message) {
    if (!value) {
        throw std::runtime_error(std::string{message});
    }
}

template <class F> void rejects(F f) {
    bool bad = false;
    try {
        f();
    } catch (const std::invalid_argument&) {
        bad = true;
    }
    require(bad, "invalid strategy execution input must fail");
}

void adapter_contracts(const StrategyExecutionInputs& inputs) {
    IntradayContinuationHistory continuation{};
    ReversionHistory reversion{};
    for (const auto& s : inputs.history) {
        continuation.append(s.request, s.bars);
        static_cast<void>(reversion.complete(s.request, s.bars, s.request.end_exclusive()));
    }
    const auto noon = inputs.current.start() + std::chrono::minutes{150};
    const auto t = noon.time_since_epoch().count();
    const TopOfBook quote{1, t, 9999, 10000, 10, 10};
    const ReversionOrderAdapter adapter{inputs.current, 10, 1};
    const auto output = adapter.entry(reversion, std::span{inputs.reversion_long}.first(150), quote);
    require(output.target.target_shares == 10 && output.signal.actions[0].enter,
            "adapter uses the authoritative candidate decision");
    require(output.target ==
                adapter.entry(reversion, std::span{inputs.reversion_long}.first(150), quote).target,
            "stable target identity and payload on repeated decision");
    const MomentumOrderAdapter momentum{IntradayMomentumStrategy{inputs.current}, 7, 0};
    auto momentum_quote = quote;
    momentum_quote.at_seconds = momentum.strategy().entry_decision_time().time_since_epoch().count();
    require(momentum.entry(std::span{inputs.momentum_long}.first(360), momentum_quote).target_shares == 7,
            "shared policy preserves configured momentum share target");
    auto future = quote;
    future.at_seconds = t + 1;
    rejects([&] {
        static_cast<void>(adapter.entry(reversion, std::span{inputs.reversion_long}.first(150), future));
    });
    auto stale = quote;
    stale.at_seconds = t - 3;
    rejects([&] {
        static_cast<void>(adapter.entry(reversion, std::span{inputs.reversion_long}.first(150), stale));
    });
    rejects([&] { static_cast<void>(adapter.entry(reversion, inputs.reversion_long, quote)); });
    ReversionHistory empty{};
    rejects(
        [&] { static_cast<void>(adapter.entry(empty, std::span{inputs.reversion_long}.first(150), quote)); });
    rejects([&] { static_cast<void>(WholeShareTargetPolicy{0, 1}); });
    auto crossed = quote;
    crossed.ask_cents = 9998;
    rejects([&] {
        static_cast<void>(adapter.entry(reversion, std::span{inputs.reversion_long}.first(150), crossed));
    });
    const auto cash = adapter.entry(reversion, std::span{inputs.reversion_cash}.first(150), quote);
    require(cash.target.target_shares == 0 && !cash.signal.actions[0].enter,
            "cash decision is durable zero target");
    auto liquidation_quote = quote;
    liquidation_quote.at_seconds = t + 1800;
    require(adapter.liquidation(liquidation_quote).target_shares == 0,
            "registered reversion liquidation target");
    const ContinuationOrderAdapter price{
        IntradayContinuationStrategy{inputs.current, ContinuationVariant::price_only}, 10, 1};
    const ContinuationOrderAdapter volume{
        IntradayContinuationStrategy{inputs.current, ContinuationVariant::volume_conditioned}, 10, 1};
    for (const auto at : price.strategy().decision_times()) {
        const auto count = static_cast<std::size_t>(
            std::chrono::duration_cast<std::chrono::minutes>(at - inputs.current.start()).count());
        auto q = quote;
        q.at_seconds = at.time_since_epoch().count();
        const auto prefix = std::span{inputs.continuation_long}.first(count);
        const auto p = price.decide(continuation, prefix, at, 0, q);
        const auto v = volume.decide(continuation, prefix, at, 0, q);
        require(p.target.target_shares == 10 && v.target.target_shares == 10 && p.target.id != v.target.id,
                "both continuation variants use separate stable identities");
        require(price.decide(continuation, prefix, at, 4, q).target.target_shares == 10,
                "partial executed inventory retains the declared full target");
        const auto falling = std::span{inputs.continuation_cash}.first(count);
        require(price.decide(continuation, falling, at, 4, q).signal.action == ContinuationAction::exit_long,
                "signal reversal liquidates actual inventory");
        rejects([&] { static_cast<void>(price.decide(continuation, prefix, at, -1, q)); });
        rejects([&] {
            static_cast<void>(price.decide(continuation, prefix, at + std::chrono::seconds{1}, 0, q));
        });
    }
    auto low_volume = inputs.continuation_long;
    for (auto& b : low_volume) {
        b = MinuteBar{"SPY", b.start(), b.prices(), 50};
    }
    auto at = price.strategy().decision_times()[0];
    auto q = quote;
    q.at_seconds = at.time_since_epoch().count();
    require(
        volume.decide(continuation, std::span{low_volume}.first(30), at, 0, q).target.target_shares == 0 &&
            volume.decide(continuation, std::span{low_volume}.first(30), at, 4, q).target.target_shares == 10,
        "volume applies only to a new entry, not an existing long hold");
}

void case_contracts(const StrategyExecutionResult& result) {
    require(
        result.cases.size() == 18,
        "four variants have long, cash and paired partial/restart cases plus fee and rounding comparisons");
    for (const auto& c : result.cases) {
        require(!c.frames.empty(), "every case retains event-time state evidence");
        std::int64_t previous = 0;
        for (const auto& f : c.frames) {
            require(f.at_seconds >= previous, "frame chronology cannot regress");
            previous = f.at_seconds;
            require(f.account.reserved_cash_units <= f.account.cash_units &&
                        f.account.reserved_sell_shares <= f.account.position_shares,
                    "each frame conserves client reservations");
            require(f.broker_fill_count <= c.fills.size() && f.broker_report_count <= c.reports.size(),
                    "frame cannot expose unexecuted future fills");
            if (f.label.starts_with("Client restart")) {
                require(!f.account.connected && f.account.reconciliation_required,
                        "restart frame preserves closed routing gate");
            }
        }
        const auto& last = c.frames.back();
        require(last.account.position_shares == last.broker.position_shares &&
                    last.account.cash_units == last.broker.cash_units,
                "final independent account matches client");
        Cents cash = c.frames.front().broker.cash_units;
        Shares shares = 0;
        for (const auto& f : c.fills) {
            cash += (f.side == Side::buy ? -1 : 1) * f.shares * f.price_cents - f.fee_units;
            shares += (f.side == Side::buy ? 1 : -1) * f.shares;
        }
        require(cash == last.broker.cash_units && shares == last.broker.position_shares,
                "independent integer arithmetic reproduces final broker ledger");
        require(last.account.open_orders == 0 && last.account.reserved_cash_units == 0 &&
                    last.account.reserved_sell_shares == 0,
                "constructed paths settle all resources");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::invalid_argument("strategy execution contracts require output root");
        }
        const auto root = create_unique_directory(argv[1], "strategy-execution");
        const StrategyExecutionDiagnostic diagnostic{root};
        const auto result = diagnostic.run();
        adapter_contracts(result.inputs);
        case_contracts(result);
        StrategyExecutionReport{result}.write(root);
        const auto summary = nlohmann::json::parse(read_text_file(root / "summary.json"));
        require(summary.at("case_count").get<int>() == 18 &&
                    summary.at("historical_manifest_sha256").is_null(),
                "synthetic output cannot imply historical access");
        for (const auto& a : summary.at("artifacts")) {
            require(sha256_file(root / a.at("path").get<std::string>()) == a.at("sha256").get<std::string>(),
                    "artifact identities match saved bytes");
        }
        const auto html = read_text_file(root / "report.html");
        require(html.find("<script") == std::string::npos &&
                    html.find("type=\"radio\"") != std::string::npos &&
                    html.find(":checked") != std::string::npos,
                "offline viewer has native HTML event selection without authored scripting");
        require(html.find("Reserved cash cents") != std::string::npos &&
                    html.find("Client restart") != std::string::npos,
                "viewer exposes reservation and restart evidence");
        const auto evidence = nlohmann::json::parse(read_text_file(root / "execution_evidence.json"));
        require(evidence.at("cases").size() == 18, "all case evidence exported");
        auto scratch = result;
        const FrozenDevelopmentDataset invalid{};
        rejects([&] { diagnostic.historical(scratch, invalid); });
        std::cout << "Strategy adapters, event evidence, account parity and replay report contracts passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Strategy execution contracts failed: " << e.what() << '\n';
        return 1;
    }
}
