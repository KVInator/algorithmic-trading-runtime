#include "report_text.hpp"
#include "daily_risk_report.hpp"
#include "daily_risk_rules.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace trading {
namespace {
using Json = nlohmann::json;
std::ostringstream stream() { std::ostringstream s; s.imbue(std::locale::classic()); s << std::setprecision(17); return s; }
std::string number(double value, int precision = 4) { auto s = stream(); s << std::fixed << std::setprecision(precision) << value; return s.str(); }
std::string percent(double value) { return number(value * 100, 2) + "%"; }
template<class T> Json nullable(const std::optional<T>& value) { return value ? Json(*value) : Json(nullptr); }
std::string optional_cell(const std::optional<double>& value) { return value ? number(*value, 8) : "Unavailable"; }
Json interval(const PercentileInterval& value) { return {{"lower", value.lower}, {"upper", value.upper}}; }
Json contrast_json(const RiskContrast& c) {
    return {{"cer_over_daily", c.cer_over_daily}, {"cer_over_static", c.cer_over_static},
        {"qlike_increment", nullable(c.qlike_increment)}, {"forecast_records", c.forecast_records}};
}
void write_optional(std::ostream& out, const std::optional<double>& value) { if (value) { out << *value; } }
std::string history_dates(const std::vector<std::string>& dates) {
    std::string text;
    for (const auto& date : dates) { if (!text.empty()) { text += ';'; } text += date; }
    return text;
}
std::string chart(const DailyRiskResult& result, int metric, const std::string& title) {
    const std::array<std::string_view, 4> colors{"#166e81", "#bb5b36", "#785ab0", "#677381"};
    std::vector<std::vector<double>> lines;
    double low = metric == 0 ? 1 : 0, high = low;
    for (const auto name : risk_rules::accounts) {
        const auto& account = risk_account_case(result, std::string{name}, 1, 1);
        std::vector<double> values, returns;
        for (const auto& d : account.days) { returns.push_back(d.net_return); }
        const auto path = compound_return_path(returns);
        values.push_back(metric == 2 ? 0.0 : metric == 1 ? 0.0 : 1.0);
        for (std::size_t i = 0; i < account.days.size(); ++i) {
            values.push_back(metric == 0 ? path[i].wealth : metric == 1 ? -path[i].drawdown : account.days[i].closing_stock_weight);
        }
        for (const auto v : values) { low = std::min(low, v); high = std::max(high, v); }
        lines.push_back(std::move(values));
    }
    if (metric == 0) { low = std::min(low, .95); }
    if (metric == 2) { high = 1; }
    if (high == low) { high += .01; }
    const double margin = .06 * (high - low);
    if (metric == 0) { low -= margin; high += margin; }
    else if (metric == 1) { low -= margin; high = 0; }
    else { low = 0; high = 1; }
    auto out = stream();
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1080 330\" role=\"img\"><title>" << ReportText{title}.html()
        << "</title><rect width=\"1080\" height=\"330\" fill=\"white\"/>";
    for (int tick = 0; tick <= 4; ++tick) {
        const auto y = 25 + 250.0 * tick / 4;
        const auto value = high - (high - low) * tick / 4;
        out << "<path d=\"M80," << y << "H1050\" stroke=\"#e1e7ed\"/><text x=\"68\" y=\"" << y + 5
            << "\" text-anchor=\"end\" font-size=\"13\" fill=\"#526477\">" << (metric == 0 ? number(value, 2) : percent(value)) << "</text>";
    }
    for (std::size_t j = 0; j < lines.size(); ++j) {
        out << "<polyline fill=\"none\" stroke=\"" << colors[j] << "\" stroke-width=\"1.8\" points=\"";
        for (std::size_t i = 0; i < lines[j].size(); ++i) {
            const auto x = 80 + 970.0 * static_cast<double>(i) / static_cast<double>(lines[j].size() - 1);
            const auto y = 25 + 250 * (high - lines[j][i]) / (high - low);
            out << x << ',' << y << ' ';
        }
        out << "\"/>";
    }
    out << "<text x=\"80\" y=\"304\" font-size=\"13\">Initial cash / Jan 2022</text><text x=\"1050\" y=\"304\" text-anchor=\"end\" font-size=\"13\">Dec 2023</text></svg>";
    return out.str();
}
} // namespace

std::filesystem::path DailyRiskReport::write(const std::filesystem::path& root) const {
    const auto& result = result_;
    if (result.accounts.size() != 40 || result.forecasts.size() != 1002 || result.gates.empty() || result.uncertainty.size() != 2) {
        throw std::invalid_argument("daily-risk report requires the complete experiment");
    }
    const auto directory = create_unique_directory(root, result.synthetic ? "SPY_RISK_01_synthetic" : "SPY_RISK_01_development_2022-2023");
    const bool passes = std::all_of(result.gates.begin(), result.gates.end(), [](const auto& g) { return g.passed; });
    const std::string status = result.synthetic ? "synthetic_contract_fixture" : passes ? "registered_screens_passed" : "not_advanced_under_registered_design";
    Json summary{{"schema_version", 1}, {"study", "SPY-RISK-01"}, {"status", status}, {"synthetic", result.synthetic},
        {"historical_strategy_scored", !result.synthetic}, {"registration_commit", risk_rules::registration_commit},
        {"protocol_md_sha256_colon_json_sha256", result.protocol_sha256}, {"development_manifest_sha256", result.development_manifest_sha256},
        {"acquisition_manifest_sha256", result.acquisition_manifest_sha256}, {"issuer_reference_sha256", result.issuer_reference_sha256},
        {"source_sha256", TRADING_SOURCE_SHA256}, {"compiler", TRADING_COMPILER_ID}, {"build_configuration", TRADING_BUILD_CONFIGURATION},
        {"created_utc", format_utc_seconds(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()))},
        {"account_cases", result.accounts.size()}, {"development_sessions", 501}, {"calibration_sessions", 252},
        {"initial_cash_usd", risk_rules::initial_cash}, {"cash_reference_daily_return", 0}, {"cash_reference_cer", 0},
        {"static_weight", result.static_weight}, {"calibration_mean_daily2", result.calibration_daily2}, {"primary", contrast_json(result.primary)}};
    summary["input_evidence"] = Json::array();
    for (const auto& [file, digest] : result.input_evidence) { summary["input_evidence"].push_back({{"identity", file}, {"sha256", digest}}); }
    summary["gates"] = Json::array(); summary["bootstrap"] = Json::array(); summary["statistics"] = Json::array();
    summary["omissions"] = Json::array(); summary["forecast_statistics"] = Json::array(); summary["fallbacks"] = Json::array();
    for (const auto& g : result.gates) { summary["gates"].push_back({{"name", g.name}, {"passed", g.passed}}); }
    for (const auto& b : result.uncertainty) {
        summary["bootstrap"].push_back({{"block_length", b.block_length}, {"replicates", b.replicates}, {"seed", b.seed},
            {"cer_over_daily_95", interval(b.cer_over_daily)}, {"cer_over_static_95", interval(b.cer_over_static)},
            {"qlike_increment_95", b.qlike_increment ? interval(*b.qlike_increment) : Json(nullptr)},
            {"undefined_loss_replicates", b.undefined_loss_replicates}, {"resampled_date_records", 501}});
    }
    for (const auto& s : result.statistics) {
        summary["statistics"].push_back({{"account", s.account}, {"delay_minutes", s.delay_minutes}, {"cost_bps", s.cost_bps},
            {"period", s.period}, {"sessions", s.sessions}, {"mean_daily_return", s.returns.mean}, {"compounded_return", s.returns.compounded},
            {"maximum_daily_close_drawdown", s.returns.maximum_drawdown}, {"sample_daily_standard_deviation", nullable(s.returns.sample_standard_deviation)},
            {"annualized_mean_std_zero_reference", nullable(s.returns.annualized_daily_sharpe)}, {"cer", s.cer},
            {"traded_notional_usd", s.traded_notional_usd}, {"turnover_equity_units", s.turnover_equity_units}, {"cost_usd", s.cost_usd},
            {"mean_closing_liquid_stock_weight", s.mean_closing_liquid_stock_weight}, {"worst_daily_return", s.worst_daily_return},
            {"daily_return_5th_percentile", s.daily_return_5th_percentile}, {"mean_bottom_five_returns", s.mean_bottom_five_returns}});
    }
    for (const auto& d : result.omissions) {
        summary["omissions"].push_back({{"kind", d.kind}, {"label", d.label}, {"removed_dates", d.removed_dates}, {"contrast", contrast_json(d.contrast)}});
    }
    for (const auto& f : result.forecast_statistics) {
        summary["forecast_statistics"].push_back({{"delay_minutes", f.delay_minutes}, {"eligible_records", f.eligible_records},
            {"qlike_increment", nullable(f.qlike_increment)}, {"squared_error_increment", nullable(f.squared_error_increment)},
            {"endpoint_qlike_increment", nullable(f.endpoint_qlike_increment)}});
    }
    auto measurements = stream(); measurements << "date,dividend_adjusted_log_return,daily2,rv1,rv5,rv15\n";
    for (const auto& m : result.measurements) {
        measurements << m.date << ',' << m.log_return << ',' << m.daily2 << ','; write_optional(measurements, m.rv1);
        measurements << ','; write_optional(measurements, m.rv5); measurements << ','; write_optional(measurements, m.rv15); measurements << '\n';
    }
    auto features = stream();
    features << "date,delay_minutes,cutoff_utc,history_first,history_last,h_daily,h_intraday,used_h,intraday_weight,daily_weight,missing_history_dates,future_rv5,future_endpoint2,qlike_increment,squared_error_increment,endpoint_qlike_increment\n";
    for (const auto& f : result.forecasts) {
        features << f.date << ',' << f.delay_minutes << ',' << f.cutoff_utc << ',' << f.history_first << ',' << f.history_last << ',' << f.h_daily << ',';
        write_optional(features, f.h_intraday); features << ',' << f.used_h << ',' << f.intraday_weight << ',' << f.daily_weight << ',' << history_dates(f.missing_history_dates) << ',';
        for (const auto& value : {f.future_rv5, f.future_endpoint2, f.qlike_increment, f.squared_error_increment}) { write_optional(features, value); features << ','; }
        write_optional(features, f.endpoint_qlike_increment); features << '\n';
        if (!f.h_intraday && f.delay_minutes == 1) { summary["fallbacks"].push_back({{"date", f.date}, {"missing_history_dates", f.missing_history_dates}}); }
    }
    auto daily = stream(), ledger = stream();
    daily << "account,delay_minutes,cost_bps,date,previous_equity_usd,opening_equity_usd,cash_usd,shares,receivable_usd,liquid_wealth_usd,equity_usd,net_return,overnight_pnl_usd,intraday_pnl_usd,cost_usd,traded_notional_usd,target_weight,closing_liquid_stock_weight,fallback,terminal_liquidation\n";
    ledger << "account,delay_minutes,cost_bps,timestamp_utc,event,action_id,raw_reference_usd,traded_notional_usd,cost_usd,cash_usd,shares,receivable_usd,liquid_wealth_usd,equity_usd\n";
    for (const auto& a : result.accounts) {
        for (const auto& d : a.days) {
            const auto& v = d.close;
            daily << a.account << ',' << a.delay_minutes << ',' << a.cost_bps << ',' << d.date << ',' << d.previous_equity_usd << ',' << d.opening_equity_usd << ','
                << v.cash_usd << ',' << v.shares << ',' << v.receivable_usd << ',' << v.liquid_wealth_usd << ',' << v.equity_usd << ',' << d.net_return << ','
                << d.overnight_pnl_usd << ',' << d.intraday_pnl_usd << ',' << d.cost_usd << ',' << d.traded_notional_usd << ',' << d.target_weight << ','
                << d.closing_stock_weight << ',' << d.fallback << ',' << d.terminal_liquidation << '\n';
        }
        for (const auto& e : a.ledger) {
            const auto& v = e.value;
            ledger << a.account << ',' << a.delay_minutes << ',' << a.cost_bps << ',' << format_utc_seconds(e.at) << ',' << e.kind << ',' << ReportText{e.action_id}.csv() << ','
                << e.reference_price_usd << ',' << e.traded_notional_usd << ',' << e.cost_usd << ',' << v.cash_usd << ',' << v.shares << ',' << v.receivable_usd << ','
                << v.liquid_wealth_usd << ',' << v.equity_usd << '\n';
        }
    }
    auto html = stream();
    html << R"(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>SPY-RISK-01 | Daily risk experiment</title><style>
*{box-sizing:border-box}body{background:#edf2f5;color:#183044;font:16px/1.55 system-ui,sans-serif;margin:0}main{max-width:1220px;margin:auto;padding:40px 28px}h1{font-size:36px;line-height:1.2}h2{font-size:23px;margin:32px 0 12px}p{max-width:100ch}.eyebrow{font-size:13px;text-transform:uppercase;letter-spacing:.1em;color:#466579}.status{background:#e2eaf0;display:inline-block;padding:8px 12px;border-radius:5px;font-weight:650}.tiles{display:grid;grid-template-columns:repeat(4,1fr);gap:14px}.tile,section{background:white;border:1px solid #d3dee6;border-radius:8px;padding:20px;margin:16px 0}.tile strong{display:block;font-size:25px}.tile span,.note{font-size:14px;color:#52687a}.scroll{overflow:auto}table{width:100%;border-collapse:collapse;font-size:14px;font-variant-numeric:tabular-nums}td,th{padding:9px 12px;border-bottom:1px solid #e2e8ed;text-align:right;white-space:nowrap}th{background:#f0f5f8}td:first-child,th:first-child{text-align:left}svg{width:100%;height:auto;min-width:650px}.mono{font:13px ui-monospace,monospace;overflow-wrap:anywhere}a{color:#166e81}.legend{display:flex;gap:20px;flex-wrap:wrap;font-size:14px}.legend b{display:inline-block;width:22px;height:3px;vertical-align:middle;margin-right:6px}@media(max-width:700px){main{padding:24px 14px}.tiles{grid-template-columns:repeat(2,1fr)}h1{font-size:29px}}</style></head><body><main><div class="eyebrow">SPY-RISK-01 / registered revision 1</div><h1>Daily risk and long/cash allocation</h1>)";
    html << "<p class=\"status\">" << (result.synthetic ? "Synthetic contract fixture. No historical result." : passes ? "Registered development screens passed" : "Registered development screens failed") << "</p>";
    html << "<p>All 501 exchange sessions in 2022–2023. Targets use the preceding twenty sessions; the static target uses 2021 calibration. Primary execution: one-minute delay and 1 bp per side. Cash earns zero. Development evidence conditions on this research design and does not establish real-time availability, broker fills or later-period performance.</p><div class=\"tiles\">";
    for (const auto& [value, label] : std::vector<std::pair<std::string, std::string>>{{percent(result.primary.cer_over_daily), "CER increment over daily-only"},
        {percent(result.primary.cer_over_static), "CER increment over static"}, {optional_cell(result.primary.qlike_increment), "QLIKE increment; positive favors intraday"},
        {std::to_string(result.primary.forecast_records), "Eligible paired forecast dates"}}) {
        html << "<div class=\"tile\"><strong>" << value << "</strong><span>" << label << "</span></div>";
    }
    html << "</div><p>Static calibration weight: " << percent(result.static_weight) << ". Missing native RV5 history invokes daily-only allocation on " << summary["fallbacks"].size()
        << " dates; these dates remain in returns and in the full-calendar bootstrap.</p><div class=\"legend\">";
    const std::array<std::string_view, 4> colors{"#166e81", "#bb5b36", "#785ab0", "#677381"};
    for (std::size_t i = 0; i < colors.size(); ++i) { html << "<span><b style=\"background:" << colors[i] << "\"></b>" << risk_rules::accounts[i] << "</span>"; }
    html << "</div>";
    for (int metric = 0; metric < 3; ++metric) {
        const auto title = metric == 0 ? "Closing equity / initial cash" : metric == 1 ? "Daily-close drawdown" : "Closing stock / liquid wealth";
        html << "<h2>" << title << "</h2><section class=\"scroll\">" << chart(result, metric, title) << "</section>";
    }
    html << "<h2>Primary accounts and annual results</h2><section class=\"scroll\"><table><tr><th>Account / period</th><th>Return</th><th>CER</th><th>Drawdown</th><th>Daily std</th><th>Mean/std × √252</th><th>Fees USD</th><th>Turnover</th><th>Worst day</th></tr>";
    for (const auto& s : result.statistics) {
        if (s.delay_minutes != 1 || s.cost_bps != 1) { continue; }
        html << "<tr><td>" << s.account << " / " << s.period << "</td><td>" << percent(s.returns.compounded) << "</td><td>" << percent(s.cer)
            << "</td><td>" << percent(s.returns.maximum_drawdown) << "</td><td>" << (s.returns.sample_standard_deviation ? percent(*s.returns.sample_standard_deviation) : "Unavailable")
            << "</td><td>" << optional_cell(s.returns.annualized_daily_sharpe) << "</td><td>" << number(s.cost_usd, 2) << "</td><td>" << number(s.turnover_equity_units, 2)
            << "</td><td>" << percent(s.worst_daily_return) << "</td></tr>";
    }
    html << "</table><p class=\"note\">CER = 252 × (mean return − 1.5 × sample variance). Mean/std uses a zero reference, without risk-free adjustment. Turnover sums actual traded notional / preceding closing equity. Cash reference return, CER, drawdown and costs are zero; its mean/std ratio is undefined. Unpaid receivables enter equity and cannot fund purchases.</p></section>";
    html << "<h2>Paired full-calendar uncertainty</h2><section class=\"scroll\"><table><tr><th>Block / resamples</th><th>CER over daily 95%</th><th>CER over static 95%</th><th>QLIKE increment 95%</th><th>Undefined loss samples</th></tr>";
    for (const auto& b : result.uncertainty) {
        html << "<tr><td>" << b.block_length << " / " << b.replicates << "</td><td>" << percent(b.cer_over_daily.lower) << " to " << percent(b.cer_over_daily.upper)
            << "</td><td>" << percent(b.cer_over_static.lower) << " to " << percent(b.cer_over_static.upper) << "</td><td>"
            << (b.qlike_increment ? number(b.qlike_increment->lower, 8) + " to " + number(b.qlike_increment->upper, 8) : "Unavailable")
            << "</td><td>" << b.undefined_loss_replicates << "</td></tr>";
    }
    html << "</table><p class=\"note\">Complete paired date records are resampled before applying forecast eligibility. Missing labels do not compress the date sequence. These intervals do not account for the full strategy-selection process.</p></section><h2>Registered advancement screens</h2><section><table><tr><th>Screen</th><th>Result</th></tr>";
    for (const auto& g : result.gates) { html << "<tr><td>" << g.name << "</td><td>" << (g.passed ? "Pass" : "Fail") << "</td></tr>"; }
    html << "</table></section><h2>Native forecast comparison</h2><section class=\"scroll\"><table><tr><th>Delay</th><th>Eligible dates</th><th>QLIKE increment</th><th>Squared-error increment</th><th>Endpoint QLIKE increment</th></tr>";
    for (const auto& f : result.forecast_statistics) {
        html << "<tr><td>" << f.delay_minutes << " min</td><td>" << f.eligible_records << "</td><td>" << optional_cell(f.qlike_increment)
            << "</td><td>" << optional_cell(f.squared_error_increment) << "</td><td>" << optional_cell(f.endpoint_qlike_increment) << "</td></tr>";
    }
    html << "</table><p class=\"note\">All three losses share the native-positive-estimate and complete-future-RV5 mask. Increments equal daily-only loss less intraday loss. The terminal interval is truncated and excluded from labels.</p></section><h2>Cost and delay sensitivity</h2><section class=\"scroll\"><table><tr><th>Account</th><th>Delay</th><th>Cost bp/side</th><th>Return</th><th>CER</th><th>Drawdown</th><th>Fees USD</th><th>Mean closing weight</th><th>5% daily quantile</th><th>Bottom-five mean</th></tr>";
    for (const auto& s : result.statistics) {
        if (s.period != "all") { continue; }
        html << "<tr><td>" << s.account << "</td><td>" << s.delay_minutes << "</td><td>" << s.cost_bps << "</td><td>" << percent(s.returns.compounded)
            << "</td><td>" << percent(s.cer) << "</td><td>" << percent(s.returns.maximum_drawdown) << "</td><td>" << number(s.cost_usd, 2)
            << "</td><td>" << percent(s.mean_closing_liquid_stock_weight) << "</td><td>" << percent(s.daily_return_5th_percentile) << "</td><td>" << percent(s.mean_bottom_five_returns) << "</td></tr>";
    }
    html << "</table><p class=\"note\">Zero-cost paths are gross accounts. All paths include terminal liquidation fees at their specified rate. Daily-close exposure is a snapshot, rather than a time-weighted intraday average.</p></section><h2>Month omission and largest-winner dependence</h2><section class=\"scroll\"><table><tr><th>Omission</th><th>CER over daily</th><th>CER over static</th><th>QLIKE increment</th></tr>";
    for (const auto& d : result.omissions) {
        html << "<tr><td>" << d.kind << " / " << d.label << "</td><td>" << percent(d.contrast.cer_over_daily) << "</td><td>" << percent(d.contrast.cer_over_static)
            << "</td><td>" << optional_cell(d.contrast.qlike_increment) << "</td></tr>";
    }
    html << "</table><p class=\"note\">Descriptive removal of the same dates from all accounts. Winner dates come from positive daily returns of the primary intraday account. Paths and targets are not re-estimated; these diagnostics do not replace a failed gate.</p></section><h2>Missing-history fallback</h2><section><table><tr><th>Target date</th><th>Incomplete prior session(s)</th></tr>";
    for (const auto& f : result.forecasts) { if (f.delay_minutes == 1 && !f.h_intraday) {
        html << "<tr><td>" << f.date << "</td><td>" << history_dates(f.missing_history_dates) << "</td></tr>";
    }}
    html << "</table></section><h2>Reproduction evidence</h2><section><p>Registration commit: <span class=\"mono\">" << risk_rules::registration_commit
        << "</span></p><p>Source digest: <span class=\"mono\">" << TRADING_SOURCE_SHA256 << "</span></p><p>Compiler: " << ReportText{TRADING_COMPILER_ID}.html()
        << " / " << ReportText{TRADING_BUILD_CONFIGURATION}.html() << "</p><p>Exact protocol and input identities, all case statistics, uncertainty seeds and artifact hashes are retained in <a href=\"summary.json\">summary.json</a>. "
        << "<a href=\"measurements.csv\">Measurements</a> · <a href=\"features.csv\">Features and labels</a> · <a href=\"daily_accounts.csv\">Daily accounts</a> · <a href=\"account_ledger.csv\">Event ledger</a>.</p></section></main></body></html>";
    summary["artifacts"] = Json::array();
    auto publish = [&](const std::string& name, const std::string& text) {
        write_new_text_file(directory / name, text); summary["artifacts"].push_back({{"file", name}, {"sha256", sha256(text)}});
    };
    publish("report.html", html.str()); publish("measurements.csv", measurements.str()); publish("features.csv", features.str());
    publish("daily_accounts.csv", daily.str()); publish("account_ledger.csv", ledger.str());
    write_new_text_file(directory / "summary.json", summary.dump(2) + '\n');
    return directory;
}
} // namespace trading
