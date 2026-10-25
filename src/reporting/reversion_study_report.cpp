#include "reversion_study_report.hpp"

#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "report_text.hpp"
#include "trading_source_inventory.hpp"
#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace trading {
namespace {
using Json = nlohmann::json;

Json number(const std::optional<double>& v) {
    return v ? Json(*v) : Json(nullptr);
}

Json interval(const std::optional<PercentileInterval>& v) {
    return v ? Json{{"lower", v->lower}, {"upper", v->upper}} : Json(nullptr);
}

void cell(std::ostream& stream, const std::optional<double>& value) {
    if (value) {
        stream << *value;
    }
}

Json statistics(const ReturnStatistics& s) {
    return {{"mean", s.mean},
            {"compounded", s.compounded},
            {"maximum_daily_close_drawdown", s.maximum_drawdown},
            {"sample_standard_deviation", number(s.sample_standard_deviation)},
            {"annualized_daily_sharpe", number(s.annualized_daily_sharpe)}};
}

Json period(const ReversionPeriod& p) {
    const auto& r = p.returns;
    const auto& c = p.concentration;
    Json winners = Json::array();
    for (const auto& w : c.largest_five_winners) {
        winners.push_back({{"date", w.date}, {"net_return", w.net_return}});
    }
    return {{"sessions", r.sessions},
            {"active_dates", r.active_days},
            {"round_trips", r.active_days},
            {"holding_minutes", p.holding_minutes},
            {"mean_daily_turnover", p.mean_daily_turnover},
            {"fraction_of_session_in_market", p.fraction_of_session_in_market},
            {"gross", statistics(p.gross)},
            {"net", statistics(r.strategy_net)},
            {"always_long_net", statistics(r.always_long_net)},
            {"cash", statistics(r.cash)},
            {"positive_break_even_cost_bps_per_side", number(r.break_even_cost_bps_per_side)},
            {"net_sum", c.arithmetic_net_sum},
            {"positive_return_sum", c.positive_return_sum},
            {"top_five_net_sum", c.largest_five_net_sum},
            {"top_five_share_of_positive_returns", number(c.largest_five_share_of_positive_returns)},
            {"largest_five_winners", winners}};
}

Json gates(const std::vector<ReversionGate>& list) {
    Json out = Json::array();
    for (const auto& g : list) {
        out.push_back({{"label", g.label}, {"passed", g.passed}});
    }
    return out;
}
} // namespace

void ReversionStudyReport::write(const std::filesystem::path& directory,
                                 std::optional<std::string> manifest) const {
    if (result_.days.empty() || result_.statistics.size() != 40 || result_.uncertainty.size() != 2) {
        throw std::invalid_argument("reversion report requires analyzed results");
    }
    const auto& result = result_;
    Json artifacts = Json::array(), cases = Json::array(), metrics = Json::array(), bootstrap = Json::array(),
         ranges = Json::array();
    const auto save = [&](std::string_view filename, const std::string& bytes) {
        write_new_text_file(directory / filename, bytes);
        artifacts.push_back({{"path", filename}, {"sha256", sha256_file(directory / filename)}});
    };
    std::ostringstream observations, features, cash, html;
    observations << std::setprecision(17) << "date,anchor_price";
    for (std::size_t k = 0; k < 25; ++k) {
        observations << ",x" << k;
    }
    observations << '\n';
    for (const auto& o : result.observations) {
        observations << o.date << ',';
        if (o.measurement) {
            observations << o.measurement->anchor_price;
        }
        for (std::size_t k = 0; k < 25; ++k) {
            observations << ',';
            if (o.measurement) {
                observations << o.measurement->values[k];
            }
        }
        observations << '\n';
    }
    features << std::setprecision(17)
             << "date,decision_at,oldest_history_start,latest_history_start,history_complete,fit_status,"
                "pairs,intercept,slope,innovation_variance,equilibrium,speed_per_minute,half_life_minutes,"
                "current_residual,expected_log_move,drift_log_move,realized_log_move,squared_loss_contrast,"
                "absolute_loss_contrast";
    for (const auto p : {ReversionPolicy::candidate, ReversionPolicy::sign_only, ReversionPolicy::drift,
                         ReversionPolicy::always_long}) {
        features << ',' << name(p) << "_active," << name(p) << "_reason";
    }
    features << "\n";
    cash << "date,gross_return,net_return,equity\n";
    for (const auto& day : result.days) {
        const auto& d = day.decision;
        const auto& f = d.prior.fit;
        features << day.date << ',' << format_utc_seconds(d.at) << ',' << format_utc_seconds(d.prior.oldest)
                 << ',' << format_utc_seconds(d.prior.latest) << ',' << d.prior.complete << ','
                 << name(f.status) << ',' << f.pairs << ',';
        for (const auto& v : {f.intercept, f.slope, f.innovation_variance, f.equilibrium, f.speed_per_minute,
                              f.half_life_minutes}) {
            cell(features, v);
            features << ',';
        }
        cell(features, d.current ? std::optional{d.current->values.back()} : std::nullopt);
        features << ',';
        cell(features, d.expected_move);
        features << ',';
        cell(features, d.prior.drift);
        features << ',' << day.realized_log_move << ',';
        cell(features, day.squared_loss_contrast);
        features << ',';
        cell(features, day.absolute_loss_contrast);
        for (const auto& a : d.actions) {
            features << ',' << a.enter << ',' << ReportText{a.reason}.csv();
        }
        features << '\n';
        cash << day.date << ",0,0,1\n";
        for (const auto& u : day.parameter_ranges) {
            ranges.push_back({{"date", day.date},
                              {"block_length", u.settings.block_length},
                              {"replicates", u.settings.replicates},
                              {"seed", u.settings.seed},
                              {"status_counts", u.status_counts},
                              {"coefficient_refits", u.coefficient_refits},
                              {"admissible_refits", u.admissible_refits},
                              {"slope", interval(u.slope)},
                              {"conditional_equilibrium", interval(u.equilibrium)},
                              {"conditional_half_life_minutes", interval(u.half_life_minutes)},
                              {"conditional_expected_log_move", interval(u.expected_log_move)}});
        }
    }
    save("observations.csv", observations.str());
    save("features.csv", features.str());
    save("cash.csv", cash.str());
    save("parameter_ranges.json", ranges.dump(2) + '\n');
    std::array<std::vector<double>, 4> wealth_paths;
    for (std::size_t policy = 0; policy < 4; ++policy) {
        std::ostringstream accounts;
        accounts << std::setprecision(17)
                 << "date,case_index,delay_minutes,cost_bps_per_side,active,entry_time,exit_time,entry_price,"
                    "exit_price,gross_return,net_return,equity_before,fractional_quantity,equity_after,"
                    "drawdown,holding_minutes,turnover\n";
        for (std::size_t row = 0; row < result.days.size(); ++row) {
            const auto& day = result.days[row];
            for (std::size_t local = 0; local < 10; ++local) {
                const auto i = 10 * policy + local;
                const auto& c = reversion_cases()[i];
                const auto& r = day.accounts[i];
                const auto& point = result.paths[i].at(row);
                const bool active = day.decision.actions[policy].enter;
                const auto d = static_cast<std::size_t>(c.delay - 1);
                accounts << day.date << ',' << i << ',' << c.delay << ',' << c.cost << ',' << active << ','
                         << format_utc_seconds(day.decision.at + std::chrono::minutes{c.delay}) << ','
                         << format_utc_seconds(day.decision.at + std::chrono::minutes{30 + c.delay}) << ','
                         << day.entry_prices[d] << ',' << day.exit_prices[d] << ',' << r.gross << ',' << r.net
                         << ',' << point.equity_before << ',' << point.fractional_quantity << ','
                         << point.equity_after << ',' << point.drawdown << ',' << (active ? 30 : 0) << ','
                         << (active ? 2 : 0) << '\n';
                if (local == 2) {
                    wealth_paths[policy].push_back(point.equity_after);
                }
            }
        }
        save("accounts_" + std::string{name(static_cast<ReversionPolicy>(policy))} + ".csv", accounts.str());
    }
    for (std::size_t i = 0; i < 40; ++i) {
        Json years = Json::object();
        for (const auto& [year, p] : result.statistics[i].years) {
            years[year] = period(p);
        }
        const auto& c = reversion_cases()[i];
        cases.push_back({{"case_index", i},
                         {"policy", name(c.policy)},
                         {"delay_minutes", c.delay},
                         {"cost_bps_per_side", c.cost},
                         {"overall", period(result.statistics[i].overall)},
                         {"years", years}});
    }
    for (const auto& m : result.metrics) {
        metrics.push_back({{"label", m.label}, {"available", m.available}, {"mean", number(m.mean)}});
    }
    for (const auto& u : result.uncertainty) {
        Json list = Json::array();
        for (const auto& m : u.metrics) {
            list.push_back({{"undefined_replicates", m.undefined_replicates}, {"range", interval(m.range)}});
        }
        bootstrap.push_back({{"block_length", u.block_length},
                             {"replicates", u.replicates},
                             {"seed", u.seed},
                             {"metrics", list}});
    }
    html << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" "
            "content=\"width=device-width,initial-scale=1\"><title>SPY-MR-01 development comparison</title>"
            "<style>body{font:16px system-ui;max-width:1200px;margin:2rem auto;padding:0 "
            "1rem;color:#192c40;background:#f6f8fb}table{border-collapse:collapse;background:white;width:100%"
            "}td,th{padding:8px;text-align:right;border-bottom:1px solid "
            "#ccd7e2}td:first-child,th:first-child{text-align:left}.scroll{overflow:auto}code{overflow-wrap:"
            "anywhere}</style>"
            "<main><h1>Opening-anchor reversion: registered development comparison</h1><p>Revision 1 | "
         << ReportText{result.status}.html() << " | " << result.days.size()
         << " scored dates; twenty earlier eligible sessions per fit.</p>"
         << (manifest ? "<p>Authenticated 2022-2023 development snapshot.</p>" :
                        "<p>Constructed fixture; not historical strategy evidence.</p>")
         << "<p>Fixed noon decisions, delayed minute-open price proxies, fractional unlevered equity and "
            "hypothetical all-in costs. "
            "The sample is inspected development data. Conditional parameter ranges are descriptive; "
            "numerical OU admissibility does not establish stationarity or latent OU identification. "
            "Local 2024-2025 prices and performance remain unopened.</p><h2>Primary normalized account "
            "paths</h2><p>Initial equity one. One-minute delay, 1 bp per side. Cash remains at one.</p>";
    double low = 1, high = 1;
    for (const auto& path : wealth_paths) {
        for (const auto w : path) {
            low = std::min(low, w);
            high = std::max(high, w);
        }
    }
    const double extent = high > low ? high - low : 1;
    const auto y = [&](double w) { return 260 - 230 * (w - low) / extent; };
    html << "<svg viewBox=\"0 0 1000 300\" role=\"img\" aria-label=\"Primary normalized wealth paths\"><line "
            "x1=\"60\" x2=\"980\" y1=\""
         << y(1) << "\" y2=\"" << y(1) << "\" stroke=\"#777\" stroke-dasharray=\"5 5\"/>";
    const std::array<const char*, 4> colors{"#146c94", "#b35d00", "#7c4295", "#247353"};
    html << "<text x=\"2\" y=\"35\">" << high << "</text><text x=\"2\" y=\"260\">" << low << "</text>";
    for (std::size_t p = 0; p < 4; ++p) {
        html << "<polyline fill=\"none\" stroke=\"" << colors[p] << "\" stroke-width=\"2\" points=\"60,"
             << y(1);
        for (std::size_t i = 0; i < wealth_paths[p].size(); ++i) {
            html << ' ' << 60 + 920 * static_cast<double>(i + 1) / static_cast<double>(result.days.size())
                 << ',' << y(wealth_paths[p][i]);
        }
        html << "\"/><text x=\"" << 60 + p * 230 << "\" y=\"294\" fill=\"" << colors[p] << "\">"
             << name(static_cast<ReversionPolicy>(p)) << "</text>";
    }
    html << "<text x=\"60\" y=\"278\">" << result.days.front().date << "</text><text x=\"875\" y=\"278\">"
         << result.days.back().date
         << "</text></svg>"
            "<h2>Forty fixed account cases</h2><div "
            "class=\"scroll\"><table><tr><th>Policy</th><th>Delay</th><th>Cost bp/side</th><th>Active "
            "dates</th><th>Gross bp/day</th><th>Net bp/day</th><th>Compounded net %</th><th>Daily-close "
            "drawdown %</th></tr>";
    html << std::setprecision(6);
    for (std::size_t i = 0; i < 40; ++i) {
        const auto& c = reversion_cases()[i];
        const auto& s = result.statistics[i].overall;
        html << "<tr><td>" << name(c.policy) << "</td><td>" << c.delay << "</td><td>" << c.cost << "</td><td>"
             << s.returns.active_days << "</td><td>" << s.gross.mean * 10000 << "</td><td>"
             << s.returns.strategy_net.mean * 10000 << "</td><td>" << s.returns.strategy_net.compounded * 100
             << "</td><td>" << s.returns.strategy_net.maximum_drawdown * 100 << "</td></tr>";
    }
    html << "</table></div><h2>Registered economic screens</h2><ul>";
    for (const auto& g : result.economic_gates) {
        html << "<li>" << ReportText{g.label}.html() << ": " << (g.passed ? "passed" : "failed") << "</li>";
    }
    html << "</ul><h2>Conditional forecast comparison</h2><p>Common-mask dates: "
         << result.metrics[86].available
         << ". Positive loss contrasts mean improvement over drift. All masked dates remain in whole-date "
            "resampling.</p><ul>";
    for (const auto& g : result.forecast_gates) {
        html << "<li>" << ReportText{g.label}.html() << ": " << (g.passed ? "passed" : "failed") << "</li>";
    }
    html << "</ul><h2>Evidence</h2><ul>";
    for (const auto& a : artifacts) {
        const auto f = a.at("path").get<std::string>();
        html << "<li><a href=\"" << f << "\">" << f << "</a></li>";
    }
    html
        << "</ul><p><a href=\"summary.json\">All case/annual statistics, paired means, ranges and gates</a>. "
           "Outcome uncertainty uses " << result.uncertainty.front().replicates <<
           " common-date resamples at blocks 5 and 10, seed 20261023. "
           "Missing-statistic replicates invalidate the corresponding entire range. No historical rule is "
           "retuned after this result. "
           "Drawdown is measured at daily closes, not within the holding window.</p></main></html>\n";
    save("source_inventory.txt", std::string{build::source_records});
    save("report.html", html.str());
    const Json summary{{"schema_version", 1},
                       {"study", "SPY-MR-01"},
                       {"revision", 1},
                       {"study_status", manifest ? "registered_development_scored" : "constructed_fixture"},
                       {"assessment", result.status},
                       {"observed_sessions", result.observations.size()},
                       {"warmup_sessions", 20},
                       {"scored_dates", result.days.size()},
                       {"market_account_cases", 40},
                       {"manifest_sha256", manifest ? Json(*manifest) : Json(nullptr)},
                       {"protocol_sha256", reversion_protocol_digest},
                       {"settings_sha256", reversion_settings_digest},
                       {"source_sha256", build::source_sha256},
                       {"compiler", TRADING_COMPILER_ID},
                       {"build_configuration", TRADING_BUILD_CONFIGURATION},
                       {"cases", cases},
                       {"metrics", metrics},
                       {"outcome_uncertainty", bootstrap},
                       {"economic_gates", gates(result.economic_gates)},
                       {"forecast_gates", gates(result.forecast_gates)},
                       {"artifacts", artifacts}};
    write_new_text_file(directory / "summary.json", summary.dump(2) + '\n');
}
} // namespace trading
