#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "paper_transport_report.hpp"
#include "paper_control.hpp"
#include <deque>
#include <iostream>

namespace {
using namespace trading;
using namespace trading::execution;
using Json = nlohmann::json;
const PaperExecutionConfiguration config{"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "2000", {20, 4}};
const std::string timestamp{"2026-10-26T14:00:00.123456789Z"};
const OrderRequest intent{"test-buy", "SPY", Side::buy, 10, 10001, 0};

void require(bool value, std::string_view message) {
    if (!value)
        throw std::logic_error(std::string{message});
}

template <class F> void rejects(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "unsupported or uncertain operation must reject");
}

Json broker_order() {
    return {{"id", "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"},
            {"client_order_id", "test-buy"},
            {"symbol", "SPY"},
            {"asset_class", "us_equity"},
            {"side", "buy"},
            {"qty", "10"},
            {"filled_qty", "0"},
            {"filled_avg_price", nullptr},
            {"limit_price", "100.01"},
            {"type", "limit"},
            {"order_class", "simple"},
            {"time_in_force", "day"},
            {"extended_hours", false},
            {"status", "new"},
            {"updated_at", timestamp}};
}

class Wire final : public PaperWire {
public:
    std::deque<PaperHttpResponse> replies;
    std::deque<std::string> messages;
    std::vector<PaperRequest> requests;
    std::function<void(const PaperRequest&)> before_reply;
    Json open_orders = Json::array();
    unsigned waits = 0;
    bool uncertain = false, health_ack = false;

    PaperHttpResponse request(const PaperRequest& request) override {
        requests.push_back(request);
        if (before_reply)
            before_reply(request);
        if (uncertain && request.method == "POST")
            throw std::runtime_error("lost response");
        if (!replies.empty()) {
            auto response = replies.front();
            replies.pop_front();
            return response;
        }
        if (request.url.ends_with("/account"))
            return {200,
                    Json{{"id", config.account_id},
                         {"cash", "2000"},
                         {"currency", "USD"},
                         {"status", "ACTIVE"},
                         {"trading_blocked", false},
                         {"account_blocked", false}}
                        .dump(),
                    "", "test"};
        if (request.url.ends_with("/positions"))
            return {200, "[]", "", "test"};
        if (request.url.find("?status=open") != std::string::npos)
            return {200, open_orders.dump(), "", "test"};
        if (request.method == "DELETE")
            return {204, "", "", "test"};
        return {200, broker_order().dump(), "", "test"};
    }

    void open_authenticated_stream() override {}

    void subscribe_trade_updates() override {
        if (health_ack)
            messages.push_back(R"({"stream":"listening","data":{"streams":["trade_updates"]}})");
    }

    void close() noexcept override {}

    std::optional<std::string> poll(std::chrono::milliseconds) override {
        if (messages.empty())
            return std::nullopt;
        auto message = std::move(messages.front());
        messages.pop_front();
        return message;
    }

    void handshake() {
        messages.push_back(R"({"stream":"authorization","data":{"status":"authorized"}})");
        messages.push_back(R"({"stream":"listening","data":{"streams":["trade_updates"]}})");
    }

    std::size_t posts() const {
        return static_cast<std::size_t>(std::count_if(requests.begin(), requests.end(),
                                                      [](const auto& r) { return r.method == "POST"; }));
    }
};

class Fixture {
public:
    std::filesystem::path path;
    Wire wire;
    PaperCapture capture;
    JournaledPaperRuntime runtime;
    PaperSession session;

    explicit Fixture(const std::filesystem::path& directory)
        : path{directory}, capture{directory / "capture"},
          runtime{JournaledPaperRuntime::create(create_journal_file(directory / "runtime.journal"), config)},
          session{runtime, wire, capture, [] { return PaperReceipt{timestamp, 1}; },
                  [this](auto) { ++wire.waits; }} {}

    void ready() {
        wire.handshake();
        session.connect();
        require(session.synchronize(), "flat baseline reconciles");
    }

    bool gated() const {
        const auto account = runtime.snapshot().at("account");
        return !account.at("connected").get<bool>() || account.at("reconciliation_required").get<bool>();
    }

    bool reserved() const {
        return runtime.snapshot().at("account").at("reserved_cash_units").get<std::int64_t>() ==
               1000100000000LL;
    }
};

class Contracts {
public:
    explicit Contracts(std::filesystem::path root) : root_{std::move(root)} {}

    void run() const {
        framing();
        request_policy();
        dispatch_failures();
        subscription();
        snapshot_activity();
        restart();
        control_faults();
    }

private:
    std::filesystem::path root_;

    void control_faults() const {
        struct Memory {
            std::string bytes;
            bool fail{false};
        };

        class Storage final : public JournalStorage {
        public:
            std::shared_ptr<Memory> memory;

            explicit Storage(std::shared_ptr<Memory> m) : memory{std::move(m)} {}

            std::string read_all() override { return memory->bytes; }

            void append_and_sync(std::string_view bytes) override {
                memory->bytes.append(bytes);
                if (memory->fail)
                    throw JournalError("constructed uncertain append");
            }
        };

        {
            Fixture f{root_ / "control_failure"};
            f.ready();
            f.wire.health_ack = true;
            auto m = std::make_shared<Memory>();
            auto c = PaperControl::create(std::make_unique<Storage>(m), {"test-control"}, f.runtime,
                                          f.session, [] { return PaperReceipt{timestamp, 1000}; });
            c.service();
            m->fail = true;
            rejects([&] { c.stop("operator_stop"); });
            require(!c.snapshot().at("healthy").get<bool>() && f.gated(),
                    "uncertain stop append closes both gates");
            rejects([&] { static_cast<void>(c.submit(intent, "2026-10-26T14:00:30Z")); });
            require(f.wire.posts() == 0, "poisoned control cannot dispatch");
            m->fail = false;
            auto recovered = PaperControl::recover(std::make_unique<Storage>(m), {"test-control"}, f.runtime,
                                                   f.session, [] { return PaperReceipt{timestamp, 1000}; });
            require(recovered.snapshot().at("stopped").get<bool>(),
                    "committed uncertain stop survives recovery");
            auto corrupted = std::make_shared<Memory>();
            corrupted->bytes = m->bytes;
            corrupted->bytes[20] = 'X';
            rejects([&] {
                static_cast<void>(PaperControl::recover(std::make_unique<Storage>(corrupted),
                                                        {"test-control"}, f.runtime, f.session,
                                                        [] { return PaperReceipt{timestamp, 1000}; }));
            });
            corrupted->bytes = m->bytes.substr(0, m->bytes.size() - 1);
            rejects([&] {
                static_cast<void>(PaperControl::recover(std::make_unique<Storage>(corrupted),
                                                        {"test-control"}, f.runtime, f.session,
                                                        [] { return PaperReceipt{timestamp, 1000}; }));
            });
        }
        {
            Fixture f{root_ / "control_active_restart"};
            f.ready();
            f.wire.health_ack = true;
            auto m = std::make_shared<Memory>();
            {
                auto c = PaperControl::create(std::make_unique<Storage>(m), {"test-control"}, f.runtime,
                                              f.session, [] { return PaperReceipt{timestamp, 1000}; });
                static_cast<void>(c.submit(intent, "2026-10-26T14:00:30Z"));
            }
            auto c = PaperControl::recover(std::make_unique<Storage>(m), {"test-control"}, f.runtime,
                                           f.session, [] { return PaperReceipt{timestamp, 1000}; });
            require(c.snapshot().at("reason").get<std::string>() == "restart_requires_review" &&
                        f.reserved() && f.gated(),
                    "active-owner restart retains deadline/reservation and durably stops");
            const auto posts = f.wire.posts();
            rejects([&] { static_cast<void>(c.submit(intent, "2026-10-26T14:00:30Z")); });
            require(f.wire.posts() == posts, "restart does not resend uncertain order");
        }
        {
            Fixture f{root_ / "control_overdue"};
            f.ready();
            f.wire.health_ack = true;
            std::int64_t tick = 1000;
            auto m = std::make_shared<Memory>();
            auto c = PaperControl::create(std::make_unique<Storage>(m), {"test-control"}, f.runtime,
                                          f.session, [&] { return PaperReceipt{timestamp, tick}; });
            c.service();
            tick += 60000;
            c.service();
            require(c.snapshot().at("reason").get<std::string>() == "health_service_overdue",
                    "missed service window permanently stops owner");
        }
        {
            Fixture f{root_ / "dispatch_deadline"};
            f.ready();
            unsigned guards = 0;
            rejects([&] {
                static_cast<void>(f.session.submit(intent, [&] {
                    if (++guards == 2)
                        throw std::runtime_error("deadline elapsed after capture");
                }));
            });
            require(guards == 2 && f.wire.posts() == 0 && f.reserved() && f.gated(),
                    "dispatch eligibility is checked after raw request capture and before wire submission");
        }
        {
            Fixture f{root_ / "stop_partial_inventory"};
            f.ready();
            f.wire.health_ack = true;
            auto m = std::make_shared<Memory>();
            auto c = PaperControl::create(std::make_unique<Storage>(m), {"test-control"}, f.runtime,
                                          f.session, [] { return PaperReceipt{timestamp, 1000}; });
            static_cast<void>(c.submit(intent, "2026-10-26T14:00:30Z"));
            auto order = broker_order();
            order["filled_qty"] = "4";
            order["filled_avg_price"] = "100.005";
            order["status"] = "partially_filled";
            f.wire.messages.push_back(Json{{"stream", "trade_updates"},
                                           {"data",
                                            {{"event", "partial_fill"},
                                             {"order", order},
                                             {"execution_id", "cccccccc-cccc-4ccc-8ccc-cccccccccccc"},
                                             {"qty", "4"},
                                             {"price", "100.005"},
                                             {"position_qty", "4"},
                                             {"timestamp", timestamp}}}}
                                          .dump());
            static_cast<void>(f.session.drain());
            c.stop("operator_stop");
            require(c.snapshot().at("execution").at("account").at("position_shares").get<int>() == 4 &&
                        f.runtime.snapshot().at("account").at("reserved_cash_units").get<std::int64_t>() ==
                            600060000000LL,
                    "controlled stop retains partial inventory and unconfirmed remainder");
            order["status"] = "canceled";
            f.wire.messages.push_back(
                Json{{"stream", "trade_updates"},
                     {"data", {{"event", "canceled"}, {"order", order}, {"timestamp", timestamp}}}}
                    .dump());
            c.service();
            require(f.runtime.snapshot().at("account").at("reserved_cash_units").get<std::int64_t>() == 0 &&
                        f.runtime.snapshot().at("account").at("position_shares").get<int>() == 4,
                    "stopped service consumes terminal confirmation without liquidating inventory");
        }
        {
            Fixture f{root_ / "health_roundtrip_age"};
            f.ready();
            f.wire.health_ack = true;
            std::int64_t tick = 1000;
            bool advanced = false;
            f.wire.before_reply = [&](const PaperRequest&) {
                if (!advanced) {
                    tick += 30000;
                    advanced = true;
                }
            };
            auto m = std::make_shared<Memory>();
            auto c = PaperControl::create(std::make_unique<Storage>(m), {"test-control"}, f.runtime,
                                          f.session, [&] { return PaperReceipt{timestamp, tick}; });
            c.service();
            require(c.snapshot().at("reason").get<std::string>() == "health_roundtrip_stale",
                    "slow successful reconciliation cannot manufacture fresh stream health");
        }
    }

    void framing() const {
        PaperMessageAssembler assembler;
        require(assembler.push(std::string(1024 * 1024, 'x'), false, true)->size() == 1024 * 1024,
                "exactly one MiB remains supported");
        rejects([&] { static_cast<void>(assembler.push("", true, true)); });
        for (int i = 0; i < 4096; ++i)
            static_cast<void>(assembler.push("", true, false));
        rejects([&] { static_cast<void>(assembler.push("x", true, true)); });
        require(assembler.push("{}", false, true).value() == "{}", "fragment failure resets assembly");
    }

    void request_policy() const {
        auto request = AlpacaPaperRequests{}.submit(intent);
        auto body = Json::parse(request.body);
        body["extended_hours"] = true;
        request.body = body.dump();
        rejects([&] { static_cast<void>(validate_paper_request(request)); });
        body["extended_hours"] = false;
        body["qty"] = 10;
        request.body = body.dump();
        rejects([&] { static_cast<void>(validate_paper_request(request)); });
        for (const auto status : {301U, 401U, 422U, 429U}) {
            Fixture f{root_ / ("status_" + std::to_string(status))};
            f.ready();
            f.wire.replies.push_back({status, "{}", "1", "test"});
            rejects([&] { static_cast<void>(f.session.submit(intent)); });
            require(f.wire.posts() == 1 && f.wire.waits == 0 && f.reserved() && f.gated(),
                    "non-200 POST is uncertain and is never retried or redirected");
        }
        for (const auto status : {301U, 401U, 429U}) {
            Fixture f{root_ / ("get_" + std::to_string(status))};
            f.wire.replies = {
                {status, "{}", "0", "test"}, {status, "{}", "0", "test"}, {status, "{}", "0", "test"}};
            rejects([&] { static_cast<void>(f.session.read(AlpacaPaperRequests{}.account())); });
            require(f.wire.requests.size() == (status == 429 ? 3U : 1U) && f.gated(),
                    "GET retries only bounded rate limits");
        }
        Fixture f{root_ / "retry_date"};
        f.wire.replies.push_back({429, "{}", "Wed, 21 Oct 2026 07:28:00 GMT", "test"});
        rejects([&] { static_cast<void>(f.session.read(AlpacaPaperRequests{}.account())); });
        require(f.wire.requests.size() == 1 && f.wire.waits == 0, "unsupported Retry-After is not guessed");
    }

    void dispatch_failures() const {
        Fixture f{root_ / "capture_failure"};
        f.ready();
        // An orphan from an interrupted write must stop capture before a new
        // dispatch.
        const auto orphan = f.path / "capture" / ("body_" + std::to_string(f.capture.records()) + ".txt");
        write_new_text_file(orphan, "preserved orphan");
        rejects([&] { static_cast<void>(f.session.submit(intent)); });
        require(f.wire.posts() == 0 && f.reserved() && f.gated(),
                "capture failure retains durable intent and prevents dispatch");
        require(read_text_file(orphan) == "preserved orphan", "orphan evidence remains unchanged");
        Fixture cancel{root_ / "cancel_failure"};
        cancel.ready();
        static_cast<void>(cancel.session.submit(intent));
        cancel.wire.replies.push_back({422, "{}", "", "test"});
        rejects([&] { static_cast<void>(cancel.session.cancel(intent.id, "cancel-id")); });
        require(cancel.reserved() && cancel.gated(), "cancel failure never releases reservation");
    }

    void subscription() const {
        Fixture denied{root_ / "no_subscription"};
        denied.wire.messages = {R"({"stream":"authorization","data":{"status":"authorized"}})",
                                R"({"stream":"listening","data":{"streams":[]}})"};
        rejects([&] { denied.session.connect(); });
        require(denied.gated(), "authorization without subscription cannot open routing");
        Fixture f{root_ / "queued_update"};
        f.ready();
        static_cast<void>(f.session.submit(intent));
        f.wire.handshake();
        f.wire.messages.insert(
            f.wire.messages.begin() + 1,
            Json{{"stream", "trade_updates"},
                 {"data", {{"event", "new"}, {"order", broker_order()}, {"timestamp", timestamp}}}}
                .dump());
        f.session.connect();
        require(f.gated() && f.reserved(), "queued update cannot substitute for reconciliation");
        f.wire.open_orders = Json::array({broker_order()});
        require(f.session.synchronize(), "queued update retained through subscription acknowledgement");
        require(f.session.drain() == 0, "quiet authenticated stream remains usable");
    }

    void snapshot_activity() const {
        Fixture f{root_ / "snapshot_activity"};
        f.ready();
        static_cast<void>(f.session.submit(intent));
        f.wire.open_orders = Json::array({broker_order()});
        bool injected = false;
        f.wire.before_reply = [&](const PaperRequest& r) {
            if (!injected && r.url.ends_with("/account")) {
                injected = true;
                f.wire.messages.push_back(
                    Json{{"stream", "trade_updates"},
                         {"data", {{"event", "new"}, {"order", broker_order()}, {"timestamp", timestamp}}}}
                        .dump());
            }
        };
        const auto before = f.wire.requests.size();
        require(f.session.synchronize() && f.wire.requests.size() - before == 12,
                "intervening trade update forces a fresh pair of collections");
    }

    void restart() const {
        const auto directory = root_ / "restart";
        {
            Fixture f{directory};
            f.ready();
            f.wire.uncertain = true;
            rejects([&] { static_cast<void>(f.session.submit(intent)); });
        }
        auto runtime =
            JournaledPaperRuntime::recover(open_journal_file(directory / "runtime.journal"), config);
        require(!runtime.snapshot().at("account").at("connected").get<bool>(), "restart is gated");
        Wire wire;
        wire.open_orders = Json::array({broker_order()});
        PaperCapture capture{directory / "reconnect_capture"};
        PaperSession session{runtime, wire, capture, [] { return PaperReceipt{timestamp, 2}; }, [](auto) {}};
        wire.handshake();
        session.connect();
        require(session.lookup(intent.id).status == 200 && session.synchronize(),
                "stable-ID lookup and exact evidence resolve accepted uncertain "
                "intent");
        rejects([&] { static_cast<void>(session.submit(intent)); });
        require(wire.posts() == 0, "recovery and duplicate caller intent never resubmit");
    }
};
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "transport contracts require output root");
        const auto directory = create_unique_directory(argv[1], "paper-transport");
        const auto result = PaperTransportDiagnostic{directory}.run();
        PaperTransportReport{result}.write(directory);
        Contracts{directory / "contracts"}.run();
        const auto policy = PaperTransportDiagnostic{directory / "controls"}.run_control();
        require(policy.checks.size() == 8 && policy.cases.size() == 3, "control fault cases complete");
        const auto summary = Json::parse(read_text_file(directory / "summary.json"));
        for (const auto& a : summary.at("artifacts"))
            require(sha256_file(directory / a.at("path").get<std::string>()) ==
                        a.at("sha256").get<std::string>(),
                    "reported artifact digest matches retained bytes");
        std::cout << "Paper transport framing, dispatch, capture, subscription and "
                     "restart contracts passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Paper transport contracts failed: " << e.what() << '\n';
        return 1;
    }
}
