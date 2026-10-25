#include "paper_execution_diagnostic.hpp"
#include "artifact_io.hpp"

namespace trading::execution {
namespace {
using Json = nlohmann::json;
const std::string account_id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
const std::string buy_id = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
const std::string sell_id = "cccccccc-cccc-4ccc-8ccc-cccccccccccc";
const std::string receipt = "2026-10-26T14:00:00.999999999Z";
const PaperExecutionConfiguration config{account_id, "2000", {20, 4}};

void check(PaperBridgeResult &r, std::string label, bool passed) {
    if (!passed)
        throw std::logic_error("paper bridge diagnostic: " + label);
    r.checks.push_back(std::move(label));
}

Json order(bool sell, int cumulative, std::string status) {
    return {{"id", sell ? sell_id : buy_id},
            {"client_order_id", sell ? "MR1-demo-sell" : "MR1-demo-buy"},
            {"symbol", "SPY"},
            {"asset_class", "us_equity"},
            {"side", sell ? "sell" : "buy"},
            {"type", "limit"},
            {"order_class", "simple"},
            {"time_in_force", "day"},
            {"extended_hours", false},
            {"qty", sell ? "7" : "10"},
            {"filled_qty", std::to_string(cumulative)},
            {"filled_avg_price", cumulative ? Json("100.005833333") : Json(nullptr)},
            {"limit_price", sell ? "100" : "100.01"},
            {"status", std::move(status)},
            {"updated_at", "2026-10-26T14:00:00.123456789Z"}};
}

Json fill(bool sell, int amount, int cumulative, int position, std::string price, int index) {
    auto id = "dddddddd-dddd-4ddd-8ddd-ddddddddddd" + std::to_string(index);
    return {{"stream", "trade_updates"},
            {"data",
             {{"event", cumulative == (sell ? 7 : 10) ? "fill" : "partial_fill"},
              {"execution_id", id},
              {"qty", std::to_string(amount)},
              {"price", std::move(price)},
              {"position_qty", std::to_string(position)},
              {"timestamp", "2026-10-26T14:00:00.123456789Z"},
              {"order",
               order(sell, cumulative, cumulative == (sell ? 7 : 10) ? "filled" : "partially_filled")}}}};
}

std::string account(std::string cash) {
    return Json{{"id", account_id},   {"cash", std::move(cash)},  {"currency", "USD"},
                {"status", "ACTIVE"}, {"account_blocked", false}, {"trading_blocked", false}}
        .dump();
}

std::string positions(int shares) {
    Json p = Json::array();
    if (shares)
        p.push_back({{"symbol", "SPY"},
                     {"qty", std::to_string(shares)},
                     {"asset_class", "us_equity"},
                     {"side", "long"},
                     {"avg_entry_price", "100.005"}});
    return p.dump();
}

void authenticate(JournaledPaperRuntime &rt) {
    static_cast<void>(rt.receive(R"({"stream":"authorization","data":{"status":"authorized"}})", receipt));
    static_cast<void>(rt.receive(R"({"stream":"listening","data":{"streams":["trade_updates"]}})", receipt));
}

Json path(const std::filesystem::path &directory, PaperBridgeResult &result, bool interrupted) {
    const auto name = interrupted ? "restarted" : "continuous";
    const auto file = directory / (std::string{name} + ".journal");
    auto rt = JournaledPaperRuntime::create(create_journal_file(file), config);
    auto mark = [&](std::string label) {
        auto state = rt.snapshot();
        result.frames.push_back({{"case", name}, {"label", std::move(label)}, {"state", state}});
    };
    mark("Fresh journal: routing closed");
    authenticate(rt);
    check(result, std::string{name} + ": initial exact reconciliation",
          rt.reconcile(account("2000"), "[]", "[]").outcome == Outcome::applied);
    const OrderRequest buy{"MR1-demo-buy", "SPY", Side::buy, 10, 10001, 0};
    check(result, std::string{name} + ": durable buy reservation",
          rt.submit(buy).outcome == Outcome::applied);
    mark("Buy intent synchronized before dispatch eligibility");
    check(result, std::string{name} + ": stable REST identity",
          rt.observe_order(order(false, 0, "new").dump()).outcome == Outcome::applied);
    const auto first = fill(false, 4, 4, 4, "100.005", 1);
    check(result, std::string{name} + ": first subcent fill",
          rt.receive(first.dump(), receipt).outcome == Outcome::applied);
    mark("Four shares executed");
    if (interrupted) {
        // Release exclusive writer ownership before opening the same retained file.
        {
            auto old = std::move(rt);
            static_cast<void>(old);
        }
        rt = JournaledPaperRuntime::recover(open_journal_file(file), config);
        mark("Restart: identities restored; routing closed");
        check(result, "recovered intent cannot be resent", rt.submit(buy).outcome == Outcome::denied);
        authenticate(rt);
        check(result, "restart exact open-order reconciliation",
              rt.reconcile(account("1599.98"), positions(4),
                           Json::array({order(false, 4, "partially_filled")}).dump())
                      .outcome == Outcome::applied);
    }
    check(result, std::string{name} + ": second incremental fill",
          rt.receive(fill(false, 2, 6, 6, "100.0075", 2).dump(), receipt).outcome == Outcome::applied);
    mark("Six shares; exact cash retains half-cent");
    check(result, std::string{name} + ": cash precision retained",
          rt.snapshot().at("account").at("cash_units").get<std::int64_t>() == 1399965000000LL);
    auto duplicate = first;
    duplicate["data"]["event_id"] = "redelivery-1";
    check(result, std::string{name} + ": duplicate execution with new event identity",
          rt.receive(duplicate.dump(), receipt).outcome == Outcome::duplicate);
    check(result, std::string{name} + ": mismatch does not overwrite cash",
          rt.reconcile(account("1399.96"), positions(6),
                       Json::array({order(false, 6, "partially_filled")}).dump())
                  .outcome == Outcome::quarantined);
    check(result, std::string{name} + ": mismatch retains exact amount",
          rt.snapshot().at("account").at("cash_units").get<std::int64_t>() == 1399965000000LL);
    check(result, std::string{name} + ": matching snapshot resolves temporary mismatch",
          rt.reconcile(account("1399.965"), positions(6),
                       Json::array({order(false, 6, "partially_filled")}).dump())
                  .outcome == Outcome::applied);
    check(result, std::string{name} + ": cancel reservation retained",
          rt.request_cancel(buy.id, "cancel-demo").outcome == Outcome::applied);
    mark("Cancellation requested; four shares still reserved");
    check(result, std::string{name} + ": fill during cancel",
          rt.receive(fill(false, 1, 7, 7, "100.005", 3).dump(), receipt).outcome == Outcome::applied);
    const Json canceled{
        {"stream", "trade_updates"},
        {"data", {{"event", "canceled"}, {"timestamp", receipt}, {"order", order(false, 7, "canceled")}}}};
    check(result, std::string{name} + ": terminal cancel cumulative match",
          rt.receive(canceled.dump(), receipt).outcome == Outcome::applied);
    mark("Cancellation confirmed after seven total shares");
    check(result, std::string{name} + ": sell actual inventory",
          rt.submit({"MR1-demo-sell", "SPY", Side::sell, 7, 10000, 0}).outcome == Outcome::applied);
    check(result, std::string{name} + ": partial sale",
          rt.receive(fill(true, 3, 3, 4, "100.0025", 4).dump(), receipt).outcome == Outcome::applied);
    mark("Three sold; four shares reserved");
    check(result, std::string{name} + ": final sale",
          rt.receive(fill(true, 4, 7, 0, "100.005", 5).dump(), receipt).outcome == Outcome::applied);
    check(result, std::string{name} + ": final exact reconciliation",
          rt.reconcile(account("1999.9875"), "[]", "[]").outcome == Outcome::applied);
    mark("Flat final account under declared paper fee convention");
    check(result, std::string{name} + ": final exact cash",
          rt.snapshot().at("account").at("cash_units").get<std::int64_t>() == 1999987500000LL);
    auto snapshot = rt.snapshot();
    snapshot.erase("journal_sha256");
    snapshot.erase("durable_records");
    return snapshot;
}
} // namespace

PaperBridgeResult PaperExecutionDiagnostic::run() const {
    PaperBridgeResult result{};
    const auto a = path(directory_, result, false), b = path(directory_, result, true);
    check(result, "paired interrupted/uninterrupted state parity", a == b);
    // Separate invalid-evidence case cannot contaminate either paired path.
    auto bad = JournaledPaperRuntime::create(create_journal_file(directory_ / "quarantine.journal"), config);
    authenticate(bad);
    static_cast<void>(bad.reconcile(account("2000"), "[]", "[]"));
    static_cast<void>(bad.submit({"MR1-demo-buy", "SPY", Side::buy, 10, 10001, 0}));
    auto unsupported = fill(false, 4, 4, 4, "100.0000000001", 1);
    check(result, "precision overflow is quarantined without rounding",
          bad.receive(unsupported.dump(), receipt).outcome == Outcome::quarantined);
    check(result, "invalid fill leaves cash/position unchanged",
          bad.snapshot().at("account").at("cash_units").get<std::int64_t>() == 2000000000000LL &&
              bad.snapshot().at("account").at("position_shares").get<int>() == 0);
    check(result, "evidence quarantine blocks new orders",
          bad.submit({"another-order", "SPY", Side::buy, 1, 10000, 0}).outcome == Outcome::denied);
    result.frames.push_back({{"case", "quarantine"},
                             {"label", "Unrepresentable fill retained; routing closed"},
                             {"state", bad.snapshot()}});
    return result;
}
} // namespace trading::execution
