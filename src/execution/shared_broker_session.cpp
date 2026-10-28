#include "shared_broker_session.hpp"
#include "shared_capital_json.hpp"
#include <algorithm>
#include <stdexcept>

namespace trading::execution {
using Json = nlohmann::json;
SharedBrokerSession::SharedBrokerSession(JournaledSharedCapital &runtime, BrokerPeer &peer,
                                         std::string account)
    : runtime_(runtime), peer_(peer), account_id_(std::move(account)) {}
void SharedBrokerSession::fault(std::int64_t at) noexcept {
    active_ = false;
    try {
        const auto now = std::max(at, runtime_.snapshot().at_seconds);
        static_cast<void>(runtime_.execute(SharedConnection{now, false}));
    } catch (const std::exception &) { /* An uncertain client writer already refuses state and dispatch. */
    }
}
void SharedBrokerSession::disconnect(std::int64_t at) {
    active_ = false;
    static_cast<void>(runtime_.execute(SharedConnection{at, false}));
}
std::uint64_t SharedBrokerSession::cursor() const noexcept {
    return cursor_;
}
const std::string &SharedBrokerSession::last_fault() const noexcept {
    return last_fault_;
}
void SharedBrokerSession::send(const SharedResult &result, std::int64_t at) {
    if (!active_) {
        if (!result.dispatch_orders.empty() || !result.dispatch_cancels.empty()) {
            throw std::logic_error("closed broker session received dispatch eligibility");
        }
        return;
    }
    for (const auto &[id, cancel] : result.dispatch_cancels) {
        const auto reply = peer_.exchange({{"kind", "cancel"},
                                           {"command_id", "cancel-" + cancel},
                                           {"at_seconds", at},
                                           {"order_id", id},
                                           {"cancel_id", cancel}});
        if (reply.at("status").get<std::string>() != "ok") {
            throw std::runtime_error("independent broker denied cancellation command");
        }
    }
    for (const auto &order : result.dispatch_orders) {
        const auto reply = peer_.exchange({{"kind", "submit"},
                                           {"command_id", "submit-" + order.id},
                                           {"at_seconds", at},
                                           {"order", broker_order_json(order)},
                                           {"time_in_force", "day"}});
        if (reply.at("status").get<std::string>() != "ok") {
            throw std::runtime_error("independent broker denied submission command");
        }
    }
}
void SharedBrokerSession::catch_up(std::int64_t at, bool dispatch) {
    std::size_t delivered = 0;
    while (true) {
        const auto page = peer_.exchange({{"kind", "activities"}, {"after", cursor_}, {"limit", 16}});
        const auto &rows = page.at("activities");
        if (!rows.is_array() || rows.size() > 16 || !page.at("next").is_number_unsigned() ||
            !page.at("end").is_number_unsigned() ||
            page.at("next").get<std::uint64_t>() != cursor_ + rows.size() ||
            page.at("end").get<std::uint64_t>() < cursor_ + rows.size()) {
            throw std::runtime_error("activity page bounds or cursor differs");
        }
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (!rows[i].at("cursor").is_number_unsigned() ||
                rows[i].at("cursor").get<std::uint64_t>() != cursor_ + i + 1 ||
                !rows[i].at("at_seconds").is_number_integer() ||
                rows[i].at("at_seconds").get<std::int64_t>() > at) {
                throw std::runtime_error("activity gap, reordering or future receipt");
            }
        }
        for (const auto &row : rows) {
            if (++delivered > 8192) {
                throw std::runtime_error("activity catch-up bound reached");
            }
            const auto report = parse_broker_report(row.at("report"));
            if (report.kind == ReportKind::rejected) {
                disconnect(at);
                dispatch = false;
            }
            const auto applied = runtime_.execute(SharedDelivery{at, report});
            if (applied.outcome == Outcome::quarantined || applied.outcome == Outcome::denied) {
                throw std::runtime_error("independent broker activity contradicts client ledger");
            }
            ++cursor_;
            if (dispatch) {
                send(applied, at);
            } else if (!applied.dispatch_orders.empty() || !applied.dispatch_cancels.empty()) {
                throw std::logic_error("recovery catch-up produced dispatch while closed");
            }
        }
        if (rows.empty()) {
            if (page.at("end").get<std::uint64_t>() != cursor_) {
                throw std::runtime_error("activity page omitted retained history");
            }
            break;
        }
    }
}
bool SharedBrokerSession::connect(std::int64_t at) {
    try {
        last_fault_.clear();
        disconnect(at);
        cursor_ = 0;
        const auto hello = peer_.exchange({{"kind", "hello"}});
        const auto config = runtime_.configuration();
        auto symbols = config.symbols;
        std::sort(symbols.begin(), symbols.end());
        if (hello.at("protocol").get<int>() != 1 ||
            hello.at("account_id").get<std::string>() != account_id_ ||
            hello.at("policy_sha256").get<std::string>() != TRADING_BROKER_PROTOCOL_SHA256 ||
            hello.at("usd_scale").get<std::int64_t>() != shared_usd_scale ||
            hello.at("symbols").get<std::vector<std::string>>() != symbols ||
            hello.at("initial_cash_units").get<MoneyUnits>() != config.initial_cash_units ||
            hello.at("fee_per_share_units").get<MoneyUnits>() != config.fee_per_share_ceiling_units) {
            throw std::runtime_error("peer account, units, fee or policy differs");
        }
        catch_up(at, false);
        const auto state = peer_.exchange({{"kind", "snapshot"}});
        if (state.at("account_id").get<std::string>() != account_id_ ||
            state.at("available_cash_units") != state.at("cash_units") ||
            state.at("at_seconds").get<std::int64_t>() > at) {
            throw std::runtime_error("peer snapshot identity or availability differs");
        }
        PortfolioBrokerSnapshot broker{state.at("cash_units").get<MoneyUnits>(),
                                       state.at("positions").get<std::map<std::string, Shares>>(),
                                       {}};
        for (const auto &o : state.at("open_orders")) {
            broker.open_orders.push_back(
                {parse_broker_order(o.at("order")), o.at("filled_shares").get<Shares>()});
        }
        static_cast<void>(runtime_.execute(SharedConnection{at, true}));
        active_ = runtime_.execute(SharedReconciliation{at, broker}).outcome == Outcome::applied;
        if (!active_) {
            fault(at);
        }
        return active_;
    } catch (const std::exception &error) {
        last_fault_ = error.what();
        fault(at);
        return false;
    }
}
bool SharedBrokerSession::drain(std::int64_t at) {
    if (!active_) {
        return false;
    }
    try {
        catch_up(at, true);
        return active_;
    } catch (const std::exception &error) {
        last_fault_ = error.what();
        fault(at);
        return false;
    }
}
SharedResult SharedBrokerSession::target(const SharedTargetBatch &batch) {
    if (!active_) {
        return {Outcome::denied, "broker session requires activity catch-up and exact reconciliation"};
    }
    try {
        const auto state = peer_.exchange({{"kind", "snapshot"}});
        if (batch.at_seconds < state.at("session_open_seconds").get<std::int64_t>() ||
            batch.at_seconds >= state.at("session_close_seconds").get<std::int64_t>()) {
            throw std::runtime_error("target outside independent broker session");
        }
        for (const auto &q : batch.quotes) {
            const auto &values = state.at("quotes");
            const auto found = std::find_if(values.begin(), values.end(), [&](const auto &v) {
                return v.at("symbol").template get<std::string>() == q.symbol;
            });
            if (found == values.end() ||
                found->at("source_at_seconds").get<std::int64_t>() != q.source_at_seconds ||
                found->at("bid_units").get<MoneyUnits>() != q.bid_units ||
                found->at("ask_units").get<MoneyUnits>() != q.ask_units) {
                throw std::runtime_error("client target quote differs from independent broker source");
            }
        }
        auto result = runtime_.execute(batch);
        send(result, batch.at_seconds);
        return result;
    } catch (const std::exception &) {
        fault(batch.at_seconds);
        throw;
    }
}
} // namespace trading::execution
