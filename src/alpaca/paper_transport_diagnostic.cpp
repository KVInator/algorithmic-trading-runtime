#include "paper_transport_diagnostic.hpp"
#include "paper_control.hpp"
#include <algorithm>
#include <deque>

namespace trading {
namespace {
using Json = nlohmann::json;
const execution::PaperExecutionConfiguration config{"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "2000", {20, 4}};
const std::string timestamp = "2026-10-26T14:00:00.123456789Z";

std::string account(std::string cash = "2000") {
    return Json{{"id", config.account_id}, {"cash", std::move(cash)},  {"currency", "USD"},
                {"status", "ACTIVE"},      {"trading_blocked", false}, {"account_blocked", false}}
        .dump();
}

Json order(int filled = 0) {
    return {{"id", "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"},
            {"client_order_id", "transport-buy"},
            {"symbol", "SPY"},
            {"asset_class", "us_equity"},
            {"side", "buy"},
            {"qty", "10"},
            {"filled_qty", std::to_string(filled)},
            {"filled_avg_price", filled ? Json("100.005") : Json(nullptr)},
            {"limit_price", "100.01"},
            {"type", "limit"},
            {"order_class", "simple"},
            {"time_in_force", "day"},
            {"extended_hours", false},
            {"status", filled ? "partially_filled" : "new"},
            {"updated_at", timestamp}};
}

class ScriptedWire final : public PaperWire {
public:
    std::deque<PaperHttpResponse> responses;
    std::deque<std::string> messages;
    std::vector<PaperRequest> requests;
    bool uncertain_post = false, close_on_poll = false, opened = false, health_ack = false;

    PaperHttpResponse request(const PaperRequest& r) override {
        static_cast<void>(validate_paper_request(r));
        requests.push_back(r);
        if (uncertain_post && r.method == "POST")
            throw std::runtime_error("constructed timeout after possible acceptance");
        if (!responses.empty()) {
            auto out = responses.front();
            responses.pop_front();
            return out;
        }
        if (r.method == "GET" && r.url.ends_with("/account"))
            return {200, account(), "", "simulated"};
        if (r.method == "GET")
            return {200, "[]", "", "simulated"};
        if (r.method == "POST")
            return {200, order().dump(), "", "simulated"};
        return {204, "", "", "simulated"};
    }

    void open_authenticated_stream() override { opened = true; }

    void subscribe_trade_updates() override {}

    std::optional<std::string> pong;

    void ping(std::string_view token, std::chrono::milliseconds) override {
        if (health_ack)
            pong = std::string{token};
    }

    std::optional<std::string> take_pong() override {
        auto value = std::move(pong);
        pong.reset();
        return value;
    }

    std::optional<std::string> poll(std::chrono::milliseconds) override {
        if (close_on_poll)
            throw std::runtime_error("constructed peer close");
        if (messages.empty())
            return std::nullopt;
        auto out = std::move(messages.front());
        messages.pop_front();
        return out;
    }

    void close() noexcept override {
        opened = false;
        pong.reset();
    }

    void handshake() {
        messages.push_back(R"({"stream":"authorization","data":{"status":"authorized"}})");
        messages.push_back(R"({"stream":"listening","data":{"streams":["trade_updates"]}})");
    }
};

void check(PaperTransportResult& r, std::string label, bool ok) {
    if (!ok)
        throw std::logic_error("paper transport diagnostic: " + label);
    r.checks.push_back(std::move(label));
}

template <class F> bool fails(F action) {
    try {
        action();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

class Fixture {
public:
    std::filesystem::path directory;
    ScriptedWire wire;
    PaperCapture capture;
    execution::JournaledPaperRuntime runtime;
    std::int64_t tick = 0;
    unsigned waits = 0;
    PaperSession session;

    Fixture(const std::filesystem::path& root, std::string name)
        : directory{root / std::move(name)}, capture{directory / "capture"},
          runtime{execution::JournaledPaperRuntime::create(
              execution::create_journal_file(directory / "runtime.journal"), config)},
          session{runtime, wire, capture, [this] { return PaperReceipt{timestamp, ++tick}; },
                  [this](auto) { ++waits; }} {}

    void ready() {
        wire.handshake();
        session.connect();
        if (!session.synchronize())
            throw std::logic_error("constructed account must synchronize");
    }

    Json result(std::string name) {
        return {{"case", std::move(name)},
                {"simulated_requests", wire.requests.size()},
                {"capture_records", capture.records()},
                {"state", runtime.snapshot()}};
    }
};
} // namespace

PaperTransportResult PaperTransportDiagnostic::run() const {
    PaperTransportResult result{};
    {
        PaperMessageAssembler a;
        check(result, "binary fragment stays pending", !a.push("{\"stream\":", true, false));
        check(result, "binary JSON assembly is exact",
              a.push("\"listening\"}", true, true) ==
                  std::optional<std::string>{"{\"stream\":\"listening\"}"});
        static_cast<void>(a.push("x", true, false));
        check(result, "mixed fragment types rejected",
              fails([&] { static_cast<void>(a.push("y", false, true)); }));
        check(result, "oversized message rejected",
              fails([&] { static_cast<void>(a.push(std::string(1024 * 1024 + 1, 'x'), true, true)); }));
    }
    for (const auto& request :
         std::vector<PaperRequest>{{"GET", "https://api.alpaca.markets/v2/account", ""},
                                   {"GET", "https://paper-api.alpaca.markets.evil.invalid/v2/account", ""},
                                   {"POST", "https://paper-api.alpaca.markets/v2/account", "{}"},
                                   {"GET", "https://paper-api.alpaca.markets/v2/account?redirect=x", ""}}) {
        check(result, "credential-bearing boundary rejects unsupported destination",
              fails([&] { static_cast<void>(validate_paper_request(request)); }));
    }
    {
        Fixture f{directory_, "lifecycle"};
        f.ready();
        check(result, "two stable account collections reconcile", f.wire.requests.size() == 6);
        check(result, "submission returns provider response",
              f.session.submit({"transport-buy", "SPY", execution::Side::buy, 10, 10001, 0}).status == 200);
        check(result, "durable reservation precedes external result",
              f.runtime.snapshot().at("account").at("reserved_cash_units").get<std::int64_t>() ==
                  1000100000000LL);
        const Json fill{{"stream", "trade_updates"},
                        {"data",
                         {{"event", "partial_fill"},
                          {"order", order(4)},
                          {"execution_id", "cccccccc-cccc-4ccc-8ccc-cccccccccccc"},
                          {"qty", "4"},
                          {"price", "100.005"},
                          {"position_qty", "4"},
                          {"timestamp", timestamp}}}};
        f.wire.messages.push_back(fill.dump());
        check(result, "streamed incremental fill reaches exact ledger",
              f.session.drain() == 1 &&
                  f.runtime.snapshot().at("account").at("cash_units").get<std::int64_t>() == 1599980000000LL);
        check(result, "cancel HTTP acceptance retains reservation",
              f.session.cancel("transport-buy", "cancel-1").status == 204 &&
                  f.runtime.snapshot().at("account").at("reserved_cash_units").get<std::int64_t>() ==
                      600060000000LL);
        auto canceled = order(4);
        canceled["status"] = "canceled";
        f.wire.messages.push_back(
            Json{{"stream", "trade_updates"},
                 {"data", {{"event", "canceled"}, {"order", canceled}, {"timestamp", timestamp}}}}
                .dump());
        static_cast<void>(f.session.drain());
        check(result, "stream confirmation releases remaining reservation",
              f.runtime.snapshot().at("account").at("reserved_cash_units").get<std::int64_t>() == 0);
        result.cases.push_back(f.result("lifecycle"));
    }
    {
        Fixture f{directory_, "uncertain"};
        f.ready();
        f.wire.uncertain_post = true;
        check(result, "uncertain POST fails without retry", fails([&] {
                  static_cast<void>(
                      f.session.submit({"transport-buy", "SPY", execution::Side::buy, 10, 10001, 0}));
              }));
        check(result, "exactly one POST attempted",
              std::count_if(f.wire.requests.begin(), f.wire.requests.end(),
                            [](const auto& r) { return r.method == "POST"; }) == 1);
        f.wire.responses.push_back({404, R"({"message":"not found"})", "", "simulated"});
        check(result, "404 lookup never resubmits", f.session.lookup("transport-buy").status == 404);
        check(result, "uncertain/404 reservation remains",
              f.runtime.snapshot().at("account").at("reserved_cash_units").get<std::int64_t>() ==
                      1000100000000LL &&
                  !f.runtime.snapshot().at("account").at("connected").get<bool>());
        result.cases.push_back(f.result("uncertain"));
    }
    {
        Fixture f{directory_, "rate_limit"};
        f.wire.responses = {
            {429, "{}", "1", "simulated"}, {429, "{}", "2", "simulated"}, {200, account(), "", "simulated"}};
        check(result, "GET 429 retries are bounded",
              f.session.read(AlpacaPaperRequests{}.account()).status == 200 && f.wire.requests.size() == 3 &&
                  f.waits == 2);
        f.wire.responses = {{429, "{}", "30", "simulated"}};
        check(result, "long Retry-After closes routing",
              fails([&] { static_cast<void>(f.session.read(AlpacaPaperRequests{}.account())); }));
        result.cases.push_back(f.result("rate_limit"));
    }
    {
        Fixture f{directory_, "unauthorized"};
        f.wire.messages = {R"({"stream":"authorization","data":{"status":"unauthorized"}})"};
        check(result, "authorization denial closes routing",
              fails([&] { f.session.connect(); }) &&
                  !f.runtime.snapshot().at("account").at("connected").get<bool>());
        result.cases.push_back(f.result("unauthorized"));
    }
    {
        Fixture f{directory_, "peer_close"};
        f.ready();
        f.wire.close_on_poll = true;
        check(result, "peer close gates the durable bridge",
              fails([&] { static_cast<void>(f.session.drain()); }) &&
                  !f.runtime.snapshot().at("account").at("connected").get<bool>());
        result.cases.push_back(f.result("peer_close"));
    }
    {
        Fixture f{directory_, "unstable"};
        f.wire.handshake();
        f.session.connect();
        for (int i = 0; i < 3; ++i) {
            for (const auto cash : {"2000", "2001"}) {
                f.wire.responses.push_back({200, account(cash), "", "simulated"});
                f.wire.responses.push_back({200, "[]", "", "simulated"});
                f.wire.responses.push_back({200, "[]", "", "simulated"});
            }
        }
        check(result, "changing snapshot cannot open routing",
              !f.session.synchronize() && !f.runtime.snapshot().at("account").at("connected").get<bool>());
        result.cases.push_back(f.result("unstable"));
    }
    {
        Fixture f{directory_, "invalid_stream"};
        f.ready();
        f.wire.messages = {"{malformed"};
        check(result, "invalid raw input is captured before decoder rejection",
              fails([&] { static_cast<void>(f.session.drain()); }) && f.capture.records() > 0 &&
                  f.runtime.snapshot().at("unresolved_evidence").get<bool>());
        result.cases.push_back(f.result("invalid_stream"));
    }
    return result;
}

PaperTransportResult PaperTransportDiagnostic::run_control() const {
    PaperTransportResult result{};
    {
        Fixture f{directory_, "deadline_stop"};
        f.ready();
        f.wire.health_ack = true;
        PaperReceipt now{"2026-10-26T14:00:00Z", 0};
        auto control =
            PaperControl::create(execution::create_journal_file(f.directory / "control.journal"),
                                 {"constructed-control"}, f.runtime, f.session, [&] { return now; });
        check(result, "fresh matching WebSocket PONG and exact snapshots precede submission",
              control.submit({"transport-buy", "SPY", execution::Side::buy, 10, 10001, 0},
                             "2026-10-26T14:00:01Z")
                      .status == 200);
        now = {"2026-10-26T14:00:01Z", 1000};
        control.service();
        check(result, "deadline requests cancellation without releasing reservation",
              f.runtime.snapshot().at("orders").at(0).at("status").get<std::string>() == "pending_cancel" &&
                  f.runtime.snapshot().at("account").at("reserved_cash_units").get<std::int64_t>() ==
                      1000100000000LL);
        control.stop("operator_stop");
        control.service();
        check(result, "stop is durable and repeated service does not repeat DELETE",
              control.snapshot().at("stopped").get<bool>() &&
                  std::count_if(f.wire.requests.begin(), f.wire.requests.end(),
                                [](const auto& r) { return r.method == "DELETE"; }) == 1);
        check(result, "stopped control refuses another submission", fails([&] {
                  static_cast<void>(control.submit({"later-buy", "SPY", execution::Side::buy, 1, 10001, 0},
                                                   "2026-10-26T14:01:00Z"));
              }));
        auto evidence = f.result("deadline_stop");
        evidence["control"] = control.snapshot();
        result.cases.push_back(std::move(evidence));
    }
    {
        Fixture f{directory_, "stale_stream"};
        f.ready();
        PaperReceipt now{"2026-10-26T14:00:00Z", 0};
        auto control =
            PaperControl::create(execution::create_journal_file(f.directory / "control.journal"),
                                 {"constructed-control"}, f.runtime, f.session, [&] { return now; });
        control.service();
        check(result, "missing matching WebSocket PONG durably stops control",
              control.snapshot().at("reason").get<std::string>() == "health_probe_failed");
        check(result, "failed stream health closes execution routing",
              !f.runtime.snapshot().at("account").at("connected").get<bool>());
        auto evidence = f.result("stale_stream");
        evidence["control"] = control.snapshot();
        result.cases.push_back(std::move(evidence));
    }
    {
        Fixture f{directory_, "clock_restart"};
        f.ready();
        f.wire.health_ack = true;
        PaperReceipt now{"2026-10-26T14:00:00Z", 1000};
        {
            auto control =
                PaperControl::create(execution::create_journal_file(f.directory / "control.journal"),
                                     {"constructed-control"}, f.runtime, f.session, [&] { return now; });
            control.service();
            now = {"2026-10-26T13:59:59Z", 1001};
            control.service();
            check(result, "wall clock regression durably stops control",
                  control.snapshot().at("reason").get<std::string>() == "clock_regression");
        }
        auto recovered =
            PaperControl::recover(execution::open_journal_file(f.directory / "control.journal"),
                                  {"constructed-control"}, f.runtime, f.session, [&] { return now; });
        check(result, "recovery preserves stop and discards process-local health",
              recovered.snapshot().at("stopped").get<bool>() &&
                  recovered.snapshot().at("last_health_monotonic_ms").is_null());
        auto evidence = f.result("clock_restart");
        evidence["control"] = recovered.snapshot();
        result.cases.push_back(std::move(evidence));
    }
    result.detail =
        "Constructed deadline, stop and WebSocket-heartbeat controls; no operational broker orders.";
    return result;
}
} // namespace trading
