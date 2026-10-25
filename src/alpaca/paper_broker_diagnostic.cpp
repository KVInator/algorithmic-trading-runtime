#include "paper_broker_diagnostic.hpp"
#include "simulated_paper_wire.hpp"
#include "paper_control.hpp"
#include <memory>

namespace trading {
namespace {
using Json = nlohmann::json;
using namespace execution;
const PaperExecutionConfiguration config{"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "2000", {20, 4}};
const OrderRequest buy{"demo-buy", "SPY", Side::buy, 10, 10001, 0};
void check(PaperTransportResult &result, std::string label, bool ok) {
    if (!ok)
        throw std::logic_error("simulated paper broker: " + label);
    result.checks.push_back(std::move(label));
}
template <class F> bool fails(F action) {
    try {
        action();
        return false;
    } catch (const std::exception &) {
        return true;
    }
}
class Scenario {
  public:
    Scenario(const std::filesystem::path &root, std::string name)
        : directory_{root / name}, name_{std::move(name)}, capture_{directory_ / "capture"},
          wire_{config.account_id, 200000} {
        runtime_ = std::make_unique<JournaledPaperRuntime>(
            JournaledPaperRuntime::create(create_journal_file(directory_ / "runtime.journal"), config));
        attach();
        session().connect();
        if (!session().synchronize())
            throw std::logic_error("initial simulated snapshot mismatch");
        frame("Connected and reconciled");
    }
    PaperSession &session() { return *session_; }
    JournaledPaperRuntime &runtime() { return *runtime_; }
    SimulatedPaperWire &wire() { return wire_; }
    PaperReceipt now() { return {"2023-01-03T14:31:00Z", ++tick_}; }
    void frame(std::string label) {
        frames_.push_back(
            {{"event", std::move(label)}, {"state", runtime().snapshot()}, {"broker", wire_.snapshot()}});
    }
    void restart() {
        session().disconnect();
        session_.reset();
        runtime_.reset();
        runtime_ = std::make_unique<JournaledPaperRuntime>(
            JournaledPaperRuntime::recover(open_journal_file(directory_ / "runtime.journal"), config));
        attach();
        frame("Restart: durable ledger retained, routing closed");
        session().connect();
    }
    Json result() {
        return {{"case", name_},
                {"state", runtime().snapshot()},
                {"broker", wire_.snapshot()},
                {"trade_history", wire_.trade_history()},
                {"frames", frames_},
                {"capture_records", capture_.records()}};
    }

  private:
    void attach() {
        session_ =
            std::make_unique<PaperSession>(*runtime_, wire_, capture_, [this] { return now(); }, [](auto) {});
    }
    std::filesystem::path directory_;
    std::string name_;
    PaperCapture capture_;
    SimulatedPaperWire wire_;
    std::unique_ptr<JournaledPaperRuntime> runtime_;
    std::unique_ptr<PaperSession> session_;
    Json frames_ = Json::array();
    std::int64_t tick_{0};
};
Json ledger(Json state) {
    state.erase("durable_records");
    state.erase("journal_sha256");
    return state;
}
Json round_trip(const std::filesystem::path &root, bool restart, PaperTransportResult &result) {
    Scenario f{root, restart ? "interrupted" : "uninterrupted"};
    f.wire().on_quote({1, 1, 9999, 10000, 10, 4});
    check(result, "buy accepted through request adapter", f.session().submit(buy).status == 200);
    static_cast<void>(f.session().drain());
    check(result, "post-response fill evidence reconciles before another dispatch",
          f.session().synchronize());
    f.frame("Buy acknowledged; four of ten shares filled at USD 100.00");
    check(result, "partial fill consumes only four shares of quote liquidity",
          f.runtime().snapshot().at("account").at("position_shares").get<int>() == 4);
    const auto before_quote_duplicate = f.wire().trade_history().size();
    f.wire().on_quote({1, 1, 9999, 10000, 10, 4});
    check(result, "duplicate quote does not replenish displayed liquidity",
          f.wire().trade_history().size() == before_quote_duplicate);
    f.wire().duplicate_last_update();
    static_cast<void>(f.session().drain());
    check(result, "duplicate fill preserves cash",
          f.runtime().snapshot().at("account").at("cash_units").get<std::int64_t>() == 1600000000000LL);
    check(result, "cancel accepted without releasing remaining reservation",
          f.session().cancel(buy.id, "demo-cancel").status == 204 &&
              f.runtime().snapshot().at("account").at("reserved_cash_units").get<std::int64_t>() ==
                  600060000000LL);
    f.frame("Cancellation requested; remainder remains reserved");
    if (restart) {
        f.session().disconnect();
        f.frame("Disconnected before cancellation resolution");
    }
    f.wire().on_quote({2, 2, 10000, 10001, 10, 6});
    f.wire().complete_cancels();
    if (restart) {
        f.restart();
        check(result, "restart cannot authorize routing",
              f.runtime().snapshot().at("account").at("reconciliation_required").get<bool>());
        check(result, "snapshot alone cannot invent missing executions", !f.session().synchronize());
        f.frame("Broker is ahead: reconciliation refuses missing fill evidence");
        f.session().connect();
        f.wire().replay_trade_history(); // Explicit fixture archive replay, never an assumed service feature.
    }
    static_cast<void>(f.session().drain());
    check(result, "fill wins cancellation race",
          f.runtime().snapshot().at("account").at("position_shares").get<int>() == 10 &&
              f.runtime().snapshot().at("orders").at(0).at("status").get<std::string>() == "filled");
    check(result, "broker and runtime reconcile after history delivery", f.session().synchronize());
    f.frame("Final buy fill precedes cancellation rejection; accounts reconcile");
    f.wire().on_quote({3, 3, 10003, 10004, 10, 10});
    check(result, "sell accepted through same adapter",
          f.session().submit({"demo-sell", "SPY", Side::sell, 10, 10002, 0}).status == 200);
    static_cast<void>(f.session().drain());
    check(result, "round trip ends flat with exact USD 0.24 constructed gain",
          f.session().synchronize() &&
              f.runtime().snapshot().at("account").at("cash_units").get<std::int64_t>() == 2000240000000LL &&
              f.runtime().snapshot().at("account").at("position_shares").get<int>() == 0);
    check(result, "recovery never resubmits an existing intent", f.wire().submissions() == 2);
    f.frame("Sell filled; flat account and all reservations reconciled");
    auto out = f.result();
    result.cases.push_back(out);
    return ledger(out.at("state"));
}
} // namespace
PaperTransportResult PaperBrokerDiagnostic::run() const {
    PaperTransportResult result;
    result.detail = "Synthetic quotes and simulated broker messages; no network, credentials or market-data "
                    "subscription. No observed broker executions.";
    const auto baseline = round_trip(directory_, false, result);
    const auto recovered = round_trip(directory_, true, result);
    check(result, "interrupted and uninterrupted final ledgers and identity maps agree",
          baseline == recovered);
    {
        Scenario f{directory_, "partial_cancel"};
        f.wire().on_quote({1, 1, 9999, 10000, 10, 4});
        static_cast<void>(f.session().submit(buy));
        static_cast<void>(f.session().drain());
        check(result, "partial-fill response reconciles before cancellation", f.session().synchronize());
        f.frame("Four shares filled; six remain");
        static_cast<void>(f.session().cancel(buy.id, "cancel-remainder"));
        f.frame("Cancel request retains reserved cash");
        f.wire().complete_cancels();
        static_cast<void>(f.session().drain());
        check(result, "confirmed partial cancellation releases only remainder",
              f.session().synchronize() &&
                  f.runtime().snapshot().at("account").at("position_shares").get<int>() == 4 &&
                  f.runtime().snapshot().at("account").at("reserved_cash_units").get<int>() == 0);
        f.frame("Cancellation confirmed; four-share inventory remains");
        result.cases.push_back(f.result());
    }
    {
        Scenario f{directory_, "uncertain_submission"};
        f.wire().on_quote({1, 1, 10002, 10003, 10, 10});
        f.wire().lose_next_submit_response();
        check(result, "lost POST response closes routing and retains reservation",
              fails([&] { static_cast<void>(f.session().submit(buy)); }) &&
                  !f.runtime().snapshot().at("account").at("connected").get<bool>());
        f.frame("Broker accepted order; response lost before client received UUID");
        f.restart();
        check(result, "stable-ID lookup resolves accepted uncertain order",
              f.session().lookup(buy.id).status == 200 && f.session().synchronize());
        check(result, "lookup and restart sent only one submission", f.wire().submissions() == 1);
        f.frame("Lookup binds original order; exact reconciliation reopens routing");
        static_cast<void>(f.session().cancel(buy.id, "cancel-uncertain"));
        f.wire().complete_cancels();
        static_cast<void>(f.session().drain());
        check(result, "uncertain order cancels without cash or position change",
              f.session().synchronize() &&
                  f.runtime().snapshot().at("account").at("cash_units").get<std::int64_t>() ==
                      2000000000000LL);
        f.frame("Original order canceled; no duplicate submission");
        result.cases.push_back(f.result());
    }
    {
        Scenario f{directory_, "snapshot_mismatch"};
        auto broker = f.wire().snapshot();
        broker["account"]["cash"] = "2000.01";
        check(
            result, "contradictory broker cash closes routing without overwriting ledger",
            f.runtime().reconcile(broker.at("account").dump(), broker.at("positions").dump(), "[]").outcome !=
                    Outcome::applied &&
                f.runtime().snapshot().at("account").at("reconciliation_required").get<bool>() &&
                f.runtime().snapshot().at("account").at("cash_units").get<std::int64_t>() == 2000000000000LL);
        f.frame("One-cent snapshot contradiction; local cash preserved");
        check(result, "valid independent snapshot clears reconciliation gate", f.session().synchronize());
        f.frame("Matching broker evidence restores routing");
        result.cases.push_back(f.result());
    }
    {
        Scenario f{directory_, "deadline_stop"};
        PaperReceipt clock{"2023-01-03T14:31:00Z", 100};
        auto control =
            PaperControl::create(create_journal_file(directory_ / "deadline_stop/control.journal"),
                                 {"simulated-broker"}, f.runtime(), f.session(), [&] { return clock; });
        f.wire().on_quote({1, 1, 10002, 10003, 10, 10});
        static_cast<void>(control.submit(buy, "2023-01-03T14:31:01Z"));
        clock = {"2023-01-03T14:31:01Z", 1100};
        control.service();
        f.frame("Durable deadline requests cancellation");
        check(result, "deadline does not pretend cancellation already completed",
              f.runtime().snapshot().at("account").at("reserved_cash_units").get<std::int64_t>() ==
                  1000100000000LL);
        f.wire().complete_cancels();
        control.service();
        control.stop("scenario_complete");
        check(result, "permanent stop prevents new submission", fails([&] {
                  static_cast<void>(
                      control.submit({"later", "SPY", Side::buy, 1, 10001, 0}, "2023-01-03T14:32:00Z"));
              }));
        f.frame("Cancellation confirmed; durable stop rejects further orders");
        auto out = f.result();
        out["control"] = control.snapshot();
        result.cases.push_back(std::move(out));
    }
    return result;
}
} // namespace trading
