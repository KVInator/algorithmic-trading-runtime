#include "paper_control.hpp"
#include "journal_frame.hpp"
#include "content_digest.hpp"

namespace trading {
namespace {
using Json = nlohmann::json;

void require(bool value, const char* message) {
    if (!value)
        throw std::invalid_argument(message);
}

Json header(const PaperControlConfiguration& c, bool legacy = false) {
    auto value = Json{{"kind", "paper_control_header"},
                      {"schema_version", 1},
                      {"owner", c.owner},
                      {"health_interval_ms", c.health_interval_ms},
                      {"health_budget_ms", c.health_budget_ms},
                      {"order_tif", "DAY"},
                      {"model_contract", "paper_control_stop_deadline_v1"}};
    if (!legacy) {
        value["schema_version"] = 2;
        value["model_contract"] = "paper_control_stop_deadline_v2";
        value["health_model"] = "websocket_ping_pong_v1";
    }
    return value;
}

Json apply_command(Json state, const Json& command) {
    const auto kind = command.at("kind").get<std::string>();
    if (kind == "stop") {
        require(command.size() == 2, "invalid stop shape");
        const auto reason = command.at("reason").get<std::string>();
        require(!reason.empty() && reason.size() <= 256, "invalid stop reason");
        if (!state.at("stopped").get<bool>()) {
            state["stopped"] = true;
            state["reason"] = reason;
        }
    } else if (kind == "deadline") {
        require(command.size() == 3 && !state.at("stopped").get<bool>(), "deadline while stopped");
        const auto id = command.at("client_id").get<std::string>();
        static_cast<void>(AlpacaPaperRequests{}.lookup(id));
        static_cast<void>(BrokerTimestamp{command.at("expires_at").get<std::string>()});
        require(!state.at("deadlines").contains(id) && state.at("deadlines").size() < 256,
                "duplicate identity or deadline capacity exhausted");
        state["deadlines"][id] = command.at("expires_at");
    } else
        throw std::invalid_argument("unsupported control command");
    return state;
}

bool terminal(const Json& order) {
    const auto status = order.at("status").get<std::string>();
    return status == "filled" || status == "canceled" || status == "rejected";
}
} // namespace

PaperControl::PaperControl(std::unique_ptr<execution::JournalStorage> storage, PaperControlConfiguration c,
                           execution::JournaledPaperRuntime& runtime, PaperSession& session,
                           PaperSession::Clock clock)
    : storage_{std::move(storage)}, configuration_{std::move(c)}, runtime_{runtime}, session_{session},
      clock_{std::move(clock)} {
    require(storage_ && clock_, "control storage and clock required");
    static_cast<void>(AlpacaPaperRequests{}.lookup(configuration_.owner));
    require(configuration_.health_interval_ms >= 100 && configuration_.health_interval_ms <= 60000 &&
                configuration_.health_budget_ms > 0 && configuration_.health_budget_ms <= 10000,
            "control health limits unsupported");
}

PaperControl PaperControl::create(std::unique_ptr<execution::JournalStorage> storage,
                                  PaperControlConfiguration c, execution::JournaledPaperRuntime& runtime,
                                  PaperSession& session, PaperSession::Clock clock) {
    PaperControl r{std::move(storage), std::move(c), runtime, session, std::move(clock)};
    try {
        require(r.storage_->read_all().empty(), "new control journal must be empty");
        auto [frame, digest] = execution::JournalFrame::encode(header(r.configuration_));
        r.storage_->append_and_sync(frame);
        r.bytes_ = frame.size();
        r.previous_ = std::move(digest);
        return r;
    } catch (...) {
        try {
            session.disconnect();
        } catch (...) {
        }
        throw;
    }
}

PaperControl PaperControl::recover(std::unique_ptr<execution::JournalStorage> storage,
                                   PaperControlConfiguration c, execution::JournaledPaperRuntime& runtime,
                                   PaperSession& session, PaperSession::Clock clock) {
    PaperControl r{std::move(storage), std::move(c), runtime, session, std::move(clock)};
    try {
        const auto raw = r.storage_->read_all();
        require(!raw.empty() && raw.size() <= execution::maximum_journal_bytes && raw.back() == '\n',
                "control journal missing, oversized or torn; preserve evidence");
        std::size_t offset = 0;
        bool seen = false;
        while (offset < raw.size()) {
            const auto end = raw.find('\n', offset);
            const auto [row, digest] =
                execution::JournalFrame::decode(std::string_view{raw}.substr(offset, end - offset));
            if (!seen) {
                require(row == header(r.configuration_) || row == header(r.configuration_, true),
                        "control configuration/header differs");
                r.journal_schema_version_ = row.at("schema_version").get<unsigned>();
                seen = true;
            } else {
                require(++r.sequence_ <= 2000 && row.at("sequence").get<std::size_t>() == r.sequence_ &&
                            row.at("previous_sha256").get<std::string>() == r.previous_,
                        "control sequence/chain differs");
                r.state_ = apply_command(r.state_, row.at("command"));
                require(row.size() == 4 && row.at("state") == r.state_, "control replay checkpoint differs");
            }
            r.previous_ = digest;
            offset = end + 1;
        }
        r.bytes_ = raw.size();
        // A recovered owner never restores process-local health or submission eligibility.
        r.commit({{"kind", "stop"}, {"reason", "restart_requires_review"}});
        session.disconnect();
        return r;
    } catch (...) {
        try {
            session.disconnect();
        } catch (...) {
        }
        throw;
    }
}

void PaperControl::commit(Json command) {
    require(healthy_, "control writer uncertain; recovery required");
    try {
        require(sequence_ < 2000, "control record bound exhausted");
        auto candidate = apply_command(state_, command);
        auto [frame, digest] = execution::JournalFrame::encode({{"sequence", sequence_ + 1},
                                                                {"previous_sha256", previous_},
                                                                {"command", std::move(command)},
                                                                {"state", candidate}});
        require(frame.size() <= execution::maximum_journal_bytes - bytes_, "control journal exhausted");
        storage_->append_and_sync(frame);
        state_ = std::move(candidate);
        previous_ = std::move(digest);
        ++sequence_;
        bytes_ += frame.size();
    } catch (...) {
        healthy_ = false;
        try {
            session_.disconnect();
        } catch (...) {
        }
        throw;
    }
}

PaperReceipt PaperControl::observe_clock() {
    PaperReceipt now;
    try {
        now = clock_();
        static_cast<void>(BrokerTimestamp{now.utc});
        require(now.monotonic_ms >= 0, "negative control monotonic clock");
    } catch (...) {
        commit({{"kind", "stop"}, {"reason", "clock_unavailable"}});
        throw;
    }
    if (last_clock_ && (now.monotonic_ms < last_clock_->monotonic_ms ||
                        BrokerTimestamp{now.utc}.value() < BrokerTimestamp{last_clock_->utc}.value()))
        commit({{"kind", "stop"}, {"reason", "clock_regression"}});
    last_clock_ = now;
    return now;
}

void PaperControl::cancel_due(const PaperReceipt& now) {
    const auto snapshot = runtime_.snapshot();
    for (const auto& order : snapshot.at("orders")) {
        if (terminal(order))
            continue;
        const auto id = order.at("client_id").get<std::string>();
        const bool managed = state_.at("deadlines").contains(id);
        if (!managed && !state_.at("stopped").get<bool>())
            commit({{"kind", "stop"}, {"reason", "unmanaged_order"}});
        const bool due = !managed || state_.at("stopped").get<bool>() ||
                         BrokerTimestamp{state_.at("deadlines").at(id).get<std::string>()}.value() <=
                             BrokerTimestamp{now.utc}.value();
        if (!due || !order.at("pending_cancel_id").get<std::string>().empty())
            continue;
        if (!snapshot.at("broker_bindings").contains(id)) {
            if (!state_.at("stopped").get<bool>())
                commit({{"kind", "stop"}, {"reason", "missing_broker_identity"}});
            continue;
        }
        try {
            static_cast<void>(session_.cancel(id, "ctl-" + sha256(id).substr(0, 24)));
        } catch (const std::exception&) {
            last_action_error_ = "cancellation_unresolved";
            if (!state_.at("stopped").get<bool>())
                commit({{"kind", "stop"}, {"reason", "cancellation_unresolved"}});
        }
    }
}

void PaperControl::service() {
    require(healthy_, "control writer uncertain");
    const auto now = observe_clock();
    cancel_due(now);
    if (state_.at("stopped").get<bool>()) {
        if (runtime_.snapshot().at("account").at("connected").get<bool>()) {
            try {
                static_cast<void>(session_.drain());
            } catch (const std::exception&) {
                last_action_error_ = "stream_unavailable_after_stop";
            }
        }
        return;
    }
    if (last_health_ && now.monotonic_ms - *last_health_ >= 2 * configuration_.health_interval_ms) {
        stop("health_service_overdue");
        return;
    }
    if (!last_health_ || now.monotonic_ms - *last_health_ >= configuration_.health_interval_ms) {
        try {
            if (!session_.check_stream(std::chrono::milliseconds{configuration_.health_budget_ms})) {
                stop("health_reconciliation_failed");
                return;
            }
        } catch (const std::exception&) {
            stop("health_probe_failed");
            return;
        }
        const auto at = observe_clock();
        if (at.monotonic_ms - now.monotonic_ms >= configuration_.health_interval_ms) {
            stop("health_roundtrip_stale");
            return;
        }
        last_health_ = now.monotonic_ms;
        cancel_due(at);
    } else {
        try {
            static_cast<void>(session_.drain());
        } catch (const std::exception&) {
            stop("stream_failed");
        }
    }
}

void PaperControl::stop(std::string reason) {
    commit({{"kind", "stop"}, {"reason", std::move(reason)}});
    cancel_due(observe_clock());
}

PaperHttpResponse PaperControl::submit(const execution::OrderRequest& order, std::string expires_at) {
    service();
    require(healthy_ && !state_.at("stopped").get<bool>() && last_health_, "control submission gate closed");
    const auto now = observe_clock();
    require(!state_.at("stopped").get<bool>(), "control clock gate closed");
    const auto lifetime = BrokerTimestamp{expires_at}.value() - BrokerTimestamp{now.utc}.value();
    require(lifetime > std::chrono::nanoseconds{0} && lifetime <= std::chrono::hours{1},
            "deadline must be future and within one hour");
    // Validate the entire supported order before consuming its durable policy identity.
    static_cast<void>(AlpacaPaperRequests{}.submit(order));
    commit({{"kind", "deadline"}, {"client_id", order.id}, {"expires_at", expires_at}});
    try {
        return session_.submit(order, [&] {
            const auto at = observe_clock();
            require(healthy_ && !state_.at("stopped").get<bool>() &&
                        at.monotonic_ms - *last_health_ < configuration_.health_interval_ms &&
                        BrokerTimestamp{at.utc}.value() < BrokerTimestamp{expires_at}.value(),
                    "deadline or health expired before dispatch");
        });
    } catch (...) {
        stop("submission_unresolved");
        throw;
    }
}

Json PaperControl::snapshot() const {
    auto r = state_;
    r["healthy"] = healthy_;
    r["control_schema_version"] = journal_schema_version_;
    r["health_model"] =
        journal_schema_version_ == 2 ? "websocket_ping_pong_v1" : "legacy_subscription_check_v1";
    r["last_action_error"] = last_action_error_;
    r["durable_records"] = sequence_;
    r["journal_sha256"] = previous_;
    r["last_health_monotonic_ms"] = last_health_ ? Json(*last_health_) : Json(nullptr);
    r["execution"] = runtime_.snapshot();
    return r;
}
} // namespace trading
