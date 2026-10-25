#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "execution_diagnostic.hpp"
#include "journaled_runtime.hpp"
#include "runtime_json.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
using namespace trading::execution;
const RuntimeConfiguration config{"SPY", 1000000, {20, 4}};
const OrderRequest request{"B", "SPY", Side::buy, 10, 10000, 10};
const BrokerReport first_fill{"F1", "B", ReportKind::fill, "X1", 4, 10000, 4, 0, ""};
void require(bool pass, std::string_view message) { if (!pass) { throw std::runtime_error(std::string{message}); } }
template<class F> void rejects(F operation) {
    bool rejected = false;
    try { operation(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "operation must fail closed");
}
void connect(JournaledRuntime& runtime, Shares filled = 0) {
    require(runtime.execute(ConnectionInput{true}) == Outcome::applied, "reconnect recorded");
    BrokerSnapshot snapshot{1000000 - filled * 10001, filled, {}};
    if (filled != 0) { snapshot.open_orders.push_back({request, filled}); }
    require(runtime.execute(snapshot) == Outcome::applied, "matching snapshot restores entry");
}
enum class Fault { none, before, partial, after, crash_before, crash_partial, crash_after };
struct FaultControl { Fault mode{Fault::none}; };
class FaultStorage final : public JournalStorage {
public:
    FaultStorage(std::unique_ptr<JournalStorage> storage, FaultControl& control)
        : storage_{std::move(storage)}, control_{control} {}
    std::string read_all() override { return storage_->read_all(); }
    void append_and_sync(std::string_view bytes) override {
        const auto mode = control_.mode;
        if (mode == Fault::before) { throw std::runtime_error("injected failure before append"); }
        if (mode == Fault::crash_before) { std::_Exit(0); }
        if (mode == Fault::partial || mode == Fault::crash_partial) {
            storage_->append_and_sync(bytes.substr(0, bytes.size() / 2));
            if (mode == Fault::crash_partial) { std::_Exit(0); }
            throw std::runtime_error("injected failure after partial append");
        }
        storage_->append_and_sync(bytes);
        if (mode == Fault::crash_after) { std::_Exit(0); }
        if (mode == Fault::after) { throw std::runtime_error("injected failure after synchronization"); }
    }
private:
    std::unique_ptr<JournalStorage> storage_;
    FaultControl& control_;
};

void test_restart_and_dedup(const std::filesystem::path& root) {
    const auto file = root / "restart.journal";
    std::vector<TraceEntry> original;
    {
        auto runtime = JournaledRuntime::create(create_journal_file(file), config);
        require(!runtime.account().connected && runtime.account().reconciliation_required, "new writer starts gated");
        require(runtime.execute(SubmitInput{request}) == Outcome::denied, "no dispatch before first reconciliation");
        connect(runtime);
        require(runtime.execute(SubmitInput{request}) == Outcome::applied, "submitted order durably recorded");
        require(runtime.execute(first_fill) == Outcome::applied, "partial fill booked");
        require(runtime.execute(CancelInput{"B", "C"}) == Outcome::applied, "cancel intent recorded");
        original = runtime.trace();
        rejects([&] { static_cast<void>(open_journal_file(file)); });
        rejects([&] { static_cast<void>(create_journal_file(file)); });
    }
    {
        auto runtime = JournaledRuntime::recover(open_journal_file(file), config);
        require(runtime.trace().size() == original.size() + 1, "restart adds a durable disconnect barrier");
        require(std::equal(original.begin(), original.end(), runtime.trace().begin()), "entire pre-restart trace reproduced");
        require(runtime.order("B").pending_cancel_id == "C", "pending cancel identity restored");
        require(runtime.account().cash_units == 959996 && runtime.account().reserved_cash_units == 60060, "partial cash and reservation restored");
        auto duplicate = first_fill; duplicate.event_id = "F1-copy";
        require(runtime.execute(duplicate) == Outcome::duplicate, "execution deduplication survives restart");
        require(runtime.execute(first_fill) == Outcome::duplicate, "delivery deduplication survives restart");
        require(runtime.execute(ConnectionInput{true}) == Outcome::applied, "connection restored");
        require(runtime.execute(SubmitInput{{"blocked", "SPY", Side::buy, 1, 10000, 10}}) == Outcome::denied, "reconnect alone cannot dispatch");
        require(runtime.execute(BrokerSnapshot{959996, 4, {{request, 4}}}) == Outcome::applied, "exact recovered snapshot");
        require(runtime.execute(BrokerReport{"CR", "B", ReportKind::cancel_rejected, "", 0, 0, 0, 0, "C"}) == Outcome::applied,
                "restored cancellation correlation handles delayed response");
        require(runtime.execute(BrokerReport{"F2", "B", ReportKind::fill, "X2", 6, 10000, 6, 0, ""}) == Outcome::applied, "remaining fill");
        require(runtime.account().cash_units == 899990 && runtime.account().position_shares == 10, "independent final cash identity");
    }
    auto twice = JournaledRuntime::recover(open_journal_file(file), config);
    require(twice.account().cash_units == 899990 && twice.account().reconciliation_required, "second restart retains fills and closes gate again");
}

void test_uncertain_writes(const std::filesystem::path& root) {
    for (const auto mode : {Fault::before, Fault::partial, Fault::after}) {
        const auto file = root / ("fault-" + std::to_string(static_cast<int>(mode)) + ".journal");
        FaultControl control;
        {
            auto runtime = JournaledRuntime::create(std::make_unique<FaultStorage>(create_journal_file(file), control), config);
            connect(runtime);
            require(runtime.execute(SubmitInput{request}) == Outcome::applied, "fault fixture order");
            control.mode = mode;
            rejects([&] { static_cast<void>(runtime.execute(first_fill)); });
            require(!runtime.healthy(), "uncertain writer is poisoned");
            rejects([&] { static_cast<void>(runtime.account()); });
            rejects([&] { static_cast<void>(runtime.execute(first_fill)); });
        }
        const auto digest = trading::sha256_file(file);
        if (mode == Fault::partial) {
            rejects([&] { static_cast<void>(JournaledRuntime::recover(open_journal_file(file), config)); });
            auto recovered = JournaledRuntime::recover_prefix(open_journal_file(file), create_journal_file(root / "prefix.journal"), config);
            require(recovered.account().position_shares == 0 && recovered.account().reserved_cash_units == 100100, "partial frame cannot become a fill");
            require(trading::sha256_file(file) == digest, "prefix recovery retains original bytes");
        } else {
            auto recovered = JournaledRuntime::recover(open_journal_file(file), config);
            require(recovered.account().position_shares == (mode == Fault::after ? 4 : 0), "flush boundary determines recovered ledger");
        }
    }
}

void test_uncertain_order_intent(const std::filesystem::path& root) {
    const auto file = root / "uncertain-intent.journal";
    FaultControl control;
    {
        auto runtime = JournaledRuntime::create(std::make_unique<FaultStorage>(create_journal_file(file), control), config);
        connect(runtime);
        control.mode = Fault::after;
        rejects([&] { static_cast<void>(runtime.execute(SubmitInput{request})); });
        require(!runtime.healthy(), "no dispatch acknowledgement returned after uncertain intent commit");
    }
    auto recovered = JournaledRuntime::recover(open_journal_file(file), config);
    require(recovered.order("B").status == OrderStatus::pending_new && recovered.account().reserved_cash_units == 100100,
            "durable but unacknowledged intent survives with its full reservation");
    require(recovered.execute(ConnectionInput{true}) == Outcome::applied, "intent fixture reconnect");
    require(recovered.execute(BrokerSnapshot{1000000, 0, {{request, 0}}}) == Outcome::applied, "broker confirms existing intent");
    require(recovered.execute(SubmitInput{request}) == Outcome::denied, "existing durable order ID cannot dispatch a second order");
}

void test_all_delivery_schedules(const std::filesystem::path& root) {
    std::size_t index = 0;
    for (const auto& result : ExecutionDiagnostic{}.run()) {
        const auto file = root / ("schedule-" + std::to_string(index++) + ".journal");
        std::vector<TraceEntry> original;
        {
            auto runtime = JournaledRuntime::create(create_journal_file(file), config);
            connect(runtime);
            for (const auto& step : result.scenario.steps) {
                require(runtime.execute(step.input) == step.expected_outcome, "durable wrapper preserves every original scenario outcome");
            }
            original = runtime.trace();
        }
        auto recovered = JournaledRuntime::recover(open_journal_file(file), config);
        require(recovered.trace().size() == original.size() + 1 &&
                std::equal(original.begin(), original.end(), recovered.trace().begin()), "every original scenario replays through the core exactly");
        require(recovered.account().cash_units == result.final_account.cash_units &&
                recovered.account().position_shares == result.final_account.position_shares, "all scenario final ledgers survive restart");
    }
}

void test_integrity_and_codec(const std::filesystem::path& root) {
    const auto source = root / "integrity.journal";
    {
        auto runtime = JournaledRuntime::create(create_journal_file(source), config);
        connect(runtime);
        require(runtime.execute(SubmitInput{request}) == Outcome::applied, "integrity fixture submit");
    }
    const auto good = trading::read_text_file(source);
    auto check_bad = [&](std::string data, const std::string& name) {
        const auto file = root / (name + ".journal");
        trading::write_new_text_file(file, data);
        rejects([&] { static_cast<void>(JournaledRuntime::recover(open_journal_file(file), config)); });
        rejects([&] { static_cast<void>(JournaledRuntime::recover_prefix(open_journal_file(file),
            create_journal_file(root / (name + "-prefix.journal")), config)); });
        require(trading::read_text_file(file) == data, "corruption rejection preserves source evidence");
    };
    auto damaged = good; damaged[5] = damaged[5] == 'x' ? 'y' : 'x'; check_bad(damaged, "checksum");
    const auto header_end = good.find('\n') + 1;
    const auto first_end = good.find('\n', header_end) + 1;
    check_bad(good.substr(0, header_end) + good.substr(first_end), "missing-record");
    check_bad(good + good.substr(header_end, first_end - header_end), "repeated-record");
    check_bad(good.substr(0, 20), "incomplete-header");
    auto last_start = good.rfind('\n', good.size() - 2) + 1;
    const auto tab = good.rfind('\t');
    auto value = nlohmann::json::parse(good.substr(last_start, tab - last_start));
    value["trace"]["account"]["cash_cents"] = 1;
    const auto payload = value.dump();
    check_bad(good.substr(0, last_start) + payload + '\t' + trading::sha256(payload) + '\n', "checkpoint");
    auto wrong = config; wrong.symbol = "QQQ";
    const auto original = trading::sha256_file(source);
    rejects([&] { static_cast<void>(JournaledRuntime::recover(open_journal_file(source), wrong)); });
    require(trading::sha256_file(source) == original, "configuration mismatch cannot append a barrier");
    auto input = input_json(SubmitInput{request});
    require(input_json(parse_runtime_input(input)) == input, "typed input round trip");
    input["order"]["shares"] = 1.5;
    bool invalid = false;
    try { static_cast<void>(parse_runtime_input(input)); } catch (const std::invalid_argument&) { invalid = true; }
    require(invalid, "fractional JSON quantities cannot be truncated to whole shares");
}

void crash_child(const std::filesystem::path& root, const std::string& phase) {
    FaultControl control;
    auto runtime = JournaledRuntime::create(std::make_unique<FaultStorage>(create_journal_file(root / "crash.journal"), control), config);
    connect(runtime);
    require(runtime.execute(SubmitInput{request}) == Outcome::applied, "crash fixture submit");
    if (phase == "before_append") { control.mode = Fault::crash_before; }
    else if (phase == "after_sync") { control.mode = Fault::crash_after; }
    else if (phase == "torn_tail") { control.mode = Fault::crash_partial; }
    else if (phase != "after_return") { throw std::invalid_argument("unknown crash phase"); }
    static_cast<void>(runtime.execute(first_fill));
    std::_Exit(0); // Deliberately bypass all C++ destructors and stream cleanup.
}
void verify_crash(const std::filesystem::path& root, const std::string& phase) {
    const auto source = root / "crash.journal";
    const auto before = trading::sha256_file(source);
    auto recovered = phase == "torn_tail"
        ? JournaledRuntime::recover_prefix(open_journal_file(source), create_journal_file(root / "recovered.journal"), config)
        : JournaledRuntime::recover(open_journal_file(source), config);
    const bool booked = phase == "after_sync" || phase == "after_return";
    require(recovered.account().position_shares == (booked ? 4 : 0), "abrupt process restart restores the durable prefix");
    require(!recovered.account().connected && recovered.account().reconciliation_required, "abrupt restart cannot dispatch");
    require(recovered.execute(first_fill) == (booked ? Outcome::duplicate : Outcome::applied), "redelivery after crash books execution exactly once");
    require(recovered.account().cash_units == 959996 && recovered.account().position_shares == 4, "crash recovery integer identity");
    if (phase == "torn_tail") { require(trading::sha256_file(source) == before, "crash tail evidence retained"); }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view{argv[1]} == "--crash") { crash_child(argv[2], argv[3]); }
        else if (argc == 4 && std::string_view{argv[1]} == "--verify-crash") { verify_crash(argv[2], argv[3]); }
        else if (argc == 2) {
            const auto root = trading::create_unique_directory(argv[1], "journal-contracts");
            test_restart_and_dedup(root);
            test_uncertain_writes(root);
            test_uncertain_order_intent(root);
            test_all_delivery_schedules(root);
            test_integrity_and_codec(root);
        } else { throw std::invalid_argument("journal contracts require an output directory or crash fixture mode"); }
        std::cout << "Journal recovery contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Journal recovery contract failure: " << error.what() << '\n';
        return 1;
    }
}
