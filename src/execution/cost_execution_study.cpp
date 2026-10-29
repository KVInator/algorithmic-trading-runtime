#include "cost_execution_study.hpp"
#include "broker_service.hpp"
#include "shared_capital_json.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <stdexcept>

namespace trading::execution {
namespace {
using Json = nlohmann::json;
constexpr MoneyUnits scale = 1000000000, cent = scale / 100;
const std::vector<std::string> symbols{"GLD", "IWM", "QQQ", "SPY", "TLT"};
Json workload_json(const ExecutionStressWorkload &w) {
    return {{"id", w.id},
            {"symbol", w.symbol},
            {"side", w.side},
            {"path", w.path},
            {"requested_shares", w.requested_shares},
            {"displayed_shares", w.displayed_shares},
            {"spread_cents", w.spread_cents},
            {"outbound_seconds", w.outbound_seconds},
            {"receipt_seconds", w.receipt_seconds},
            {"cancel_processing_seconds", w.cancel_processing_seconds},
            {"source_age_seconds", w.source_age_seconds}};
}
Json policy_json(const ExecutionPolicy &p) {
    return {{"id", p.id},
            {"aggressive_limit", p.aggressive_limit},
            {"cancel_after_seconds", p.cancel_after_seconds}};
}
Json client_json(const OrderRuntime &client) {
    const auto a = client.portfolio_account();
    Json orders = Json::array(), positions = Json::object(), sells = Json::object(), buys = Json::object();
    for (const auto &[symbol, p] : a.positions) {
        positions[symbol] = p.shares;
        sells[symbol] = p.reserved_sell_shares;
        buys[symbol] = p.outstanding_buy_shares;
    }
    for (const auto &o : client.orders()) {
        orders.push_back({{"order", broker_order_json(o.request)},
                          {"filled_shares", o.filled_shares},
                          {"status", name(o.status)},
                          {"acknowledged", o.acknowledged},
                          {"pending_cancel_id", o.pending_cancel_id}});
    }
    return {{"cash_units", a.cash_units},
            {"positions", positions},
            {"reserved_cash_units", a.reserved_cash_units},
            {"reserved_sell_shares", sells},
            {"outstanding_buy_shares", buys},
            {"open_orders", a.open_orders},
            {"reconciliation_required", a.reconciliation_required},
            {"orders", orders}};
}
class CaseReplay {
  public:
    CaseReplay(ExecutionStressWorkload workload, ExecutionPolicy policy)
        : workload_(std::move(workload)), policy_(std::move(policy)),
          broker_(workload_.id + '_' + policy_.id), client_(symbols, broker_initial_cash_units, {1000, 4}),
          base_(parse_utc_seconds("2023-01-03T14:30:00Z").time_since_epoch().count()) {
        command({{"kind", "session"}, {"date", "2023-01-03"}}, 0);
        if (workload_.side == "sell") {
            const OrderRequest seed{"seed",      workload_.symbol, Side::buy, workload_.requested_shares,
                                    100 * scale, broker_fee_units};
            command(quote(1, 1, 100 * scale, 100 * scale, workload_.requested_shares), 1);
            require(client_.submit(seed) == Outcome::applied, "seed inventory admission failed");
            client_events_.push_back({{"kind", "submit"},
                                      {"at_seconds", 1},
                                      {"order", broker_order_json(seed)},
                                      {"state", client_json(client_)}});
            command({{"kind", "submit"}, {"order", broker_order_json(seed)}, {"time_in_force", "day"}}, 1);
            deliver(1);
            reconcile();
        }
        baseline_ = client_json(client_);
        const bool buy = workload_.side == "buy";
        const auto half = workload_.spread_cents * cent / 2;
        const MoneyUnits bid = 100 * scale - half, ask = 100 * scale + half;
        const auto limit = policy_.aggressive_limit ? (buy ? ask : bid) : (buy ? bid : ask);
        intent_ = {"measured", workload_.symbol, buy ? Side::buy : Side::sell, workload_.requested_shares,
                   limit,      broker_fee_units};
        observation_ = {intent_.side, intent_.shares, limit, bid, ask, bid, ask, 10, 10, -1, -1, 30, {}};
        const auto last = synthetic_execution_midpoint_path(workload_.path).back();
        const auto terminal = 100 * scale + (buy ? 1 : -1) * last.move_cents * cent;
        observation_.terminal_bid_cents = terminal - half;
        observation_.terminal_ask_cents = terminal + half;
    }
    Json run() {
        const auto steps = synthetic_execution_midpoint_path(workload_.path);
        for (std::int64_t at = 10; at <= 30; ++at) {
            command({{"kind", "advance"}}, at);
            for (std::size_t i = 0; i < steps.size(); ++i) {
                if (steps[i].at_seconds != at) {
                    continue;
                }
                const auto mid =
                    100 * scale + (workload_.side == "buy" ? 1 : -1) * steps[i].move_cents * cent;
                const auto half = workload_.spread_cents * cent / 2;
                command(quote(static_cast<std::int64_t>(i + 2), at - workload_.source_age_seconds, mid - half,
                              mid + half, workload_.displayed_shares),
                        at);
            }
            if (at == 10) {
                require(client_.submit(intent_) == Outcome::applied,
                        "measured client order admission failed");
                client_events_.push_back({{"kind", "submit"},
                                          {"at_seconds", at},
                                          {"order", broker_order_json(intent_)},
                                          {"state", client_json(client_)}});
            }
            if (at == 10 + workload_.outbound_seconds) {
                command({{"kind", "submit"}, {"order", broker_order_json(intent_)}, {"time_in_force", "day"}},
                        at);
            }
            cancellations(at);
            deliver(at);
            if (at == 10 + policy_.cancel_after_seconds) {
                const auto status = client_.order(intent_.id).status;
                if (status != OrderStatus::filled && status != OrderStatus::canceled &&
                    status != OrderStatus::rejected) {
                    require(client_.request_cancel(intent_.id, "deadline") == Outcome::applied,
                            "client cancellation admission failed");
                    cancel_dispatch_ = at;
                    cancel_at_ = at + workload_.outbound_seconds;
                    complete_at_ = *cancel_at_ + workload_.cancel_processing_seconds;
                    client_events_.push_back(
                        {{"kind", "cancel"}, {"at_seconds", at}, {"state", client_json(client_)}});
                    cancellations(at);
                    deliver(at);
                }
            }
        }
        for (const auto &d : pending_) {
            require(d.delivered, "common horizon censored a pending receipt");
        }
        reconcile();
        const auto final = client_json(client_);
        require(
            final.at("open_orders").get<std::size_t>() == 0 &&
                final.at("reserved_cash_units").get<MoneyUnits>() == 0 &&
                final.at("reserved_sell_shares").get<std::map<std::string, Shares>>() ==
                    std::map<std::string, Shares>{{"GLD", 0}, {"IWM", 0}, {"QQQ", 0}, {"SPY", 0}, {"TLT", 0}},
            "terminal account retained reservations or open orders");
        const auto metrics = ExecutionQuality{}.measure(observation_, scale);
        const auto sign = intent_.side == Side::buy ? 1 : -1;
        require(final.at("cash_units").get<MoneyUnits>() - baseline_.at("cash_units").get<MoneyUnits>() ==
                    -sign * metrics.at("fill_notional_units").get<MoneyUnits>() -
                        metrics.at("fees_twice_units").get<MoneyUnits>() / 2,
                "measured cash change differs from fills and fees");
        require(final.at("positions").at(workload_.symbol).get<Shares>() -
                        baseline_.at("positions").at(workload_.symbol).get<Shares>() ==
                    sign * metrics.at("filled_quantity").get<Shares>(),
                "measured inventory change differs from fills");
        return {{"id", workload_.id + '_' + policy_.id},
                {"workload", workload_json(workload_)},
                {"policy", policy_json(policy_)},
                {"baseline_client", baseline_},
                {"final_client", final},
                {"final_broker", broker_.snapshot()},
                {"broker_commands", commands_},
                {"client_events", client_events_},
                {"fills", fills_},
                {"limit_units", intent_.limit_units},
                {"cancel_dispatch_seconds", cancel_dispatch_ ? Json(*cancel_dispatch_) : Json(nullptr)},
                {"cancel_broker_seconds", cancel_at_ ? Json(*cancel_at_) : Json(nullptr)},
                {"cancel_completion_seconds", complete_at_ ? Json(*complete_at_) : Json(nullptr)},
                {"cancel_outcome", cancel_outcome_},
                {"reconciled", true},
                {"metrics", metrics}};
    }

  private:
    struct Delivery {
        Json activity;
        std::int64_t due;
        bool delivered{};
    };
    static void require(bool value, const char *message) {
        if (!value) {
            throw std::logic_error(message);
        }
    }
    Json quote(std::int64_t sequence, std::int64_t source, MoneyUnits bid, MoneyUnits ask,
               Shares shares) const {
        return {{"kind", "quote"},
                {"quote",
                 {{"symbol", workload_.symbol},
                  {"sequence", sequence},
                  {"source_at_seconds", base_ + source},
                  {"bid_units", bid},
                  {"ask_units", ask},
                  {"bid_shares", shares},
                  {"ask_shares", shares}}}};
    }
    void command(Json value, std::int64_t at) {
        value["at_seconds"] = base_ + at;
        value["command_id"] = "C" + std::to_string(commands_.size() + 1);
        const auto reply = broker_.mutate(value);
        require(reply.at("status").get<std::string>() == "ok", "synthetic broker command denied");
        const auto state = broker_.snapshot();
        commands_.push_back({{"command", value}, {"reply", reply}, {"state", state}});
        while (true) {
            const auto page = broker_.read({{"kind", "activities"}, {"after", cursor_}, {"limit", 16}});
            if (page.at("activities").empty()) {
                break;
            }
            for (const auto &activity : page.at("activities")) {
                const auto report = parse_broker_report(activity.at("report"));
                const auto due = at + (report.order_id == "seed" ? 0 : workload_.receipt_seconds);
                pending_.push_back({activity, due, false});
                if (report.kind == ReportKind::fill && report.order_id == "measured") {
                    const auto &q = state.at("quotes").at(0);
                    fills_.push_back(
                        {{"quantity", report.fill_shares},
                         {"price_units", report.fill_price_units},
                         {"fee_units", report.fee_units},
                         {"bid_units", q.at("bid_units")},
                         {"ask_units", q.at("ask_units")},
                         {"quote_sequence", q.at("sequence")},
                         {"quote_source_seconds", q.at("source_at_seconds").get<std::int64_t>() - base_},
                         {"source_seconds", at},
                         {"receipt_seconds", due}});
                }
                ++cursor_;
            }
        }
    }
    void cancellations(std::int64_t at) {
        if (cancel_at_ == at && !cancel_sent_) {
            command({{"kind", "cancel"}, {"order_id", intent_.id}, {"cancel_id", "deadline"}}, at);
            cancel_sent_ = true;
        }
        if (complete_at_ == at && !cancel_completed_) {
            command({{"kind", "complete_cancels"}}, at);
            cancel_completed_ = true;
        }
    }
    void deliver(std::int64_t at) {
        for (auto &item : pending_) {
            if (item.delivered || item.due != at) {
                continue;
            }
            const auto report = parse_broker_report(item.activity.at("report"));
            const auto result = client_.receive(report);
            require(result == Outcome::applied, "delayed broker report contradicts client account");
            item.delivered = true;
            client_events_.push_back({{"kind", "report"},
                                      {"at_seconds", at},
                                      {"activity", item.activity},
                                      {"state", client_json(client_)}});
            if (report.order_id != "measured") {
                continue;
            }
            const auto source = item.activity.at("at_seconds").get<std::int64_t>() - base_;
            if (report.kind == ReportKind::accepted) {
                observation_.acknowledgement_source_seconds = source;
                observation_.acknowledgement_receipt_seconds = at;
            }
            if (report.kind == ReportKind::canceled) {
                observation_.cancellation_source_seconds = source;
                observation_.cancellation_receipt_seconds = at;
                cancel_outcome_ = "canceled";
            }
            if (report.kind == ReportKind::cancel_rejected) {
                cancel_outcome_ = "rejected_after_fill";
            }
            if (report.kind == ReportKind::fill) {
                const auto f = std::find_if(fills_.begin(), fills_.end(), [&](const auto &fill) {
                    return fill.at("source_seconds").template get<std::int64_t>() == source;
                });
                require(f != fills_.end(), "received fill lacks a source quote");
                observation_.fills.push_back(
                    {report.fill_shares, report.fill_price_units, f->at("bid_units").get<MoneyUnits>(),
                     f->at("ask_units").get<MoneyUnits>(), f->at("quote_sequence").get<std::uint64_t>(),
                     source, at, report.fee_units});
            }
        }
    }
    void reconcile() {
        const auto snapshot = broker_.snapshot();
        PortfolioBrokerSnapshot expected{snapshot.at("cash_units").get<MoneyUnits>(),
                                         snapshot.at("positions").get<std::map<std::string, Shares>>(),
                                         {}};
        for (const auto &row : snapshot.at("open_orders")) {
            expected.open_orders.push_back(
                {parse_broker_order(row.at("order")), row.at("filled_shares").get<Shares>()});
        }
        require(client_.reconcile(expected) == Outcome::applied,
                "independent terminal ledgers do not reconcile");
    }
    ExecutionStressWorkload workload_;
    ExecutionPolicy policy_;
    BrokerState broker_;
    OrderRuntime client_;
    std::int64_t base_;
    OrderRequest intent_{};
    ExecutionObservation observation_{};
    Json commands_ = Json::array(), client_events_ = Json::array(), fills_ = Json::array(), baseline_;
    std::vector<Delivery> pending_;
    std::size_t cursor_{};
    std::optional<std::int64_t> cancel_dispatch_, cancel_at_, complete_at_;
    bool cancel_sent_{}, cancel_completed_{};
    std::string cancel_outcome_{"not_requested"};
};
} // namespace
Json CostExecutionStudy::plan() const {
    Json workloads = Json::array(), policies = Json::array(), path = Json::object();
    for (const auto &w : cost_execution_workloads()) {
        workloads.push_back(workload_json(w));
    }
    for (const auto &p : ExecutionMeasurementStudy::policies()) {
        policies.push_back(policy_json(p));
    }
    for (const auto *name : {"steady", "adverse", "reverting"}) {
        Json steps = Json::array();
        for (const auto &s : synthetic_execution_midpoint_path(name)) {
            steps.push_back({s.at_seconds, s.move_cents});
        }
        path[name] = steps;
    }
    return {{"policy_sha256", TRADING_COST_EXECUTION_PROTOCOL_SHA256},
            {"broker_policy_sha256", TRADING_BROKER_PROTOCOL_SHA256},
            {"usd_scale", scale},
            {"fee_per_filled_share_units", broker_fee_units},
            {"initial_cash_units", broker_initial_cash_units},
            {"session_date", "2023-01-03"},
            {"decision_seconds", 10},
            {"dispatch_seconds", 10},
            {"horizon_seconds", 30},
            {"seed_seconds", 1},
            {"seed_price_units", 100 * scale},
            {"maximum_quote_age_seconds", 2},
            {"path_moves_cents", path},
            {"workloads", workloads},
            {"policies", policies},
            {"baseline_policy", "cross_5s"},
            {"event_order", "advance_quotes_submit_cancel_completion_receipts_client_deadline"},
            {"mode", "in_process_existing_broker_state_and_independent_order_runtime"},
            {"fees_from_historical_quotes", false},
            {"symbol_paths_identical", true},
            {"one_factor_only", true}};
}
void CostExecutionStudy::run(const Consumer &consume, const Progress &progress) const {
    if (!consume) {
        throw std::invalid_argument("cost execution study requires an evidence consumer");
    }
    std::size_t count = 0;
    for (const auto &w : cost_execution_workloads()) {
        for (const auto &p : ExecutionMeasurementStudy::policies()) {
            consume(CaseReplay{w, p}.run());
            ++count;
            if (progress) {
                progress(count, 680);
            }
        }
    }
    if (count != 680) {
        throw std::logic_error("registered synthetic execution matrix changed");
    }
}
} // namespace trading::execution
