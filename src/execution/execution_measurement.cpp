#include "execution_measurement.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace trading::execution {
namespace {
using Json = nlohmann::json;
void require(bool value, std::string_view reason) {
    if (!value)
        throw std::invalid_argument(std::string{reason});
}
std::string utc(std::int64_t seconds) {
    return format_utc_seconds(parse_utc_seconds("2023-01-03T14:30:00Z") + std::chrono::seconds{seconds});
}
const PaperExecutionConfiguration configuration{"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "10000", {40, 4}};
Json workload_json(const ExecutionWorkload &w) {
    return {{"id", w.id},
            {"path", w.path},
            {"side", name(w.side)},
            {"spread_cents", w.spread_cents},
            {"quote_size", w.quote_size},
            {"outbound_delay_seconds", w.outbound_delay_seconds},
            {"delivery_delay_seconds", w.delivery_delay_seconds},
            {"cancel_processing_seconds", w.cancel_processing_seconds}};
}
Json policy_json(const ExecutionPolicy &p) {
    return {{"id", p.id},
            {"aggressive_limit", p.aggressive_limit},
            {"cancel_after_seconds", p.cancel_after_seconds}};
}
Json quote_json(const TopOfBook &q) {
    return {{"sequence", q.sequence},   {"at_seconds", q.at_seconds}, {"bid_cents", q.bid_cents},
            {"ask_cents", q.ask_cents}, {"bid_shares", q.bid_shares}, {"ask_shares", q.ask_shares}};
}
std::vector<TopOfBook> quotes(const ExecutionWorkload &w) {
    require(w.spread_cents > 0 && w.spread_cents <= 20 && w.spread_cents % 2 == 0 && w.quote_size > 0 &&
                w.quote_size <= 10,
            "unsupported measurement quote bounds");
    require(w.outbound_delay_seconds >= 0 && w.outbound_delay_seconds <= 2 && w.delivery_delay_seconds >= 0 &&
                w.delivery_delay_seconds <= 2 && w.cancel_processing_seconds >= 0 &&
                w.cancel_processing_seconds <= 3,
            "unsupported measurement delay bounds");
    static_cast<void>(name(w.side));
    const auto sign = w.side == Side::buy ? 1 : -1;
    std::vector<TopOfBook> result;
    for (const auto &step : synthetic_execution_midpoint_path(w.path)) {
        const auto mid = 10000 + sign * step.move_cents;
        result.push_back({result.size() + 2, step.at_seconds, mid - w.spread_cents / 2,
                          mid + w.spread_cents / 2, w.quote_size, w.quote_size});
    }
    return result;
}
class MeasurementReplay {
  public:
    MeasurementReplay(std::filesystem::path directory, const ExecutionWorkload &workload,
                      const ExecutionPolicy &policy)
        : directory_{std::move(directory)}, workload_{workload}, policy_{policy}, quotes_{quotes(workload)},
          capture_{directory_ / "capture"}, wire_{configuration.account_id, 1000000},
          runtime_{JournaledPaperRuntime::create(create_journal_file(directory_ / "runtime.journal"),
                                                 configuration)},
          session_{runtime_, wire_, capture_, [this] { return PaperReceipt{utc(now_), now_ * 1000}; },
                   [](auto) {}} {
        require(!policy.id.empty() && (policy.cancel_after_seconds == 5 || policy.cancel_after_seconds == 11),
                "measurement policy requires a supported cancellation deadline");
        session_.connect();
        require(session_.synchronize(), "initial measurement account mismatch");
        if (workload.side == Side::sell) {
            wire_.on_quote({1, 1, 9998, 10000, 10, 10});
            static_cast<void>(session_.submit({"seed-inventory", "SPY", Side::buy, 10, 10000, 0}));
            static_cast<void>(session_.drain());
            require(session_.synchronize(), "sell inventory setup did not reconcile");
        }
        const auto &arrival = quotes_.front();
        const bool buy = workload.side == Side::buy;
        const auto limit = policy.aggressive_limit ? (buy ? arrival.ask_cents : arrival.bid_cents)
                                                   : (buy ? arrival.bid_cents : arrival.ask_cents);
        intent_ = {"measured-order", "SPY", workload.side, 10, limit, 0};
        observation_ = {workload.side,
                        10,
                        limit,
                        arrival.bid_cents,
                        arrival.ask_cents,
                        quotes_.back().bid_cents,
                        quotes_.back().ask_cents,
                        10,
                        11,
                        -1,
                        -1,
                        30,
                        {}};
    }
    Json run() {
        for (now_ = 10; now_ <= observation_.horizon_seconds; ++now_) {
            wire_.advance_to(now_);
            for (const auto &q : quotes_)
                if (q.at_seconds == now_) {
                    wire_.on_quote(q);
                    collect();
                }
            if (now_ == observation_.decision_seconds)
                frame("decision");
            if (now_ == observation_.dispatch_seconds) {
                require(runtime_.submit(intent_).outcome == Outcome::applied, "measurement submit denied");
                outbound_ = AlpacaPaperRequests{}.submit(intent_);
                capture_request(*outbound_);
                frame("dispatch");
            }
            if (now_ == observation_.dispatch_seconds + workload_.outbound_delay_seconds) {
                exchange(*outbound_);
                collect();
                frame("broker_acceptance");
            }
            if (cancel_source_ && now_ == *cancel_source_) {
                exchange(*cancel_request_);
                collect();
                frame("cancel_at_broker");
            }
            if (cancel_confirmation_ && now_ == *cancel_confirmation_) {
                wire_.complete_cancels();
                collect();
                frame("cancel_resolution_at_broker");
            }
            deliver();
            if (now_ == observation_.dispatch_seconds + policy_.cancel_after_seconds) {
                const auto order = runtime_.snapshot().at("orders");
                const auto found = std::find_if(order.begin(), order.end(), [](const auto &o) {
                    return o.at("client_id") == "measured-order";
                });
                require(found != order.end(), "measured order missing at deadline");
                if (found->at("status") != "filled" && found->at("status") != "canceled") {
                    require(runtime_.request_cancel(intent_.id, "measurement-deadline").outcome ==
                                Outcome::applied,
                            "measurement deadline cancellation denied");
                    const auto broker_id =
                        runtime_.snapshot().at("broker_bindings").at(intent_.id).get<std::string>();
                    cancel_request_ = AlpacaPaperRequests{}.cancel(broker_id);
                    capture_request(*cancel_request_);
                    cancel_source_ = now_ + workload_.outbound_delay_seconds;
                    cancel_confirmation_ = *cancel_source_ + workload_.cancel_processing_seconds;
                    frame("cancel_dispatch");
                    if (*cancel_source_ == now_) {
                        exchange(*cancel_request_);
                        collect();
                        frame("cancel_at_broker");
                    }
                    if (*cancel_confirmation_ == now_) {
                        wire_.complete_cancels();
                        collect();
                        frame("cancel_resolution_at_broker");
                    }
                    deliver();
                }
            }
        }
        now_ = observation_.horizon_seconds;
        for (const auto &item : pending_)
            require(item.delivered, "measurement horizon censored a pending delivery");
        require(session_.synchronize(), "final measurement account does not reconcile");
        const auto state = runtime_.snapshot();
        require(state.at("account").at("open_orders").get<int>() == 0 &&
                    state.at("account").at("reserved_cash_units").get<std::int64_t>() == 0 &&
                    state.at("account").at("reserved_sell_shares").get<int>() == 0,
                "measurement ended with unresolved reservations");
        frame("horizon_reconciled");
        Json input_quotes = Json::array(), fills = Json::array();
        for (const auto &q : quotes_)
            input_quotes.push_back(quote_json(q));
        for (const auto &f : observation_.fills)
            fills.push_back({{"quantity", f.quantity},
                             {"price_cents", f.price_cents},
                             {"bid_cents", f.bid_cents},
                             {"ask_cents", f.ask_cents},
                             {"quote_sequence", f.quote_sequence},
                             {"source_seconds", f.source_seconds},
                             {"receipt_seconds", f.receipt_seconds}});
        return {{"case", workload_.id + "_" + policy_.id},
                {"workload", workload_json(workload_)},
                {"policy", policy_json(policy_)},
                {"quotes", input_quotes},
                {"fills", fills},
                {"events", events_},
                {"frames", frames_},
                {"state", state},
                {"broker", wire_.snapshot()},
                {"decision_seconds", observation_.decision_seconds},
                {"dispatch_seconds", observation_.dispatch_seconds},
                {"horizon_seconds", observation_.horizon_seconds},
                {"limit_cents", intent_.limit_units},
                {"cancel_outcome", cancel_outcome_},
                {"cancel_dispatch_seconds",
                 cancel_source_ ? Json(*cancel_source_ - workload_.outbound_delay_seconds) : Json(nullptr)},
                {"cancel_source_seconds", cancel_source_ ? Json(*cancel_source_) : Json(nullptr)},
                {"cancel_confirmation_source_seconds",
                 cancel_confirmation_ ? Json(*cancel_confirmation_) : Json(nullptr)},
                {"cancel_confirmation_receipt_seconds",
                 cancel_confirmation_ ? Json(*cancel_confirmation_ + workload_.delivery_delay_seconds)
                                      : Json(nullptr)},
                {"metrics", ExecutionQuality{}.measure(observation_)}};
    }

  private:
    struct Delivery {
        std::int64_t due;
        bool stream;
        std::string raw;
        unsigned status;
        bool delivered{false};
    };
    void capture_request(const PaperRequest &request) {
        capture_.record("http_request", request.method + " " + request.url, request.body,
                        {utc(now_), now_ * 1000});
    }
    void exchange(const PaperRequest &request) {
        const auto response = wire_.request(request);
        require(response.status == (request.method == "POST" ? 200U : 204U),
                "unexpected simulated measurement response");
        pending_.push_back({now_ + workload_.delivery_delay_seconds, false, response.body, response.status});
    }
    void collect() {
        while (auto raw = wire_.poll(std::chrono::milliseconds{0}))
            pending_.push_back({now_ + workload_.delivery_delay_seconds, true, std::move(*raw), 0});
    }
    void deliver() {
        // Source-order delivery is stable. Stream events precede an HTTP response
        // created at the same instant, allowing fills to arrive before that response.
        for (const bool stream : {true, false})
            for (auto &d : pending_) {
                if (d.delivered || d.due != now_ || d.stream != stream)
                    continue;
                d.delivered = true;
                capture_.record(stream ? "stream" : "http_response", "simulated measurement receipt", d.raw,
                                {utc(now_), now_ * 1000}, d.status);
                if (!stream) {
                    if (d.status == 200)
                        require(runtime_.observe_order(d.raw).outcome == Outcome::applied,
                                "measurement response identity mismatch");
                    continue;
                }
                const auto message = AlpacaPaperDecoder{}.stream(d.raw, BrokerTimestamp{utc(now_)});
                require(runtime_.receive(d.raw, utc(now_)).outcome != Outcome::quarantined,
                        "measurement stream quarantined");
                const auto &update = *message.update;
                const auto source = std::chrono::duration_cast<std::chrono::seconds>(
                                        update.timestamp->value() - BrokerTimestamp{utc(0)}.value())
                                        .count();
                events_.push_back({{"event", update.event},
                                   {"source_seconds", source},
                                   {"receipt_seconds", now_},
                                   {"raw", d.raw}});
                if (update.event == "canceled") {
                    cancel_outcome_ = "canceled";
                    observation_.cancellation_source_seconds = source;
                    observation_.cancellation_receipt_seconds = now_;
                }
                if (update.event == "order_cancel_rejected")
                    cancel_outcome_ = "rejected_after_fill";
                if (update.event == "new") {
                    observation_.acknowledgement_source_seconds = source;
                    observation_.acknowledgement_receipt_seconds = now_;
                }
                if (update.fill) {
                    const auto quote =
                        std::find_if(quotes_.rbegin(), quotes_.rend(),
                                     [source](const auto &q) { return q.at_seconds <= source; });
                    require(quote != quotes_.rend(), "fill has no contemporaneous quote");
                    observation_.fills.push_back({update.fill->quantity.scaled_integer(0),
                                                  update.fill->price.scaled_integer(2), quote->bid_cents,
                                                  quote->ask_cents, quote->sequence, source, now_});
                }
                frame(update.event);
            }
    }
    void frame(std::string label) {
        frames_.push_back(
            {{"event", std::move(label)}, {"at_seconds", now_}, {"state", runtime_.snapshot()}});
    }
    std::filesystem::path directory_;
    ExecutionWorkload workload_;
    ExecutionPolicy policy_;
    std::vector<TopOfBook> quotes_;
    std::int64_t now_{1};
    PaperCapture capture_;
    SimulatedPaperWire wire_;
    JournaledPaperRuntime runtime_;
    PaperSession session_;
    OrderRequest intent_;
    ExecutionObservation observation_{};
    std::optional<PaperRequest> outbound_, cancel_request_;
    std::optional<std::int64_t> cancel_source_, cancel_confirmation_;
    std::vector<Delivery> pending_;
    std::string cancel_outcome_{"not_requested"};
    Json events_ = Json::array(), frames_ = Json::array();
};
} // namespace

std::vector<SyntheticMidpointStep> synthetic_execution_midpoint_path(std::string_view path) {
    const std::vector<std::int64_t> times{10, 12, 15, 18, 21, 24, 30};
    const std::vector<std::int64_t> moves =
        path == "steady"      ? std::vector<std::int64_t>{0, 0, 0, 0, 0, 0, 0}
        : path == "reverting" ? std::vector<std::int64_t>{0, 0, -2, -4, 0, 2, 0}
        : path == "adverse"   ? std::vector<std::int64_t>{0, 2, 4, 6, 8, 10, 12}
                              : throw std::invalid_argument("unknown synthetic execution path");
    std::vector<SyntheticMidpointStep> out;
    for (std::size_t i = 0; i < times.size(); ++i) {
        out.push_back({times[i], moves[i]});
    }
    return out;
}
Json ExecutionQuality::measure(const ExecutionObservation &o, MoneyUnits scale) const {
    require(scale == 100 || scale == 1000000000, "unsupported execution monetary scale");
    const auto maximum_price = 10000 * scale;
    static_cast<void>(name(o.side));
    require(o.requested_quantity > 0 && o.requested_quantity <= 1000 && o.limit_cents > 0 &&
                o.limit_cents <= maximum_price,
            "invalid execution measurement order bounds");
    require(o.arrival_bid_cents > 0 && o.arrival_ask_cents >= o.arrival_bid_cents &&
                o.arrival_ask_cents <= maximum_price && o.terminal_bid_cents > 0 &&
                o.terminal_ask_cents >= o.terminal_bid_cents && o.terminal_ask_cents <= maximum_price,
            "invalid execution benchmark quotes");
    require(o.decision_seconds >= 0 && o.dispatch_seconds >= o.decision_seconds &&
                o.horizon_seconds >= o.dispatch_seconds && o.horizon_seconds <= 3600 &&
                o.acknowledgement_source_seconds >= o.dispatch_seconds &&
                o.acknowledgement_receipt_seconds >= o.acknowledgement_source_seconds &&
                o.acknowledgement_receipt_seconds <= o.horizon_seconds,
            "invalid execution measurement clock sequence");
    require(o.cancellation_source_seconds.has_value() == o.cancellation_receipt_seconds.has_value(),
            "cancellation censoring requires both source and receipt clocks");
    if (o.cancellation_source_seconds)
        require(*o.cancellation_source_seconds >= o.acknowledgement_source_seconds &&
                    *o.cancellation_receipt_seconds >= *o.cancellation_source_seconds &&
                    *o.cancellation_receipt_seconds <= o.horizon_seconds,
                "invalid cancellation censoring clocks");
    const auto sign = o.side == Side::buy ? 1 : -1;
    const auto arrival2 = o.arrival_bid_cents + o.arrival_ask_cents;
    const auto terminal2 = o.terminal_bid_cents + o.terminal_ask_cents;
    Shares filled = 0;
    std::int64_t notional = 0, spread2 = 0, timing2 = 0, fees = 0;
    Json first_source = nullptr, first_receipt = nullptr, completion_source = nullptr,
         completion_receipt = nullptr;
    std::int64_t previous_source = o.acknowledgement_source_seconds, previous_receipt = o.dispatch_seconds;
    for (const auto &f : o.fills) {
        require(f.quantity > 0 && f.quantity <= o.requested_quantity - filled && f.price_cents > 0 &&
                    f.price_cents <= maximum_price && f.bid_cents > 0 && f.ask_cents >= f.bid_cents &&
                    f.ask_cents <= maximum_price && f.quote_sequence > 0 && f.fee_units >= 0 &&
                    f.fee_units <= f.quantity * f.price_cents,
                "invalid measured fill quantity or quote");
        require(f.source_seconds >= previous_source && f.receipt_seconds >= previous_receipt &&
                    f.receipt_seconds >= f.source_seconds && f.receipt_seconds <= o.horizon_seconds,
                "invalid measured fill clock sequence");
        require(o.side == Side::buy ? f.price_cents <= o.limit_cents : f.price_cents >= o.limit_cents,
                "measured fill exceeds limit");
        if (o.cancellation_source_seconds)
            require(f.source_seconds <= *o.cancellation_source_seconds &&
                        f.receipt_seconds <= *o.cancellation_receipt_seconds,
                    "fill occurs beyond cancellation censoring endpoint");
        const auto midpoint2 = f.bid_cents + f.ask_cents;
        notional += f.quantity * f.price_cents;
        fees += f.fee_units;
        spread2 += sign * f.quantity * (2 * f.price_cents - midpoint2);
        timing2 += sign * f.quantity * (midpoint2 - arrival2);
        if (filled == 0) {
            first_source = f.source_seconds - o.dispatch_seconds;
            first_receipt = f.receipt_seconds - o.dispatch_seconds;
        }
        filled += f.quantity;
        if (filled == o.requested_quantity) {
            completion_source = f.source_seconds - o.dispatch_seconds;
            completion_receipt = f.receipt_seconds - o.dispatch_seconds;
        }
        previous_source = f.source_seconds;
        previous_receipt = f.receipt_seconds;
    }
    const auto executed2 = sign * (2 * notional - filled * arrival2);
    const auto unfilled = o.requested_quantity - filled;
    const auto opportunity2 = sign * unfilled * (terminal2 - arrival2);
    require(executed2 == spread2 + timing2, "execution cost decomposition does not conserve integer units");
    const auto observed_source =
        filled == o.requested_quantity
            ? completion_source.get<std::int64_t>()
            : o.cancellation_source_seconds.value_or(o.horizon_seconds) - o.dispatch_seconds;
    const auto observed_receipt =
        filled == o.requested_quantity
            ? completion_receipt.get<std::int64_t>()
            : o.cancellation_receipt_seconds.value_or(o.horizon_seconds) - o.dispatch_seconds;
    const std::string censor_reason = filled == o.requested_quantity  ? "not_censored"
                                      : o.cancellation_source_seconds ? "terminal_cancellation"
                                                                      : "observation_horizon";
    const auto full_denominator = static_cast<double>(o.requested_quantity * arrival2);
    Json result{
        {"requested_quantity", o.requested_quantity},
        {"filled_quantity", filled},
        {"unfilled_quantity", unfilled},
        {"fill_fraction", static_cast<double>(filled) / static_cast<double>(o.requested_quantity)},
        {"fill_notional_cents", notional},
        {"arrival_midpoint_twice_cents", arrival2},
        {"terminal_midpoint_twice_cents", terminal2},
        {"executed_shortfall_twice_cents", executed2},
        {"spread_component_twice_cents", spread2},
        {"timing_component_twice_cents", timing2},
        {"unfilled_opportunity_twice_cents", opportunity2},
        {"fees_twice_cents", 2 * fees},
        {"total_shortfall_twice_cents", executed2 + opportunity2 + 2 * fees},
        {"executed_shortfall_bps",
         filled ? Json(10000.0 * static_cast<double>(executed2) / static_cast<double>(filled * arrival2))
                : Json(nullptr)},
        {"total_shortfall_bps",
         10000.0 * static_cast<double>(executed2 + opportunity2 + 2 * fees) / full_denominator},
        {"decision_to_dispatch_seconds", o.dispatch_seconds - o.decision_seconds},
        {"acknowledgement_source_delay_seconds", o.acknowledgement_source_seconds - o.dispatch_seconds},
        {"acknowledgement_receipt_delay_seconds", o.acknowledgement_receipt_seconds - o.dispatch_seconds},
        {"first_fill_source_delay_seconds", first_source},
        {"first_fill_receipt_delay_seconds", first_receipt},
        {"completion_source_delay_seconds", completion_source},
        {"completion_receipt_delay_seconds", completion_receipt},
        {"completion_censored", filled != o.requested_quantity},
        {"completion_censor_reason", censor_reason},
        {"completion_source_observation_seconds", observed_source},
        {"completion_observation_seconds", observed_receipt},
        {"completion_horizon_seconds", o.horizon_seconds - o.dispatch_seconds}};
    if (scale != 100) {
        Json units = Json::object();
        for (const auto &[key, value] : result.items()) {
            units[key.ends_with("_cents") ? key.substr(0, key.size() - 6) + "_units" : key] = value;
        }
        units["usd_scale"] = scale;
        return units;
    }
    return result;
}

std::vector<ExecutionWorkload> ExecutionMeasurementStudy::workloads() {
    std::vector<ExecutionWorkload> result;
    for (const auto path : {"steady", "reverting", "adverse"})
        for (const auto side : {Side::buy, Side::sell}) {
            for (int variant = 0; variant < 6; ++variant) {
                const std::string variation =
                    std::vector<std::string>{"base",           "wide_spread",    "thin_size",
                                             "outbound_delay", "delivery_delay", "cancel_delay"}
                        .at(static_cast<std::size_t>(variant));
                result.push_back({std::string{path} + "_" + std::string{name(side)} + "_" + variation, path,
                                  side, variant == 1 ? 6 : 2, variant == 2 ? 2 : 4, variant == 3 ? 2 : 0,
                                  variant == 4 ? 2 : 0, variant == 5 ? 3 : 1});
            }
        }
    return result;
}
std::vector<ExecutionPolicy> ExecutionMeasurementStudy::policies() {
    return {
        {"cross_5s", true, 5}, {"cross_11s", true, 11}, {"passive_5s", false, 5}, {"passive_11s", false, 11}};
}
ExecutionMeasurementResult ExecutionMeasurementStudy::run(const std::filesystem::path &directory,
                                                          const std::vector<ExecutionWorkload> &workloads,
                                                          const std::vector<ExecutionPolicy> &policies,
                                                          const Progress &progress) const {
    require(!workloads.empty() && workloads.size() <= 36 && policies.size() == 4,
            "measurement study requires 1-36 workloads and four policies");
    std::set<std::string> workload_ids, policy_ids;
    for (const auto &w : workloads)
        require(!w.id.empty() &&
                    w.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == std::string::npos &&
                    workload_ids.insert(w.id).second,
                "invalid or duplicate execution workload ID");
    for (const auto &p : policies)
        require(!p.id.empty() &&
                    p.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == std::string::npos &&
                    policy_ids.insert(p.id).second,
                "invalid or duplicate execution policy ID");
    require(policy_ids.contains("cross_11s"), "paired comparison baseline is missing");
    const auto defined_policies = ExecutionMeasurementStudy::policies();
    for (const auto &p : policies)
        require(std::find(defined_policies.begin(), defined_policies.end(), p) != defined_policies.end(),
                "policy label differs from its declared rule");
    ExecutionMeasurementResult result;
    const auto total = workloads.size() * policies.size();
    if (progress)
        progress(0, total);
    for (const auto &w : workloads) {
        Json cell = Json::array();
        for (const auto &p : policies) {
            auto c = MeasurementReplay{directory / (w.id + "_" + p.id), w, p}.run();
            cell.push_back(c);
            result.cases.push_back(std::move(c));
        }
        if (progress)
            progress(result.cases.size(), total);
        const auto baseline = std::find_if(
            cell.begin(), cell.end(), [](const auto &c) { return c.at("policy").at("id") == "cross_11s"; });
        for (const auto &c : cell)
            if (c.at("policy").at("id") != "cross_11s") {
                const auto &a = c.at("metrics");
                const auto &b = baseline->at("metrics");
                result.comparisons.push_back(
                    {{"workload", w.id},
                     {"candidate", c.at("policy").at("id")},
                     {"baseline", "cross_11s"},
                     {"fill_fraction_difference",
                      a.at("fill_fraction").get<double>() - b.at("fill_fraction").get<double>()},
                     {"total_shortfall_difference_twice_cents",
                      a.at("total_shortfall_twice_cents").get<std::int64_t>() -
                          b.at("total_shortfall_twice_cents").get<std::int64_t>()}});
            }
    }
    for (const auto &c : result.cases) {
        const auto &w = c.at("workload");
        const auto base_id =
            w.at("path").get<std::string>() + "_" + w.at("side").get<std::string>() + "_base";
        if (w.at("id") == base_id)
            continue;
        const auto baseline = std::find_if(result.cases.begin(), result.cases.end(), [&](const auto &b) {
            return b.at("workload").at("id") == base_id && b.at("policy") == c.at("policy");
        });
        if (baseline == result.cases.end())
            continue;
        const auto &a = c.at("metrics");
        const auto &b = baseline->at("metrics");
        result.sensitivities.push_back(
            {{"workload", w.at("id")},
             {"baseline_workload", base_id},
             {"policy", c.at("policy").at("id")},
             {"fill_fraction_difference",
              a.at("fill_fraction").get<double>() - b.at("fill_fraction").get<double>()},
             {"total_shortfall_difference_twice_cents",
              a.at("total_shortfall_twice_cents").get<std::int64_t>() -
                  b.at("total_shortfall_twice_cents").get<std::int64_t>()},
             {"acknowledgement_receipt_difference_seconds",
              a.at("acknowledgement_receipt_delay_seconds").get<std::int64_t>() -
                  b.at("acknowledgement_receipt_delay_seconds").get<std::int64_t>()}});
    }
    for (const auto &p : policies) {
        std::int64_t requested = 0, filled = 0, cost2 = 0, notional2 = 0, completed = 0, first_fills = 0;
        std::int64_t spread2 = 0, timing2 = 0, opportunity2 = 0, canceled = 0, cancel_rejected = 0;
        double completion_delay = 0.0, first_delay = 0.0, ack_delay = 0.0;
        std::size_t count = 0;
        for (const auto &c : result.cases)
            if (c.at("policy").at("id") == p.id) {
                const auto &m = c.at("metrics");
                ++count;
                requested += m.at("requested_quantity").get<std::int64_t>();
                filled += m.at("filled_quantity").get<std::int64_t>();
                cost2 += m.at("total_shortfall_twice_cents").get<std::int64_t>();
                notional2 += m.at("requested_quantity").get<std::int64_t>() *
                             m.at("arrival_midpoint_twice_cents").get<std::int64_t>();
                spread2 += m.at("spread_component_twice_cents").get<std::int64_t>();
                timing2 += m.at("timing_component_twice_cents").get<std::int64_t>();
                opportunity2 += m.at("unfilled_opportunity_twice_cents").get<std::int64_t>();
                if (c.at("cancel_outcome") == "canceled")
                    ++canceled;
                if (c.at("cancel_outcome") == "rejected_after_fill")
                    ++cancel_rejected;
                ack_delay += m.at("acknowledgement_receipt_delay_seconds").get<double>();
                if (!m.at("completion_censored").get<bool>()) {
                    ++completed;
                    completion_delay += m.at("completion_receipt_delay_seconds").get<double>();
                }
                if (!m.at("first_fill_receipt_delay_seconds").is_null()) {
                    ++first_fills;
                    first_delay += m.at("first_fill_receipt_delay_seconds").get<double>();
                }
            }
        result.policy_summaries.push_back(
            {{"policy", p.id},
             {"case_count", count},
             {"completed_cases", completed},
             {"censored_cases", static_cast<std::int64_t>(count) - completed},
             {"first_fill_cases", first_fills},
             {"canceled_cases", canceled},
             {"cancel_rejected_cases", cancel_rejected},
             {"fill_fraction", static_cast<double>(filled) / static_cast<double>(requested)},
             {"total_shortfall_bps", 10000.0 * static_cast<double>(cost2) / static_cast<double>(notional2)},
             {"spread_component_bps",
              10000.0 * static_cast<double>(spread2) / static_cast<double>(notional2)},
             {"timing_component_bps",
              10000.0 * static_cast<double>(timing2) / static_cast<double>(notional2)},
             {"unfilled_opportunity_bps",
              10000.0 * static_cast<double>(opportunity2) / static_cast<double>(notional2)},
             {"mean_acknowledgement_receipt_seconds", ack_delay / static_cast<double>(count)},
             {"mean_completion_receipt_seconds",
              completed ? Json(completion_delay / static_cast<double>(completed)) : Json(nullptr)},
             {"mean_first_fill_receipt_seconds",
              first_fills ? Json(first_delay / static_cast<double>(first_fills)) : Json(nullptr)}});
    }
    return result;
}
} // namespace trading::execution
