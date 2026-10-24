#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "execution_diagnostic_report.hpp"
#include "order_runtime.hpp"
#include "report_text.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace trading::execution;

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}
OrderRequest buy(std::string id, Shares shares = 10, Cents price = 10000, Cents fee = 10) {
    return {std::move(id), "SPY", Side::buy, shares, price, fee};
}
BrokerReport fill(std::string event, std::string order, std::string execution, Shares shares, Cents price, Cents fee) {
    return {std::move(event), std::move(order), ReportKind::fill, std::move(execution), shares, price, fee, 0, ""};
}
BrokerReport message(std::string event, std::string order, ReportKind kind, Shares cumulative = 0, std::string cancel = "") {
    return {std::move(event), std::move(order), kind, "", 0, 0, 0, cumulative, std::move(cancel)};
}
void same_finances(AccountSnapshot a, AccountSnapshot b) {
    a.connected = b.connected;
    a.reconciliation_required = b.reconciliation_required;
    require(a == b, "rejected message must preserve balances and every reservation");
}

void test_reservations_and_races() {
    OrderRuntime runtime{"SPY", 200000, {20, 2}};
    require(runtime.submit(buy("B")) == Outcome::applied, "buy admitted");
    require(runtime.account().reserved_cash_units == 100100, "buy reserves limit notional plus maximum fees");
    require(runtime.submit(buy("cash", 10)) == Outcome::denied, "aggregate cash reservation prevents overspend");
    require(runtime.request_cancel("B", "C1") == Outcome::applied, "cancel request admitted");
    require(runtime.account().reserved_cash_units == 100100, "pending cancellation cannot finance another order");
    require(runtime.receive(fill("F1", "B", "X1", 4, 9900, 4)) == Outcome::applied, "partial fill before acknowledgement");
    require(runtime.order("B").status == OrderStatus::pending_cancel, "fill preserves cancel intent");
    require(runtime.account().cash_units == 160396 && runtime.account().position_shares == 4 &&
            runtime.account().reserved_cash_units == 60060, "cash debited exactly and remaining units reserved");
    require(runtime.receive(message("A", "B", ReportKind::accepted)) == Outcome::obsolete, "late acknowledgement cannot regress pending cancel");
    require(runtime.receive(message("CR", "B", ReportKind::cancel_rejected, 0, "C1")) == Outcome::applied, "cancel rejection restores partial state");
    require(runtime.request_cancel("B", "C2") == Outcome::applied, "second cancel has a distinct identity");
    require(runtime.receive(message("CR-late", "B", ReportKind::cancel_rejected, 0, "C1")) == Outcome::obsolete,
            "late rejection of old cancel cannot remove new cancel intent");
    require(runtime.order("B").pending_cancel_id == "C2", "current cancel identity retained");
    require(runtime.receive(message("CA", "B", ReportKind::canceled, 4, "C2")) == Outcome::applied, "confirmed partial cancellation");
    require(runtime.account().reserved_cash_units == 0 && runtime.account().position_shares == 4,
            "terminal cancellation releases only remaining reservation");
    require(runtime.request_cancel("B", "C3") == Outcome::denied, "terminal order cannot be canceled again");
    require(runtime.submit(buy("B", 1)) == Outcome::denied, "terminal order ID cannot be reused");
}

void test_delivery_permutations_and_integer_oracle() {
    std::array<int, 3> permutation{0, 1, 2};
    do {
        OrderRuntime runtime{"SPY", 1000000, {20, 4}};
        require(runtime.submit(buy("B")) == Outcome::applied, "order admitted for permutation");
        const std::array reports{message("A", "B", ReportKind::accepted),
                                fill("F1", "B", "X1", 3, 9900, 3), fill("F2", "B", "X2", 7, 10000, 7)};
        for (const auto index : permutation) {
            const auto& report = reports.at(static_cast<std::size_t>(index));
            require(runtime.receive(report) != Outcome::quarantined, "acknowledgement and incremental fills tolerate reordering");
            const auto before = runtime.account();
            require(runtime.receive(report) == Outcome::duplicate, "same event is idempotent at every stage");
            same_finances(before, runtime.account());
            if (report.kind == ReportKind::fill) {
                auto repeat = report;
                repeat.event_id += "-copy";
                require(runtime.receive(repeat) == Outcome::duplicate, "same execution under a fresh event ID is idempotent");
                same_finances(before, runtime.account());
            }
        }
        // Independent integer identity: cash = initial - sum(q * price + fee).
        require(runtime.account().cash_units == 1000000 - 3 * 9900 - 3 - 7 * 10000 - 7 &&
                runtime.account().position_shares == 10 && runtime.account().reserved_cash_units == 0,
                "all delivery permutations conserve the independent fill ledger");
        require(runtime.order("B").status == OrderStatus::filled, "full fill is terminal regardless of acknowledgement order");
    } while (std::next_permutation(permutation.begin(), permutation.end()));
}

void test_quarantine_and_reconciliation() {
    OrderRuntime runtime{"SPY", 1000000, {20, 4}};
    const auto request = buy("B");
    require(runtime.submit(request) == Outcome::applied, "order admitted");
    require(runtime.receive(fill("F", "B", "X", 4, 10000, 4)) == Outcome::applied, "first execution booked");
    const auto before = runtime.account();
    const auto order_before = runtime.order("B");
    for (const auto& report : {
            fill("F", "B", "X", 4, 10000, 5), fill("other", "B", "X", 3, 10000, 3),
            fill("over", "B", "over", 7, 10000, 7), fill("fee", "B", "fee", 1, 10000, 11),
            fill("limit", "B", "limit", 1, 10001, 0), fill("negative", "B", "negative", -1, 10000, 0),
            message("unknown", "absent", ReportKind::accepted), message("gap", "B", ReportKind::canceled, 7)}) {
        require(runtime.receive(report) == Outcome::quarantined, "inconsistent external report quarantined");
        same_finances(before, runtime.account());
        require(runtime.order("B") == order_before, "quarantine preserves the complete order state");
        require(runtime.account().reconciliation_required, "fault closes entry gate");
    }
    require(runtime.submit(buy("blocked", 1)) == Outcome::denied, "new orders blocked after fault");
    BrokerSnapshot snapshot{959996, 4, {{request, 4}}};
    auto mismatch = snapshot;
    ++mismatch.open_orders.front().request.limit_units;
    require(runtime.reconcile(mismatch) == Outcome::quarantined, "snapshot must match complete order terms");
    mismatch = snapshot;
    mismatch.open_orders.push_back(mismatch.open_orders.front());
    require(runtime.reconcile(mismatch) == Outcome::quarantined, "duplicate snapshot order cannot pass reconciliation");
    mismatch = snapshot;
    ++mismatch.cash_units;
    require(runtime.reconcile(mismatch) == Outcome::quarantined, "cash discrepancy is not silently overwritten");
    require(runtime.reconcile(snapshot) == Outcome::applied && !runtime.account().reconciliation_required, "exact snapshot opens entry gate");
    runtime.disconnect();
    require(runtime.request_cancel("B", "offline") == Outcome::denied, "no dispatch while disconnected");
    require(runtime.reconcile(snapshot) == Outcome::quarantined, "snapshot alone cannot restore transport connection");
    runtime.reconnect();
    require(runtime.account().reconciliation_required, "reconnection alone cannot open entry gate");
    require(runtime.reconcile(snapshot) == Outcome::applied, "connected exact snapshot restores gate");
    require(runtime.request_cancel("B", "C") == Outcome::applied, "cancel allowed after recovery");
    const auto pending = runtime.account();
    require(runtime.receive(message("gap-2", "B", ReportKind::canceled, 10, "C")) == Outcome::quarantined, "missing fill blocks terminal release");
    same_finances(pending, runtime.account());
    require(runtime.receive(fill("missing", "B", "missing", 6, 10000, 6)) == Outcome::applied, "missing execution accepted while gated");
    require(runtime.receive(message("gap-2", "B", ReportKind::canceled, 10, "C")) == Outcome::obsolete,
            "quarantined message can be retried after missing evidence arrives");
    require(runtime.account().reconciliation_required, "repairing deliveries still requires explicit reconciliation");
}

void test_inventory_limits_and_invalid_units() {
    OrderRuntime runtime{"SPY", 1000000, {10, 1}};
    for (const auto& request : {buy("zero", 0), buy("negative", -1), buy("bad-price", 1, 0), buy("fee", 1, 100, 100),
                                buy("overflow", 2, std::numeric_limits<Cents>::max(), 1), buy("with space", 1)}) {
        require(runtime.submit(request) == Outcome::denied, "invalid or overflowing order units denied");
    }
    auto wrong_symbol = buy("symbol"); wrong_symbol.symbol = "QQQ";
    require(runtime.submit(wrong_symbol) == Outcome::denied, "single-symbol boundary enforced");
    require(runtime.submit(buy("B", 9)) == Outcome::applied, "valid buy admitted");
    require(runtime.submit(buy("count", 1)) == Outcome::denied, "open-order count limit enforced independently of position");
    require(runtime.receive(message("A", "B", ReportKind::accepted)) == Outcome::applied, "new acknowledgement");
    require(runtime.request_cancel("B", "C") == Outcome::applied, "cancel admitted");
    require(runtime.receive(message("contradiction", "B", ReportKind::rejected)) == Outcome::quarantined,
            "cancel-pending label does not erase earlier new acknowledgement");
    require(runtime.receive(fill("BF", "B", "BX", 9, 10000, 9)) == Outcome::applied, "fill remains authoritative while gated");
    require(runtime.reconcile({909991, 9, {}}) == Outcome::applied, "reconcile terminal filled account");
    auto sale = buy("S", 7); sale.side = Side::sell;
    require(runtime.submit(sale) == Outcome::applied, "inventory-backed sell admitted");
    require(runtime.account().reserved_sell_shares == 7, "sell reserves shares");
    const auto before = runtime.account();
    require(runtime.receive(fill("bad-sell", "S", "bad", 1, 9999, 1)) == Outcome::quarantined, "sell cannot fill below limit");
    same_finances(before, runtime.account());
    require(runtime.receive(fill("SF", "S", "SX", 7, 10100, 7)) == Outcome::applied, "sell books improved price and explicit fee");
    require(runtime.account().cash_units == 980684 && runtime.account().position_shares == 2, "independent buy and sell cash identity");
    OrderRuntime huge{"SPY", std::numeric_limits<Cents>::max(), {10, 4}};
    require(huge.submit(buy("tiny", 1, 1, 0)) == Outcome::applied, "large account small buy");
    require(huge.receive(fill("H1", "tiny", "HX1", 1, 1, 0)) == Outcome::applied, "large account purchase");
    auto high_sale = buy("high", 1, std::numeric_limits<Cents>::max(), 0); high_sale.side = Side::sell;
    require(huge.submit(high_sale) == Outcome::applied, "high limit sell admitted");
    const auto huge_before = huge.account();
    require(huge.receive(fill("H2", "high", "HX2", 1, std::numeric_limits<Cents>::max(), 0)) == Outcome::quarantined,
            "cash addition overflow quarantined without undefined arithmetic");
    same_finances(huge_before, huge.account());
    OrderRuntime rejected{"SPY", 1000000, {20, 4}};
    require(rejected.submit(buy("R")) == Outcome::applied, "order before new rejection");
    require(rejected.receive(message("R1", "R", ReportKind::rejected)) == Outcome::applied, "new rejection releases reservation");
    const auto rejected_before = rejected.account();
    require(rejected.receive(message("R2", "R", ReportKind::accepted)) == Outcome::quarantined,
            "accepted and rejected new-order reports are contradictory");
    same_finances(rejected_before, rejected.account());
}

void test_report_text_ownership_and_encoding() {
    std::string input{"\"<&>,\n"};
    const trading::ReportText text{input};
    input.clear();
    require(text.html() == "&quot;&lt;&amp;&gt;,\n", "HTML encoding owns its input and escapes markup exactly once");
    require(text.csv() == "\"\"\"<&>,\n\"", "CSV field quotes preserve commas/newlines and double embedded quotes");
    require(trading::ReportText{""}.html().empty() && trading::ReportText{""}.csv() == "\"\"",
        "empty text keeps format-specific semantics");
    const std::string utf8{"\xC3\xA9"};
    require(trading::ReportText{utf8}.html() == utf8, "encoding preserves non-markup UTF-8 bytes");
}

void test_scenarios_and_artifacts(const std::filesystem::path& output) {
    const auto results = ExecutionDiagnostic{}.run();
    require(results.size() == 6, "six constructed failure-mode schedules");
    const auto directory = trading::create_unique_directory(output, "execution-report");
    ExecutionDiagnosticReport{results}.write(directory);
    const auto summary = nlohmann::json::parse(trading::read_text_file(directory / "summary.json"));
    require(summary.at("status").get<std::string>() == "synthetic_contracts_passed", "manifest identifies synthetic scope");
    require(summary.at("scenario_count").get<std::size_t>() == results.size(), "manifest includes every scenario");
    for (const auto& artifact : summary.at("artifacts")) {
        require(trading::sha256_file(directory / artifact.at("path").get<std::string>()) == artifact.at("sha256").get<std::string>(),
                "retained artifact identity matches actual bytes");
    }
    require(trading::sha256_file(directory / "source_inventory.txt") == summary.at("source_sha256").get<std::string>(),
            "per-file inventory reproduces the compiled aggregate source identity");
    const auto inputs = nlohmann::json::parse(trading::read_text_file(directory / "inputs.json"));
    require(inputs.size() == results.size() && inputs.front().at("steps").size() == results.front().trace.size(),
            "complete input schedules retained alongside traces");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) { throw std::invalid_argument("order runtime contracts require an output directory"); }
        test_reservations_and_races();
        test_delivery_permutations_and_integer_oracle();
        test_quarantine_and_reconciliation();
        test_inventory_limits_and_invalid_units();
        test_report_text_ownership_and_encoding();
        test_scenarios_and_artifacts(argv[1]);
        std::cout << "Order runtime contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Order runtime contract failure: " << error.what() << '\n';
        return 1;
    }
}
