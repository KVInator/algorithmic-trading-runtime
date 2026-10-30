#include "panel_account_study.hpp"
#include "content_digest.hpp"
#include "daily_risk_rules.hpp"
#include "shared_capital_json.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

namespace trading {
namespace {
using Json = nlohmann::json;
using namespace std::chrono;
using namespace execution;
constexpr MoneyUnits scale = 1000000000;
const std::vector<std::string> variants{"candidate", "momentum_control", "continuation_control",
                                        "reversion_sign_control", "reversion_drift_control"};
void require(bool yes, const char *reason) {
    if (!yes) {
        throw std::invalid_argument(reason);
    }
}
Json value_json(const AccountValue &v) {
    return {{"cash", v.cash_usd},
            {"shares", v.shares},
            {"receivable", v.receivable_usd},
            {"liquid", v.liquid_wealth_usd},
            {"equity", v.equity_usd}};
}
Json event_json(const AccountEvent &e) {
    return {{"at", format_utc_seconds(e.at)},
            {"kind", e.kind},
            {"id", e.action_id},
            {"price", e.reference_price_usd},
            {"notional", e.traded_notional_usd},
            {"cost", e.cost_usd},
            {"value", value_json(e.value)}};
}
struct Summary {
    std::vector<double> returns;
    std::vector<std::pair<std::string, double>> path;
    double notional{}, cost{}, exposure_sum{}, maximum_drawdown{}, peak{100000}, equity{100000};
    std::string started, stopped, reason;
    void observe(const std::string &date, double nav, double r, double exposure) {
        returns.push_back(r);
        path.emplace_back(date, nav);
        equity = nav;
        peak = std::max(peak, nav);
        maximum_drawdown = std::max(maximum_drawdown, 1 - nav / peak);
        exposure_sum += exposure;
    }
    Json json() const {
        Json points = Json::array();
        for (const auto &[d, e] : path) {
            points.push_back({d, e});
        }
        return {{"observed_dates", returns.size()},
                {"started", started.empty() ? Json(nullptr) : Json(started)},
                {"stopped", stopped.empty() ? Json(nullptr) : Json(stopped)},
                {"stop_reason", reason},
                {"last_observed_equity", equity},
                {"mean_return", returns.empty() ? Json(nullptr)
                                                : Json(std::accumulate(returns.begin(), returns.end(), 0.0) /
                                                       static_cast<double>(returns.size()))},
                {"cer", returns.size() < 2 ? Json(nullptr) : Json(certainty_equivalent(returns))},
                {"observed_prefix_drawdown", maximum_drawdown},
                {"traded_notional", notional},
                {"cost_paid", cost},
                {"mean_exposure",
                 returns.empty() ? Json(nullptr) : Json(exposure_sum / static_cast<double>(returns.size()))},
                {"equity_path", points}};
    }
};
class RiskCase {
  public:
    RiskCase(std::string instrument, std::string policy, int lag, double charge)
        : symbol(std::move(instrument)), name(std::move(policy)), delay(lag), cost(charge), account(100000) {}
    void initialize_price(double price) {
        if (summary.started.empty()) {
            last_price = price;
        }
    }
    Json run(const PanelReplayDay &day, const RiskSessionReferences &refs, const Json &actions,
             bool terminal, const std::optional<double>* explicit_target = nullptr) {
        Json events = Json::array();
        const auto begin = account.ledger().size();
        const auto &cal = refs.calendar();
        const auto date = cal.date();
        const auto halt = [&](const char *why) {
            if (summary.stopped.empty()) {
                summary.stopped = date;
                summary.reason = why;
            }
        };
        std::optional<double> target;
        if (day.risk.intraday_weight && day.risk.daily_weight && day.risk.static_weight) {
            target = name == "intraday_risk"        ? day.risk.intraday_weight
                     : name == "daily_only"         ? day.risk.daily_weight
                     : name == "static_risk_budget" ? day.risk.static_weight
                                                    : std::optional<double>{1};
        }
        if (explicit_target) { target = *explicit_target; }
        if (summary.stopped.empty() && (!summary.started.empty() || target)) {
            if (summary.started.empty()) {
                summary.started = date;
                previous_date = date;
            }
            // Apply only actions reached on the calendar, before this session.
            double continuity = last_price;
            for (auto cursor = parse_session_date(previous_date); cursor <= parse_session_date(date);
                 cursor += days{1}) {
                const auto id = format_utc_seconds(cursor).substr(0, 10);
                if (id <= last_action_date) {
                    continue;
                }
                for (const auto &a : actions) {
                    if (a.at("symbol") == symbol && a.at("ex_date") == id && id >= summary.started) {
                        continuity -= a.at("usd_per_share").get<double>();
                        require(continuity > 0, "nonpositive ex-date continuity mark");
                        account.accrue_dividend(cursor, symbol + "_" + id,
                                                parse_session_date(a.at("payable_date").get<std::string>()),
                                                a.at("usd_per_share").get<double>(), continuity);
                        last_price = continuity;
                    }
                }
                account.settle_dividends(cursor, continuity);
                last_action_date = id;
            }
            const auto opening = refs.open_at(cal.open_utc());
            if (!opening) {
                halt("opening_mark_unavailable");
            } else {
                account.mark(cal.open_utc(), *opening);
                last_price = *opening;
                if (name != "passive_equity" || summary.started == date) {
                    if (!target) {
                        halt("required_prior_risk_unavailable");
                    } else {
                        const auto at = cal.open_utc() + minutes{delay};
                        const auto price = refs.open_at(at);
                        if (!price) {
                            halt("execution_open_unavailable");
                        } else {
                            account.rebalance(at, *price, *target, cost);
                            last_price = *price;
                        }
                    }
                }
                if (summary.stopped.empty() && terminal) {
                    const auto at = cal.close_utc() - minutes{4};
                    const auto price = refs.open_at(at);
                    if (!price) {
                        halt("terminal_liquidation_open_unavailable");
                    } else {
                        account.rebalance(at, *price, 0, cost);
                        last_price = *price;
                    }
                }
                if (summary.stopped.empty()) {
                    const auto closing = refs.observed_close();
                    if (!closing) {
                        halt("closing_mark_unavailable");
                    } else {
                        account.mark(cal.close_utc(), *closing);
                        last_price = *closing;
                    }
                }
            }
            previous_date = date;
        }
        double notional = 0, paid = 0;
        for (auto i = begin; i < account.ledger().size(); ++i) {
            const auto &e = account.ledger()[i];
            events.push_back(event_json(e));
            notional += e.traded_notional_usd;
            paid += e.cost_usd;
        }
        const auto v = account.value(last_price);
        summary.notional += notional;
        summary.cost += paid;
        const bool valid = summary.stopped.empty();
        const double r = v.equity_usd / previous_equity - 1;
        if (valid) {
            summary.observe(date, v.equity_usd, r, v.shares * last_price / v.liquid_wealth_usd);
            previous_equity = v.equity_usd;
        }
        return {{"symbol", symbol},
                {"account", name},
                {"delay", delay},
                {"cost_bps", cost},
                {"status", !valid                    ? "stopped"
                           : summary.started.empty() ? "awaiting_history"
                                                     : "observed"},
                {"stop_reason", summary.reason},
                {"target_weight", target ? Json(*target) : Json(nullptr)},
                {"fallback", name == "intraday_risk" && day.risk.estimate && !day.risk.estimate->intraday},
                {"net_return", valid ? Json(r) : Json(nullptr)},
                {"value", value_json(v)},
                {"last_price", last_price},
                {"events", events},
                {"terminal_liquidation", valid && terminal && !summary.started.empty()}};
    }
    std::string symbol, name;
    int delay;
    double cost;
    Summary summary;

  private:
    LongCashAccount account;
    double last_price{100}, previous_equity{100000};
    std::string previous_date, last_action_date;
};
std::string policy_for(const std::string &variant, const std::string &family) {
    if (family == "momentum") {
        return variant == "momentum_control" ? "momentum_always_long" : "momentum";
    }
    if (family == "volume_conditioned_continuation") {
        return variant == "continuation_control" ? "continuation_price" : "continuation_volume";
    }
    return variant == "reversion_sign_control"    ? "reversion_sign_only"
           : variant == "reversion_drift_control" ? "reversion_drift"
                                                  : "reversion_candidate";
}
class SharedCase {
  public:
    SharedCase(const ExtensionStudyContract &contract, std::string policy, int lag, double charge)
        : variant(std::move(policy)), delay(lag), cost(charge),
          config(SharedCapitalConfiguration::registered(contract)) {}
    Json run(std::span<const PanelReplayDay> records, std::span<const SessionDownload> sessions) {
        const auto date = records.front().date;
        Json transcript = Json::array();
        if (!summary.stopped.empty()) {
            return output(transcript, nullptr);
        }
        const auto before = checkpoint ? checkpoint->cash_units : config.initial_cash_units;
        SharedCapitalBook book{config, generation, checkpoint};
        std::size_t serial = 0;
        MoneyUnits notional = 0, paid = 0;
        std::size_t transfers = 0;
        double exposure = 0;
        const auto at = sessions.front().request.start().time_since_epoch().count();
        const auto step = [&](const SharedInput &input) {
            const auto result = book.apply(input);
            require(result.outcome == Outcome::applied || result.outcome == Outcome::obsolete,
                    "shared historical transition failed");
            auto state = shared_snapshot_json(book.snapshot());
            Json claims = Json::array();
            for (const auto &o : result.dispatch_orders) {
                for (const auto &p : state.at("open_orders")) {
                    if (p.at("order").at("id") == o.id) {
                        claims.push_back(p);
                    }
                }
            }
            auto effects = shared_result_json(result);
            effects.erase("reason");
            transcript.push_back(
                {{"effects", effects}, {"claims", claims}, {"checkpoint_sha256", sha256(state.dump())}});
            return result;
        };
        static_cast<void>(step(SharedConnection{at, true}));
        std::map<std::string, Shares> zero;
        for (const auto &s : config.symbols) {
            zero[s] = 0;
        }
        static_cast<void>(step(SharedReconciliation{at, {before, zero, {}}}));
        std::map<std::int64_t, std::vector<FamilySignal>> schedule;
        for (const auto &day : records) {
            for (const auto &family : config.families) {
                const auto name = policy_for(variant, family);
                const auto found = std::find_if(day.policies.begin(), day.policies.end(),
                                                [&](const auto &p) { return p.policy == name; });
                if (found == day.policies.end()) {
                    continue;
                }
                for (const auto &d : found->decisions) {
                    const auto time = d.at.time_since_epoch().count() + delay * 60;
                    if (!d.target_long) {
                        schedule[time];
                        break;
                    }
                    schedule[time].push_back({{family, day.symbol}, *d.target_long});
                }
            }
        }
        // Missing future execution can affect later primary path targets. The
        // individual unavailable target is checked only when its clock arrives.
        const auto dispatch = [&](SharedResult first, std::int64_t clock) {
            using Task = std::variant<SharedResult, std::pair<OrderRequest, bool>,
                                      std::pair<std::string, std::string>>;
            std::vector<Task> tasks;
            tasks.emplace_back(std::move(first));
            while (!tasks.empty()) {
                require(++serial < 10000, "shared synchronous reference loop exceeded its bound");
                auto task = std::move(tasks.back());
                tasks.pop_back();
                if (const auto *result = std::get_if<SharedResult>(&task)) {
                    transfers += result->transfers.size();
                    const auto snap = book.snapshot();
                    MoneyUnits gross = 0;
                    for (const auto &[key, p] : snap.owners) {
                        if (p.shares) {
                            const auto &q = snap.quotes.at(key.symbol);
                            gross += p.shares * (q.bid_units + (q.ask_units - q.bid_units) / 2);
                        }
                    }
                    exposure = std::max(exposure,
                                        static_cast<double>(gross) / static_cast<double>(snap.equity_units));
                    for (auto it = result->dispatch_orders.rbegin(); it != result->dispatch_orders.rend();
                         ++it) {
                        tasks.emplace_back(std::pair{*it, false});
                    }
                    for (auto it = result->dispatch_cancels.rbegin(); it != result->dispatch_cancels.rend();
                         ++it) {
                        tasks.emplace_back(*it);
                    }
                } else if (const auto *request = std::get_if<std::pair<OrderRequest, bool>>(&task)) {
                    const auto &o = request->first;
                    if (!request->second) {
                        tasks.emplace_back(std::pair{o, true});
                        tasks.emplace_back(step(SharedDelivery{
                            clock, {"A" + o.id, o.id, ReportKind::accepted, "", 0, 0, 0, 0, ""}}));
                    } else {
                        const auto snap = book.snapshot();
                        const auto active = std::find_if(snap.open_orders.begin(), snap.open_orders.end(),
                                                         [&](const auto &x) { return x.request.id == o.id; });
                        if (active == snap.open_orders.end()) {
                            continue;
                        }
                        const auto &q = snap.quotes.at(o.symbol);
                        const auto mid = q.bid_units + (q.ask_units - q.bid_units) / 2;
                        notional += o.shares * mid;
                        paid += o.shares * std::abs(o.limit_units - mid);
                        tasks.emplace_back(
                            step(SharedDelivery{clock,
                                                {"F" + o.id, o.id, ReportKind::fill, "X" + o.id, o.shares,
                                                 o.limit_units, 0, 0, ""}}));
                    }
                } else {
                    const auto &[id, cancel] = std::get<std::pair<std::string, std::string>>(task);
                    const auto snap = book.snapshot();
                    const auto found = std::find_if(snap.open_orders.begin(), snap.open_orders.end(),
                                                    [&](const auto &o) { return o.request.id == id; });
                    BrokerReport report{"C" + cancel,
                                        id,
                                        found == snap.open_orders.end() ? ReportKind::cancel_rejected
                                                                        : ReportKind::canceled,
                                        "",
                                        0,
                                        0,
                                        0,
                                        found == snap.open_orders.end() ? 0 : found->filled_shares,
                                        cancel};
                    tasks.emplace_back(step(SharedDelivery{clock, report}));
                }
            }
        };
        std::size_t batches = 0;
        for (const auto &[clock, signals] : schedule) {
            SharedTargetBatch batch{"D" + std::to_string(++batches), clock, {}, signals};
            bool available = true, targets = true;
            for (std::size_t i = 0; i < records.size(); ++i) {
                for (const auto &p : records[i].policies) {
                    for (const auto &d : p.decisions) {
                        if (d.at.time_since_epoch().count() + delay * 60 == clock && !d.target_long &&
                            (p.policy == policy_for(variant, "momentum") ||
                             p.policy == policy_for(variant, "reversion") ||
                             p.policy == policy_for(variant, "volume_conditioned_continuation"))) {
                            targets = false;
                        }
                    }
                }
                const auto found = std::lower_bound(
                    sessions[i].bars.begin(), sessions[i].bars.end(), sys_seconds{seconds{clock}},
                    [](const auto &b, const auto &t) { return b.start() < t; });
                if (found == sessions[i].bars.end() || found->start() != sys_seconds{seconds{clock}}) {
                    available = false;
                    break;
                }
                const auto raw = found->prices().open;
                require(std::isfinite(raw) && raw > 0 && raw <= 100000,
                        "shared price exceeds explicit unit bound");
                const auto mid = static_cast<MoneyUnits>(std::llround(raw * static_cast<double>(scale)));
                const auto ticks = static_cast<MoneyUnits>(std::llround(cost * 2));
                const auto charge = (mid * ticks + 19999) / 20000;
                batch.quotes.push_back({records[i].symbol, clock, mid - charge, mid + charge});
            }
            if (!available || !targets) {
                summary.stopped = date;
                summary.reason =
                    !available ? "shared_execution_quote_unavailable" : "shared_primary_target_unavailable";
                break;
            }
            dispatch(step(batch), clock);
        }
        if (summary.started.empty()) {
            summary.started = date;
        }
        summary.notional += static_cast<double>(notional) / static_cast<double>(scale);
        summary.cost += static_cast<double>(paid) / static_cast<double>(scale);
        const auto snap = book.snapshot();
        if (summary.stopped.empty() &&
            (snap.account.open_orders ||
             std::any_of(snap.owners.begin(), snap.owners.end(),
                         [](const auto &p) { return p.second.shares || p.second.desired_shares; }))) {
            summary.stopped = date;
            summary.reason = "shared_terminal_not_flat";
        }
        if (summary.stopped.empty()) {
            static_cast<void>(
                step(SharedSeal{sessions.front().request.end_exclusive().time_since_epoch().count()}));
            checkpoint = book.flat_checkpoint();
            ++generation;
            const double nav = static_cast<double>(checkpoint->cash_units) / static_cast<double>(scale);
            const double r =
                static_cast<double>(checkpoint->cash_units - before) / static_cast<double>(before);
            summary.observe(date, nav, r, exposure);
        }
        auto result = output(transcript, shared_snapshot_json(book.snapshot()));
        result["traded_mid_notional_units"] = notional;
        result["crossing_cost_units"] = paid;
        result["internal_transfers"] = transfers;
        result["maximum_gross_fraction"] = exposure;
        result["net_return"] =
            summary.stopped.empty()
                ? Json(static_cast<double>(checkpoint->cash_units - before) / static_cast<double>(before))
                : Json(nullptr);
        return result;
    }
    std::string variant;
    int delay;
    double cost;
    Summary summary;

  private:
    Json output(const Json &events, const Json &final) const {
        return {{"variant", variant},
                {"delay", delay},
                {"cost_bps", cost},
                {"status", summary.stopped.empty() ? "observed" : "stopped"},
                {"stop_reason", summary.reason},
                {"net_return", nullptr},
                {"transitions", events},
                {"final", final},
                {"traded_mid_notional_units", 0},
                {"crossing_cost_units", 0},
                {"internal_transfers", 0},
                {"maximum_gross_fraction", nullptr}};
    }
    SharedCapitalConfiguration config;
    std::uint64_t generation{};
    std::optional<SharedFlatCheckpoint> checkpoint;
};
} // namespace
class PanelRiskLedger::State {
  public:
    State(std::string symbol, std::string policy, int delay, double cost)
        : ledger(std::move(symbol), std::move(policy), delay, cost) {}
    Json advance(const PanelReplayDay& day, const RiskSessionReferences& references,
                 const Json& actions, bool terminal, const std::optional<double>* target) {
        if (previous_close) { ledger.initialize_price(*previous_close); }
        Json result = day.scored_period ? ledger.run(day, references, actions, terminal, target)
            : Json{{"symbol",day.symbol},{"account",ledger.name},{"status","initialization"},
                   {"stop_reason",""},{"target_weight",nullptr},{"net_return",nullptr},{"events",Json::array()}};
        previous_close = references.observed_close();
        return result;
    }
    RiskCase ledger;
    std::optional<double> previous_close;
};
PanelRiskLedger::PanelRiskLedger(std::string symbol,std::string policy,int delay,double cost)
    : state_(std::make_unique<State>(std::move(symbol),std::move(policy),delay,cost)) {}
PanelRiskLedger::~PanelRiskLedger() = default;
Json PanelRiskLedger::append(const PanelReplayDay& day,const RiskSessionReferences& refs,const Json& actions,bool terminal) {
    return state_->advance(day,refs,actions,terminal,nullptr);
}
Json PanelRiskLedger::append_sized(const PanelReplayDay& day,const RiskSessionReferences& refs,const Json& actions,bool terminal,std::optional<double> target) {
    return state_->advance(day,refs,actions,terminal,&target);
}
class PanelAccountStudy::State {
  public:
    explicit State(const ExtensionStudyContract &contract, Json cash)
        : actions(std::move(cash)), contract_(contract) {
        const auto symbols = contract.symbols();
        for (const auto &symbol : symbols) {
            by_symbol[symbol] = Json::array();
        }
        std::set<std::pair<std::string, std::string>> seen;
        for (const auto &a : actions) {
            const auto symbol = a.at("symbol").get<std::string>(), ex = a.at("ex_date").get<std::string>(),
                       pay = a.at("payable_date").get<std::string>();
            const auto amount = a.at("usd_per_share").get<double>();
            require(by_symbol.contains(symbol) && ex < "2024" && std::isfinite(amount) && amount > 0 &&
                        parse_session_date(pay) >= parse_session_date(ex) && seen.emplace(symbol, ex).second,
                    "invalid account cash event");
            by_symbol.at(symbol).push_back(a);
        }
        for (const auto &s : contract.symbols()) {
            for (const auto name : risk_rules::accounts) {
                for (int delay : risk_rules::delays) {
                    for (double cost : risk_rules::costs) {
                        risk.emplace_back(s, std::string{name}, delay, cost);
                    }
                }
            }
        }
        for (const auto &v : variants) {
            for (int delay : risk_rules::delays) {
                for (double cost : risk_rules::costs) {
                    shared.emplace_back(contract, v, delay, cost);
                }
            }
        }
    }
    Json actions;
    std::map<std::string, Json> by_symbol;
    ExtensionStudyContract contract_;
    std::vector<RiskCase> risk;
    std::vector<SharedCase> shared;
    std::map<std::string, std::pair<PanelReplayDay, RiskSessionReferences>> pending;
    std::string previous;
    bool terminal{}, finished{};
    struct ForecastTotals {
        std::size_t dates{}, rv{}, endpoint{}, losses{};
        long double loss_sum{};
    };
    std::map<std::pair<std::string, int>, ForecastTotals> forecasts;
};
PanelAccountStudy::PanelAccountStudy(const ExtensionStudyContract &contract, Json actions)
    : state_(std::make_unique<State>(contract, std::move(actions))) {}
PanelAccountStudy::~PanelAccountStudy() = default;
Json PanelAccountStudy::plan() const {
    return {{"policy_sha256", TRADING_COST_EXECUTION_PROTOCOL_SHA256},
            {"protocol_sha256", TRADING_EXTENSION_PROTOCOL_SHA256},
            {"symbols", state_->contract_.symbols()},
            {"shared_usd_scale", scale},
            {"shared_fee_per_share_units", 0},
            {"shared_budget_denominators", {{"owner", 15}, {"family", 3}, {"instrument", 5}}},
            {"risk_accounts", risk_rules::accounts},
            {"costs_bps", risk_rules::costs},
            {"delays_minutes", risk_rules::delays},
            {"shared_variants", variants},
            {"initial_usd", 100000},
            {"risk_start", "first_scored_date_with_all_three_prior_weights"},
            {"risk_terminal", "last_development_session_close_minus_four_minutes_actual_open"},
            {"shared_fill_model", "synchronous_full_fill_at_cost_shifted_raw_opens_no_observed_liquidity"},
            {"shared_cost_rounding", "raw_price_nearest_nanodollar_charge_ceiling_nanodollar_per_share"},
            {"shared_sizing_clock", "arrival_at_decision_plus_delay"},
            {"shared_daily_handover", "sealed_flat_checkpoint_no_capital_reset"},
            {"missing_required_reference", "stop_at_observation_without_pre_gap_liquidation_or_restart"},
            {"cash_interest", 0},
            {"accounting_actions", state_->actions}};
}
Json PanelAccountStudy::append(std::span<const PanelReplayDay> days,
                               std::span<const SessionDownload> sessions,
                               std::span<const std::optional<MinuteBar>> recovered, bool terminal) {
    require(days.size() == 5 && sessions.size() == 5 && recovered.size() == 5,
            "account replay requires one complete five-symbol date");
    require(!state_->terminal && !state_->finished, "account study already reached its terminal date");
    const auto date = days.front().date;
    state_->contract_.require_development_access(date);
    require(state_->previous.empty() || date > state_->previous, "account dates must increase");
    if (!state_->previous.empty()) {
        const auto first =
            format_utc_seconds(parse_session_date(state_->previous) + std::chrono::days{1}).substr(0, 10);
        require(extension_calendar(CalendarRange{first, date}).size() == 1,
                "account study skipped an exchange session");
    }
    std::set<std::string> symbols;
    for (const auto &day : days) {
        symbols.insert(day.symbol);
    }
    const auto registered = state_->contract_.symbols();
    require(symbols == std::set<std::string>{registered.begin(), registered.end()},
            "account study symbol coverage differs");
    state_->previous = date;
    Json output{
        {"date", date}, {"risk", Json::array()}, {"shared", Json::array()}, {"forecasts", Json::array()}};
    for (std::size_t i = 0; i < days.size(); ++i) {
        require(days[i].date == date && sessions[i].request.symbol() == days[i].symbol,
                "account market identity differs");
        require(days[i].scored_period == (date > "2016-12-31"),
                "account initialization/scoring boundary differs");
        auto bars = sessions[i].bars;
        if (recovered[i]) {
            bars.push_back(*recovered[i]);
        }
        const auto calendar = extension_calendar(CalendarRange{date, date}).at(0);
        require(sessions[i].request.start() == calendar.open_utc() &&
                    sessions[i].request.end_exclusive() == calendar.close_utc(),
                "account session clocks differ");
        RiskSessionReferences refs{calendar, std::move(bars), days[i].symbol,
                                   ResearchInputMode::registered_panel};
        const auto prior = state_->pending.find(days[i].symbol);
        if (prior != state_->pending.end() && prior->second.second.observed_close()) {
            for (auto &account : state_->risk) {
                if (account.symbol == days[i].symbol) {
                    account.initialize_price(*prior->second.second.observed_close());
                }
            }
        }
        if (prior != state_->pending.end()) {
            const auto &[prediction, first] = prior->second;
            if (prediction.scored_period) {
                double dividend = 0;
                for (const auto &a : state_->by_symbol.at(days[i].symbol)) {
                    if (a.at("symbol") == days[i].symbol && a.at("ex_date") == date) {
                        dividend += a.at("usd_per_share").get<double>();
                    }
                }
                for (int delay : risk_rules::delays) {
                    Json rv = nullptr, endpoint = nullptr, qlike = nullptr;
                    if (first.open_at(first.calendar().open_utc() + minutes{delay}) &&
                        refs.open_at(calendar.open_utc() + minutes{delay}) &&
                        refs.open_at(calendar.open_utc())) {
                        const auto y = measure_holding_risk(first, refs, dividend, delay);
                        endpoint = y.endpoint2;
                        if (y.rv5) {
                            rv = *y.rv5;
                        }
                        if (y.rv5 && prediction.risk.estimate && prediction.risk.estimate->intraday) {
                            const auto hd = prediction.risk.estimate->daily,
                                       hi = *prediction.risk.estimate->intraday;
                            qlike = (std::log(hd) - std::log(hi)) + *y.rv5 * (1 / hd - 1 / hi);
                        }
                    }
                    output["forecasts"].push_back({{"symbol", days[i].symbol},
                                                   {"date", prediction.date},
                                                   {"next_date", date},
                                                   {"delay", delay},
                                                   {"future_rv5", rv},
                                                   {"endpoint2", endpoint},
                                                   {"control_minus_candidate_qlike", qlike}});
                }
            }
        }
        state_->pending.insert_or_assign(days[i].symbol, std::pair{days[i], refs});
        if (days[i].scored_period) {
            for (auto &account : state_->risk) {
                if (account.symbol == days[i].symbol) {
                    output["risk"].push_back(
                        account.run(days[i], refs, state_->by_symbol.at(days[i].symbol), terminal));
                }
            }
        } else if (refs.observed_close()) {
            for (auto &account : state_->risk) {
                if (account.symbol == days[i].symbol) {
                    account.initialize_price(*refs.observed_close());
                }
            }
        }
        if (terminal && days[i].scored_period) {
            for (int delay : risk_rules::delays) {
                output["forecasts"].push_back({{"symbol", days[i].symbol},
                                               {"date", date},
                                               {"next_date", nullptr},
                                               {"delay", delay},
                                               {"future_rv5", nullptr},
                                               {"endpoint2", nullptr},
                                               {"control_minus_candidate_qlike", nullptr}});
            }
        }
    }
    if (days.front().scored_period) {
        for (auto &account : state_->shared) {
            output["shared"].push_back(account.run(days, sessions));
        }
    }
    for (const auto &f : output.at("forecasts")) {
        auto &total = state_->forecasts[{f.at("symbol").get<std::string>(), f.at("delay").get<int>()}];
        ++total.dates;
        total.rv += !f.at("future_rv5").is_null();
        total.endpoint += !f.at("endpoint2").is_null();
        if (!f.at("control_minus_candidate_qlike").is_null()) {
            ++total.losses;
            total.loss_sum += f.at("control_minus_candidate_qlike").get<double>();
        }
    }
    state_->terminal = terminal;
    return output;
}
Json PanelAccountStudy::finish() {
    require(state_->terminal && !state_->finished, "account study requires exactly one terminal completion");
    state_->finished = true;
    Json risk = Json::array(), shared = Json::array();
    for (const auto &c : state_->risk) {
        risk.push_back({{"symbol", c.symbol},
                        {"account", c.name},
                        {"delay", c.delay},
                        {"cost_bps", c.cost},
                        {"statistics", c.summary.json()}});
    }
    for (const auto &c : state_->shared) {
        shared.push_back({{"variant", c.variant},
                          {"delay", c.delay},
                          {"cost_bps", c.cost},
                          {"statistics", c.summary.json()}});
    }
    Json forecasts = Json::array();
    for (const auto &[key, total] : state_->forecasts) {
        forecasts.push_back(
            {{"symbol", key.first},
             {"delay", key.second},
             {"forecast_dates", total.dates},
             {"future_rv5_dates", total.rv},
             {"endpoint_dates", total.endpoint},
             {"qlike_dates", total.losses},
             {"mean_control_minus_candidate_qlike",
              total.losses ? Json(static_cast<double>(total.loss_sum / total.losses)) : Json(nullptr)}});
    }
    return {{"risk_series", risk}, {"shared_series", shared}, {"forecast_statistics", forecasts}};
}
} // namespace trading
