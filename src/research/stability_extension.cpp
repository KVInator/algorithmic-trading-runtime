#include "stability_extension.hpp"
#include "cost_delay_study.hpp"
#include "panel_replay_diagnostic.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <tuple>

namespace trading {
namespace {
using Json = nlohmann::json;
const std::vector<std::string> intraday_units{"momentum",
                                              "momentum_always_long",
                                              "continuation_price",
                                              "continuation_volume",
                                              "reversion_candidate",
                                              "reversion_sign_only",
                                              "reversion_drift"};
const std::vector<std::tuple<std::string, std::string, std::string>> pairs{
    {"momentum_increment", "momentum", "momentum_always_long"},
    {"continuation_increment", "continuation_volume", "continuation_price"},
    {"reversion_sign_increment", "reversion_candidate", "reversion_sign_only"},
    {"reversion_drift_increment", "reversion_candidate", "reversion_drift"},
    {"risk_cer_increment", "intraday_risk", "daily_only"}};
std::vector<std::string> variant_policies(const StabilityVariant &v) {
    const auto &family = v.parameters.family;
    if (family == "momentum")
        return {"momentum", "momentum_always_long"};
    if (family == "continuation")
        return {"continuation_price", "continuation_volume"};
    if (family == "risk")
        return {"intraday_risk", "daily_only"};
    return {"reversion_candidate", "reversion_sign_only", "reversion_drift", "reversion_always_long"};
}
void require(bool ok, const char *reason) {
    if (!ok)
        throw std::invalid_argument(reason);
}
std::optional<double> observed_return(const Json &r) {
    return r.at("status").get<std::string>() == "observed" && !r.at("net_return").is_null()
               ? std::optional<double>{r.at("net_return").get<double>()}
               : std::nullopt;
}
const PanelPolicyReplay *policy(const PanelReplayDay &d, const std::string &name) {
    const auto it = std::find_if(d.policies.begin(), d.policies.end(),
                                 [&](const auto &p) { return p.policy == name; });
    return it == d.policies.end() ? nullptr : &*it;
}
std::optional<StabilityPair> joined(std::optional<double> a, std::optional<double> b) {
    return a && b ? std::optional<StabilityPair>{{*a, *b}} : std::nullopt;
}
} // namespace
Json replay_parameters_json(const PanelReplayParameters &p) {
    return {{"family", p.family},
            {"momentum_hold_minutes", p.momentum_hold_minutes},
            {"continuation_sessions", p.continuation_sessions},
            {"reversion_sessions", p.reversion.history_sessions},
            {"reversion_grid_minutes", p.reversion.observation_minutes},
            {"reversion_hold_minutes", p.reversion.hold_minutes},
            {"risk_sessions", p.risk_sessions},
            {"risk_grid_minutes", p.risk_grid_minutes}};
}
std::vector<StabilityVariant> stability_variants(const ExtensionStudyContract &contract) {
    std::vector<StabilityVariant> result;
    std::set<std::string> names;
    for (const auto &v : contract.document().at("stability").at("variants")) {
        StabilityVariant x{v.at("id").get<std::string>(), v.at("family").get<std::string>(), {}};
        const auto field = v.at("parameter").get<std::string>();
        const auto n = v.at("value").get<int>();
        if (x.family == "momentum" && field == "hold_minutes") {
            x.parameters.family = "momentum";
            x.parameters.momentum_hold_minutes = n;
        } else if (x.family == "continuation" && field == "history_sessions") {
            x.parameters.family = "continuation";
            x.parameters.continuation_sessions = static_cast<std::size_t>(n);
        } else if (x.family == "reversion") {
            x.parameters.family = "reversion";
            if (field == "history_sessions")
                x.parameters.reversion.history_sessions = static_cast<std::size_t>(n);
            else if (field == "observation_minutes")
                x.parameters.reversion.observation_minutes = n;
            else if (field == "hold_minutes")
                x.parameters.reversion.hold_minutes = n;
            else
                throw std::invalid_argument("unknown reversion stability parameter");
        } else if (x.family == "daily_risk" && field == "history_sessions") {
            x.parameters.family = "risk";
            x.parameters.risk_sessions = static_cast<std::size_t>(n);
        } else if (x.family == "daily_risk_measurement" && field == "observation_minutes") {
            x.parameters.family = "risk";
            x.parameters.risk_grid_minutes = n;
        } else
            throw std::invalid_argument("unknown registered stability case");
        x.parameters.validate();
        require(names.insert(x.id).second, "duplicate stability case");
        result.push_back(std::move(x));
    }
    require(result.size() == 14, "the complete fourteen-case neighborhood is required");
    return result;
}
class StabilityExtensionStudy::State {
  public:
    State(const ExtensionStudyContract &c, PanelInputPolicy p, Json a)
        : contract(c), input_policy(std::move(p)), actions(std::move(a)),
          variants(stability_variants(c)), costs(c) {
        for (const auto &symbol : c.symbols()) {
            for (const auto &v : variants) {
                replays.try_emplace(v.id + '/' + symbol, symbol, input_policy, v.parameters);
                if (v.parameters.family == "risk")
                    for (const auto *name : {"intraday_risk", "daily_only"})
                        secondary_risk.try_emplace(v.id + '/' + symbol + '/' + name, symbol, name);
            }
            for (const auto &name : intraday_units)
                unit_intraday.try_emplace(symbol + '/' + name);
            for (const auto *name : {"intraday_risk", "daily_only"})
                unit_risk.try_emplace(symbol + '/' + name, symbol, name);
            for (const auto &[id, a_name, b_name] : pairs) {
                static_cast<void>(a_name);
                static_cast<void>(b_name);
                windows.try_emplace(id + '/' + symbol);
                for (const auto *side : {"candidate", "control"}) {
                    const auto key = id + '/' + symbol + '/' + side;
                    if (id == "risk_cer_increment")
                        matched_risk.try_emplace(key, symbol,
                                                 std::string{side} == "candidate" ? "intraday_risk"
                                                                                  : "daily_only");
                    else
                        matched_intraday.try_emplace(key);
                }
            }
        }
    }
    StabilitySeries &series(const std::string &id, const std::string &layer, const std::string &symbol,
                            const std::string &name) {
        return output.try_emplace(id, StabilitySeries{id, layer, symbol, name, {}}).first->second;
    }
    ExtensionStudyContract contract;
    PanelInputPolicy input_policy;
    Json actions;
    std::vector<StabilityVariant> variants;
    CostDelayStudy costs;
    std::map<std::string, PanelReplayState> replays;
    std::map<std::string, PanelRiskLedger> secondary_risk, unit_risk, matched_risk;
    std::map<std::string, IntradayAccountLedger> unit_intraday, matched_intraday;
    std::map<std::string, RiskMatchWindow> windows;
    std::map<std::string, StabilitySeries> output;
    std::map<std::string, PriorMarketCondition> conditions;
    std::map<std::string, std::size_t> complete_prior, regular_prior;
    std::map<std::string, bool> momentum_initialized;
    std::map<std::string, std::pair<PanelReplayDay, RiskSessionReferences>> pending;
    std::map<std::string, std::map<std::string, std::optional<double>>> losses;
    std::vector<std::string> dates, calendar;
    std::vector<bool> common;
    Json market{{"dates", Json::array()},
                {"common_operational", Json::array()},
                {"log_returns", Json::object()},
                {"conditions", Json::object()}};
    bool terminal{};
};
StabilityExtensionStudy::StabilityExtensionStudy(const ExtensionStudyContract &c, PanelInputPolicy p,
                                                 Json actions)
    : state_(std::make_unique<State>(c, std::move(p), std::move(actions))) {}
StabilityExtensionStudy::~StabilityExtensionStudy() = default;
Json StabilityExtensionStudy::plan() const {
    Json variants = Json::array();
    for (const auto &v : state_->variants)
        variants.push_back({{"id", v.id},
                            {"registered_family", v.family},
                            {"parameters", replay_parameters_json(v.parameters)}});
    return {
        {"protocol_sha256", state_->contract.identity()},
        {"stage", "M7_secondary_and_risk_matched"},
        {"variants", variants},
        {"primary_cost_bps", 1},
        {"delay_minutes", 1},
        {"secondary_parameter_crossing", false},
        {"primary_rules_replaced", false},
        {"risk_matching", state_->contract.document().at("risk_matching")},
        {"risk_window",
         "252_prior_calendar_slots_of_genuine_unit_ledger_returns_missing_slots_retained"},
        {"unit_initialization",
         "momentum_first_observed_feature_continuation_20_complete_prior_sessions_reversion_20_prior_"
         "regular_slots_risk_first_deployable_forecast"},
        {"shortened_unit_days",
         "observed_cash_zero_only_after_account_started_and_proved_flat_no_intraday_trading"},
        {"missing_execution", "retain_partial_fills_then_stop_no_liquidation_reset_or_restart"},
        {"missing_overlay_after_start", "stop_regular_session_account_without_inventing_a_cash_return"},
        {"overlay_placement", "intraday_entry_target_fraction_risk_base_target_times_multiplier_costs_"
                              "replayed_on_actual_notional"},
        {"intraday_exposure",
         "target_fraction_times_minutes_in_market_divided_by_390_not_a_mark_to_market_average"},
        {"unit_costs", "primary_one_basis_point_per_side_net_unit_ledger_returns"},
        {"secondary_economic_mask",
         "original_five_instrument_common_operational_mask_intraday_risk_own_observed_account"},
        {"matched_economic_mask",
         "intraday_original_common_operational_mask_and_observed_pair_risk_observed_pair"},
        {"matched_qlike", "unchanged_primary_forecast_diagnostic_not_scaled_by_capital_overlay"},
        {"matched_inference",
         "secondary_diagnostic_family_does_not_replace_primary_results_or_authorize_advancement"},
        {"inference_calibration_validated", false},
        {"reserved_strategy_inputs_opened", false}};
}
std::vector<Json> StabilityExtensionStudy::append(std::span<const PanelReplayDay> primary,
                                                  std::span<const SessionDownload> sessions,
                                                  std::span<const std::optional<MinuteBar>> recovered,
                                                  bool terminal) {
    auto &s = *state_;
    require(primary.size() == 5 && sessions.size() == 5 && recovered.size() == 5 && !s.terminal,
            "extension requires complete source batches before terminal");
    const auto date = primary.front().date;
    s.contract.require_development_access(date);
    require(s.calendar.empty() || date > s.calendar.back(), "extension calendar unordered");
    std::set<std::string> names;
    for (const auto &d : primary) {
        require(d.date == date && names.insert(d.symbol).second, "mixed extension source batch");
    }
    require(names == std::set<std::string>{"SPY", "QQQ", "IWM", "TLT", "GLD"},
            "extension universe differs");
    s.calendar.push_back(date);
    s.market["dates"].push_back(date);
    s.market["common_operational"].push_back(primary.front().common_operational);
    if (date > "2016-12-31") {
        s.dates.push_back(date);
        s.common.push_back(primary.front().common_operational);
    }
    std::vector<Json> records;
    std::optional<double> pooled_log = 0;
    for (const auto &d : primary) {
        if (d.measurement)
            *pooled_log += d.measurement->log_return / 5;
        else {
            pooled_log.reset();
            break;
        }
    }
    const auto pooled_condition = s.conditions["equal_weight"].advance(date, pooled_log);
    s.market["conditions"]["equal_weight"].push_back(pooled_condition);
    for (std::size_t i = 0; i < 5; ++i) {
        const auto &d = primary[i];
        const auto &session = sessions[i];
        require(session.request.symbol() == d.symbol, "extension market identity differs");
        const auto log = d.measurement ? std::optional<double>{d.measurement->log_return} : std::nullopt;
        s.market["log_returns"][d.symbol].push_back(log ? Json(*log) : Json(nullptr));
        const auto condition = s.conditions[d.symbol].advance(date, log);
        s.market["conditions"][d.symbol].push_back(condition);
        auto bars = session.bars;
        if (recovered[i])
            bars.push_back(*recovered[i]);
        const auto cal = extension_calendar(CalendarRange{date, date}).at(0);
        const RiskSessionReferences refs{cal, std::move(bars), d.symbol,
                                         ResearchInputMode::registered_panel};
        const auto prior = s.pending.find(d.symbol);
        Json forecast = nullptr;
        if (prior != s.pending.end() && prior->second.first.scored_period) {
            const auto &[pred, first] = prior->second;
            std::optional<double> loss, target;
            double cash = 0;
            for (const auto &a : s.actions)
                if (a.at("symbol").get<std::string>() == d.symbol &&
                    a.at("ex_date").get<std::string>() == date)
                    cash += a.at("usd_per_share").get<double>();
            if (first.open_at(first.calendar().open_utc() + std::chrono::minutes{1}) &&
                refs.open_at(cal.open_utc() + std::chrono::minutes{1}) && refs.open_at(cal.open_utc())) {
                const auto y = measure_holding_risk(first, refs, cash, 1);
                target = y.rv5;
                if (target && pred.risk.estimate && pred.risk.estimate->intraday) {
                    const double hd = pred.risk.estimate->daily, hi = *pred.risk.estimate->intraday;
                    loss = (std::log(hd) - std::log(hi)) + *target * (1 / hd - 1 / hi);
                }
            }
            s.losses[d.symbol][pred.date] = loss;
            forecast = {{"date", pred.date},
                        {"next_date", date},
                        {"future_rv5", target ? Json(*target) : Json(nullptr)},
                        {"control_minus_candidate_qlike", loss ? Json(*loss) : Json(nullptr)}};
        }
        s.pending.insert_or_assign(d.symbol, std::pair{d, refs});
        if (terminal)
            s.losses[d.symbol][date] = std::nullopt;
        Json variants = Json::object(), secondary_accounts = Json::object();
        const auto retain = [&](const std::string &id, const std::string &layer, const std::string &name,
                                StabilityObservation observation) {
            if (!d.scored_period)
                return;
            observation.condition = condition.at("label").get<std::string>();
            s.series(id, layer, d.symbol, name).days.push_back(std::move(observation));
        };
        for (const auto &v : s.variants) {
            auto row = s.replays.at(v.id + '/' + d.symbol)
                           .advance(
                               session.request, session.bars,
                               [&] {
                                   double x = 0;
                                   for (const auto &a : s.actions)
                                       if (a.at("symbol").get<std::string>() == d.symbol &&
                                           a.at("ex_date").get<std::string>() == date)
                                           x += a.at("usd_per_share").get<double>();
                                   return x;
                               }(),
                               recovered[i]);
            row.common_operational = d.common_operational;
            variants[v.id] = panel_replay_record(row);
            if (v.parameters.family == "risk") {
                for (const auto *name : {"intraday_risk", "daily_only"}) {
                    auto r = s.secondary_risk.at(v.id + '/' + d.symbol + '/' + name)
                                 .append(row, refs, s.actions, terminal);
                    secondary_accounts[v.id][name] = r;
                    retain("secondary/" + v.id + '/' + d.symbol + '/' + name, "risk", name,
                           ledger_observation(r, true));
                }
            } else
                for (const auto &name : variant_policies(v)) {
                    StabilityObservation o;
                    const auto *p = policy(row, name);
                    require(!row.availability.regular_session || p != nullptr,
                            "secondary regular policy absent");
                    if (d.scored_period && d.common_operational) {
                        require(p != nullptr, "common secondary path absent");
                        const auto path = s.costs.replay(*p, session.request, session.bars, 1);
                        require(path.resolved, "common mask admitted unresolved secondary path");
                        const auto &c = path.outcomes.at(2);
                        o.net = c.net_return;
                        o.gross = c.gross_return;
                        o.cost = c.cost_paid;
                        o.turnover = c.traded_notional;
                        o.holding_minutes = static_cast<double>(path.minutes_held);
                        o.exposure = *o.holding_minutes / 390;
                        o.trades = static_cast<double>(path.trades.size());
                    }
                    retain("secondary/" + v.id + '/' + d.symbol + '/' + name, "intraday", name,
                           std::move(o));
                }
        }
        Json unit = Json::object(), matched = Json::object();
        if (d.momentum)
            s.momentum_initialized[d.symbol] = true;
        for (const auto &name : intraday_units) {
            const bool ready = name.starts_with("momentum")       ? s.momentum_initialized[d.symbol]
                               : name.starts_with("continuation") ? s.complete_prior[d.symbol] >= 20
                                                                  : s.regular_prior[d.symbol] >= 20;
            auto r =
                s.unit_intraday.at(d.symbol + '/' + name).append(d, session, policy(d, name), ready, 1.);
            unit[name] = r;
            retain("unit/" + d.symbol + '/' + name, "intraday", name, ledger_observation(r, false));
        }
        for (const auto *name : {"intraday_risk", "daily_only"}) {
            auto r = s.unit_risk.at(d.symbol + '/' + name).append(d, refs, s.actions, terminal);
            unit[name] = r;
            retain("unit/" + d.symbol + '/' + name, "risk", name, ledger_observation(r, true));
        }
        for (const auto &[id, a, b] : pairs) {
            auto &window = s.windows.at(id + '/' + d.symbol);
            const auto sizing = window.prior(date);
            const bool risk = id == "risk_cer_increment";
            Json a_row, b_row;
            if (risk) {
                const auto ta =
                    d.risk.intraday_weight && sizing.candidate_scale
                        ? std::optional<double>{*d.risk.intraday_weight * *sizing.candidate_scale}
                        : std::nullopt;
                const auto tb = d.risk.daily_weight && sizing.control_scale
                                    ? std::optional<double>{*d.risk.daily_weight * *sizing.control_scale}
                                    : std::nullopt;
                a_row = s.matched_risk.at(id + '/' + d.symbol + "/candidate")
                            .append_sized(d, refs, s.actions, terminal, ta);
                b_row = s.matched_risk.at(id + '/' + d.symbol + "/control")
                            .append_sized(d, refs, s.actions, terminal, tb);
            } else {
                a_row = s.matched_intraday.at(id + '/' + d.symbol + "/candidate")
                            .append(d, session, policy(d, a), true, sizing.candidate_scale);
                b_row = s.matched_intraday.at(id + '/' + d.symbol + "/control")
                            .append(d, session, policy(d, b), true, sizing.control_scale);
            }
            matched[id] = {{"sizing", sizing.json()}, {"candidate", a_row}, {"control", b_row}};
            retain("matched/" + id + '/' + d.symbol + "/candidate", risk ? "risk" : "intraday", a,
                   ledger_observation(a_row, risk));
            retain("matched/" + id + '/' + d.symbol + "/control", risk ? "risk" : "intraday", b,
                   ledger_observation(b_row, risk));
            window.commit(date, joined(observed_return(unit.at(a)), observed_return(unit.at(b))));
        }
        if (d.availability.regular_session) {
            ++s.regular_prior[d.symbol];
            if (audit_session(session.request, session.bars).complete() &&
                !d.availability.diagnostic_excluded)
                ++s.complete_prior[d.symbol];
        }
        records.push_back({{"primary", panel_replay_record(d)},
                           {"variants", variants},
                           {"secondary_accounts", secondary_accounts},
                           {"unit", unit},
                           {"matched", matched},
                           {"forecast", forecast}});
    }
    s.terminal = terminal;
    return records;
}
StabilityInputs StabilityExtensionStudy::observations() const {
    const auto &s = *state_;
    require(s.terminal, "extension requires an explicit terminal date");
    StabilityInputs result;
    result.dates = s.dates;
    result.market = s.market;
    auto all = s.output;
    const auto pool = [&](const std::string &prefix, const std::string &name, const std::string &layer) {
        StabilitySeries out{prefix + "/equal_weight/" + name, layer, "equal_weight", name, {}};
        for (std::size_t i = 0; i < s.dates.size(); ++i) {
            StabilityObservation o;
            o.net = 0;
            o.gross = 0;
            o.exposure = 0;
            o.turnover = 0;
            o.cost = 0;
            o.holding_minutes = 0;
            o.trades = 0;
            for (const auto &symbol : s.contract.symbols()) {
                const auto &v = all.at(prefix + '/' + symbol + '/' + name).days[i];
                const auto add = [](auto &a, const auto &b) {
                    if (a && b)
                        *a += *b / 5;
                    else
                        a.reset();
                };
                add(o.net, v.net);
                add(o.gross, v.gross);
                add(o.exposure, v.exposure);
                add(o.turnover, v.turnover);
                add(o.cost, v.cost);
                add(o.holding_minutes, v.holding_minutes);
                add(o.trades, v.trades);
            }
            o.condition = s.market.at("conditions")
                              .at("equal_weight")
                              .at(s.calendar.size() - s.dates.size() + i)
                              .at("label")
                              .get<std::string>();
            out.days.push_back(std::move(o));
        }
        all.emplace(out.id, std::move(out));
    };
    for (const auto &v : s.variants) {
        const auto family = v.parameters.family;
        const auto names = variant_policies(v);
        for (const auto &name : names)
            pool("secondary/" + v.id, name, family == "risk" ? "risk" : "intraday");
    }
    for (const auto &[id, a, b] : pairs) {
        static_cast<void>(a);
        static_cast<void>(b);
        pool("matched/" + id, "candidate", id == "risk_cer_increment" ? "risk" : "intraday");
        pool("matched/" + id, "control", id == "risk_cer_increment" ? "risk" : "intraday");
    }
    for (std::size_t j = 0; j < 6; ++j) {
        const auto &e = s.contract.document().at("primary_endpoints").at(j);
        StabilityEndpoint endpoint{
            e.at("id").get<std::string>(), j == 4, e.at("material_increment").get<double>(), {}};
        for (std::size_t i = 0; i < s.dates.size(); ++i) {
            if (j < 5) {
                const auto &id = std::get<0>(pairs[j]);
                if (j < 4 && !s.common[i]) {
                    endpoint.days.push_back(std::nullopt);
                    continue;
                }
                endpoint.days.push_back(
                    joined(all.at("matched/" + id + "/equal_weight/candidate").days[i].net,
                           all.at("matched/" + id + "/equal_weight/control").days[i].net));
            } else {
                std::optional<double> loss = 0;
                for (const auto &symbol : s.contract.symbols()) {
                    const auto x = s.losses.at(symbol).at(s.dates[i]);
                    if (x)
                        *loss += *x / 5;
                    else {
                        loss.reset();
                        break;
                    }
                }
                endpoint.days.push_back(loss ? std::optional<StabilityPair>{{*loss, 0}} : std::nullopt);
            }
        }
        result.endpoints.push_back(std::move(endpoint));
    }
    for (auto &[id, v] : all) {
        static_cast<void>(id);
        require(v.days.size() == s.dates.size(), "extension series calendar truncated");
        result.series.push_back(std::move(v));
    }
    require(result.series.size() == 345, "extension series matrix differs");
    return result;
}
Json StabilityExtensionStudy::finish(const StabilityStudy::Progress &progress) const {
    const auto input = observations();
    Json inference = Json::array();
    for (std::size_t block : {5U, 10U, 20U, 40U}) {
        if (progress)
            progress("Secondary risk-matched full-calendar uncertainty | block " +
                     std::to_string(block));
        inference.push_back(FullCalendarInference{state_->contract}.run(input.endpoints, block));
    }
    return {{"groups", StabilityStudy::describe(input)},
            {"matched_inference", inference},
            {"secondary_variants", 14},
            {"secondary_series", 240},
            {"matched_accounts", 50},
            {"unit_accounts", 45},
            {"series_count", 345},
            {"calendar_dates", input.dates.size()},
            {"secondary_variants_executed", true},
            {"risk_matching_executed", true},
            {"inference_calibration_validated", false},
            {"advancement_assessed", false}};
}
Json StabilityExtensionStudy::numerical_checks() {
    RiskMatchWindow window, constant;
    const auto fixed = std::chrono::sys_days{std::chrono::year{2016} / 1 / 1};
    Json checks = Json::array();
    const auto check = [&](const char *label, bool value) {
        require(value, label);
        checks.push_back(label);
    };
    const auto rejects = [&](const char *label, const auto &action) {
        bool rejected = false;
        try {
            action();
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(label, rejected);
    };
    for (int i = 0; i < 252; ++i) {
        const auto date = format_utc_seconds(fixed + std::chrono::days{i}).substr(0, 10);
        window.commit(date, StabilityPair{.001 * std::sin(.13 * i), .002 * std::cos(.17 * i)});
        constant.commit(date, StabilityPair{.003, .002});
    }
    const auto date = format_utc_seconds(fixed + std::chrono::days{252}).substr(0, 10);
    const auto sizing = window.prior(date);
    check("strictly_prior_unlevered_sizing",
          sizing.target && *sizing.candidate_scale <= 1 && *sizing.control_scale <= 1);
    check("constant_nonzero_returns_unavailable", !constant.prior(date).target);
    rejects("current_return_cannot_enter_overlay",
            [&] { static_cast<void>(window.prior(sizing.last)); });
    rejects("duplicate_unit_slot_rejected", [&] { window.commit(sizing.last, StabilityPair{.1, .1}); });
    rejects("invalid_unit_return_rejected", [&] { window.commit(date, StabilityPair{-1, .1}); });
    window.commit(date, std::nullopt);
    const auto missing = window.prior("2017-01-01");
    check("missing_unit_slot_retained", !missing.target && missing.slots == 252 && missing.valid == 251);
    const PanelReplayFixture fixture;
    const auto calendar = fixture.calendar();
    const auto make = [&](std::size_t index, bool missing_exit) {
        const auto &cal = calendar[index];
        auto bars = PanelReplayFixture::bars(cal, "QQQ", index, false);
        if (missing_exit)
            bars.erase(bars.begin() + 21);
        const auto request = cal.bar_request("QQQ");
        const auto audit = audit_session(request, bars);
        return SessionDownload{request, std::move(bars), {}, audit, {}, {}};
    };
    const auto day = [&](std::size_t index) {
        PanelReplayDay d;
        d.symbol = "QQQ";
        d.date = calendar[index].date();
        d.availability.regular_session = true;
        return d;
    };
    const auto path = [&](std::size_t index) {
        PanelPolicyReplay p;
        p.policy = "constructed";
        p.decisions = {{calendar[index].open_utc() + std::chrono::minutes{10}, true, "", {}},
                       {calendar[index].open_utc() + std::chrono::minutes{20}, false, "", {}}};
        return p;
    };
    const auto market = make(0, false);
    const auto policy = path(0);
    const auto first = day(0);
    IntradayAccountLedger full, half, waiting, stopped;
    const auto one = full.append(first, market, &policy, true, 1.);
    const auto scaled = half.append(first, market, &policy, true, .5);
    check("funded_scaled_round_trip", scaled.at("status") == "observed" &&
                                          scaled.at("events").size() == 2 &&
                                          scaled.at("shares").get<double>() < 1e-8);
    check("fees_replayed_on_scaled_notional",
          scaled.at("cost").get<double>() > 0 &&
              scaled.at("cost").get<double>() < one.at("cost").get<double>());
    check("scaled_return_is_not_a_pnl_multiplier",
          std::abs(scaled.at("net_return").get<double>() - .5 * one.at("net_return").get<double>()) >
              1e-12);
    const auto initial = waiting.append(first, market, &policy, false, 1.);
    check("warmup_has_no_economic_zero",
          initial.at("status") == "awaiting_history" && initial.at("net_return").is_null());
    rejects("levered_overlay_rejected", [&] {
        IntradayAccountLedger a;
        static_cast<void>(a.append(first, market, &policy, true, 1.1));
    });
    const auto partial = stopped.append(first, make(0, true), &policy, true, 1.);
    check("missing_exit_retains_filled_position",
          partial.at("status") == "stopped" && partial.at("events").size() == 1 &&
              partial.at("shares").get<double>() > 0 && partial.at("net_return").is_null());
    const auto next_policy = path(1);
    const auto after = stopped.append(day(1), make(1, false), &next_policy, true, 1.);
    check("stopped_account_cannot_restart", after.at("status") == "stopped" &&
                                                after.at("events").empty() &&
                                                after.at("shares") == partial.at("shares"));
    const auto missing_overlay = half.append(day(1), make(1, false), &next_policy, true, std::nullopt);
    check("missing_overlay_after_start_stops",
          missing_overlay.at("status") == "stopped" && missing_overlay.at("net_return").is_null());
    return {{"sizing", sizing.json()},
            {"missing_slot", missing.json()},
            {"native_checks", checks},
            {"source", "252_sine_cosine_unit_returns_not_market_data"}};
}
} // namespace trading
