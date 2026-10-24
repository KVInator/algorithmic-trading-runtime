#include "paper_execution_report.hpp"
#include "journal_frame.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include <iostream>

namespace {
using namespace trading;
using namespace trading::execution;
using Json = nlohmann::json;
const PaperExecutionConfiguration config{"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "2000", {20, 4}};

void require(bool ok, std::string_view msg) {
    if (!ok)
        throw std::runtime_error(std::string{msg});
}

template <class F> void rejects(F f) {
    bool bad = false;
    try {
        f();
    } catch (const std::exception&) {
        bad = true;
    }
    require(bad, "operation must refuse unsupported or uncertain evidence");
}

struct Memory {
    std::string bytes;
    int fault = 0;
};

class Storage : public JournalStorage {
public:
    explicit Storage(std::shared_ptr<Memory> data) : data_{std::move(data)} {}

    std::string read_all() override { return data_->bytes; }

    void append_and_sync(std::string_view bytes) override {
        if (data_->fault == 1)
            throw JournalError("injected pre-write failure");
        data_->bytes.append(bytes.substr(0, data_->fault == 2 ? bytes.size() / 2 : bytes.size()));
        if (data_->fault)
            throw JournalError("injected uncertain append/sync");
    }

private:
    std::shared_ptr<Memory> data_;
};

void prime(JournaledPaperRuntime& r) {
    static_cast<void>(
        r.receive(R"({"stream":"authorization","data":{"status":"authorized"}})", "2026-10-26T14:00:00Z"));
    static_cast<void>(
        r.receive(R"({"stream":"listening","data":{"streams":["trade_updates"]}})", "2026-10-26T14:00:00Z"));
    require(
        r.reconcile(
             R"({"id":"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa","cash":"2000","currency":"USD","status":"ACTIVE","account_blocked":false,"trading_blocked":false})",
             "[]", "[]")
                .outcome == Outcome::applied,
        "initial snapshot gate");
}

void write_failures() {
    for (int mode = 1; mode <= 3; ++mode) {
        auto data = std::make_shared<Memory>();
        {
            auto r = JournaledPaperRuntime::create(std::make_unique<Storage>(data), config);
            prime(r);
            data->fault = mode;
            rejects(
                [&] { static_cast<void>(r.submit({"uncertain-order", "SPY", Side::buy, 10, 10001, 0})); });
            require(!r.healthy(), "uncertain append poisons writer");
            rejects([&] { static_cast<void>(r.snapshot()); });
        }
        data->fault = 0;
        const auto original = data->bytes;
        if (mode == 2) {
            rejects([&] {
                static_cast<void>(JournaledPaperRuntime::recover(std::make_unique<Storage>(data), config));
            });
            auto destination = std::make_shared<Memory>();
            auto recovered = JournaledPaperRuntime::recover_prefix(
                std::make_unique<Storage>(data), std::make_unique<Storage>(destination), config);
            require(data->bytes == original, "torn original is preserved");
            require(recovered.snapshot().at("account").at("reserved_cash_units").get<std::int64_t>() == 0,
                    "incomplete intent is not booked");
        } else {
            auto recovered = JournaledPaperRuntime::recover(std::make_unique<Storage>(data), config);
            require(recovered.snapshot().at("account").at("reserved_cash_units").get<std::int64_t>() ==
                        (mode == 3 ? 1000100000000LL : 0),
                    "durable-but-unacknowledged intent is retained once");
            require(!recovered.snapshot().at("account").at("connected").get<bool>(),
                    "restart cannot resend automatically");
        }
    }
}

void invalid_evidence(const std::filesystem::path& directory) {
    const auto bytes = read_text_file(directory / "continuous.journal");
    std::vector<Json> commands;
    std::size_t offset = 0;
    while (true) {
        const auto end = bytes.find('\n', offset);
        if (end == bytes.npos)
            break;
        auto [row, digest] = JournalFrame::decode(std::string_view{bytes}.substr(offset, end - offset));
        static_cast<void>(digest);
        if (row.contains("command"))
            commands.push_back(row.at("command"));
        offset = end + 1;
    }
    PaperExecutionState before{config};
    Json first_fill;
    for (const auto& command : commands) {
        if (command.at("kind").get<std::string>() == "stream") {
            const auto raw = Json::parse(command.at("raw").get<std::string>());
            if (raw.at("stream").get<std::string>() == "trade_updates") {
                first_fill = command;
                break;
            }
        }
        static_cast<void>(before.apply(command));
    }
    require(!first_fill.is_null(), "fixture retains a first fill");
    for (int mode = 0; mode < 8; ++mode) {
        auto state = before;
        auto command = first_fill;
        auto raw = Json::parse(command.at("raw").get<std::string>());
        if (mode == 0)
            raw["data"]["order"]["id"] = "ffffffff-ffff-4fff-8fff-ffffffffffff";
        if (mode == 1)
            raw["data"]["order"]["client_order_id"] = "unknown-order";
        if (mode == 2)
            raw["data"]["price"] = "100.02";
        if (mode == 3)
            raw["data"]["qty"] = "3.5";
        if (mode == 4)
            raw["data"]["order"]["filled_qty"] = "5";
        if (mode == 5)
            raw["data"]["position_qty"] = "5";
        if (mode == 6)
            raw["data"]["event"] = "trade_bust";
        if (mode == 7)
            raw["data"]["price"] = "100.0000000001";
        command["raw"] = raw.dump();
        const auto old = state.account();
        require(state.apply(command).outcome == Outcome::quarantined, "invalid broker evidence quarantines");
        const auto now = state.account();
        require(now.cash_units == old.cash_units && now.position_shares == old.position_shares &&
                    now.reserved_cash_units == old.reserved_cash_units,
                "invalid input rolls back ledger and reservations");
        require(state.snapshot().at("broker_bindings") == before.snapshot().at("broker_bindings"),
                "invalid identity cannot partially bind");
    }
    auto receipt_then_reject = before;
    auto broker_receipt = first_fill;
    auto receipt_raw = Json::parse(broker_receipt.at("raw").get<std::string>());
    receipt_raw["data"]["event"] = "accepted";
    receipt_raw["data"]["order"]["status"] = "accepted";
    receipt_raw["data"]["order"]["filled_qty"] = "0";
    receipt_raw["data"]["order"]["filled_avg_price"] = nullptr;
    broker_receipt["raw"] = receipt_raw.dump();
    require(receipt_then_reject.apply(broker_receipt).outcome == Outcome::applied,
            "broker receipt is retained before venue acceptance");
    receipt_raw["data"]["event"] = "rejected";
    receipt_raw["data"]["order"]["status"] = "rejected";
    broker_receipt["raw"] = receipt_raw.dump();
    require(receipt_then_reject.apply(broker_receipt).outcome == Outcome::applied &&
                receipt_then_reject.account().reserved_cash_units == 0,
            "venue rejection after broker receipt releases unfilled reservation");
    auto duplicate = before;
    require(duplicate.apply(first_fill).outcome == Outcome::applied, "first execution applied");
    auto late_ack = first_fill;
    auto ack = Json::parse(late_ack.at("raw").get<std::string>());
    ack["data"]["event"] = "new";
    ack["data"]["order"]["status"] = "new";
    ack["data"]["order"]["filled_qty"] = "0";
    ack["data"]["order"]["filled_avg_price"] = nullptr;
    late_ack["raw"] = ack.dump();
    require(duplicate.apply(late_ack).outcome == Outcome::applied && duplicate.account().position_shares == 4,
            "late acknowledgement cannot regress executed inventory");
    auto conflict = first_fill;
    auto raw = Json::parse(conflict.at("raw").get<std::string>());
    raw["data"]["price"] = "100.004";
    conflict["raw"] = raw.dump();
    require(duplicate.apply(conflict).outcome == Outcome::quarantined,
            "same execution with changed economics quarantines");
    auto expire = before;
    auto expiry = first_fill;
    raw = Json::parse(expiry.at("raw").get<std::string>());
    raw["data"] = {
        {"event", "expired"}, {"timestamp", "2026-10-26T20:00:00Z"}, {"order", raw["data"]["order"]}};
    raw["data"]["order"]["filled_qty"] = "0";
    raw["data"]["order"]["filled_avg_price"] = nullptr;
    raw["data"]["order"]["status"] = "expired";
    expiry["raw"] = raw.dump();
    require(expire.apply(expiry).outcome == Outcome::applied && expire.account().reserved_cash_units == 0,
            "expiry releases only matched cumulative remainder");
    auto invalid_config = config;
    invalid_config.fee_policy = "implicit_missing_fee_zero";
    rejects([&] { static_cast<void>(PaperExecutionState{invalid_config}); });
    invalid_config = config;
    invalid_config.initial_cash_usd = "2000.0000000001";
    rejects([&] { static_cast<void>(PaperExecutionState{invalid_config}); });
    PaperExecutionState finished{config};
    for (const auto& command : commands) {
        static_cast<void>(finished.apply(command));
    }
    auto late_reject = expiry;
    raw = Json::parse(expiry.at("raw").get<std::string>());
    raw["data"]["event"] = "order_cancel_rejected";
    raw["data"]["order"]["filled_qty"] = "7";
    raw["data"]["order"]["filled_avg_price"] = "100.005";
    raw["data"]["order"]["status"] = "canceled";
    late_reject["raw"] = raw.dump();
    require(finished.apply(late_reject).outcome == Outcome::obsolete &&
                finished.account().position_shares == 0,
            "late cancellation rejection cannot reopen a terminal order");
    // Complete corruption and forged checkpoints are rejected, even with a fresh frame checksum.
    auto corrupt = std::make_shared<Memory>();
    corrupt->bytes = bytes;
    corrupt->bytes[5] = 'X';
    rejects([&] {
        static_cast<void>(JournaledPaperRuntime::recover(std::make_unique<Storage>(corrupt), config));
    });
    const auto start = bytes.rfind('\n', bytes.size() - 2) + 1;
    auto [last, hash] = JournalFrame::decode(std::string_view{bytes}.substr(start, bytes.size() - start - 1));
    static_cast<void>(hash);
    last["account"]["cash_units"] = last.at("account").at("cash_units").get<std::int64_t>() + 1;
    corrupt->bytes = bytes.substr(0, start) + JournalFrame::encode(last).first;
    rejects([&] {
        static_cast<void>(JournaledPaperRuntime::recover(std::make_unique<Storage>(corrupt), config));
    });
    auto wrong = config;
    wrong.initial_cash_usd = "2001";
    corrupt->bytes = bytes;
    rejects([&] {
        static_cast<void>(JournaledPaperRuntime::recover(std::make_unique<Storage>(corrupt), wrong));
    });
    auto storage = std::make_unique<Storage>(corrupt);
    const RuntimeConfiguration legacy{"SPY", 200000, {20, 4}};
    rejects([&] { static_cast<void>(JournaledRuntime::recover(std::move(storage), legacy)); });
    auto legacy_data = std::make_shared<Memory>();
    {
        auto r = JournaledRuntime::create(std::make_unique<Storage>(legacy_data), legacy);
        static_cast<void>(r);
    }
    rejects([&] {
        static_cast<void>(JournaledPaperRuntime::recover(std::make_unique<Storage>(legacy_data), config));
    });
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::invalid_argument("paper execution tests require output root");
        const auto directory = create_unique_directory(argv[1], "paper-bridge");
        const auto result = PaperExecutionDiagnostic{directory}.run();
        PaperExecutionReport{result}.write(directory);
        invalid_evidence(directory);
        write_failures();
        const auto summary = Json::parse(read_text_file(directory / "summary.json"));
        for (const auto& a : summary.at("artifacts"))
            require(sha256_file(directory / a.at("path").get<std::string>()) ==
                        a.at("sha256").get<std::string>(),
                    "artifact hashes");
        std::cout
            << "Exact paper accounting, identity, replay, corruption and uncertain-write contracts passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Paper execution contracts failed: " << e.what() << '\n';
        return 1;
    }
}
