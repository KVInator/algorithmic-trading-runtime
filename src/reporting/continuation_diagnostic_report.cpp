#include "report_text.hpp"
#include "continuation_diagnostic_report.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace trading {
namespace {


void validate_pair(const IntradayContinuationReplay& price, const IntradayContinuationReplay& volume) {
    if (price.decisions.size() != 13 || volume.decisions.size() != 13) {
        throw std::invalid_argument("diagnostic report requires twelve decisions and terminal liquidation");
    }
    for (std::size_t index = 0; index < 12; ++index) {
        const auto& p = price.decisions[index];
        const auto& v = volume.decisions[index];
        const auto expected = price.decisions.front().at + std::chrono::minutes{30 * static_cast<int>(index)};
        if (!p.features || !v.features || p.at != expected || p.at != v.at || p.features->at != p.at ||
            v.features->at != v.at || p.features->move != v.features->move ||
            p.features->typical_absolute_move != v.features->typical_absolute_move ||
            p.features->relative_volume != v.features->relative_volume) {
            throw std::invalid_argument("paired diagnostic decisions must share the same causal features");
        }
    }
    if (price.decisions.back().features || volume.decisions.back().features ||
        price.decisions.back().at != volume.decisions.back().at ||
        price.decisions.back().at != price.decisions.front().at + std::chrono::minutes{355}) {
        throw std::invalid_argument("terminal paired decisions must be scheduled without signal features");
    }
    const auto delay = price.always_long_control.entry_reference_time - price.always_long_control.entry_decision;
    if (delay != volume.always_long_control.entry_reference_time - volume.always_long_control.entry_decision ||
        (delay != std::chrono::minutes{1} && delay != std::chrono::minutes{2})) {
        throw std::invalid_argument("paired diagnostic controls must use the same registered execution delay");
    }
}

std::string local_clock(std::chrono::sys_seconds at, std::chrono::sys_seconds open) {
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(at - open).count() + 570;
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setfill('0') << std::setw(2) << minutes / 60 << ':' << std::setw(2) << minutes % 60;
    return stream.str();
}

std::string chart(std::string_view id, std::string_view title,
                  const std::array<double, 12>& values,
                  const std::array<double, 12>& reference, std::string_view unit,
                  double minimum_step) {
    const double lower = std::min({0.0, *std::min_element(values.begin(), values.end()),
                                  *std::min_element(reference.begin(), reference.end())});
    const double maximum = std::max({minimum_step, *std::max_element(values.begin(), values.end()),
                                  *std::max_element(reference.begin(), reference.end())});
    const double step = std::max(minimum_step, std::ceil((maximum - lower) / (4.0 * minimum_step)) * minimum_step);
    const double axis_lower = std::floor(lower / step) * step;
    const double axis_upper = std::ceil(maximum / step) * step;
    const double span = axis_upper - axis_lower;
    const auto x = [](std::size_t index) { return 66.0 + 770.0 * static_cast<double>(index) / 11.0; };
    const auto y = [&](double value) { return 212.0 - 180.0 * (value - axis_lower) / span; };
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(2);
    out << "<svg viewBox=\"0 0 880 260\" role=\"img\" aria-labelledby=\"" << id
        << "\"><title id=\"" << id << "\">" << ReportText{title}.html() << "</title>";
    for (int tick = 0; tick <= static_cast<int>(std::round(span / step)); ++tick) {
        const double value = axis_lower + step * static_cast<double>(tick);
        out << "<line x1=\"66\" x2=\"836\" y1=\"" << y(value) << "\" y2=\"" << y(value)
            << "\" stroke=\"#e2e8f0\"/><text x=\"55\" y=\"" << y(value) + 4
            << "\" text-anchor=\"end\">" << value << "</text>";
    }
    const auto path = [&](const auto& series, std::string_view color, bool dashed) {
        out << "<polyline fill=\"none\" stroke=\"" << color << "\" stroke-width=\"2.5\"";
        if (dashed) { out << " stroke-dasharray=\"6 4\""; }
        out << " points=\"";
        for (std::size_t index = 0; index < series.size(); ++index) {
            out << x(index) << ',' << y(series[index]) << ' ';
        }
        out << "\"/>";
    };
    path(reference, "#64748b", true);
    path(values, "#1d4ed8", false);
    for (std::size_t index = 0; index < values.size(); ++index) {
        out << "<circle cx=\"" << x(index) << "\" cy=\"" << y(values[index])
            << "\" r=\"3.5\" fill=\"#1d4ed8\"/>";
        if (index % 2 == 0 || index == 11) {
            const auto minutes = 600 + 30 * static_cast<int>(index);
            out << "<text x=\"" << x(index) << "\" y=\"234\" text-anchor=\"middle\">"
                << minutes / 60 << ':' << std::setfill('0') << std::setw(2) << minutes % 60 << "</text>";
        }
    }
    out << "<text x=\"66\" y=\"16\">" << ReportText{unit}.html()
        << "</text><text x=\"836\" y=\"255\" text-anchor=\"end\">New York decision time</text></svg>";
    return out.str();
}

} // namespace

std::string ContinuationDiagnosticReport::html(const HistoricalBarRequest& session) const {
    const auto& price = price_;
    const auto& volume = volume_;
    validate_pair(price, volume);
    if (price.decisions.front().at != session.start() + std::chrono::minutes{30}) {
        throw std::invalid_argument("diagnostic report session does not match the replay clocks");
    }
    std::array<double, 12> move{};
    std::array<double, 12> scale{};
    std::array<double, 12> relative_volume{};
    std::array<double, 12> volume_reference{};
    volume_reference.fill(1.0);
    for (std::size_t index = 0; index < move.size(); ++index) {
        const auto& f = *price.decisions[index].features;
        move[index] = f.move * 10000;
        scale[index] = f.typical_absolute_move * 10000;
        relative_volume[index] = f.relative_volume;
        if (!std::isfinite(move[index]) || !std::isfinite(scale[index]) || !std::isfinite(relative_volume[index])) {
            throw std::range_error("diagnostic chart coordinates require finite displayed features");
        }
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(3);
    out << R"(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Intraday continuation: synthetic contract trace</title><style>
body{margin:0;background:#f1f5f9;color:#172b4d;font:16px/1.55 system-ui,Segoe UI,sans-serif}main{max-width:1080px;margin:auto;padding:32px 22px}h1{font-size:32px;line-height:1.2;margin:12px 0}h2{font-size:22px}p{max-width:900px}.label{color:#6b4a10;font-size:13px;font-weight:700;letter-spacing:.06em;text-transform:uppercase}.note{background:#fff7dc;border-left:4px solid #d69e36;padding:16px 20px}.card{background:white;border:1px solid #dbe4ef;border-radius:10px;padding:22px;margin:22px 0}.cards{display:grid;grid-template-columns:1fr 1fr;gap:18px}.cards .card{margin:8px 0}.number{font-size:30px;font-weight:700}svg{display:block;width:100%;height:auto}svg text{font:11px system-ui,sans-serif;fill:#526882}.legend{font-size:13px;color:#526882}.table{overflow:auto}table{border-collapse:collapse;width:100%;font-size:13px}th,td{text-align:right;padding:10px;border-bottom:1px solid #e2e8f0;white-space:nowrap}th:first-child,td:first-child{text-align:left}th{background:#f8fafc}code{font-size:12px;overflow-wrap:anywhere}.small{font-size:13px;color:#526882}@media(max-width:650px){main{padding:20px 12px}.cards{grid-template-columns:1fr}h1{font-size:27px}.card{padding:16px}}@media print{body{background:white}main{padding:0}.card{break-inside:avoid}}
</style></head><body><main data-kind="synthetic"><div class="label">SPY-TOD-01 · Synthetic contract trace</div><h1>Price, volume and the decision clock</h1><p class="note"><strong>Constructed observations, not market results.</strong> This fixture demonstrates causal features and the state machine. It supplies no evidence of strategy profitability. Twenty earlier synthetic sessions form the reference history.</p>)";
    out << "<div class=\"cards\"><div class=\"card\"><div>Price-only round trips</div><div class=\"number\">"
        << price.trades.size() << "</div></div><div class=\"card\"><div>Volume-conditioned round trips</div><div class=\"number\">"
        << volume.trades.size() << "</div></div></div>";
    out << "<section class=\"card\"><h2>1. Price condition</h2><p>The positive move must exceed the mean absolute move of the previous twenty eligible sessions at the same clock.</p>"
        << chart("price-chart", "Synthetic intraday move and historical time-of-day scale", move, scale,
                 "Basis points from the session open", 50)
        << "<p class=\"legend\">Blue: completed-bar move. Dashed grey: historical absolute-move scale. Connecting lines join decision observations; they do not imply decisions between those clocks.</p></section>";
    out << "<section class=\"card\"><h2>2. Volume condition</h2><p>Fresh entries require cumulative volume above the earlier same-clock median. Equality at one does not pass. Falling relative volume does not independently close an existing position.</p>"
        << chart("volume-chart", "Synthetic relative cumulative volume", relative_volume, volume_reference,
                 "Relative volume, times historical median", 0.5)
        << "<p class=\"legend\">Blue: relative cumulative volume. Dashed grey: entry threshold of one.</p></section>";
    out << "<section class=\"card\"><h2>3. Paired decision trace</h2><div class=\"table\"><table><thead><tr><th>New York time</th><th>Move, bp</th><th>Scale, bp</th><th>Relative volume</th><th>Price-only</th><th>With volume</th></tr></thead><tbody>";
    for (std::size_t index = 0; index < price.decisions.size(); ++index) {
        const auto& p = price.decisions[index];
        const auto& v = volume.decisions[index];
        out << "<tr><td>" << local_clock(p.at, session.start()) << "</td>";
        if (p.features) {
            out << "<td>" << p.features->move * 10000 << "</td><td>" << p.features->typical_absolute_move * 10000
                << "</td><td>" << p.features->relative_volume << "</td>";
        } else { out << "<td colspan=\"3\">Scheduled liquidation; no feature required</td>"; }
        out << "<td>" << continuation_action_name(p.action) << "</td><td>"
            << continuation_action_name(v.action) << "</td></tr>";
    }
    const auto delay = std::chrono::duration_cast<std::chrono::minutes>(
        price.always_long_control.entry_reference_time - price.always_long_control.entry_decision).count();
    out << "</tbody></table></div><p class=\"small\">Displayed features are rounded; decisions use the full double values. Instructions use completed prefixes, followed by "
        << delay << "-minute delayed open-price references. No broker orders are submitted.</p></section>";
    out << "<section class=\"card\"><h2>Provenance</h2><p>Synthetic fixture calendar date: "
        << ReportText{format_utc_seconds(session.start()).substr(0, 10)}.html()
        << ". The diagnostic uses no API credentials, network request or historical market file.</p><p>Compiled protocol SHA-256: <code>"
        << continuation_protocol_sha256() << "</code></p></section></main></body></html>";
    return out.str();
}

std::string ContinuationDiagnosticReport::csv() const {
    const auto& price = price_;
    const auto& volume = volume_;
    validate_pair(price, volume);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17)
        << "fixture_kind,variant,decision_utc,action,move,typical_absolute_move,cumulative_volume_shares,historical_median_volume_shares,relative_volume\n";
    const auto append = [&](std::string_view variant, const auto& replay) {
        for (const auto& d : replay.decisions) {
            out << "synthetic," << variant << ',' << format_utc_seconds(d.at) << ',' << continuation_action_name(d.action);
            if (d.features) {
                const auto& f = *d.features;
                out << ',' << f.move << ',' << f.typical_absolute_move << ',' << f.cumulative_volume_shares
                    << ',' << f.historical_median_volume_shares << ',' << f.relative_volume;
            } else { out << ",,,,,"; }
            out << '\n';
        }
    };
    append("price_only", price);
    append("volume_conditioned", volume);
    return out.str();
}

} // namespace trading
