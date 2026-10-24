#include "recovery_diagnostic.hpp"

#include "artifact_io.hpp"
#include "content_digest.hpp"

#include <algorithm>

namespace trading::execution {

RecoveryDiagnosticResult RecoveryDiagnostic::run() const {
    const auto& directory = directory_;
    const RuntimeConfiguration config{"SPY", 1000000, {20, 4}};
    const OrderRequest request{"B", "SPY", Side::buy, 10, 10000, 10};
    const BrokerReport fill{"F1", "B", ReportKind::fill, "X1", 4, 10000, 4, 0, ""};
    RecoveryDiagnosticResult result{{}, {}, {"restart.journal", "torn-original.journal", "prefix-recovered.journal", "corrupt.journal"}, 0};
    auto check = [&](std::string name, bool passed) {
        if (!passed) { throw std::logic_error("recovery diagnostic failed: " + name); }
        result.checks.push_back({std::move(name), passed});
    };
    auto observe = [&](std::string scenario, std::string stage, const JournaledRuntime& runtime) {
        result.checkpoints.push_back({std::move(scenario), std::move(stage), runtime.account(), runtime.order("B"),
                                      runtime.inspection().input_records});
    };
    auto seed = [&](const std::filesystem::path& file) {
        auto runtime = JournaledRuntime::create(create_journal_file(file), config);
        check("New writer starts disconnected", !runtime.account().connected && runtime.account().reconciliation_required);
        check("Connection recorded", runtime.execute(ConnectionInput{true}) == Outcome::applied);
        check("Initial exact snapshot recorded", runtime.execute(BrokerSnapshot{1000000, 0, {}}) == Outcome::applied);
        check("Order intent durably accepted", runtime.execute(SubmitInput{request}) == Outcome::applied);
        check("Partial execution durably accepted", runtime.execute(fill) == Outcome::applied);
        return runtime;
    };
    std::vector<TraceEntry> before;
    {
        auto runtime = seed(directory / "restart.journal");
        check("Cancellation intent durably accepted", runtime.execute(CancelInput{"B", "C"}) == Outcome::applied);
        before = runtime.trace();
        observe("Partial fill restart", "Before writer destruction", runtime);
    }
    {
        auto runtime = JournaledRuntime::recover(open_journal_file(directory / "restart.journal"), config);
        check("Complete prior trace reproduced", runtime.trace().size() == before.size() + 1 &&
              std::equal(before.begin(), before.end(), runtime.trace().begin()));
        check("Restart retains balances and reservations", runtime.account().cash_units == 959996 &&
              runtime.account().position_shares == 4 && runtime.account().reserved_cash_units == 60060);
        check("Restart retains cancel identity and closes entry", runtime.order("B").pending_cancel_id == "C" &&
              !runtime.account().connected && runtime.account().reconciliation_required);
        observe("Partial fill restart", "Recovered and gated", runtime);
        auto duplicate = fill; duplicate.event_id = "redelivery";
        check("Execution redelivery remains idempotent", runtime.execute(duplicate) == Outcome::duplicate);
        check("New order remains blocked after restart",
              runtime.execute(SubmitInput{{"blocked", "SPY", Side::buy, 1, 10000, 10}}) == Outcome::denied);
        check("Reconnect recorded", runtime.execute(ConnectionInput{true}) == Outcome::applied);
        check("Incomplete broker snapshot rejected", runtime.execute(BrokerSnapshot{959996, 4, {}}) == Outcome::quarantined);
        check("Complete broker snapshot restores entry", runtime.execute(BrokerSnapshot{959996, 4, {{request, 4}}}) == Outcome::applied);
        observe("Partial fill restart", "Reconciled", runtime);
        check("Delayed cancellation rejection correlated",
              runtime.execute(BrokerReport{"CR", "B", ReportKind::cancel_rejected, "", 0, 0, 0, 0, "C"}) == Outcome::applied);
        check("Remaining execution accepted",
              runtime.execute(BrokerReport{"F2", "B", ReportKind::fill, "X2", 6, 10000, 6, 0, ""}) == Outcome::applied);
        check("Final integer ledger conserves cash", runtime.account().cash_units == 899990 &&
              runtime.account().position_shares == 10 && runtime.account().reserved_cash_units == 0);
        observe("Partial fill restart", "Fully filled", runtime);
    }
    {
        auto runtime = seed(directory / "torn-original.journal");
        observe("Incomplete final write", "Last complete input", runtime);
    }
    {
        auto storage = open_journal_file(directory / "torn-original.journal");
        storage->append_and_sync("{\"kind\":\"input\",\"sequence\":6");
        result.preserved_tail_bytes = JournaledRuntime::inspect(*storage, config).incomplete_tail_bytes;
    }
    const auto original_hash = sha256_file(directory / "torn-original.journal");
    bool rejected = false;
    try { static_cast<void>(JournaledRuntime::recover(open_journal_file(directory / "torn-original.journal"), config)); }
    catch (const JournalError&) { rejected = true; }
    check("Ordinary recovery refuses an incomplete tail", rejected);
    {
        auto runtime = JournaledRuntime::recover_prefix(open_journal_file(directory / "torn-original.journal"),
            create_journal_file(directory / "prefix-recovered.journal"), config);
        check("Original torn file retained byte for byte", sha256_file(directory / "torn-original.journal") == original_hash);
        check("Verified prefix restores partial state and gate", runtime.account().cash_units == 959996 &&
              runtime.account().reserved_cash_units == 60060 && !runtime.account().connected);
        observe("Incomplete final write", "Separate prefix recovery", runtime);
        check("Prefix retains fill deduplication", runtime.execute(fill) == Outcome::duplicate);
        check("Prefix reconnect recorded", runtime.execute(ConnectionInput{true}) == Outcome::applied);
        check("Prefix snapshot reconciled", runtime.execute(BrokerSnapshot{959996, 4, {{request, 4}}}) == Outcome::applied);
        check("Recovered cancel intent recorded", runtime.execute(CancelInput{"B", "C"}) == Outcome::applied);
        check("Recovered cancellation releases only remaining resources",
              runtime.execute(BrokerReport{"CA", "B", ReportKind::canceled, "", 0, 0, 0, 4, "C"}) == Outcome::applied);
        check("Canceled partial account retains executed shares", runtime.account().cash_units == 959996 &&
              runtime.account().position_shares == 4 && runtime.account().reserved_cash_units == 0 && runtime.account().open_orders == 0);
        observe("Incomplete final write", "Reconciled and canceled", runtime);
    }
    auto corrupt = read_text_file(directory / "restart.journal");
    corrupt[5] = corrupt[5] == 'x' ? 'y' : 'x';
    write_new_text_file(directory / "corrupt.journal", corrupt);
    rejected = false;
    try { static_cast<void>(JournaledRuntime::recover(open_journal_file(directory / "corrupt.journal"), config)); }
    catch (const JournalError&) { rejected = true; }
    check("Complete-frame corruption rejected", rejected);
    check("Corrupt evidence retained without modification", read_text_file(directory / "corrupt.journal") == corrupt);
    return result;
}

} // namespace trading::execution
