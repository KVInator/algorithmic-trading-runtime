#include "continuation_study_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {

using Json = nlohmann::json;
constexpr std::array<std::string_view, 3> colors{"#2563eb", "#0f766e", "#64748b"};
constexpr std::array<std::string_view, 3> names{"Price-only", "With volume", "Always long"};

std::ostringstream stream() {
    std::ostringstream result;
    result.imbue(std::locale::classic());
    result << std::setprecision(17);
    return result;
}

std::string number(double value, int precision = 3) {
    if (!std::isfinite(value)) { throw std::invalid_argument("report requires finite numbers"); }
    auto out = stream();
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string optional(const std::optional<double>& value, double scale = 1) {
    return value ? number(*value * scale) : "undefined";
}

std::string escape(std::string_view value) {
    std::string result;
    for (const char character : value) {
        switch (character) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        default: result += character;
        }
    }
    return result;
}

void validate_report(const ContinuationStudyResult& study) {
    if (study.days.empty() || study.statistics.size() != continuation_case_count || study.bootstrap.size() != 2) {
        throw std::invalid_argument("continuation report requires complete paired statistics");
    }
}

struct PlotSeries {
    std::string name;
    std::string color;
    std::vector<double> values;
};

std::string plot(std::string_view id, std::string_view title, std::string_view unit,
                 const std::vector<double>& x, const std::vector<std::pair<double, std::string>>& labels,
                 const std::vector<PlotSeries>& series) {
    if (x.size() < 2 || x.back() <= x.front()) { throw std::invalid_argument("plot requires an ascending x range"); }
    double low = 0, high = 0;
    bool first = true;
    for (const auto& item : series) {
        if (item.values.size() != x.size()) { throw std::invalid_argument("plot series length mismatch"); }
        for (const auto value : item.values) {
            if (!std::isfinite(value)) { throw std::invalid_argument("plot contains a non-finite observation"); }
            low = first ? value : std::min(low, value);
            high = first ? value : std::max(high, value);
            first = false;
        }
    }
    if (first) { throw std::invalid_argument("plot requires at least one series"); }
    if (low == high) { low -= 0.5; high += 0.5; }
    const double raw_step = (high - low) / 5;
    const double power = std::pow(10.0, std::floor(std::log10(raw_step)));
    const double scaled = raw_step / power;
    const double step = (scaled <= 1 ? 1 : scaled <= 2 ? 2 : scaled <= 5 ? 5 : 10) * power;
    low = std::floor(low / step) * step;
    high = std::ceil(high / step) * step;
    const auto horizontal = [&](double value) { return 82 + 850 * (value - x.front()) / (x.back() - x.front()); };
    const auto vertical = [&](double value) { return 268 - 220 * (value - low) / (high - low); };
    auto out = stream();
    out << "<svg viewBox=\"0 0 1000 330\" role=\"img\" aria-labelledby=\"" << id
        << "\"><title id=\"" << id << "\">" << escape(title) << "</title>";
    for (double tick = low; tick <= high + step * 0.1; tick += step) {
        out << "<line x1=\"82\" x2=\"932\" y1=\"" << vertical(tick) << "\" y2=\"" << vertical(tick)
            << "\" stroke=\"#dce4ee\"/><text x=\"72\" y=\"" << vertical(tick) + 4
            << "\" text-anchor=\"end\">" << number(tick, 2) << "</text>";
    }
    for (const auto& [position, label] : labels) {
        out << "<text x=\"" << horizontal(position) << "\" y=\"292\" text-anchor=\"middle\">" << escape(label) << "</text>";
    }
    out << "<text x=\"82\" y=\"24\">" << escape(unit) << "</text>";
    for (const auto& item : series) {
        out << "<polyline fill=\"none\" stroke=\"" << item.color << "\" stroke-width=\"2.2\" points=\"";
        for (std::size_t i = 0; i < x.size(); ++i) { out << horizontal(x[i]) << ',' << vertical(item.values[i]) << ' '; }
        out << "\"/>";
    }
    out << "</svg><div class=\"legend\">";
    for (const auto& item : series) { out << "<span style=\"color:" << item.color << "\">" << escape(item.name) << "</span> "; }
    return out.str() + "</div>";
}

Json optional_json(const std::optional<double>& value) { return value ? Json(*value) : Json(nullptr); }
Json interval_json(const PercentileInterval& value) { return {{"lower", value.lower}, {"upper", value.upper}}; }
Json interval_json(const std::optional<PercentileInterval>& value) { return value ? interval_json(*value) : Json(nullptr); }

Json series_json(const ReturnStatistics& value) {
    return {{"mean_daily_return", value.mean}, {"compounded_return", value.compounded},
        {"maximum_daily_close_drawdown", value.maximum_drawdown},
        {"sample_daily_standard_deviation", optional_json(value.sample_standard_deviation)},
        {"annualized_daily_sharpe", optional_json(value.annualized_daily_sharpe)}};
}

Json period_json(const ContinuationPeriodStatistics& value) {
    Json winners = Json::array();
    const auto& concentration = value.concentration;
    for (const auto& winner : concentration.largest_five_winners) {
        winners.push_back({{"date", winner.date}, {"net_return", winner.net_return}});
    }
    return {{"sessions", value.sessions}, {"active_days", value.active_days}, {"round_trips", value.round_trips},
        {"fraction_of_full_session_in_market", value.fraction_of_session_in_market}, {"mean_daily_turnover", value.mean_daily_turnover},
        {"gross", series_json(value.gross)}, {"net", series_json(value.net)}, {"always_long_net", series_json(value.control_net)},
        {"concentration", {{"largest_five_winners", winners}, {"arithmetic_net_sum", concentration.arithmetic_net_sum},
            {"positive_return_sum", concentration.positive_return_sum}, {"largest_five_net_sum", concentration.largest_five_net_sum},
            {"net_sum_without_five_winners", concentration.arithmetic_net_sum - concentration.largest_five_net_sum},
            {"largest_five_share_of_positive_returns", optional_json(concentration.largest_five_share_of_positive_returns)},
            {"largest_five_share_of_arithmetic_net", optional_json(concentration.largest_five_share_of_arithmetic_net)}}}};
}

Json assessment_json(const ContinuationAssessment& value) {
    Json point = Json::array(), uncertainty = Json::array();
    for (const auto& gate : value.point_gates) { point.push_back({{"name", gate.name}, {"passed", gate.passed}}); }
    for (const auto& gate : value.uncertainty_gates) { uncertainty.push_back({{"name", gate.name}, {"passed", gate.passed}}); }
    return {{"status", value.status}, {"point_gates", point}, {"uncertainty_gates", uncertainty},
        {"later_evaluation_allowed", value.later_evaluation_allowed}};
}

Json groups_json(const ContinuationContrasts& value) {
    Json clocks = Json::array();
    for (std::size_t i = 0; i < value.clocks.size(); ++i) {
        const auto& group = value.clocks[i];
        clocks.push_back({{"elapsed_minute", 30 * (i + 1)}, {"price_true_count", group.price_true_count},
            {"price_false_count", group.price_false_count}, {"high_volume_among_price_true_count", group.high_volume_count},
            {"other_volume_among_price_true_count", group.other_volume_count}, {"price_true_forward_sum", group.price_true_sum},
            {"price_false_forward_sum", group.price_false_sum}, {"high_volume_forward_sum", group.high_volume_sum},
            {"other_volume_forward_sum", group.other_volume_sum}});
    }
    return {{"price_contrast", optional_json(value.price)}, {"volume_contrast", optional_json(value.volume)}, {"clocks", clocks}};
}

std::string interval_text(const std::optional<PercentileInterval>& value) {
    return value ? "[" + number(value->lower * 10000) + ", " + number(value->upper * 10000) + "]" : "undefined";
}

std::string gate_table(const ContinuationAssessment& assessment) {
    std::string result = "<table><thead><tr><th>Screen</th><th>Result</th></tr></thead><tbody>";
    for (const auto* gates : {&assessment.point_gates, &assessment.uncertainty_gates}) {
        for (const auto& gate : *gates) {
            auto label = gate.name;
            std::replace(label.begin(), label.end(), '_', ' ');
            result += "<tr><td>" + escape(label) + "</td><td>" + (gate.passed ? "pass" : "fail") + "</td></tr>";
        }
    }
    return result + "</tbody></table>";
}

Json diagnostic_periods_json(std::span<const PairedDiagnosticPeriod> periods) {
    Json rows = Json::array();
    for (const auto& p : periods) {
        rows.push_back({{"label", p.label}, {"sessions", p.sessions}, {"active_days", p.active_days},
            {"price_gross", series_json(p.gross[0])}, {"volume_gross", series_json(p.gross[1])},
            {"price_net", series_json(p.net[0])}, {"volume_net", series_json(p.net[1])},
            {"paired_net_mean", p.paired_net_mean}, {"paired_gross_mean", p.paired_gross_mean}, {"cash_dates", p.cash_dates}});
    }
    return rows;
}

Json diagnostics_json(const ContinuationDiagnosticsResult& d) {
    Json matches = Json::array(), clocks = Json::array();
    for (const auto& row : d.attribution) {
        matches.push_back({{"match", exposure_match_name(row.match)}, {"price_trades", row.price_trades},
            {"removed_positive_segments", row.removed_positive_segments}, {"removed_negative_segments", row.removed_negative_segments},
            {"gross_log_increment", row.gross_log_increment}, {"saved_cost_log_drag", row.saved_cost_log_drag}, {"net_log_increment", row.net_log_increment}});
    }
    for (std::size_t clock = 0; clock < d.covariates.size(); ++clock) {
        for (std::size_t volume = 0; volume < 2; ++volume) {
            const auto& g = d.covariates[clock][volume];
            clocks.push_back({{"elapsed_minute", 30 * (clock + 1)}, {"high_volume", volume == 0}, {"count", g.count},
                {"normalized_count", g.normalized_count}, {"move_sum", g.move_sum}, {"normalized_move_sum", g.normalized_move_sum},
                {"realized_volatility_sum", g.realized_volatility_sum}, {"forward_gross_sum", g.forward_gross_sum}});
        }
    }
    Json entries = Json::array();
    for (std::size_t v = 0; v < 2; ++v) {
        const auto& e = d.entry_summaries[v];
        entries.push_back({{"variant", v == 0 ? "price_only" : "volume_conditioned"}, {"count", e.count},
            {"mean_signed_gap", e.count ? Json(e.mean_signed_gap) : Json(nullptr)},
            {"mean_absolute_gap", e.count ? Json(e.mean_absolute_gap) : Json(nullptr)},
            {"mean_entry_minute_range", e.count ? Json(e.mean_entry_minute_range) : Json(nullptr)},
            {"mean_entry_minute_volume", e.count ? Json(e.mean_entry_minute_volume) : Json(nullptr)}});
    }
    return {{"purpose", "post-result descriptive development diagnostics; no revised gates"},
        {"gross_log_increment", d.gross_log_increment}, {"saved_cost_log_drag", d.saved_cost_log_drag},
        {"net_log_increment", d.net_log_increment}, {"mean_daily_saved_cost_drag", d.mean_daily_saved_cost_drag},
        {"exposure_matches", matches}, {"release_groups", diagnostic_periods_json(d.release_groups)},
        {"common_winner_removal", diagnostic_periods_json(d.common_winner_removal)},
        {"leave_one_month_out", diagnostic_periods_json(d.leave_one_month_out)}, {"clock_covariates", clocks}, {"entry_timing", entries},
        {"release_calendar_sha256", continuation_release_calendar_sha256()}, {"release_calendar_records", 64}};
}

std::string diagnostic_period_table(std::span<const PairedDiagnosticPeriod> periods) {
    auto out = stream();
    out << "<div class=\"scroll\"><table><thead><tr><th>Group / stress</th><th>Sessions</th><th>Active price / volume</th>"
        "<th>Gross mean price / volume, bp</th><th>Net mean price / volume, bp</th>"
        "<th>Net compounded price / volume, %</th><th>Paired net, bp/day</th></tr></thead><tbody>";
    for (const auto& p : periods) {
        out << "<tr><td>" << escape(p.label) << "</td><td>" << p.sessions << "</td><td>" << p.active_days[0] << " / " << p.active_days[1]
            << "</td><td>" << number(p.gross[0].mean * 10000) << " / " << number(p.gross[1].mean * 10000)
            << "</td><td>" << number(p.net[0].mean * 10000) << " / " << number(p.net[1].mean * 10000)
            << "</td><td>" << number(p.net[0].compounded * 100) << " / " << number(p.net[1].compounded * 100)
            << "</td><td>" << number(p.paired_net_mean * 10000) << "</td></tr>";
    }
    return out.str() + "</tbody></table></div>";
}

std::string diagnostic_html(const ContinuationStudyResult& study, const ContinuationDiagnosticsResult& d) {
    auto out = stream();
    out << "<section class=\"card\" id=\"diagnostics\"><h2>What does volume remove?</h2>"
        "<p class=\"note\">Post-result development diagnostics. These explain the existing sample; they do not change registered screens or constitute new confirmation.</p>"
        "<p>Arithmetic paired net increment = " << number(study.paired_gross_increment * 10000) << " bp/day gross + "
        << number(d.mean_daily_saved_cost_drag * 10000) << " bp/day lower cost drag = "
        << number(study.paired_net_increment * 10000) << " bp/day net.</p>"
        "<p>Each volume trade shares the exit of one price trade. Delayed entries remove an early segment; skipped trades remove a whole segment. "
        "The table uses additive natural-log contributions, multiplied by 100. These are log percentage points, not simple returns.</p>"
        "<table><thead><tr><th>Price-trade match</th><th>Count</th><th>Removed winners / losers</th>"
        "<th>Gross increment, log pp</th><th>Saved costs, log pp</th><th>Net increment, log pp</th></tr></thead><tbody>";
    for (const auto& row : d.attribution) {
        out << "<tr><td>" << exposure_match_name(row.match) << "</td><td>" << row.price_trades
            << "</td><td>" << row.removed_positive_segments << " / " << row.removed_negative_segments
            << "</td><td>" << number(100 * row.gross_log_increment)
            << "</td><td>" << number(100 * row.saved_cost_log_drag)
            << "</td><td>" << number(100 * row.net_log_increment) << "</td></tr>";
    }
    out << "</tbody></table><p>Total log wealth-ratio increment: " << number(100 * d.net_log_increment)
        << " log pp = " << number(100 * d.gross_log_increment) << " gross + " << number(100 * d.saved_cost_log_drag)
        << " saved cost drag. All contributions reconcile to the original daily returns.</p></section>";
    out << "<section class=\"card\"><h2>Dependence on exceptional dates</h2>"
        "<p>Replace the volume variant's largest winning dates with cash in both variants. Dates and the denominator remain fixed. "
        "This uses future outcomes and is not an implementable filter. The original own-five-winner screen above is unchanged.</p>"
        << diagnostic_period_table(d.common_winner_removal);
    if (!d.common_winner_removal.empty()) {
        const auto& last = d.common_winner_removal.back();
        out << "<p class=\"small\">Largest common-date set: ";
        for (std::size_t i = 0; i < last.cash_dates.size(); ++i) { out << (i ? ", " : "") << last.cash_dates[i]; }
        out << ". Every set is retained in summary.json.</p>";
    }
    if (!d.leave_one_month_out.empty()) {
        std::vector<double> x;
        std::vector<std::pair<double, std::string>> labels;
        std::vector<PlotSeries> curves{{"Price-only", "#2563eb", {}}, {"With volume", "#0f766e", {}}};
        for (std::size_t i = 0; i < d.leave_one_month_out.size(); ++i) {
            x.push_back(static_cast<double>(i));
            for (std::size_t v = 0; v < 2; ++v) { curves[v].values.push_back(d.leave_one_month_out[i].net[v].mean * 10000); }
            if (i % 4 == 0 || i + 1 == d.leave_one_month_out.size()) { labels.emplace_back(x.back(), d.leave_one_month_out[i].label); }
        }
        out << "<h3>Leave one calendar month out</h3><p>The x-axis identifies the omitted month. Means use the remaining dates; "
            "connected points are overlapping samples, not a time series of independent tests.</p>";
        if (x.size() >= 2) { out << plot("month-removal", "Mean returns after omitting each month", "Remaining-sample mean net return (bp/day)", x, labels, curves); }
        out << "<details><summary>Monthly omission table</summary>" << diagnostic_period_table(d.leave_one_month_out) << "</details>";
    }
    out << "</section><section class=\"card\"><h2>Scheduled-release days</h2>"
        "<p>All scored sessions receive a disjoint label from the complete 2022-2023 CPI, Employment Situation and FOMC statement calendar. "
        "CPI and employment releases occur at 08:30 New York; FOMC statements at 14:00. Counts are small. "
        "No selected release does not mean no news. Calendar associations do not identify an announcement's causal effect.</p>"
        << diagnostic_period_table(d.release_groups)
        << "<p class=\"small\">The 64-record calendar includes warm-up and closed-market dates; only scored sessions enter the table. "
        "<a href=\"scheduled_releases.csv\">Calendar with official source links</a>. No release values or surprises are used.</p></section>";
    out << "<section class=\"card\"><h2>Volume, price magnitude and realized risk</h2>"
        "<p>Both groups below require the price condition. Prefix volatility is the square root of summed one-minute squared log changes, "
        "from the current open to the decision. It excludes overnight returns. Covariate differences are descriptive; repeated clocks are dependent.</p>"
        "<div class=\"scroll\"><table><thead><tr><th>Elapsed minute / volume</th><th>Count</th><th>Mean move, bp</th>"
        "<th>Mean move / historical scale</th><th>Mean prefix volatility, bp</th><th>Mean next-window gross, bp</th></tr></thead><tbody>";
    for (std::size_t clock = 0; clock < d.covariates.size(); ++clock) {
        for (std::size_t volume = 0; volume < 2; ++volume) {
            const auto& g = d.covariates[clock][volume];
            const auto mean = [&](double sum, std::size_t count, double scale = 1) {
                return count ? number(sum * scale / static_cast<double>(count)) : "undefined";
            };
            out << "<tr><td>" << 30 * (clock + 1) << " / " << (volume ? "other" : "high") << "</td><td>" << g.count
                << "</td><td>" << mean(g.move_sum, g.count, 10000) << "</td><td>" << mean(g.normalized_move_sum, g.normalized_count)
                << "</td><td>" << mean(g.realized_volatility_sum, g.count, 10000) << "</td><td>"
                << mean(g.forward_gross_sum, g.count, 10000) << "</td></tr>";
        }
    }
    out << "</tbody></table></div><p class=\"small\">A zero historical scale leaves the normalized move undefined and outside its mean denominator. "
        "Unsigned share volume cannot measure signed order flow or dealer gamma.</p></section>";
    out << "<section class=\"card\"><h2>Entry timing and execution evidence</h2>"
        "<p>The entry gap is the delayed reference open relative to the last completed decision close. It measures intervening price movement, "
        "not broker slippage. Entry-minute range and shares are ex-post context, unavailable at entry.</p>"
        "<table><thead><tr><th>Variant</th><th>Entries</th><th>Mean signed gap, bp</th><th>Mean absolute gap, bp</th>"
        "<th>Mean entry-minute range, bp</th><th>Mean entry-minute shares</th></tr></thead><tbody>";
    for (std::size_t variant = 0; variant < 2; ++variant) {
        const auto& e = d.entry_summaries[variant];
        out << "<tr><td>" << names[variant] << "</td><td>" << e.count;
        for (const double value : {e.mean_signed_gap * 10000, e.mean_absolute_gap * 10000, e.mean_entry_minute_range * 10000, e.mean_entry_minute_volume}) {
            out << "</td><td>" << (e.count ? number(value) : "undefined");
        }
        out << "</td></tr>";
    }
    out << "</tbody></table><p>Quotes, order size, queue position, impact and actual fills remain unmeasured. "
        "Alpaca can revise completed bars after late trades, including close and volume. Historical final bars therefore do not prove "
        "point-in-time signal availability. Historical SIP access also does not establish live SIP subscription entitlement.</p>"
        "<p><a href=\"removed_exposure.csv\">Matched exposure</a> | <a href=\"opportunity_diagnostics.csv\">Clock covariates and calendar labels</a> | "
        "<a href=\"entry_timing.csv\">Entry timing</a></p></section>";
    return out.str();
}

} // namespace

std::string format_continuation_daily_csv(const ContinuationStudyResult& study) {
    auto out = stream();
    out << "date,variant,delay_minutes,cost_bps_per_side,gross_return,net_return,control_gross_return,control_net_return,round_trips,minutes_in_market,turnover\n";
    for (const auto& day : study.days) {
        for (std::size_t i = 0; i < continuation_cases().size(); ++i) {
            const auto& spec = continuation_cases()[i];
            const auto& value = day.cases[i];
            out << day.date << ',' << continuation_variant_name(spec.variant) << ',' << spec.delay.count() << ','
                << spec.cost_bps_per_side << ',' << value.gross_return << ',' << value.net_return << ','
                << value.control_gross_return << ',' << value.control_net_return << ',' << value.trades.size() << ','
                << value.minutes_in_market.count() << ',' << value.turnover << '\n';
        }
    }
    return out.str();
}

std::string format_continuation_features_csv(const ContinuationStudyResult& study) {
    auto out = stream();
    out << "date,decision_utc,oldest_history_start,latest_history_start,move,typical_absolute_move,cumulative_volume_shares,historical_median_volume_shares,relative_volume,price_condition,volume_condition,price_action,volume_action,delay_minutes,forward_entry_utc,forward_exit_utc,forward_entry_price,forward_exit_price,forward_gross_return\n";
    for (const auto& day : study.days) {
        for (std::size_t clock = 0; clock < day.opportunities.size(); ++clock) {
            const auto& opportunity = day.opportunities[clock];
            const auto& f = opportunity.features;
            for (std::size_t delay = 0; delay < 2; ++delay) {
                const auto& forward = opportunity.forward[delay];
                out << day.date << ',' << format_utc_seconds(f.at) << ',' << format_utc_seconds(f.oldest_history_start) << ','
                    << format_utc_seconds(f.latest_history_start) << ',' << f.move << ',' << f.typical_absolute_move << ','
                    << f.cumulative_volume_shares << ',' << f.historical_median_volume_shares << ',' << f.relative_volume << ','
                    << (f.move > f.typical_absolute_move) << ',' << (f.relative_volume > 1) << ','
                    << continuation_action_name(day.actions[0][clock]) << ',' << continuation_action_name(day.actions[1][clock]) << ','
                    << delay + 1 << ',' << format_utc_seconds(forward.entry_reference_time) << ','
                    << format_utc_seconds(forward.exit_reference_time) << ',' << forward.entry_reference_price << ','
                    << forward.exit_reference_price << ',' << forward.returns.gross_return << '\n';
            }
        }
    }
    return out.str();
}

std::string format_continuation_trades_csv(const ContinuationStudyResult& study) {
    auto out = stream();
    out << "date,variant,delay_minutes,cost_bps_per_side,entry_decision_utc,entry_reference_utc,exit_decision_utc,exit_reference_utc,entry_reference_price,exit_reference_price,gross_return,net_return\n";
    for (const auto& day : study.days) {
        for (std::size_t i = 0; i < continuation_cases().size(); ++i) {
            const auto& spec = continuation_cases()[i];
            for (const auto& trade : day.cases[i].trades) {
                out << day.date << ',' << continuation_variant_name(spec.variant) << ',' << spec.delay.count() << ','
                    << spec.cost_bps_per_side << ',' << format_utc_seconds(trade.entry_decision) << ','
                    << format_utc_seconds(trade.entry_reference_time) << ',' << format_utc_seconds(trade.exit_decision) << ','
                    << format_utc_seconds(trade.exit_reference_time) << ',' << trade.entry_reference_price << ','
                    << trade.exit_reference_price << ',' << trade.returns.gross_return << ',' << trade.returns.net_return << '\n';
            }
        }
    }
    return out.str();
}

std::string_view continuation_release_calendar_sha256() noexcept { return TRADING_RELEASE_CALENDAR_SHA256; }

std::string format_removed_exposure_csv(const ContinuationDiagnosticsResult& d) {
    auto out = stream();
    out << "date,match,price_entry_utc,exit_utc,price_entry_price,exit_price,volume_entry_utc,volume_entry_price,removed_gross_log_return,saved_cost_log_drag,net_log_increment\n";
    for (const auto& row : d.exposure) {
        out << row.date << ',' << exposure_match_name(row.match) << ',' << format_utc_seconds(row.price_trade.entry_reference_time)
            << ',' << format_utc_seconds(row.price_trade.exit_reference_time) << ',' << row.price_trade.entry_reference_price
            << ',' << row.price_trade.exit_reference_price << ',';
        if (row.volume_trade) { out << format_utc_seconds(row.volume_trade->entry_reference_time); }
        out << ',';
        if (row.volume_trade) { out << row.volume_trade->entry_reference_price; }
        out << ',' << row.removed_gross_log_return << ',' << row.saved_cost_log_drag << ',' << row.net_log_increment << '\n';
    }
    return out.str();
}

std::string format_opportunity_diagnostics_csv(const ContinuationDiagnosticsResult& d) {
    auto out = stream();
    out << "date,elapsed_minute,release_flags,release_group,price_condition,high_volume,move,normalized_move,prefix_realized_volatility,forward_gross_return\n";
    for (const auto& row : d.opportunities) {
        out << row.date << ',' << row.elapsed_minute << ',' << row.release_flags << ',' << scheduled_release_label(row.release_flags)
            << ',' << row.price_condition << ',' << row.high_volume << ',' << row.move << ',';
        if (row.normalized_move) { out << *row.normalized_move; }
        out << ',' << row.prefix_realized_volatility << ',' << row.forward_gross_return << '\n';
    }
    return out.str();
}

std::string format_entry_timing_csv(const ContinuationDiagnosticsResult& d) {
    auto out = stream();
    out << "date,variant,entry_decision_utc,entry_reference_utc,entry_gap,ex_post_entry_minute_range,ex_post_entry_minute_volume\n";
    for (const auto& row : d.entries) {
        out << row.date << ',' << continuation_variant_name(row.variant) << ',' << format_utc_seconds(row.decision) << ','
            << format_utc_seconds(row.reference) << ',' << row.entry_gap << ',' << row.entry_minute_range << ',' << row.entry_minute_volume << '\n';
    }
    return out.str();
}

std::string format_continuation_study_html(const ContinuationStudyResult& study, const ContinuationReportContext& context,
                                          const ContinuationDiagnosticsResult* diagnostics) {
    validate_report(study);
    if (context.data_kind != "development" && context.data_kind != "synthetic_contract") {
        throw std::invalid_argument("report requires an explicit data kind");
    }
    auto out = stream();
    out << R"(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>SPY-TOD-01 development comparison</title><style>
body{margin:0;background:#eef2f7;color:#16273e;font:15px/1.55 system-ui,Segoe UI,sans-serif}main{max-width:1180px;margin:auto;padding:30px 24px}h1{font-size:34px;line-height:1.15}h2{font-size:23px}h3{font-size:18px}.eyebrow{text-transform:uppercase;letter-spacing:.1em;color:#526783;font-size:12px}.card{background:white;border:1px solid #d5dfea;border-radius:10px;padding:24px;margin:22px 0}.note{background:#fff5dc;border-left:4px solid #b8860b;padding:16px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:18px}.scroll{overflow:auto}table{border-collapse:collapse;width:100%;font-size:13px}th,td{padding:10px 9px;border-bottom:1px solid #e1e7ef;text-align:right;white-space:nowrap}th{background:#f5f7fb}th:first-child,td:first-child{text-align:left}svg{display:block;width:100%;height:auto}svg text{font:12px system-ui,sans-serif;fill:#526783}.legend{display:flex;flex-wrap:wrap;gap:20px;font-size:13px;font-weight:650}code{overflow-wrap:anywhere;font-size:12px}.small{font-size:13px;color:#526783}a{color:#1d4ed8}@media(max-width:760px){main{padding:16px 10px}.card{padding:15px}.grid{grid-template-columns:1fr}h1{font-size:28px}}@media print{body{background:white}.card{break-inside:avoid}main{padding:0}}
</style></head><body><main><div class="eyebrow">SPY-TOD-01 / protocol revision 1</div><h1>Does volume improve intraday continuation?</h1>)";
    if (context.data_kind == "synthetic_contract") {
        out << "<p class=\"note\"><strong>Synthetic contract fixture. These are constructed observations, not market results.</strong></p>";
    } else {
        out << "<p class=\"note\">2022-2023 development evidence. Selection follows prior research and an inspected baseline. "
            "This sample is not an independent final test. Bar opens are delayed trade-price proxies; costs are hypothetical.</p>";
    }
    if (diagnostics) { out << "<p><a href=\"#diagnostics\">Exposure attribution and development diagnostics</a> | <a href=\"#gates\">Registered gates</a></p>"; }
    out << "<p>" << study.days.front().date << " to " << study.days.back().date << " | " << study.days.size()
        << " scored sessions | 20 earlier eligible sessions warm up the features. Primary: one-minute delay, 1 bp per side.</p>"
        << "<div class=\"grid\">";
    for (std::size_t variant = 0; variant < 2; ++variant) {
        const auto& stats = study.statistics[variant * 10 + 2].overall;
        out << "<section class=\"card\"><h2>" << names[variant] << "</h2><p><strong>"
            << number(stats.net.compounded * 100) << "%</strong> compounded | " << number(stats.net.mean * 10000)
            << " bp mean daily net</p><p>" << stats.active_days << " active days | " << stats.round_trips
            << " round trips | " << number(100 * stats.fraction_of_session_in_market) << "% of full-session minutes</p><p>"
            << escape(study.variants[variant].status) << "</p></section>";
    }
    out << "</div><section class=\"card\"><h2>Primary equity and daily-close drawdown</h2>"
        "<p>All scored days, including cash days. Initial equity is 100. The always-long control uses the same "
        "10:01-15:56 New York reference window. Different time in market makes these comparisons exposure-sensitive.</p>";
    std::vector<double> x(study.days.size() + 1);
    std::iota(x.begin(), x.end(), 0.0);
    const std::vector<std::pair<double, std::string>> dates{{0, "Initial"},
        {static_cast<double>(study.days.size() / 2), study.days[study.days.size() / 2 - 1].date},
        {static_cast<double>(study.days.size()), study.days.back().date}};
    std::vector<PlotSeries> equity, drawdown;
    for (std::size_t variant = 0; variant < 3; ++variant) {
        std::vector<double> values;
        for (const auto& day : study.days) {
            values.push_back(variant == 2 ? day.cases[2].control_net_return : day.cases[variant * 10 + 2].net_return);
        }
        PlotSeries wealth{std::string{names[variant]}, std::string{colors[variant]}, {100}};
        PlotSeries loss{wealth.name, wealth.color, {0}};
        for (const auto& point : compound_return_path(values)) {
            wealth.values.push_back(100 * point.wealth);
            loss.values.push_back(-100 * point.drawdown);
        }
        equity.push_back(std::move(wealth));
        drawdown.push_back(std::move(loss));
    }
    out << plot("equity", "Primary daily-close equity", "Equity index, initial = 100", x, dates, equity)
        << plot("drawdown", "Primary daily-close drawdown", "Drawdown from prior peak (%)", x, dates, drawdown)
        << "<p class=\"small\">Cash stays at 100 with zero return. Horizontal positions follow ordered scored sessions. Drawdown includes initial equity and measures daily closes, not intraday worst-case loss.</p></section>";
    out << "<section class=\"card\"><h2>Costs and delay sensitivity</h2><p>All twenty declared cases are retained. "
        "The matrix is a stress test, not a parameter search.</p>";
    const std::vector<double> costs{0, 0.5, 1, 2, 5};
    std::vector<PlotSeries> sensitivity;
    for (std::size_t variant = 0; variant < 2; ++variant) {
        for (std::size_t delay = 0; delay < 2; ++delay) {
            PlotSeries curve{std::string{names[variant]} + ", " + std::to_string(delay + 1) + " min",
                variant == 0 ? (delay == 0 ? "#2563eb" : "#7c3aed") : (delay == 0 ? "#0f766e" : "#b45309"), {}};
            for (std::size_t cost = 0; cost < costs.size(); ++cost) {
                curve.values.push_back(10000 * study.statistics[variant * 10 + delay * 5 + cost].overall.net.mean);
            }
            sensitivity.push_back(std::move(curve));
        }
    }
    out << plot("costs", "Mean daily net return by cost and delay", "Mean daily net return (bp)", costs,
        {{0, "0 bp"}, {0.5, "0.5"}, {1, "1"}, {2, "2"}, {5, "5 bp per side"}}, sensitivity)
        << "<div class=\"scroll\"><table><thead><tr><th>Variant</th><th>Delay, min</th><th>Cost/side, bp</th>"
        "<th>Mean net, bp/day</th><th>Compounded, %</th><th>Max drawdown, %</th><th>Sharpe</th><th>Control net, bp/day</th>"
        "<th>Mean turnover</th></tr></thead><tbody>";
    for (std::size_t i = 0; i < continuation_cases().size(); ++i) {
        const auto& spec = continuation_cases()[i];
        const auto& stats = study.statistics[i].overall;
        out << "<tr><td>" << continuation_variant_name(spec.variant) << "</td><td>" << spec.delay.count()
            << "</td><td>" << number(spec.cost_bps_per_side, 1) << "</td><td>" << number(stats.net.mean * 10000)
            << "</td><td>" << number(stats.net.compounded * 100) << "</td><td>" << number(stats.net.maximum_drawdown * 100)
            << "</td><td>" << optional(stats.net.annualized_daily_sharpe) << "</td><td>" << number(stats.control_net.mean * 10000)
            << "</td><td>" << number(stats.mean_daily_turnover) << "</td></tr>";
    }
    out << "</tbody></table></div><p class=\"small\">Sharpe uses sqrt(252) times the daily mean divided by sample standard deviation, "
        "with zero cash rate. Zero deviation is undefined. Turnover is buy plus sell reference notional divided by start-of-day equity.</p></section>";
    out << "<section class=\"card\"><h2>Paired evidence and uncertainty</h2><p>Volume minus price-only: <strong>"
        << number(study.paired_net_increment * 10000) << " bp/day net</strong>, " << number(study.paired_gross_increment * 10000)
        << " bp/day gross. Price-information contrast: " << optional(study.contrasts.price, 10000)
        << " bp. Volume-information contrast among positive price conditions: " << optional(study.contrasts.volume, 10000)
        << " bp.</p><p>10,000 non-circular moving-block resamples, seed 20261021. Entire paired session records share sampled indices. "
        "95% percentile intervals are in basis points; any undefined contrast replicate makes its interval undefined.</p>"
        "<div class=\"scroll\"><table><thead><tr><th>Statistic</th><th>5-session blocks</th><th>10-session blocks</th></tr></thead><tbody>";
    const std::array<std::string_view, 7> interval_names{"Price-only net daily mean", "Volume net daily mean", "Always-long net daily mean",
        "Paired net increment", "Paired gross increment", "Price-information contrast", "Volume-information contrast"};
    for (std::size_t metric = 0; metric < interval_names.size(); ++metric) {
        out << "<tr><td>" << interval_names[metric] << "</td>";
        for (const auto& b : study.bootstrap) {
            const std::array<std::optional<PercentileInterval>, 7> intervals{b.price_net_mean_95, b.volume_net_mean_95,
                b.control_net_mean_95, b.paired_net_increment_95, b.paired_gross_increment_95, b.price_contrast_95, b.volume_contrast_95};
            out << "<td>" << interval_text(intervals[metric]) << "</td>";
        }
        out << "</tr>";
    }
    for (const bool volume : {false, true}) {
        out << "<tr><td>Undefined " << (volume ? "volume" : "price") << " contrast resamples</td>";
        for (const auto& b : study.bootstrap) { out << "<td>" << (volume ? b.undefined_volume_replicates : b.undefined_price_replicates) << "</td>"; }
        out << "</tr>";
    }
    out << "</tbody></table></div><p class=\"small\">Intervals do not correct for the full strategy-selection history. "
        "Fewer trades and lower costs alone do not establish incremental volume information.</p></section>";
    out << "<section class=\"card\"><h2>Decision-clock group coverage</h2><p>Contrasts compare gross delayed forward returns "
        "within each clock, then average the twelve differences equally. A missing group at any clock makes the aggregate undefined.</p>"
        "<div class=\"scroll\"><table><thead><tr><th>New York clock</th><th>Price true</th><th>Price false</th>"
        "<th>High volume / price true</th><th>Other volume / price true</th><th>Price contrast, bp</th><th>Volume contrast, bp</th></tr></thead><tbody>";
    for (std::size_t i = 0; i < study.contrasts.clocks.size(); ++i) {
        const auto& group = study.contrasts.clocks[i];
        const auto minute = 600 + i * 30;
        std::optional<double> price, volume;
        if (group.price_true_count && group.price_false_count) { price = group.price_true_sum / static_cast<double>(group.price_true_count) - group.price_false_sum / static_cast<double>(group.price_false_count); }
        if (group.high_volume_count && group.other_volume_count) { volume = group.high_volume_sum / static_cast<double>(group.high_volume_count) - group.other_volume_sum / static_cast<double>(group.other_volume_count); }
        out << "<tr><td>" << minute / 60 << ':' << (minute % 60 == 0 ? "00" : "30") << "</td><td>" << group.price_true_count
            << "</td><td>" << group.price_false_count << "</td><td>" << group.high_volume_count << "</td><td>"
            << group.other_volume_count << "</td><td>" << optional(price, 10000) << "</td><td>" << optional(volume, 10000) << "</td></tr>";
    }
    out << "</tbody></table></div></section><section class=\"card\"><h2>Annual primary results</h2><table><thead><tr>"
        "<th>Year / variant</th><th>Sessions</th><th>Active</th><th>Gross mean, bp</th><th>Net mean, bp</th><th>Compounded, %</th><th>Max drawdown, %</th></tr></thead><tbody>";
    for (std::size_t variant = 0; variant < 2; ++variant) {
        for (const auto& [year, stats] : study.statistics[variant * 10 + 2].years) {
            out << "<tr><td>" << year << " / " << names[variant] << "</td><td>" << stats.sessions << "</td><td>" << stats.active_days
                << "</td><td>" << number(stats.gross.mean * 10000) << "</td><td>" << number(stats.net.mean * 10000)
                << "</td><td>" << number(stats.net.compounded * 100) << "</td><td>" << number(stats.net.maximum_drawdown * 100) << "</td></tr>";
        }
    }
    out << "</tbody></table></section><section class=\"card\"><h2>Winner concentration</h2><div class=\"grid\">";
    for (std::size_t variant = 0; variant < 2; ++variant) {
        const auto& c = study.statistics[variant * 10 + 2].overall.concentration;
        out << "<div><h3>" << names[variant] << "</h3><p>Five-largest-winner share of positive returns: "
            << optional(c.largest_five_share_of_positive_returns, 100) << "%. Net arithmetic sum without those winners: "
            << number((c.arithmetic_net_sum - c.largest_five_net_sum) * 10000) << " bp.</p><table><thead><tr><th>Date</th><th>Daily net, bp</th></tr></thead><tbody>";
        for (const auto& winner : c.largest_five_winners) { out << "<tr><td>" << winner.date << "</td><td>" << number(winner.net_return * 10000) << "</td></tr>"; }
        out << "</tbody></table></div>";
    }
    out << "</div><p class=\"small\">These are arithmetic daily-return contributions, not compounded attribution. "
        "The registered screen requires a share at most 50% and a positive sum after removing the five winners.</p></section>";
    if (diagnostics) { out << diagnostic_html(study, *diagnostics); }
    out << "<section class=\"card\" id=\"gates\"><h2>Registered continuation gates</h2><div class=\"grid\">";
    for (std::size_t variant = 0; variant < 2; ++variant) {
        out << "<div><h3>" << names[variant] << ": " << escape(study.variants[variant].status) << "</h3>" << gate_table(study.variants[variant]) << "</div>";
    }
    out << "</div><h3>Incremental volume: " << escape(study.volume_increment.status) << "</h3>" << gate_table(study.volume_increment)
        << "<p>Passing a screen permits a separately declared later test; it does not establish tradability. No later-period evaluation or broker orders occur in this run.</p></section>";
    out << "<section class=\"card\"><h2>Inputs and reproducibility</h2><p>Complete SPY SIP minute sessions, raw prices and share volume. "
        "Exclusions: 2022-11-25, 2023-01-03, 2023-06-05, 2023-07-03 and 2023-11-24. The first 20 eligible sessions are warm-up, not cash observations. "
        "The literature-informed design has external 2024-2026 exposure; local later-period candidate results remain unopened.</p><p>Input manifest SHA-256: <code>"
        << escape(context.input_manifest_sha256) << "</code></p><p>Protocol SHA-256 at build: <code>" << continuation_protocol_sha256()
        << "</code></p><p>Derived feature CSV SHA-256: <code>" << escape(context.features_sha256)
        << "</code></p><p>Source-file digest at build: <code>" << TRADING_SOURCE_SHA256 << "</code></p><p>"
        << escape(TRADING_BUILD_COMPILER) << " | " << escape(TRADING_BUILD_CONFIGURATION)
        << "</p><p><a href=\"daily_returns.csv\">Daily case table</a> | <a href=\"features.csv\">Features and forward windows</a> | "
        "<a href=\"trades.csv\">Completed trades</a> | <a href=\"summary.json\">Statistics and provenance</a></p></section></main></body></html>";
    return out.str();
}

std::filesystem::path save_continuation_report(const FrozenDevelopmentDataset& dataset, const ContinuationStudyResult& study,
                                              const ContinuationDiagnosticsResult& diagnostics, std::string_view release_calendar_csv,
                                              const std::filesystem::path& output_root) {
    validate_report(study);
    if (dataset.manifest_sha256 != continuation_development_manifest_sha256 || dataset.sessions.size() != 496 ||
        study.days.size() != 476 || sha256_file(contained_path(dataset.directory, dataset.manifest_file)) != dataset.manifest_sha256 ||
        sha256(release_calendar_csv) != continuation_release_calendar_sha256()) {
        throw std::invalid_argument("development report requires the unchanged frozen snapshot and 476 scored days");
    }
    const auto daily = format_continuation_daily_csv(study);
    const auto features = format_continuation_features_csv(study);
    const auto trades = format_continuation_trades_csv(study);
    const auto removed = format_removed_exposure_csv(diagnostics);
    const auto covariates = format_opportunity_diagnostics_csv(diagnostics);
    const auto entries = format_entry_timing_csv(diagnostics);
    const auto html = format_continuation_study_html(study, {"development", dataset.manifest_sha256, sha256(features)}, &diagnostics);
    Json cases = Json::array(), boots = Json::array(), excluded = Json::array();
    for (std::size_t i = 0; i < continuation_cases().size(); ++i) {
        const auto& spec = continuation_cases()[i];
        Json years = Json::object();
        for (const auto& [year, value] : study.statistics[i].years) { years[year] = period_json(value); }
        cases.push_back({{"variant", continuation_variant_name(spec.variant)}, {"delay_minutes", spec.delay.count()},
            {"cost_bps_per_side", spec.cost_bps_per_side}, {"overall", period_json(study.statistics[i].overall)}, {"calendar_years", years}});
    }
    for (const auto& b : study.bootstrap) {
        boots.push_back({{"block_length_sessions", b.block_length}, {"replicates", b.replicates}, {"seed", b.seed},
            {"price_net_mean_95", interval_json(b.price_net_mean_95)}, {"volume_net_mean_95", interval_json(b.volume_net_mean_95)},
            {"always_long_net_mean_95", interval_json(b.control_net_mean_95)}, {"paired_net_increment_95", interval_json(b.paired_net_increment_95)},
            {"paired_gross_increment_95", interval_json(b.paired_gross_increment_95)}, {"price_contrast_95", interval_json(b.price_contrast_95)},
            {"volume_contrast_95", interval_json(b.volume_contrast_95)}, {"undefined_price_replicates", b.undefined_price_replicates},
            {"undefined_volume_replicates", b.undefined_volume_replicates}});
    }
    for (const auto& exclusion : dataset.exclusions) { excluded.push_back({{"date", exclusion.date}, {"reason", exclusion.reason}}); }
    const Json summary{{"schema_version", 2}, {"protocol", "SPY-TOD-01"}, {"protocol_revision", 1}, {"purpose", "development"},
        {"primary_variant", "volume_conditioned"}, {"price_only_role", "declared reference; assessed separately"},
        {"eligible_sessions", 496}, {"warmup_sessions", 20}, {"scored_sessions", study.days.size()},
        {"first_scored_date", study.days.front().date}, {"last_scored_date", study.days.back().date}, {"exclusions", excluded},
        {"input_manifest_sha256", dataset.manifest_sha256}, {"input_manifest_file", dataset.manifest_file},
        {"frozen_march_manifest_sha256", dataset.march_manifest_sha256}, {"data_policy_sha256", dataset.data_policy_sha256},
        {"input_dataset_protocol_sha256", dataset.protocol_sha256}, {"continuation_protocol_sha256_at_build", continuation_protocol_sha256()},
        {"source_files_sha256_at_build", TRADING_SOURCE_SHA256}, {"compiler", TRADING_BUILD_COMPILER}, {"build_configuration", TRADING_BUILD_CONFIGURATION},
        {"source_digest_method", "SHA-256 of sorted relative-path:SHA-256(file-bytes) LF records; src/apps C++ files, research files and root CMake configuration"},
        {"return_unit", "decimal fraction; one basis point = 0.0001"}, {"cash_return", 0},
        {"exposure_unit", "minutes held / (390 * scored sessions); control holds 355 minutes per session"},
        {"turnover_unit", "buy plus sell reference notional / start-of-day equity"},
        {"maximum_drawdown_unit", "nonnegative daily-close loss fraction; includes initial wealth"},
        {"cases", cases}, {"contrasts", groups_json(study.contrasts)}, {"bootstrap", boots},
        {"bootstrap_method", {{"sampling", "non-circular whole-session moving blocks; truncate to N; shared paired indices"},
            {"random_engine", "mt19937_64"}, {"integer_mapping", "unsigned rejection/modulo"}, {"percentiles", "linear interpolation at (M-1)*p"},
            {"undefined_groups", "any undefined replicate makes the corresponding interval null"}}},
        {"paired_net_increment", study.paired_net_increment}, {"paired_gross_increment", study.paired_gross_increment},
        {"price_assessment", assessment_json(study.variants[0])}, {"volume_assessment", assessment_json(study.variants[1])},
        {"incremental_volume_assessment", assessment_json(study.volume_increment)},
        {"diagnostics", diagnostics_json(diagnostics)},
        {"artifacts", {{"daily_returns.csv", sha256(daily)}, {"features.csv", sha256(features)}, {"trades.csv", sha256(trades)}, {"report.html", sha256(html)},
            {"removed_exposure.csv", sha256(removed)}, {"opportunity_diagnostics.csv", sha256(covariates)},
            {"entry_timing.csv", sha256(entries)}, {"scheduled_releases.csv", sha256(release_calendar_csv)}}},
        {"limitations", {"development sample; literature includes 2024-2026", "delayed bar-open price proxies; hypothetical costs",
            "no independent later evaluation, broker fills or account reconciliation", "daily-close drawdown is not intraday worst-case loss"}}};
    const auto directory = create_unique_directory(output_root, "SPY_TOD_01_development_2022-2023");
    write_new_text_file(directory / "daily_returns.csv", daily);
    write_new_text_file(directory / "features.csv", features);
    write_new_text_file(directory / "trades.csv", trades);
    write_new_text_file(directory / "removed_exposure.csv", removed);
    write_new_text_file(directory / "opportunity_diagnostics.csv", covariates);
    write_new_text_file(directory / "entry_timing.csv", entries);
    write_new_text_file(directory / "scheduled_releases.csv", release_calendar_csv);
    write_new_text_file(directory / "report.html", html);
    // Summary is the publication record and is written only after its artifacts.
    write_new_text_file(directory / "summary.json", summary.dump(2) + "\n");
    return directory;
}

} // namespace trading
