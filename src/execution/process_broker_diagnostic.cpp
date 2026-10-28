#include "process_broker_diagnostic.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "process_broker_peer.hpp"
#include "shared_broker_session.hpp"
#include "shared_capital_json.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <functional>
#include <optional>
#include <stdexcept>

namespace trading::execution {
namespace {
using Json = nlohmann::json;
constexpr MoneyUnits usd = 1000000000;
const std::int64_t opening = parse_utc_seconds("2023-01-03T14:30:00Z").time_since_epoch().count();
const std::int64_t tomorrow = parse_utc_seconds("2023-01-04T14:30:00Z").time_since_epoch().count();
const std::vector<std::string> symbols{"GLD", "IWM", "QQQ", "SPY", "TLT"};
Json session(std::string id = "begin", std::string date = "2023-01-03", std::int64_t at = opening) {
    return {{"kind", "session"}, {"command_id", id}, {"at_seconds", at}, {"date", date}};
}
Json quote(std::string id, std::string symbol, std::uint64_t sequence, std::int64_t at, Shares liquidity,
           MoneyUnits price = 100 * usd) {
    return {{"kind", "quote"},
            {"command_id", id},
            {"at_seconds", at},
            {"quote",
             {{"symbol", symbol},
              {"sequence", sequence},
              {"source_at_seconds", at},
              {"bid_units", price},
              {"ask_units", price},
              {"bid_shares", liquidity},
              {"ask_shares", liquidity}}}};
}
OrderRequest request(std::string id, Shares q = 5, std::string symbol = "SPY", Side side = Side::buy,
                     MoneyUnits price = 100 * usd, MoneyUnits fee = broker_fee_units) {
    return {std::move(id), std::move(symbol), side, q, price, fee};
}
Json submit(std::string id, const OrderRequest &order, std::string tif = "day",
            std::int64_t at = opening + 1) {
    return {{"kind", "submit"},
            {"command_id", id},
            {"at_seconds", at},
            {"order", broker_order_json(order)},
            {"time_in_force", tif}};
}
Json financial(const SharedCapitalSnapshot &state) {
    auto j = shared_snapshot_json(state);
    for (const auto *k : {"connected", "reconciliation_required", "at_seconds", "generation", "sealed"}) {
        j.erase(k);
    }
    return j;
}
class TracedPeer final : public BrokerPeer {
  public:
    TracedPeer(std::unique_ptr<BrokerPeer> peer, Json &calls, std::string name)
        : peer_(std::move(peer)), calls_(calls), name_(std::move(name)) {}
    Json exchange(const Json &c) override {
        try {
            auto reply = peer_->exchange(c);
            calls_.push_back({{"peer", name_}, {"command", c}, {"response", reply}});
            return reply;
        } catch (const std::exception &e) {
            calls_.push_back({{"peer", name_}, {"command", c}, {"error", e.what()}});
            throw;
        }
    }

  private:
    std::unique_ptr<BrokerPeer> peer_;
    Json &calls_;
    std::string name_;
};
class AlteredPeer final : public BrokerPeer {
  public:
    explicit AlteredPeer(BrokerPeer &peer) : peer_(peer) {}
    std::string fault;
    Json exchange(const Json &c) override {
        auto reply = peer_.exchange(c);
        if (c.at("kind").get<std::string>() == "activities" && !reply.at("activities").empty()) {
            if (fault == "gap") {
                reply["activities"][0]["cursor"] =
                    reply["activities"][0].at("cursor").get<std::uint64_t>() + 1;
            } else if (fault == "reverse") {
                std::reverse(reply["activities"].begin(), reply["activities"].end());
            } else if (fault == "duplicate") {
                reply["activities"].push_back(reply["activities"].back());
            }
        }
        if (fault == "snapshot" && c.at("kind").get<std::string>() == "snapshot") {
            reply["cash_units"] = reply.at("cash_units").get<MoneyUnits>() + 1;
        }
        return reply;
    }

  private:
    BrokerPeer &peer_;
};
enum class Fault { none, before, partial, after };
struct FaultControl {
    Fault mode{Fault::none};
};
class FaultStorage final : public JournalStorage {
  public:
    FaultStorage(std::unique_ptr<JournalStorage> file, FaultControl &control)
        : file_(std::move(file)), control_(control) {}
    std::string read_all() override {
        return file_->read_all();
    }
    void append_and_sync(std::string_view bytes) override {
        const auto mode = control_.mode;
        control_.mode = Fault::none;
        if (mode == Fault::before) {
            throw JournalError("constructed peer pre-append failure");
        }
        if (mode == Fault::partial) {
            file_->append_and_sync(bytes.substr(0, bytes.size() / 2));
            throw JournalError("constructed peer partial append");
        }
        file_->append_and_sync(bytes);
        if (mode == Fault::after) {
            throw JournalError("constructed peer post-sync uncertainty");
        }
    }

  private:
    std::unique_ptr<JournalStorage> file_;
    FaultControl &control_;
};
class Cases {
  public:
    Cases(std::filesystem::path root, std::filesystem::path executable) : executable_(std::move(executable)) {
        result_.directory = std::move(root);
        result_.cases = Json::array();
        result_.calls = Json::array();
        result_.rejected_files = Json::array();
    }
    ProcessBrokerDiagnosticResult run() {
        protocol();
        fifo();
        day();
        ioc();
        gtc();
        replacement();
        rejection();
        delivery();
        ambiguous();
        client_restart();
        broker_restart();
        storage();
        return std::move(result_);
    }

  private:
    void check(std::string name, bool value) {
        if (!value) {
            throw std::runtime_error("process broker check failed: " + name);
        }
        result_.checks.push_back(std::move(name));
    }
    template <class F> void rejects(std::string name, F &&f) {
        bool failed = false;
        try {
            f();
        } catch (const std::exception &) {
            failed = true;
        }
        check(std::move(name), failed);
    }
    std::filesystem::path path(const std::string &name) {
        auto p = result_.directory / name;
        std::filesystem::create_directories(p.parent_path());
        return p;
    }
    std::unique_ptr<BrokerPeer> peer(std::string name, std::string account, bool process = true,
                                     std::vector<std::string> extra = {}) {
        std::unique_ptr<BrokerPeer> wire;
        if (process) {
            auto p = std::make_unique<ProcessBrokerPeer>(executable_, path(name + ".journal"), account,
                                                         std::move(extra));
            check(name + ": separate native process started", p->process_id() > 0);
            result_.process_ids.push_back(p->process_id());
            wire = std::move(p);
        } else {
            auto p = path(name + ".journal");
            auto service = std::filesystem::exists(p)
                               ? BrokerService::recover(open_journal_file(p), account)
                               : BrokerService::create(create_journal_file(p), account);
            wire = std::make_unique<InProcessBrokerPeer>(std::move(service));
        }
        return std::make_unique<TracedPeer>(std::move(wire), result_.calls, name);
    }
    Json call(BrokerPeer &p, const Json &c) {
        auto reply = p.exchange(c);
        check(c.at("command_id").get<std::string>() + ": broker command committed",
              reply.at("status").get<std::string>() == "ok");
        return reply;
    }
    Json state(BrokerPeer &p) {
        return p.exchange({{"kind", "snapshot"}});
    }
    void begin(BrokerPeer &p, Shares liquidity = 0) {
        call(p, session());
        for (const auto &s : symbols) {
            call(p, quote("initial-" + s, s, 1, opening + 1, liquidity));
        }
    }
    void save(std::string name, Json journals, Json finals, Json clients = Json::array()) {
        result_.cases.push_back({{"name", name},
                                 {"broker_journals", journals},
                                 {"broker_states", finals},
                                 {"client_journals", clients}});
    }
    SharedCapitalConfiguration config() {
        auto c = SharedCapitalConfiguration::registered(ExtensionStudyContract::load(
            std::filesystem::path{TRADING_PROJECT_ROOT} / "research/extension_protocol.json"));
        c.fee_per_share_ceiling_units = broker_fee_units;
        return c;
    }
    JournaledSharedCapital client(const std::string &name) {
        return JournaledSharedCapital::create(create_journal_file(path(name + ".journal")), config());
    }
    SharedTargetBatch targets(BrokerPeer &p, std::string id, std::int64_t at,
                              std::vector<FamilySignal> signals) {
        SharedTargetBatch b{std::move(id), at, {}, std::move(signals)};
        const auto snapshot = state(p);
        for (const auto &q : snapshot.at("quotes")) {
            b.quotes.push_back({q.at("symbol").get<std::string>(),
                                q.at("source_at_seconds").get<std::int64_t>(),
                                q.at("bid_units").get<MoneyUnits>(), q.at("ask_units").get<MoneyUnits>()});
        }
        return b;
    }
    void parity(SharedBrokerSession &s, JournaledSharedCapital &c, BrokerPeer &p, std::int64_t at) {
        check("independent activity drain completed", s.drain(at));
        const auto a = state(p);
        const auto b = c.snapshot();
        check("independent broker cash equals shared global cash",
              a.at("cash_units").get<MoneyUnits>() == b.account.cash_units);
        for (const auto &symbol : symbols) {
            check("independent inventory equals owned " + symbol,
                  a.at("positions").at(symbol).get<Shares>() == b.account.positions.at(symbol).shares);
        }
        check("independent pending cash equals client reservation",
              a.at("reserved_cash_units").get<MoneyUnits>() == b.account.reserved_cash_units);
    }
    void protocol();
    void fifo();
    void day();
    void ioc();
    void gtc();
    void replacement();
    void rejection();
    void delivery();
    void ambiguous();
    void client_restart();
    void broker_restart();
    void storage();
    std::filesystem::path executable_;
    ProcessBrokerDiagnosticResult result_;
};
void Cases::protocol() {
    auto local = peer("protocol/local", "parity", false), process = peer("protocol/process", "parity");
    for (auto *p : {local.get(), process.get()}) {
        begin(*p, 3);
        call(*p, submit("order", request("parity-order")));
    }
    check("in-process and native-process ledgers match exactly", state(*local) == state(*process));
    check("terminal and partial order lookup retains broker identity",
          process->exchange({{"kind", "lookup"}, {"order_id", "parity-order"}})
                  .at("value")
                  .at("broker_order_id")
                  .get<std::string>() == "B5-O1");
    rejects("activity pages cannot exceed sixteen", [&] {
        static_cast<void>(local->exchange({{"kind", "activities"}, {"after", 0}, {"limit", 17}}));
    });
    for (const auto &mode : {std::string{"malformed"}, std::string{"version"}}) {
        ChildChannel channel{executable_, {path("protocol/" + mode + ".journal").string(), mode}};
        result_.process_ids.push_back(channel.process_id());
        rejects("native worker rejects " + mode + " request", [&] {
            static_cast<void>(channel.exchange(
                mode == "malformed"
                    ? "{"
                    : Json{{"protocol", 2}, {"id", 1}, {"command", {{"kind", "hello"}}}}.dump(),
                std::chrono::seconds{5}));
        });
    }
    ChildChannel paused{
        executable_,
        {path("protocol/deadline.journal").string(), "deadline", "--pause-after-commit", "begin"}};
    result_.process_ids.push_back(paused.process_id());
    const auto greeting = Json::parse(paused.exchange(
        Json{{"protocol", 1}, {"id", 1}, {"command", {{"kind", "hello"}}}}.dump(), std::chrono::seconds{5}));
    result_.calls.push_back({{"peer", "protocol/deadline"},
                             {"command", {{"kind", "hello"}}},
                             {"response", greeting.at("response")}});
    const auto before = std::chrono::steady_clock::now();
    std::string deadline_error;
    rejects("parent enforces response deadline after durable broker commit", [&] {
        try {
            static_cast<void>(paused.exchange(Json{{"protocol", 1}, {"id", 2}, {"command", session()}}.dump(),
                                              std::chrono::milliseconds{500}));
        } catch (const std::exception &error) {
            deadline_error = error.what();
            throw;
        }
    });
    result_.calls.push_back(
        {{"peer", "protocol/deadline"}, {"command", session()}, {"error", deadline_error}});
    check("timed-out child is stopped and reaped within bound",
          paused.process_id() == 0 && std::chrono::steady_clock::now() - before < std::chrono::seconds{2});
    auto recovered = peer("protocol/deadline", "deadline");
    check("deadline does not erase committed peer session",
          state(*recovered).at("session_date").get<std::string>() == "2023-01-03");
    ChildChannel bounded{executable_, {path("protocol/bounded.journal").string(), "bounded"}};
    result_.process_ids.push_back(bounded.process_id());
    static_cast<void>(bounded.exchange(
        Json{{"protocol", 1}, {"id", 1}, {"command", {{"kind", "hello"}}}}.dump(), std::chrono::seconds{5}));
    rejects("outgoing IPC frame size is bounded before transmission", [&] {
        static_cast<void>(
            bounded.exchange(std::string(maximum_peer_payload_bytes + 1, 'x'), std::chrono::seconds{5}));
    });
    for (const auto &payload : {std::string{"{"}, std::string{R"({"protocol":2,"id":1,"response":{}})"},
                                std::string{R"({"protocol":1,"id":2,"response":{}})"},
                                std::string{R"({"protocol":1.5,"id":1,"response":{}})"},
                                std::string{R"({"protocol":1,"id":1.5,"response":{}})"},
                                std::string{R"({"protocol":1,"id":1,"response":{},"extra":0})"}}) {
        rejects("reply decoder rejects malformed, nonintegral or mismatched envelope",
                [&] { static_cast<void>(decode_broker_reply(payload, 1)); });
    }
    auto spaced = peer("protocol/path with spaces/broker", "spaces");
    check("native process arguments retain paths containing spaces",
          state(*spaced).at("account_id").get<std::string>() == "spaces");
    auto concurrent = peer("protocol/concurrent", "concurrent");
    begin(*concurrent, 200);
    auto owned = client("protocol/client");
    SharedBrokerSession concurrent_session{owned, *concurrent, "concurrent"};
    check("five-instrument shared account reconciles the independent process",
          concurrent_session.connect(opening + 1));
    std::vector<FamilySignal> all_long;
    for (const auto &family : config().families) {
        for (const auto &symbol : symbols) {
            all_long.push_back({{family, symbol}, true});
        }
    }
    const auto routed = concurrent_session.target(targets(*concurrent, "all-owners", opening + 1, all_long));
    check("fifteen shared owners net to five independent broker orders", routed.dispatch_orders.size() == 5);
    parity(concurrent_session, owned, *concurrent, opening + 1);
    const auto filled = owned.snapshot();
    check("five-instrument fills retain every strategy ownership",
          std::all_of(filled.owners.begin(), filled.owners.end(),
                      [](const auto &entry) { return entry.second.shares == 66; }) &&
              filled.account.open_orders == 0);
    save("protocol",
         {"protocol/local.journal", "protocol/process.journal", "protocol/malformed.journal",
          "protocol/version.journal", "protocol/deadline.journal", "protocol/bounded.journal",
          "protocol/path with spaces/broker.journal", "protocol/concurrent.journal"},
         Json::array({state(*local), state(*process), state(*recovered), state(*spaced), state(*concurrent)}),
         {"protocol/client.journal"});
}
void Cases::fifo() {
    auto p = peer("fifo/broker", "fifo");
    begin(*p);
    call(*p, quote("liquidity-1", "SPY", 2, opening + 1, 3));
    call(*p, submit("first", request("first", 5)));
    call(*p, submit("second", request("second", 4, "SPY", Side::buy, 101 * usd)));
    call(*p, quote("liquidity-2", "SPY", 3, opening + 1, 4));
    const auto before = state(*p);
    check("FIFO uses one finite quote budget across orders",
          before.at("positions").at("SPY").get<Shares>() == 7 && before.at("fill_count").get<int>() == 3);
    check("per-share fees conserve nanodollars",
          before.at("cash_units").get<MoneyUnits>() ==
              broker_initial_cash_units - 700 * usd - 7 * broker_fee_units);
    auto duplicate = quote("duplicate-quote", "SPY", 3, opening + 1, 4);
    call(*p, duplicate);
    check("duplicate quote cannot replenish consumed liquidity", state(*p) == before);
    duplicate["command_id"] = "conflicting-quote";
    duplicate["quote"]["ask_shares"] = 99;
    check("conflicting quote sequence is denied without ledger change",
          p->exchange(duplicate).at("status").get<std::string>() == "denied" && state(*p) == before);
    call(*p, submit("stale-submit", request("stale", 1), "day", opening + 4));
    check("stale displayed quote cannot fill a new order", state(*p).at("fill_count").get<int>() == 3);
    auto future = quote("future-quote", "IWM", 2, opening + 4, 1);
    future["quote"]["source_at_seconds"] = opening + 5;
    check("future source quote is denied", p->exchange(future).at("status").get<std::string>() == "denied");
    call(*p, {{"kind", "cancel"},
              {"command_id", "late-cancel"},
              {"at_seconds", opening + 4},
              {"order_id", "first"},
              {"cancel_id", "late-cancel-id"}});
    call(*p, {{"kind", "complete_cancels"}, {"command_id", "late-complete"}, {"at_seconds", opening + 4}});
    const auto cursor = state(*p).at("activity_cursor").get<std::uint64_t>();
    const auto last = p->exchange({{"kind", "activities"}, {"after", cursor - 1}, {"limit", 1}});
    check("late cancel rejection preserves the terminal filled order",
          last.at("activities").at(0).at("report").at("report_kind").get<std::string>() ==
                  "cancel_rejected" &&
              p->exchange({{"kind", "lookup"}, {"order_id", "first"}})
                      .at("value")
                      .at("status")
                      .get<std::string>() == "filled");
    const auto cash = state(*p).at("cash_units").get<MoneyUnits>();
    call(*p, submit("sell", request("sell-order", 2, "SPY", Side::sell), "day", opening + 4));
    auto sale_quote = quote("sale-liquidity", "SPY", 4, opening + 4, 2);
    sale_quote["quote"]["ask_shares"] = 0;
    call(*p, sale_quote);
    const auto sold = state(*p);
    check("sell fills deduct fees and immediately release trade-date cash",
          sold.at("cash_units").get<MoneyUnits>() == cash + 200 * usd - 2 * broker_fee_units &&
              sold.at("available_cash_units") == sold.at("cash_units") &&
              sold.at("positions").at("SPY").get<Shares>() == 5);
    save("fifo", {"fifo/broker.journal"}, Json::array({state(*p)}));
}
void Cases::day() {
    auto p = peer("day/broker", "day");
    begin(*p, 2);
    call(*p, submit("day-entry", request("day-order")));
    const auto close = state(*p).at("session_close_seconds").get<std::int64_t>();
    call(*p, {{"kind", "advance"}, {"command_id", "close"}, {"at_seconds", close}});
    check("DAY expires residual shares at close",
          state(*p).at("open_orders").empty() && state(*p).at("positions").at("SPY").get<int>() == 2 &&
              state(*p).at("reserved_cash_units").get<MoneyUnits>() == 0);
    check("quotes at close cannot execute expired DAY orders",
          p->exchange(quote("closed-quote", "SPY", 2, close, 100)).at("status").get<std::string>() ==
              "denied");
    call(*p, session("tomorrow", "2023-01-04", tomorrow));
    call(*p, quote("next-quote", "SPY", 2, tomorrow + 1, 100));
    check("DAY expiration persists into the next session", state(*p).at("fill_count").get<int>() == 1);
    save("day", {"day/broker.journal"}, Json::array({state(*p)}));
}
void Cases::ioc() {
    auto p = peer("ioc/broker", "ioc");
    begin(*p, 3);
    call(*p, submit("ioc-entry", request("ioc-order", 7), "ioc"));
    check("IOC fills once and cancels only unfilled remainder",
          state(*p).at("positions").at("SPY").get<int>() == 3 && state(*p).at("open_orders").empty());
    call(*p, quote("new-budget", "SPY", 2, opening + 1, 100));
    check("later liquidity does not reactivate IOC", state(*p).at("positions").at("SPY").get<int>() == 3);
    call(*p, submit("ioc-unmarketable", request("ioc-no-fill", 2, "SPY", Side::buy, 99 * usd), "ioc"));
    check("unmarketable IOC terminates without fills or reservations",
          state(*p).at("order_count").get<int>() == 2 && state(*p).at("open_orders").empty());
    save("ioc", {"ioc/broker.journal"}, Json::array({state(*p)}));
}
void Cases::gtc() {
    auto p = peer("gtc/broker", "gtc");
    begin(*p, 2);
    call(*p, submit("gtc-entry", request("gtc-order", 10), "gtc"));
    call(*p, session("next-session", "2023-01-04", tomorrow));
    const auto before = state(*p);
    p.reset();
    p = peer("gtc/broker", "gtc");
    check("GTC account and reservations survive broker restart",
          state(*p) == before && before.at("open_orders").size() == 1);
    call(*p, quote("next-fill", "SPY", 2, tomorrow + 1, 8));
    check("GTC completes across sessions without duplicate fees",
          state(*p).at("positions").at("SPY").get<int>() == 10 && state(*p).at("open_orders").empty() &&
              state(*p).at("fill_count").get<int>() == 2);
    save("gtc", {"gtc/broker.journal"}, Json::array({state(*p)}));
}
void Cases::replacement() {
    auto p = peer("replacement/broker", "replacement");
    begin(*p);
    auto c = client("replacement/client");
    SharedBrokerSession s{c, *p, "replacement"};
    check("replacement client synchronizes independent account", s.connect(opening + 1));
    const auto entry = s.target(targets(*p, "owner-old", opening + 1, {{{"momentum", "SPY"}, true}}));
    check("shared account dispatches original owner order", entry.dispatch_orders.size() == 1);
    const auto change = s.target(
        targets(*p, "owner-new", opening + 2, {{{"momentum", "SPY"}, false}, {{"reversion", "SPY"}, true}}));
    check("managed replacement begins with cancel and retains old claims",
          change.dispatch_cancels.size() == 1 && change.dispatch_orders.empty());
    call(*p, quote("race-fill", "SPY", 2, opening + 2, 40));
    parity(s, c, *p, opening + 2);
    check("race fill belongs to old owner before cancel confirmation",
          c.snapshot().owners.at({"momentum", "SPY"}).shares == 40 &&
              c.snapshot().owners.at({"reversion", "SPY"}).shares == 0);
    call(*p, {{"kind", "complete_cancels"}, {"command_id", "cancel-terminal"}, {"at_seconds", opening + 2}});
    parity(s, c, *p, opening + 2);
    check("terminal cancel permits internal crossing and residual new identity",
          c.snapshot().owners.at({"reversion", "SPY"}).shares == 40 &&
              state(*p).at("order_count").get<int>() == 2 &&
              c.snapshot().open_orders.at(0).request.shares == 26);
    call(*p, quote("residual-fill", "SPY", 3, opening + 3, 26));
    parity(s, c, *p, opening + 3);
    check("replacement completes target without double inventory",
          c.snapshot().owners.at({"reversion", "SPY"}).shares == 66 &&
              c.snapshot().owners.at({"momentum", "SPY"}).shares == 0);
    save("replacement", {"replacement/broker.journal"}, Json::array({state(*p)}),
         {"replacement/client.journal"});
}
void Cases::rejection() {
    auto p = peer("rejection/broker", "rejection");
    begin(*p);
    call(*p, submit("fee-reject", request("low-fee", 1, "SPY", Side::buy, 100 * usd, broker_fee_units - 1)));
    call(*p, submit("sell-reject", request("unowned", 1, "SPY", Side::sell)));
    call(*p, submit("cash-reject", request("too-large", 1001)));
    check("fee, inventory and cash rejections do not alter balances",
          state(*p).at("cash_units").get<MoneyUnits>() == broker_initial_cash_units &&
              state(*p).at("open_orders").empty() && state(*p).at("activity_cursor").get<int>() == 3);
    call(*p, submit("reserve", request("reserved", 900)));
    call(*p, submit("cross-reserve", request("unfunded-other", 200, "IWM")));
    check("cross-symbol pending buys share broker cash",
          state(*p).at("open_orders").size() == 1 &&
              state(*p).at("reserved_cash_units").get<MoneyUnits>() == 900 * (100 * usd + broker_fee_units));
    auto conflict = submit("changed-order", request("reserved", 901));
    const auto before = state(*p);
    check("conflicting client identity is denied without replacement",
          p->exchange(conflict).at("status").get<std::string>() == "denied" && state(*p) == before);
    auto repeated = submit("reserve", request("reserved", 900));
    check("identical durable command does not create another order",
          p->exchange(repeated).at("status").get<std::string>() == "ok" && state(*p) == before);
    repeated["order"]["shares"] = 899;
    check("conflicting durable command identity is denied",
          p->exchange(repeated).at("status").get<std::string>() == "denied" && state(*p) == before);
    SimulatedBroker bounded{symbols, broker_initial_cash_units, broker_fee_units};
    rejects("portfolio DAY order cannot omit its expiry clock",
            [&] { static_cast<void>(bounded.submit(request("missing-expiry", 1), opening + 1)); });
    for (int i = 0; i < 33; ++i) {
        static_cast<void>(bounded.submit(request("bounded-" + std::to_string(i), 1), opening + 1,
                                         BrokerTimeInForce::gtc, 0));
    }
    check("independent peer caps open orders before reserving a thirty-third order",
          bounded.portfolio_snapshot().open_orders.size() == 32 &&
              bounded.reports().back().kind == ReportKind::rejected);
    auto c = client("rejection/client");
    SharedBrokerSession wrong{c, *p, "wrong-account"};
    check("client cannot reconcile a different broker account identity",
          !wrong.connect(opening + 1) && !c.snapshot().account.connected);
    save("rejection", {"rejection/broker.journal"}, Json::array({state(*p)}), {"rejection/client.journal"});
}
void Cases::delivery() {
    auto p = peer("delivery/broker", "delivery");
    begin(*p);
    auto c = client("delivery/client");
    AlteredPeer altered{*p};
    SharedBrokerSession s{c, altered, "delivery"};
    check("delivery client initially reconciles", s.connect(opening + 1));
    static_cast<void>(s.target(targets(*p, "entry", opening + 1, {{{"momentum", "SPY"}, true}})));
    altered.fault = "gap";
    check("gapped activity closes routing before applying any row",
          !s.drain(opening + 1) && c.snapshot().account.positions.at("SPY").shares == 0 &&
              !c.snapshot().account.connected);
    call(*p, quote("offline-fill", "SPY", 2, opening + 2, 66));
    for (const auto &mode : {std::string{"reverse"}, std::string{"duplicate"}}) {
        altered.fault = mode;
        check(mode + " activity page cannot authorize trading",
              !s.connect(opening + 2) && c.snapshot().account.positions.at("SPY").shares == 0);
    }
    altered.fault.clear();
    check("valid retained history recovers missed fills", s.connect(opening + 2));
    parity(s, c, *p, opening + 2);
    const auto before = financial(c.snapshot());
    check("replaying all stable activity is idempotent",
          s.connect(opening + 2) && financial(c.snapshot()) == before);
    altered.fault = "snapshot";
    check("contradictory snapshot never overwrites the owned ledger",
          !s.connect(opening + 2) && financial(c.snapshot()) == before && !c.snapshot().account.connected);
    altered.fault.clear();
    check("exact snapshot restores gate after retained evidence", s.connect(opening + 2));
    save("delivery", {"delivery/broker.journal"}, Json::array({state(*p)}), {"delivery/client.journal"});
}
void Cases::ambiguous() {
    auto p = peer("ambiguous/broker", "ambiguous", true, {"--stop-after-commit", "submit-S4-0-1"});
    begin(*p, 5);
    auto c = client("ambiguous/client");
    SharedBrokerSession s{c, *p, "ambiguous"};
    check("ambiguity client reconciles before entry", s.connect(opening + 1));
    rejects("lost submit response cannot produce automatic retry", [&] {
        static_cast<void>(
            s.target(targets(*p, "ambiguous-entry", opening + 1, {{{"momentum", "SPY"}, true}})));
    });
    check("lost response leaves client closed with original reservation",
          !c.snapshot().account.connected && c.snapshot().account.positions.at("SPY").shares == 0 &&
              c.snapshot().account.open_orders == 1);
    p.reset();
    auto recovered = peer("ambiguous/broker", "ambiguous");
    const auto lookup = recovered->exchange({{"kind", "lookup"}, {"order_id", "S4-0-1"}});
    check("independent lookup resolves committed ambiguous submission",
          lookup.at("found").get<bool>() && lookup.at("value").at("filled_shares").get<int>() == 5 &&
              state(*recovered).at("order_count").get<int>() == 1);
    SharedBrokerSession repaired{c, *recovered, "ambiguous"};
    check("history precedes reconciliation after ambiguous submit", repaired.connect(opening + 1));
    parity(repaired, c, *recovered, opening + 1);
    check("recovery never resubmits committed order",
          state(*recovered).at("order_count").get<int>() == 1 &&
              c.snapshot().owners.at({"momentum", "SPY"}).shares == 5);
    save("ambiguous", {"ambiguous/broker.journal"}, Json::array({state(*recovered)}),
         {"ambiguous/client.journal"});
}
void Cases::client_restart() {
    auto p = peer("client_restart/broker", "client_restart");
    begin(*p, 7);
    auto c = std::make_optional(client("client_restart/client"));
    auto s = std::make_unique<SharedBrokerSession>(*c, *p, "client_restart");
    check("client restart initial reconciliation", s->connect(opening + 1));
    static_cast<void>(s->target(targets(*p, "entry", opening + 1, {{{"momentum", "QQQ"}, true}})));
    parity(*s, *c, *p, opening + 1);
    s.reset();
    c.reset();
    call(*p, quote("while-client-offline", "QQQ", 2, opening + 2, 13));
    c.emplace(
        JournaledSharedCapital::recover(open_journal_file(path("client_restart/client.journal")), config()));
    check("client journal recovery begins closed",
          !c->snapshot().account.connected && c->snapshot().owners.at({"momentum", "QQQ"}).shares == 7);
    s = std::make_unique<SharedBrokerSession>(*c, *p, "client_restart");
    check("client restart consumes unseen broker activity", s->connect(opening + 2));
    parity(*s, *c, *p, opening + 2);
    check("offline broker fills receive original owner attribution",
          c->snapshot().owners.at({"momentum", "QQQ"}).shares == 20 &&
              state(*p).at("order_count").get<int>() == 1);
    call(*p, quote("rest", "QQQ", 3, opening + 3, 46));
    parity(*s, *c, *p, opening + 3);
    save("client_restart", {"client_restart/broker.journal"}, Json::array({state(*p)}),
         {"client_restart/client.journal"});
}
void Cases::broker_restart() {
    auto p = peer("broker_restart/broker", "broker_restart");
    begin(*p, 20);
    auto c = client("broker_restart/client");
    auto s = std::make_unique<SharedBrokerSession>(c, *p, "broker_restart");
    check("broker restart initial reconciliation", s->connect(opening + 1));
    static_cast<void>(s->target(targets(*p, "entry", opening + 1, {{{"reversion", "TLT"}, true}})));
    parity(*s, c, *p, opening + 1);
    const auto before = state(*p);
    const auto owned = financial(c.snapshot());
    s->disconnect(opening + 1);
    s.reset();
    p.reset();
    p = peer("broker_restart/broker", "broker_restart");
    check("peer restart preserves independent full checkpoint", state(*p) == before);
    s = std::make_unique<SharedBrokerSession>(c, *p, "broker_restart");
    check("peer restart requires catch-up and exact reconciliation",
          s->connect(opening + 1) && financial(c.snapshot()) == owned);
    call(*p, quote("same-quote-after-restart", "TLT", 1, opening + 1, 20));
    check("restart and quote redelivery do not replenish liquidity", state(*p) == before);
    call(*p, quote("remaining", "TLT", 2, opening + 2, 46));
    parity(*s, c, *p, opening + 2);
    check("broker restart does not regenerate order or execution identities",
          state(*p).at("order_count").get<int>() == 1 && state(*p).at("fill_count").get<int>() == 2);
    save("broker_restart", {"broker_restart/broker.journal"}, Json::array({state(*p)}),
         {"broker_restart/client.journal"});
}
void Cases::storage() {
    Json journals = Json::array(), states = Json::array();
    for (const auto mode : {Fault::before, Fault::partial, Fault::after}) {
        const auto name = mode == Fault::before    ? std::string{"before"}
                          : mode == Fault::partial ? std::string{"partial"}
                                                   : std::string{"after"};
        const auto file = path("storage/" + name + ".journal");
        FaultControl control;
        auto service = std::make_optional(
            BrokerService::create(std::make_unique<FaultStorage>(create_journal_file(file), control), name));
        static_cast<void>(service->exchange(session()));
        static_cast<void>(service->exchange(quote("quote", "SPY", 1, opening + 1, 5)));
        const auto original = sha256_file(file);
        control.mode = mode;
        rejects(name + ": uncertain peer append returns no publishable response", [&] {
            static_cast<void>(service->exchange(submit("uncertain", request("uncertain-order"))));
        });
        check(name + ": uncertain writer is latched", !service->healthy());
        rejects(name + ": uncertain peer cannot expose state",
                [&] { static_cast<void>(service->snapshot()); });
        service.reset();
        std::string accepted = "storage/" + name + ".journal";
        if (mode == Fault::partial) {
            rejects("normal peer recovery rejects incomplete tail",
                    [&] { static_cast<void>(BrokerService::recover(open_journal_file(file), name)); });
            const auto damaged = sha256_file(file);
            accepted = "storage/prefix.journal";
            service.emplace(BrokerService::recover_prefix(open_journal_file(file),
                                                          create_journal_file(path(accepted)), name));
            check("verified peer prefix leaves damaged source unchanged", sha256_file(file) == damaged);
            result_.rejected_files.push_back(
                {{"path", "storage/partial.journal"}, {"failure", "incomplete_tail"}});
        } else {
            if (mode == Fault::before) {
                check("pre-append fault leaves broker journal unchanged", sha256_file(file) == original);
            }
            service.emplace(BrokerService::recover(open_journal_file(file), name));
        }
        const auto state = service->snapshot();
        check(name + ": recovery distinguishes absent and committed orders",
              state.at("order_count").get<int>() == (mode == Fault::after ? 1 : 0));
        check(name + ": recovered broker balance follows only committed fills",
              state.at("cash_units").get<MoneyUnits>() ==
                  broker_initial_cash_units -
                      (mode == Fault::after ? 5 * (100 * usd + broker_fee_units) : 0));
        journals.push_back(accepted);
        states.push_back(state);
    }
    auto bytes = read_text_file(path("storage/after.journal"));
    bytes[0] = '!';
    write_new_text_file(path("storage/corrupt.journal"), bytes);
    rejects("complete peer corruption is rejected", [&] {
        static_cast<void>(
            BrokerService::recover(open_journal_file(path("storage/corrupt.journal")), "after"));
    });
    check("corrupt peer source is retained without truncation",
          read_text_file(path("storage/corrupt.journal")) == bytes);
    result_.rejected_files.push_back({{"path", "storage/corrupt.journal"}, {"failure", "checksum"}});
    rejects("peer journal cannot recover under another account", [&] {
        static_cast<void>(BrokerService::recover(open_journal_file(path("storage/after.journal")), "wrong"));
    });
    auto held = open_journal_file(path("storage/after.journal"));
    rejects("peer journal retains exclusive writer ownership",
            [&] { static_cast<void>(open_journal_file(path("storage/after.journal"))); });
    held.reset();
    rejects("peer journal frame bound rejects oversized input",
            [&] { static_cast<void>(JournalFrame::encode(Json{{"body", std::string(65536, 'x')}})); });
    save("storage", journals, states);
}
} // namespace
ProcessBrokerDiagnostic::ProcessBrokerDiagnostic(std::filesystem::path root, std::filesystem::path exe)
    : root_(std::move(root)), executable_(std::move(exe)) {}
ProcessBrokerDiagnosticResult ProcessBrokerDiagnostic::run() const {
    const auto directory = create_unique_directory(root_, "process_broker");
    try {
        return Cases{directory, executable_}.run();
    } catch (const std::exception &error) {
        throw std::runtime_error(std::string{error.what()} + "; retained evidence: " + directory.string());
    }
}
} // namespace trading::execution
