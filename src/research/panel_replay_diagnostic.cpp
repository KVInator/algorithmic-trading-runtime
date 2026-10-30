#include "panel_replay_diagnostic.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trading {
namespace {
std::vector<CalendarSession> calendar_sessions(std::size_t count) {
    std::vector<CalendarSession> calendar;
    for (int year = 2016; year <= (count > 503 ? 2018 : 2017); ++year) {
        for (unsigned month = 1; month <= 12; ++month) {
            const auto last = std::chrono::year_month_day{std::chrono::year{year} /
                                                          std::chrono::month{month} / std::chrono::last};
            const auto id = std::to_string(year) + (month < 10 ? "-0" : "-") + std::to_string(month);
            const auto days = extension_calendar(
                CalendarRange{id + "-01", id + '-' + std::to_string(static_cast<unsigned>(last.day()))});
            calendar.insert(calendar.end(), days.begin(), days.end());
        }
    }
    return calendar;
}
} // namespace
std::vector<MinuteBar> PanelReplayFixture::bars(const CalendarSession &day, const std::string &symbol,
                                                std::size_t index, bool faults) {
    const std::vector<std::string> symbols{"SPY", "QQQ", "IWM", "TLT", "GLD"};
    const auto instrument =
        static_cast<double>(std::find(symbols.begin(), symbols.end(), symbol) - symbols.begin());
    const double base = 100 + 10 * instrument + .02 * static_cast<double>(index);
    std::vector<MinuteBar> out;
    double previous = base;
    const auto minutes = day.bar_request(symbol).expected_minutes();
    for (std::size_t i = 0; i < minutes; ++i) {
        const double close =
            base *
            std::exp(.000002 * static_cast<double>(i) +
                     .0015 * std::sin(.2 * static_cast<double>(i) + .17 * static_cast<double>(index)));
        const bool missing = faults && ((symbol == "TLT" && index == 240 && i == 100) ||
                                        (symbol == "GLD" && index == 250 && i == 149) ||
                                        (symbol == "SPY" && index == 260 && i == 389) ||
                                        (symbol == "IWM" && index == 265 && i == 151));
        if (!missing) {
            out.emplace_back(
                symbol, day.open_utc() + std::chrono::minutes{static_cast<int>(i)},
                Ohlc{previous, std::max(previous, close) + .1, std::min(previous, close) - .1, close},
                1000 + instrument * 50 + static_cast<double>(index % 5) * 10);
        }
        previous = close;
    }
    return out;
}

PanelReplayFixture::PanelReplayFixture(std::size_t count) : calendar_(calendar_sessions(count)), sessions_(count) {
    if (count != date_count && count != 600) { throw std::invalid_argument("unsupported analytic fixture horizon"); }
}
std::span<const CalendarSession> PanelReplayFixture::calendar() const noexcept { return calendar_; }
PanelReplayFixture::States PanelReplayFixture::replay(const PanelInputPolicy &policy,
                                                      const std::vector<std::string> &symbols,
                                                      const Consumer &consume) const {
    if (!consume || symbols.size() != 5) {
        throw std::invalid_argument("analytic replay requires five symbols and a consumer");
    }
    States states;
    for (const auto &symbol : symbols) {
        if (!states.emplace(symbol, PanelReplayState{symbol, policy}).second) {
            throw std::invalid_argument("analytic replay has a duplicate instrument");
        }
    }
    for (std::size_t i = 0; i < sessions_; ++i) {
        std::vector<PanelReplayDay> rows;
        std::vector<SessionDownload> sessions;
        for (const auto &symbol : symbols) {
            auto data = bars(calendar_[i], symbol, i);
            const auto request = calendar_[i].bar_request(symbol);
            rows.push_back(
                states.at(symbol).advance(request, data, symbol == "QQQ" && i == 268 ? 1.25 : 0));
            const auto audit = audit_session(request, data);
            sessions.push_back({request, std::move(data), {}, audit, {}, {}});
        }
        const bool common = std::all_of(rows.begin(), rows.end(),
                                        [](const auto &row) { return row.availability.operational; });
        for (auto &row : rows) {
            row.common_operational = common;
        }
        consume(std::move(rows), std::move(sessions), i + 1 == sessions_);
    }
    return states;
}

std::filesystem::path PanelReplayDiagnostic::run(const std::filesystem::path &root,
                                                 const PanelInputPolicy &policy, const Check &check,
                                                 const Reject &reject) const {
    const PanelReplayFixture fixture;
    const auto calendar = fixture.calendar();
    nlohmann::json provenance{{"evidence_origin", "synthetic"},
                              {"fixture", "analytic_panel_300"},
                              {"protocol_sha256", TRADING_EXTENSION_PROTOCOL_SHA256}};
    PanelReplayReport report{root, provenance, {{"passed", true}, {"scope", "constructed_fixture"}}};
    const auto states =
        fixture.replay(policy, {"GLD", "IWM", "QQQ", "SPY", "TLT"}, [&](auto rows, auto, bool) {
            if (rows.front().date == calendar[265].date()) {
                check("common outcome mask is shared after instrument decisions",
                      !rows.front().common_operational);
            }
            report.append(std::move(rows));
        });
    const auto output = report.finish();
    const auto &day = calendar[300];
    const auto request = day.bar_request("QQQ");
    auto strong = PanelReplayFixture::bars(day, "QQQ", 300, false);
    for (std::size_t i = 0; i < strong.size(); ++i) {
        const double base = strong.front().prices().open,
                     close = base * (1 + .001 * static_cast<double>(i));
        strong[i] = MinuteBar{"QQQ", strong[i].start(), {base, close + 1, base - 1, close}, 5000};
    }
    reject("a partial minute cannot be exposed as a completed observation", [&] {
        static_cast<void>(
            ResearchSessionView{request, strong}.before(request.start() + std::chrono::seconds{1}));
    });
    auto clean_state = states.at("QQQ");
    auto baseline = clean_state.advance(request, strong);
    check("risk forecast initializes from 252 strictly prior returns",
          baseline.risk.estimate.has_value() && baseline.risk.prior_returns == 252);
    auto future_state = states.at("QQQ");
    auto future = strong;
    future.back() = MinuteBar{"QQQ", future.back().start(), {200, 201, 199, 200}, 5000};
    const auto changed = future_state.advance(request, future);
    check("future close cannot change the morning forecast",
          panel_replay_record(baseline).at("risk") == panel_replay_record(changed).at("risk"));
    auto missing_close = states.at("QQQ");
    auto no_close = strong;
    no_close.pop_back();
    const auto gap = missing_close.advance(request, no_close);
    check("unobserved daily close retains a causal forecast but no daily outcome",
          gap.risk.estimate.has_value() && !gap.measurement);
    const auto next_data = PanelReplayFixture::bars(calendar[301], "QQQ", 301, false);
    const auto after_gap = missing_close.advance(calendar[301].bar_request("QQQ"), next_data);
    check("daily history cannot bridge an unavailable closing mark",
          !after_gap.risk.estimate && after_gap.risk.prior_returns == 0 && !after_gap.measurement);
    auto sparse_state = states.at("QQQ");
    auto sparse = strong;
    sparse.erase(sparse.begin() + 100);
    const auto sparse_day = sparse_state.advance(request, sparse);
    const auto &a = baseline.policies[3];
    const auto &b = sparse_day.policies[3];
    check("continuation enters before a later unavailable prefix",
          !a.trades.empty() && !b.trades.empty());
    for (std::size_t i = 0; i < 3; ++i) {
        check("future missing minute leaves earlier continuation targets unchanged",
              a.decisions[i].target_long == b.decisions[i].target_long);
    }
    check("missing prefix liquidates an existing position causally",
          b.trades.front().entry_decision == request.start() + std::chrono::minutes{30} &&
              b.trades.front().exit_decision == request.start() + std::chrono::minutes{120});
    check("an irrelevant missing minute preserves fixed-grid reversion",
          sparse_day.reversion && baseline.reversion &&
              sparse_day.reversion->current->values == baseline.reversion->current->values);
    auto unknown_state = states.at("QQQ");
    auto no_execution = strong;
    no_execution.erase(no_execution.begin() + 361);
    auto unknown = unknown_state.advance(request, no_execution);
    check("missing future execution does not cancel an earlier momentum signal",
          unknown.momentum->enter_long == baseline.momentum->enter_long);
    check("unobserved execution stops the affected path",
          !unknown.policies[0].execution_resolved && !unknown.policies[0].decisions.back().target_long);
    unknown.common_operational = unknown.availability.operational;
    check("missing execution outcome remains NA rather than zero",
          panel_replay_record(unknown).at("policies").at(0).at("net").is_null());
    auto anchor_state = states.at("QQQ");
    auto no_anchor = strong;
    no_anchor.erase(no_anchor.begin() + 5);
    const auto anchor_day = anchor_state.advance(request, no_anchor);
    check("missing current anchor sends every reversion comparison to cash",
          anchor_day.reversion &&
              std::none_of(anchor_day.reversion->actions.begin(), anchor_day.reversion->actions.end(),
                           [](const auto &action) { return action.enter; }));
    const auto following = anchor_state.advance(calendar[301].bar_request("QQQ"), next_data);
    check("missing historical grid occupies its original reversion slot",
          following.reversion && !following.reversion->prior.complete);
    reject("panel state rejects cross-instrument contamination", [&] {
        auto state = states.at("QQQ");
        static_cast<void>(
            state.advance(day.bar_request("IWM"), PanelReplayFixture::bars(day, "IWM", 300)));
    });
    reject("calendar history cannot silently compress an omitted date", [&] {
        auto state = states.at("QQQ");
        static_cast<void>(state.advance(calendar[301].bar_request("QQQ"), next_data));
    });
    reject("reserved dates are blocked at the strategy boundary", [&] {
        static_cast<void>(
            IntradayMomentumStrategy{CalendarSession{"2024-01-02", "09:30", "16:00"}.bar_request("QQQ"),
                                     ResearchInputMode::registered_panel});
    });
    reject("legacy defaults still reject another instrument",
           [&] { static_cast<void>(IntradayMomentumStrategy{request}); });
    std::vector<SessionDownload> legacy;
    for (const auto &month : std::vector<CalendarRange>{
             {"2022-01-01", "2022-01-31"}, {"2022-02-01", "2022-02-28"}, {"2022-03-01", "2022-03-31"}}) {
        for (const auto &session : development_calendar(month)) {
            if (legacy.size() == 40) {
                break;
            }
            auto observations = PanelReplayFixture::bars(session, "SPY", legacy.size(), false);
            legacy.push_back({session.bar_request(),
                              observations,
                              {},
                              audit_session(session.bar_request(), observations),
                              {},
                              {}});
        }
    }
    IntradayContinuationHistory no_volume{ResearchInputMode::registered_panel};
    for (std::size_t i = 0; i < 20; ++i) {
        auto zero = legacy[i].bars;
        for (auto &bar : zero) {
            bar = MinuteBar{bar.symbol(), bar.start(), bar.prices(), 0};
        }
        no_volume.append(legacy[i].request, zero);
    }
    for (const auto variant :
         {ContinuationVariant::price_only, ContinuationVariant::volume_conditioned}) {
        const auto &current = legacy[20];
        const IntradayContinuationStrategy strategy{current.request, variant,
                                                    ResearchInputMode::registered_panel};
        const auto decision = strategy.decide(no_volume, std::span{current.bars}.first(30),
                                              strategy.decision_times()[0], false);
        check("undefined volume feature sends both continuation comparisons to cash",
              !decision.features && decision.action == ContinuationAction::stay_cash);
    }
    std::vector<DailyRiskMeasurement> missing_rv;
    for (std::size_t i = 0; i < 20; ++i) {
        missing_rv.push_back({format_utc_seconds(legacy[i].request.start()).substr(0, 10),
                              0,
                              .000001 * static_cast<double>(i + 1),
                              {},
                              i == 7 ? std::nullopt : std::optional<double>{.000002},
                              {}});
    }
    const auto fallback = LaggedRiskEstimator{}.estimate(missing_rv);
    check("lagged daily risk fallback retains the exact missing date",
          !fallback.intraday && fallback.missing_dates.size() == 1 &&
              fallback.missing_dates.front() == missing_rv[7].date &&
              std::abs(fallback.daily - .0000105) < 1e-18);
    reject("risk history rejects an out-of-order measurement", [&] {
        auto invalid = missing_rv;
        std::swap(invalid[0], invalid[1]);
        static_cast<void>(LaggedRiskEstimator{}.estimate(invalid));
    });
    const auto parity = verify_panel_reference_parity(legacy);
    check("legacy-reference actions and returns retain numerical parity",
          parity.at("passed").get<bool>() && parity.at("momentum_dates").get<int>() == 40 &&
              parity.at("continuation_dates").get<int>() == 20 &&
              parity.at("reversion_dates").get<int>() == 20);
    return output;
}
} // namespace trading
