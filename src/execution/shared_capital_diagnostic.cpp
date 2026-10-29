#include "shared_capital_diagnostic.hpp"
#include "shared_capital_json.hpp"
#include "shared_strategy_adapter.hpp"
#include "journal_frame.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace trading::execution {
namespace {
using Json = nlohmann::json;
constexpr auto unit = shared_usd_scale;
PortfolioBrokerSnapshot broker(const SharedCapitalSnapshot& s) {
    PortfolioBrokerSnapshot result{s.account.cash_units, {}, {}};
    for (const auto& [symbol, p] : s.account.positions) { result.positions.emplace(symbol, p.shares); }
    for (const auto& order : s.open_orders) { result.open_orders.push_back({order.request, order.filled_shares}); }
    return result;
}
std::vector<SharedQuote> quotes(std::int64_t at, MoneyUnits price = 100 * unit) {
    std::vector<SharedQuote> result;
    for (const auto* symbol : {"GLD", "IWM", "QQQ", "SPY", "TLT"}) { result.push_back({symbol, at, price, price}); }
    return result;
}
Json financial(const SharedCapitalSnapshot& state) {
    auto value = shared_snapshot_json(state);
    for (const auto* key : {"connected", "reconciliation_required", "at_seconds", "generation", "sealed"}) { value.erase(key); }
    return value;
}
enum class WriteFault { none, before, partial, after };
struct WriteControl { WriteFault fault{WriteFault::none}; };
class FaultStorage final : public JournalStorage {
public:
    // The diagnostic declares control before the owning runtime, so this
    // non-owning reference outlives every use of the storage adapter.
    FaultStorage(std::unique_ptr<JournalStorage> storage, WriteControl& control) : storage_(std::move(storage)), control_(control) {}
    std::string read_all() override { return storage_->read_all(); }
    void append_and_sync(std::string_view bytes) override {
        const auto fault = control_.fault; control_.fault = WriteFault::none;
        if (fault == WriteFault::before) { throw JournalError("constructed failure before append"); }
        if (fault == WriteFault::partial) { storage_->append_and_sync(bytes.substr(0, bytes.size() / 2)); throw JournalError("constructed partial append"); }
        storage_->append_and_sync(bytes);
        if (fault == WriteFault::after) { throw JournalError("constructed failure after synchronization"); }
    }
private:
    std::unique_ptr<JournalStorage> storage_;
    WriteControl& control_;
};
class Cases {
public:
    Cases(std::filesystem::path directory, SharedCapitalConfiguration config) : directory_(std::move(directory)), config_(std::move(config)) {}
    SharedDiagnosticResult run() {
        result_.evidence = {{"rejected_files", Json::array()}, {"financial_parity", Json::array()}, {"strategy_records", Json::array()}};
        global_resources(); internal_netting(); cancel_race(); fault_gates(); expiry_and_permutation(); mark_limits(); recovery(); handover(); precision(); storage_faults(); strategy_integration(); budget_limited_order();
        return std::move(result_);
    }
private:
    void check(std::string label, bool passed) {
        if (!passed) { throw std::runtime_error("shared-capital check failed: " + label); }
        result_.checks.push_back(std::move(label));
    }
    template<class F> void reject(std::string label, F&& operation) {
        bool stopped = false;
        try { operation(); } catch (const std::exception&) { stopped = true; }
        check(std::move(label), stopped);
    }
    JournaledSharedCapital start(const std::string& name, const SharedCapitalConfiguration& config) {
        std::filesystem::create_directories(directory_ / name);
        auto runtime = JournaledSharedCapital::create(create_journal_file(directory_ / name / "0.journal"), config);
        check(name + ": cold account starts closed", !runtime.snapshot().account.connected && runtime.snapshot().account.reconciliation_required);
        check(name + ": reconnect applied", runtime.execute(SharedConnection{10, true}).outcome == Outcome::applied);
        check(name + ": complete initial broker snapshot reconciles", runtime.execute(SharedReconciliation{10, broker(runtime.snapshot())}).outcome == Outcome::applied);
        return runtime;
    }
    void save(const std::string& name, const JournaledSharedCapital& runtime,
        std::vector<SharedSegmentEvidence> segments = {}) {
        if (segments.empty()) { segments.push_back({name + "/0.journal", runtime.identity()}); }
        result_.cases.push_back({name, std::move(segments), runtime.configuration(), runtime.snapshot()});
    }
    SharedResult batch(JournaledSharedCapital& runtime, std::string id, std::int64_t at, std::vector<FamilySignal> signals,
        std::vector<SharedQuote> market = {}) {
        if (market.empty()) { market = quotes(at); }
        return runtime.execute(SharedTargetBatch{std::move(id), at, std::move(market), std::move(signals)});
    }
    SharedResult fill(JournaledSharedCapital& runtime, const OrderRequest& order, Shares q, std::int64_t at,
        const std::string& execution, MoneyUnits fee = 0) {
        return runtime.execute(SharedDelivery{at, {execution + "-event", order.id, ReportKind::fill, execution, q, order.limit_units, fee, 0, ""}});
    }
    void global_resources() {
        auto config = config_; config.fee_per_share_ceiling_units = 3;
        auto runtime = start("global_resources", config);
        std::vector<FamilySignal> signals;
        for (const auto& family : config.families) { for (const auto& symbol : config.symbols) { signals.push_back({{family, symbol}, true}); } }
        const auto routed = batch(runtime, "all-long", 1000, signals);
        check("fifteen owners net to five broker orders", routed.dispatch_orders.size() == 5);
        check("global buys reserve one shared cash pool", runtime.snapshot().account.reserved_cash_units == 990 * (100 * unit + 3));
        check("pending-buy exposure respects every registered admission budget", runtime.snapshot().budget_breaches.empty());
        for (const auto& order : routed.dispatch_orders) { check("equal owner budgets produce 198 shares per instrument", order.shares == 198); }
        const auto order = routed.dispatch_orders.front();
        for (const auto& o : routed.dispatch_orders) {
            check("broker acknowledgement advances the net order to working", runtime.execute(SharedDelivery{1000,
                {"ack-" + o.symbol, o.id, ReportKind::accepted, "", 0, 0, 0, 0, ""}}).outcome == Outcome::applied);
        }
        const auto partial = fill(runtime, order, 5, 1001, "global-partial", 7);
        check("partial fill is booked in global and owner accounts", partial.outcome == Outcome::applied && runtime.snapshot().account.positions.at(order.symbol).shares == 5);
        check("incremental quantities use exact largest remainders", partial.fills.size() == 3 && partial.fills[0].shares == 2 && partial.fills[1].shares == 2 && partial.fills[2].shares == 1);
        check("nanodollar fees are conserved under allocation", partial.fills[0].fee_units == 3 && partial.fills[1].fee_units == 3 && partial.fills[2].fee_units == 1);
        const auto before = financial(runtime.snapshot());
        const auto duplicate = runtime.execute(SharedDelivery{1001, {"global-redelivery", order.id, ReportKind::fill, "global-partial", 5, order.limit_units, 7, 0, ""}});
        check("stable execution identity prevents duplicate owner booking", duplicate.outcome == Outcome::duplicate && financial(runtime.snapshot()) == before);
        for (const auto& o : routed.dispatch_orders) { static_cast<void>(fill(runtime, o, o.shares - (o.id == order.id ? 5 : 0), 1002, "global-rest-" + o.symbol)); }
        check("late acknowledgement cannot reopen a filled order", runtime.execute(SharedDelivery{1002,
            {"late-ack", order.id, ReportKind::accepted, "", 0, 0, 0, 0, ""}}).outcome == Outcome::obsolete && runtime.snapshot().account.open_orders == 0);
        check("all five symbols share the resulting global cash", runtime.snapshot().account.cash_units == 1000 * unit - 7 && runtime.snapshot().account.reserved_cash_units == 0);
        save("global_resources", runtime);
    }
    void budget_limited_order() {
        auto runtime = start("budget_limited_order", config_);
        const auto initial =
            batch(runtime, "two-owners", 1000, {{{"momentum", "SPY"}, true}, {{"reversion", "SPY"}, true}});
        check("initial two owners share a 132-share order",
              initial.dispatch_orders.size() == 1 && initial.dispatch_orders.front().shares == 132);
        static_cast<void>(fill(runtime, initial.dispatch_orders.front(), 132, 1000, "budget-initial"));
        const auto next =
            batch(runtime, "third-owner", 1001, {{{"volume_conditioned_continuation", "SPY"}, true}},
                  quotes(1001, 110 * unit));
        check("instrument cap admits 52 shares of a 61-share target",
              next.dispatch_orders.size() == 1 && next.dispatch_orders.front().shares == 52 &&
                  runtime.snapshot().owners.at({"volume_conditioned_continuation", "SPY"}).desired_shares ==
                      61);
        const auto order = next.dispatch_orders.front();
        const auto acknowledged = runtime.execute(
            SharedDelivery{1001, {"budget-ack", order.id, ReportKind::accepted, "", 0, 0, 0, 0, ""}});
        check("budget-limited acknowledgement does not cancel unchanged intent",
              acknowledged.dispatch_cancels.empty() &&
                  runtime.snapshot().open_plans.at(order.id).remaining.at(
                      {"volume_conditioned_continuation", "SPY"}) == 52);
        const auto completed = fill(runtime, order, 52, 1001, "budget-complete");
        check("budget-limited fill terminates without a replacement loop",
              completed.dispatch_orders.empty() && completed.dispatch_cancels.empty() &&
                  runtime.snapshot().account.open_orders == 0);
        check("budget-limited cash and ownership remain exact",
              runtime.snapshot().account.cash_units == 81080 * unit &&
                  runtime.snapshot().owners.at({"volume_conditioned_continuation", "SPY"}).shares == 52);
        save("budget_limited_order", runtime);
    }
    void internal_netting() {
        auto runtime = start("internal_netting", config_);
        const auto entry = batch(runtime, "momentum-long", 1000, {{{"momentum", "SPY"}, true}});
        check("single owner entry creates one order", entry.dispatch_orders.size() == 1);
        static_cast<void>(fill(runtime, entry.dispatch_orders[0], entry.dispatch_orders[0].shares, 1001, "net-entry"));
        const auto initial = runtime.snapshot();
        const auto transfer = batch(runtime, "switch-owner", 1002, {{{"momentum", "SPY"}, false}, {{"reversion", "SPY"}, true}});
        check("opposing owner changes net without broker turnover", transfer.dispatch_orders.empty() && transfer.transfers.size() == 1);
        check("internal crossing preserves global cash and inventory", runtime.snapshot().account.cash_units == initial.account.cash_units && runtime.snapshot().account.positions == initial.account.positions);
        check("internal crossing assigns shares and marked cash flow", runtime.snapshot().owners.at({"momentum", "SPY"}).shares == 0 && runtime.snapshot().owners.at({"reversion", "SPY"}).shares == 66 &&
            runtime.snapshot().owners.at({"momentum", "SPY"}).attribution_cash_flow_units == 0 && runtime.snapshot().owners.at({"reversion", "SPY"}).attribution_cash_flow_units == -6600 * unit);
        const auto exit = batch(runtime, "reversion-cash", 1003, {{{"reversion", "SPY"}, false}});
        static_cast<void>(fill(runtime, exit.dispatch_orders.at(0), 66, 1004, "net-exit"));
        const auto closed = runtime.snapshot();
        check("shared round trip returns every owner and global account to flat", closed.account.cash_units == config_.initial_cash_units && closed.account.open_orders == 0 &&
            std::all_of(closed.account.positions.begin(), closed.account.positions.end(), [](const auto& item) { return item.second.shares == 0; }));
        save("internal_netting", runtime);
    }
    void cancel_race() {
        auto runtime = start("cancel_race", config_);
        const auto entry = batch(runtime, "old-owner", 1000, {{{"momentum", "SPY"}, true}});
        const auto order = entry.dispatch_orders.at(0);
        const auto change = batch(runtime, "new-owner", 1001, {{{"momentum", "SPY"}, false}, {{"reversion", "SPY"}, true}});
        check("pending owner change requests cancel without replacement", change.dispatch_cancels.size() == 1 && change.dispatch_orders.empty());
        check("cancel retains original cash and ownership reservations", runtime.snapshot().account.reserved_cash_units == 6600 * unit &&
            runtime.snapshot().open_plans.at(order.id).remaining.at({"momentum", "SPY"}) == 66);
        const auto late = fill(runtime, order, 40, 1002, "cancel-race-fill");
        check("race fill remains assigned to the dispatch-time owner", late.fills.size() == 1 && late.fills[0].owner == OwnerKey{"momentum", "SPY"} &&
            runtime.snapshot().owners.at({"momentum", "SPY"}).shares == 40 && runtime.snapshot().owners.at({"reversion", "SPY"}).shares == 0);
        const auto canceled = runtime.execute(SharedDelivery{1002, {"cancel-terminal", order.id, ReportKind::canceled, "", 0, 0, 0, 40, change.dispatch_cancels[0].second}});
        check("terminal cancel releases only the unfilled cash", canceled.outcome == Outcome::applied && canceled.transfers.size() == 1 && canceled.transfers[0].shares == 40);
        check("post-cancel residual buy is assigned to the new owner", canceled.dispatch_orders.size() == 1 && canceled.dispatch_orders[0].shares == 26 &&
            runtime.snapshot().open_plans.at(canceled.dispatch_orders[0].id).remaining.at({"reversion", "SPY"}) == 26);
        static_cast<void>(fill(runtime, canceled.dispatch_orders[0], 26, 1003, "new-owner-fill"));
        check("cancel race preserves total inventory and exact shared cash", runtime.snapshot().account.positions.at("SPY").shares == 66 && runtime.snapshot().account.cash_units == 93400 * unit);
        save("cancel_race", runtime);
    }
    void fault_gates();
    void expiry_and_permutation();
    void mark_limits();
    void recovery();
    void handover();
    void precision();
    void storage_faults();
    void strategy_integration();
    std::filesystem::path directory_;
    SharedCapitalConfiguration config_;
    SharedDiagnosticResult result_;
 }; // Cases

void Cases::fault_gates() {
    auto runtime = start("fault_gates", config_);
    const auto empty = financial(runtime.snapshot());
    auto future = quotes(1001);
    check("future quotes cannot enter target sizing", batch(runtime, "future", 1000, {{{"momentum", "SPY"}, true}}, future).outcome == Outcome::denied);
    auto stale = quotes(997);
    check("stale quotes cannot enter target sizing", batch(runtime, "stale", 1000, {{{"momentum", "SPY"}, true}}, stale).outcome == Outcome::denied);
    auto missing = quotes(1000); missing.pop_back();
    check("incomplete quote universe is rejected", batch(runtime, "missing", 1000, {}, missing).outcome == Outcome::denied);
    check("invalid quote batches retain the financial ledger", financial(runtime.snapshot()) == empty);
    const auto entered = batch(runtime, "entry", 1000, {{{"momentum", "SPY"}, true}});
    const auto id = entered.dispatch_orders.at(0).id;
    const auto registered = financial(runtime.snapshot());
    check("duplicate batch is idempotent", batch(runtime, "entry", 1000, {{{"momentum", "SPY"}, true}}).outcome == Outcome::duplicate && financial(runtime.snapshot()) == registered);
    check("conflicting target identity closes routing", batch(runtime, "entry", 1000, {{{"momentum", "SPY"}, false}}).outcome == Outcome::quarantined && !runtime.snapshot().account.connected);
    const auto offline = fill(runtime, entered.dispatch_orders[0], 1, 1001, "offline-fill");
    check("disconnected fills still update global and owner inventory", offline.outcome == Outcome::applied && offline.dispatch_orders.empty() && runtime.snapshot().owners.at({"momentum", "SPY"}).shares == 1);
    static_cast<void>(runtime.execute(SharedConnection{1001, true}));
    auto wrong = broker(runtime.snapshot()); wrong.positions.erase("GLD");
    check("reconciliation requires all five position entries", runtime.execute(SharedReconciliation{1001, wrong}).outcome == Outcome::quarantined);
    wrong = broker(runtime.snapshot()); ++wrong.cash_units;
    check("reconciliation never overwrites mismatched global cash", runtime.execute(SharedReconciliation{1001, wrong}).outcome == Outcome::quarantined);
    wrong = broker(runtime.snapshot()); ++wrong.open_orders[0].request.limit_units;
    check("reconciliation checks complete open order terms", runtime.execute(SharedReconciliation{1001, wrong}).outcome == Outcome::quarantined);
    check("exact full snapshot reopens routing", runtime.execute(SharedReconciliation{1001, broker(runtime.snapshot())}).outcome == Outcome::applied);
    const auto before_bad = financial(runtime.snapshot());
    check("conflicting execution identity is quarantined", runtime.execute(SharedDelivery{1002, {"conflicting-fill", id, ReportKind::fill, "offline-fill", 2, 100 * unit, 0, 0, ""}}).outcome == Outcome::quarantined);
    check("conflicting fill retains ownership and reservations", financial(runtime.snapshot()) == before_bad && runtime.snapshot().account.reconciliation_required);
    static_cast<void>(runtime.execute(SharedConnection{1002, true}));
    static_cast<void>(runtime.execute(SharedReconciliation{1002, broker(runtime.snapshot())}));
    const auto cancel = batch(runtime, "cancel", 1002, {{{"momentum", "SPY"}, false}});
    check("target change requests a first cancellation", cancel.dispatch_cancels.size() == 1);
    const auto refused = runtime.execute(SharedDelivery{1002, {"cancel-refused", id, ReportKind::cancel_rejected, "", 0, 0, 0, 0, cancel.dispatch_cancels[0].second}});
    check("cancel rejection retains reservation and issues a distinct cancel identity", refused.dispatch_cancels.size() == 1 && refused.dispatch_cancels[0].second != cancel.dispatch_cancels[0].second && runtime.snapshot().account.reserved_cash_units == 6500 * unit);
    save("fault_gates", runtime);
}
void Cases::expiry_and_permutation() {
    auto runtime = start("expiry", config_);
    const auto entry = batch(runtime, "expiring", 1000, {{{"momentum", "SPY"}, true}});
    const auto order = entry.dispatch_orders.at(0);
    const auto late_ack = runtime.execute(SharedDelivery{1031, {"expired-ack", order.id, ReportKind::accepted, "", 0, 0, 0, 0, ""}});
    check("expired pending intent requests cancel without new entry", late_ack.dispatch_cancels.size() == 1 && late_ack.dispatch_orders.empty());
    const auto late_fill = fill(runtime, order, 10, 1032, "expiry-race");
    check("expired target still books legitimate race fills to original owner", late_fill.fills.size() == 1 && runtime.snapshot().owners.at({"momentum", "SPY"}).shares == 10);
    const auto terminal = runtime.execute(SharedDelivery{1033, {"expired-cancel", order.id, ReportKind::canceled, "", 0, 0, 0, 10, late_ack.dispatch_cancels.at(0).second}});
    check("expiry retains held inventory and dispatches no implicit liquidation", terminal.dispatch_orders.empty() && runtime.snapshot().account.positions.at("SPY").shares == 10 && runtime.snapshot().account.reserved_cash_units == 0);
    save("expiry", runtime);
    SharedCapitalBook a{config_}, b{config_};
    for (auto* book : {&a, &b}) {
        static_cast<void>(book->apply(SharedConnection{10, true}));
        static_cast<void>(book->apply(SharedReconciliation{10, broker(book->snapshot())}));
    }
    SharedTargetBatch command{"permutation", 1000, quotes(1000), {}};
    for (const auto& family : config_.families) { for (const auto& symbol : config_.symbols) { command.signals.push_back({{family, symbol}, true}); } }
    const auto first = a.apply(command);
    std::reverse(command.signals.begin(), command.signals.end()); std::reverse(command.quotes.begin(), command.quotes.end());
    const auto second = b.apply(command);
    check("input permutations retain identical dispatches and frozen owner allocations", shared_result_json(first) == shared_result_json(second) && shared_snapshot_json(a.snapshot()) == shared_snapshot_json(b.snapshot()));
    check("canonical batch identity treats a permutation as duplicate", a.apply(command).outcome == Outcome::duplicate);
    auto encoded = shared_input_json(command); encoded["signals"][0]["long_target"] = 1;
    reject("schema 4 rejects numeric booleans", [&] { static_cast<void>(parse_shared_input(encoded)); });
    encoded = shared_input_json(command); encoded["at_seconds"] = 1.5;
    reject("schema 4 rejects fractional receipt clocks", [&] { static_cast<void>(parse_shared_input(encoded)); });
    encoded = shared_input_json(command); encoded["extra"] = true;
    reject("schema 4 rejects unknown command fields", [&] { static_cast<void>(parse_shared_input(encoded)); });
    encoded = shared_input_json(command); encoded["at_seconds"] = std::numeric_limits<std::uint64_t>::max();
    reject("schema 4 rejects unrepresentable unsigned integers", [&] { static_cast<void>(parse_shared_input(encoded)); });
}
void Cases::mark_limits() {
    auto runtime = start("mark_limits", config_);
    std::vector<FamilySignal> signals;
    for (const auto& family : config_.families) { signals.push_back({{family, "SPY"}, true}); }
    const auto initial = batch(runtime, "spy-full", 1000, signals);
    static_cast<void>(fill(runtime, initial.dispatch_orders.at(0), 198, 1001, "limits-entry"));
    auto market = quotes(1002);
    for (auto& q : market) { if (q.symbol == "SPY") { q.bid_units = q.ask_units = 500 * unit; } }
    signals.clear();
    for (const auto& family : config_.families) { for (const auto& symbol : config_.symbols) { signals.push_back({{family, symbol}, true}); } }
    const auto routed = batch(runtime, "remark", 1002, signals, market);
    const auto state = runtime.snapshot();
    check("market-move limit breaches are explicitly visible", std::find(state.budget_breaches.begin(), state.budget_breaches.end(), "symbol:SPY") != state.budget_breaches.end());
    check("overweight symbol routes a reduction", !routed.dispatch_orders.empty() && std::any_of(routed.dispatch_orders.begin(), routed.dispatch_orders.end(), [](const auto& o) { return o.symbol == "SPY" && o.side == Side::sell && o.shares == 129; }));
    check("unfilled sale proceeds do not fund new buys", state.account.reserved_cash_units <= state.account.cash_units && state.account.cash_units == 80200 * unit);
    check("aggregate pending buys and held shares remain unlevered", state.account.cash_units >= state.account.reserved_cash_units);
    save("mark_limits", runtime);
}
void Cases::recovery() {
    auto reference = start("uninterrupted", config_);
    auto interrupted = std::make_optional(start("interrupted", config_));
    const auto a = batch(reference, "entry", 1000, {{{"momentum", "SPY"}, true}, {{"reversion", "QQQ"}, true}});
    const auto b = batch(*interrupted, "entry", 1000, {{{"momentum", "SPY"}, true}, {{"reversion", "QQQ"}, true}});
    check("matched recovery paths start from identical dispatch terms", a.dispatch_orders == b.dispatch_orders);
    static_cast<void>(fill(reference, a.dispatch_orders[0], 20, 1001, "first-partial"));
    static_cast<void>(fill(*interrupted, b.dispatch_orders[0], 20, 1001, "first-partial"));
    const auto before = financial(interrupted->snapshot());
    interrupted.reset();
    interrupted.emplace(JournaledSharedCapital::recover(open_journal_file(directory_ / "interrupted/0.journal"), config_));
    check("active-order recovery preserves every owner and reservation", financial(interrupted->snapshot()) == before);
    check("recovery durably closes routing", !interrupted->snapshot().account.connected && interrupted->snapshot().account.reconciliation_required);
    static_cast<void>(interrupted->execute(SharedConnection{1001, true}));
    check("recovered account requires an exact snapshot", interrupted->execute(SharedReconciliation{1001, broker(interrupted->snapshot())}).outcome == Outcome::applied);
    for (const auto& o : a.dispatch_orders) {
        const auto q = o.shares - (o.id == a.dispatch_orders[0].id ? 20 : 0);
        static_cast<void>(fill(reference, o, q, 1002, "rest-" + o.symbol));
        static_cast<void>(fill(*interrupted, o, q, 1002, "rest-" + o.symbol));
    }
    check("interrupted and uninterrupted global and owner ledgers are identical", financial(reference.snapshot()) == financial(interrupted->snapshot()));
    save("uninterrupted", reference); save("interrupted", *interrupted);
    result_.evidence["financial_parity"].push_back({{"left", "uninterrupted"}, {"right", "interrupted"}});
}
void Cases::handover() {
    auto runtime = start("handover", config_);
    const auto entry = batch(runtime, "entry", 1000, {{{"momentum", "SPY"}, true}});
    const auto rejected_path = directory_ / "handover/nonflat-child.journal";
    reject("handover cannot discard a pending order", [&] { static_cast<void>(runtime.handover(create_journal_file(rejected_path))); });
    check("failed nonflat handover leaves predecessor usable", !runtime.snapshot().sealed && runtime.healthy());
    result_.evidence["rejected_files"].push_back({{"path", "handover/nonflat-child.journal"}, {"failure", "empty_unpublished_successor"}});
    static_cast<void>(fill(runtime, entry.dispatch_orders[0], 66, 1001, "roll-entry"));
    const auto exit = batch(runtime, "cash", 1002, {{{"momentum", "SPY"}, false}}, quotes(1002, 101 * unit));
    static_cast<void>(fill(runtime, exit.dispatch_orders.at(0), 66, 1003, "roll-exit"));
    const auto flat = runtime.snapshot();
    auto child = std::make_optional(runtime.handover(create_journal_file(directory_ / "handover/1.journal")));
    const auto identity = child->identity();
    check("predecessor is durably sealed before successor routing", runtime.snapshot().sealed && !child->snapshot().account.connected);
    check("handover carries exact global cash and attribution P&L", child->snapshot().account.cash_units == flat.account.cash_units && child->snapshot().owners.at({"momentum", "SPY"}).attribution_cash_flow_units == 66 * unit);
    check("handover advances a linked order namespace", identity.generation == 1 && identity.previous_segment_sha256 == runtime.inspection().last_sha256);
    reject("sealed writer cannot append another trade command", [&] { static_cast<void>(batch(runtime, "forbidden", 1004, {{{"momentum", "SPY"}, true}})); });
    child.reset();
    reject("successor recovery requires its expected generation and predecessor", [&] { static_cast<void>(JournaledSharedCapital::recover(open_journal_file(directory_ / "handover/1.journal"), config_)); });
    child.emplace(JournaledSharedCapital::recover(open_journal_file(directory_ / "handover/1.journal"), config_, identity));
    static_cast<void>(child->execute(SharedConnection{1004, true}));
    static_cast<void>(child->execute(SharedReconciliation{1004, broker(child->snapshot())}));
    const auto new_orders = batch(*child, "new-segment", 1004, {{{"momentum", "QQQ"}, true}});
    check("new generation cannot reuse prior dispatch identities", new_orders.dispatch_orders.size() == 1 && new_orders.dispatch_orders[0].id.starts_with("S4-1-") && new_orders.dispatch_orders[0].id != entry.dispatch_orders[0].id);
    const auto value = financial(child->snapshot());
    check("retired-segment evidence closes routing without double booking", child->execute(SharedDelivery{1005, {"retired-delivery", entry.dispatch_orders[0].id, ReportKind::fill, "roll-entry", 66, 100 * unit, 0, 0, ""}}).outcome == Outcome::quarantined && financial(child->snapshot()) == value);
    save("handover", *child, {{"handover/0.journal", runtime.identity()}, {"handover/1.journal", identity}});
}
void Cases::precision() {
    auto config = config_; config.fee_per_share_ceiling_units = 1;
    auto runtime = start("integer_precision", config);
    std::vector<FamilySignal> signals;
    for (const auto& family : config.families) { signals.push_back({{family, "SPY"}, true}); }
    const auto entry = batch(runtime, "tiny-unit-prices", 1000, signals, quotes(1000, 3));
    constexpr Shares quantity = 3000000000001LL;
    constexpr MoneyUnits fee = 1000000000003LL;
    const auto allocated = fill(runtime, entry.dispatch_orders.at(0), quantity, 1001, "large-integer-fill", fee);
    Shares total = 0; MoneyUnits allocated_fee = 0;
    for (const auto& item : allocated.fills) { total += item.shares; allocated_fee += item.fee_units; }
    check("large allocation products retain whole shares without wide platform integers", total == quantity && allocated.fills[0].shares == 1000000000001LL);
    check("large proportional fee allocations conserve every nanodollar", allocated_fee == fee && runtime.snapshot().account.cash_units == config.initial_cash_units - quantity * 3 - fee);
    save("integer_precision", runtime);
    OrderRuntime core{std::vector<std::string>{"SPY", "QQQ"}, 100, {1000, 2}};
    check("multi-symbol core denies overflow before reserving", core.submit({"overflow", "SPY", Side::buy, std::numeric_limits<Shares>::max(), 2, 0}) == Outcome::denied);
    check("multi-symbol core shares cash across pending symbols", core.submit({"spy", "SPY", Side::buy, 50, 2, 0}) == Outcome::applied && core.submit({"qqq", "QQQ", Side::buy, 1, 2, 0}) == Outcome::denied);
    reject("legacy scalar inventory view cannot mislabel a multi-symbol portfolio", [&] { static_cast<void>(core.account()); });
}

void Cases::storage_faults() {
    for (const auto fault : {WriteFault::before, WriteFault::partial, WriteFault::after}) {
        const std::string name = fault == WriteFault::before ? "append_before" : fault == WriteFault::partial ? "append_partial" : "append_after";
        std::filesystem::create_directories(directory_ / name);
        const auto path = directory_ / name / "0.journal";
        WriteControl control;
        auto runtime = std::make_optional(JournaledSharedCapital::create(std::make_unique<FaultStorage>(create_journal_file(path), control), config_));
        static_cast<void>(runtime->execute(SharedConnection{10, true}));
        static_cast<void>(runtime->execute(SharedReconciliation{10, broker(runtime->snapshot())}));
        const auto before = sha256_file(path);
        control.fault = fault;
        reject(name + ": uncertain append does not return dispatch eligibility", [&] { static_cast<void>(batch(*runtime, "uncertain", 1000, {{{"momentum", "SPY"}, true}})); });
        check(name + ": failed writer latches unhealthy", !runtime->healthy());
        reject(name + ": failed writer cannot expose tradable state", [&] { static_cast<void>(runtime->snapshot()); });
        runtime.reset();
        std::string active = name + "/0.journal";
        if (fault == WriteFault::partial) {
            reject("incomplete shared tail cannot be opened for normal continuation", [&] { static_cast<void>(JournaledSharedCapital::recover(open_journal_file(path), config_)); });
            const auto source_hash = sha256_file(path);
            active = name + "/verified_prefix.journal";
            runtime.emplace(JournaledSharedCapital::recover_prefix(open_journal_file(path), create_journal_file(directory_ / active), config_));
            check("verified prefix recovery leaves damaged source bytes unchanged", sha256_file(path) == source_hash);
            result_.evidence["rejected_files"].push_back({{"path", name + "/0.journal"}, {"failure", "incomplete_tail"}});
        } else {
            if (fault == WriteFault::before) { check("pre-append failure leaves file unchanged", sha256_file(path) == before); }
            runtime.emplace(JournaledSharedCapital::recover(open_journal_file(path), config_));
        }
        check(name + ": recovery begins closed and performs no resend", !runtime->snapshot().account.connected && runtime->snapshot().account.reconciliation_required);
        const auto expected_orders = fault == WriteFault::after ? 1U : 0U;
        check(name + ": replay distinguishes absent and committed uncertain orders", runtime->snapshot().account.open_orders == expected_orders);
        if (fault == WriteFault::after) {
            check("committed uncertain order retains its original owner claim", runtime->snapshot().open_plans.begin()->second.remaining.at({"momentum", "SPY"}) == 66);
            static_cast<void>(runtime->execute(SharedConnection{1001, true}));
            auto absent = broker(runtime->snapshot()); absent.open_orders.clear();
            check("uncertain external dispatch cannot be repaired by an empty broker order list", runtime->execute(SharedReconciliation{1001, absent}).outcome == Outcome::quarantined);
        }
        save(name, *runtime, {{active, {}}});
    }
    const auto clean = read_text_file(directory_ / "uninterrupted/0.journal");
    auto altered = clean; altered[0] = '!';
    const auto corrupt_path = directory_ / "corrupt.journal";
    write_new_text_file(corrupt_path, altered);
    reject("complete shared-frame corruption is rejected", [&] { static_cast<void>(JournaledSharedCapital::recover(open_journal_file(corrupt_path), config_)); });
    check("rejected complete corruption is never silently truncated", read_text_file(corrupt_path) == altered);
    result_.evidence["rejected_files"].push_back({{"path", "corrupt.journal"}, {"failure", "checksum"}});
    auto wrong = config_; wrong.initial_cash_units += unit;
    reject("recovery requires the exact initial capital configuration", [&] { static_cast<void>(JournaledSharedCapital::recover(open_journal_file(directory_ / "uninterrupted/0.journal"), wrong)); });
    auto held = open_journal_file(directory_ / "uninterrupted/0.journal");
    reject("a second shared account writer cannot acquire the same segment", [&] { static_cast<void>(open_journal_file(directory_ / "uninterrupted/0.journal")); });
    held.reset();
    auto predecessor = start("handover_fault", config_);
    WriteControl control; control.fault = WriteFault::after;
    reject("successor-header uncertainty retains a sealed predecessor", [&] {
        static_cast<void>(predecessor.handover(std::make_unique<FaultStorage>(create_journal_file(directory_ / "handover_fault/1.journal"), control)));
    });
    check("handover failure cannot reopen the predecessor", predecessor.snapshot().sealed);
    const SharedJournalIdentity identity{1, predecessor.inspection().last_sha256};
    auto successor = JournaledSharedCapital::recover(open_journal_file(directory_ / "handover_fault/1.journal"), config_, identity);
    check("committed successor header can recover with the same sealed-parent identity", successor.snapshot().generation == 1 && !successor.snapshot().account.connected && successor.snapshot().account.cash_units == config_.initial_cash_units);
    save("handover_fault", successor, {{"handover_fault/0.journal", {}}, {"handover_fault/1.journal", identity}});
}
void Cases::strategy_integration() {
    auto runtime = start("strategy_integration", config_);
    const auto root = std::filesystem::path{TRADING_PROJECT_ROOT};
    const auto contract = ExtensionStudyContract::load(root / "research/extension_protocol.json");
    const auto policy = PanelInputPolicy::load(root / "research/extension_input_policy.json", contract);
    auto calendar = extension_calendar({"2017-01-03", "2017-01-31"});
    const auto february = extension_calendar({"2017-02-01", "2017-02-15"});
    calendar.insert(calendar.end(), february.begin(), february.end());
    std::map<std::string, PanelReplayState> states;
    for (const auto& symbol : config_.symbols) { states.emplace(symbol, PanelReplayState{symbol, policy}); }
    std::vector<PanelReplayDay> rows;
    for (std::size_t day = 0; day < 21; ++day) {
        for (auto& [symbol, state] : states) {
            std::vector<MinuteBar> bars;
            double previous = 100;
            for (int minute = 0; minute < 390; ++minute) {
                // A bounded deterministic perturbation gives the prior a
                // nonzero residual variance without relying on roundoff.
                const auto m = static_cast<double>(minute);
                double close = 100 * std::exp(.002 * std::exp(-m / 50) + .00001 * std::sin(.7 * m));
                if (day == 20 && minute >= 30) {
                    close = minute < 150 ? 100 - static_cast<double>(minute - 29) / 120 : 99 + static_cast<double>(minute - 149) / 35;
                }
                bars.emplace_back(symbol, calendar[day].open_utc() + std::chrono::minutes{minute},
                    Ohlc{previous, std::max(previous, close) + .01, std::min(previous, close) - .01, close}, day == 20 ? 3000 : 1000);
                previous = close;
            }
            auto record = state.advance(calendar[day].bar_request(symbol), bars);
            if (day == 20) { rows.push_back(std::move(record)); }
        }
    }
    for (const auto& row : rows) { result_.evidence["strategy_records"].push_back(panel_replay_record(row)); }
    const SharedStrategyAdapter adapter;
    std::set<std::string> active_families;
    std::size_t event = 0;
    for (const int minute : {30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330, 360, 385}) {
        const auto at = calendar[20].open_utc() + std::chrono::minutes{minute};
        const auto time = at.time_since_epoch().count();
        const auto command = adapter.at("primary-" + std::to_string(minute), at, rows, runtime.snapshot(), quotes(time));
        for (const auto& signal : command.signals) { if (signal.long_target) { active_families.insert(signal.owner.family); } }
        const auto result = runtime.execute(command);
        check("authoritative primary feature batch accepted at minute " + std::to_string(minute), result.outcome == Outcome::applied);
        for (const auto& order : result.dispatch_orders) {
            const auto filled = fill(runtime, order, order.shares, time + 1, "primary-fill-" + std::to_string(++event));
            check("shared primary order and ownership allocation accepted", filled.outcome == Outcome::applied);
        }
    }
    check("all three authoritative strategy families create shared-capital targets", active_families.size() == 3);
    const auto final = runtime.snapshot();
    check("primary constructed session ends flat across all five instruments", final.account.open_orders == 0 &&
        std::all_of(final.owners.begin(), final.owners.end(), [](const auto& item) { return item.second.shares == 0; }));
    // An existing intended hold must still pass the fresh-entry volume filter
    // when its real owner received no inventory.
    auto modified = rows;
    const auto decision_at = calendar[20].open_utc() + std::chrono::minutes{30};
    for (auto& row : modified) { for (auto& path : row.policies) { if (path.policy == "continuation_volume") {
        for (auto& decision : path.decisions) { if (decision.at == decision_at && decision.continuation) {
            decision.continuation->move = .02; decision.continuation->typical_absolute_move = .001; decision.continuation->relative_volume = .5;
            decision.target_long = true;
        } }
    } } }
    const auto cash_decisions = adapter.at("actual-owner-cash", decision_at, modified, runtime.snapshot(), quotes(decision_at.time_since_epoch().count()));
    check("continuation re-evaluates fresh-entry volume against actual owned inventory", std::none_of(cash_decisions.signals.begin(), cash_decisions.signals.end(), [](const auto& signal) {
        return signal.owner.family == "volume_conditioned_continuation" && signal.long_target;
    }));
    auto future = rows; for (auto& row : future) { row.date = "2024-01-02"; }
    reject("shared strategy adapter retains the reserved-period boundary", [&] { static_cast<void>(adapter.at("reserved", parse_utc_seconds("2024-01-02T15:00:00Z"), future, runtime.snapshot(), quotes(1))); });
    save("strategy_integration", runtime);
}

} // namespace
SharedCapitalDiagnostic::SharedCapitalDiagnostic(std::filesystem::path directory, SharedCapitalConfiguration config)
    : directory_(std::move(directory)), configuration_(std::move(config)) {}
SharedDiagnosticResult SharedCapitalDiagnostic::run() const { return Cases{directory_, configuration_}.run(); }
} // namespace trading::execution
