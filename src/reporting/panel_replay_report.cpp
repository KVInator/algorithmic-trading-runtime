#include "panel_replay_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "report_text.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace trading {
namespace {
using Json = nlohmann::json;
Json opt(const std::optional<double>& value) { return value ? Json(*value) : Json(nullptr); }
Json features(const std::optional<IntradayContinuationFeatures>& f) {
    if (!f) { return nullptr; }
    return {{"move", f->move}, {"scale", f->typical_absolute_move}, {"volume", f->cumulative_volume_shares},
        {"median_volume", f->historical_median_volume_shares}, {"relative_volume", f->relative_volume},
        {"history_first", format_utc_seconds(f->oldest_history_start)}, {"history_last", format_utc_seconds(f->latest_history_start)}};
}
}
Json panel_replay_record(const PanelReplayDay& day) {
    Json paths = Json::array();
    for (const auto& path : day.policies) {
        Json decisions = Json::array(), trades = Json::array();
        for (const auto& d : path.decisions) {
            decisions.push_back({{"at", format_utc_seconds(d.at)}, {"target_long", d.target_long ? Json(*d.target_long) : Json(nullptr)},
                {"reason", d.reason}, {"features", features(d.continuation)}});
        }
        for (const auto& t : path.trades) {
            trades.push_back({{"entry_decision", format_utc_seconds(t.entry_decision)}, {"entry_at", format_utc_seconds(t.entry_reference_time)},
                {"exit_decision", format_utc_seconds(t.exit_decision)}, {"exit_at", format_utc_seconds(t.exit_reference_time)},
                {"entry_price", t.entry_reference_price}, {"exit_price", t.exit_reference_price},
                {"gross", t.returns.gross_return}, {"net", t.returns.net_return}});
        }
        const bool available = day.scored_period && day.common_operational && path.execution_resolved;
        paths.push_back({{"policy", path.policy}, {"execution_resolved", path.execution_resolved}, {"outcome_available", available},
            {"gross", available ? Json(path.gross) : Json(nullptr)}, {"net", available ? Json(path.net) : Json(nullptr)},
            {"decisions", decisions}, {"trades", trades}});
    }
    Json measurement = nullptr;
    if (day.measurement) {
        const auto& m = *day.measurement;
        measurement = {{"log_return", m.log_return}, {"daily2", m.daily2}, {"rv1", opt(m.rv1)}, {"rv5", opt(m.rv5)}, {"rv15", opt(m.rv15)}};
    }
    const auto& risk = day.risk;
    Json risk_record{{"available", risk.estimate.has_value()}, {"reason", risk.reason}, {"prior_returns", risk.prior_returns},
        {"history_first", risk.history_first}, {"history_last", risk.history_last}, {"cutoff_utc", risk.cutoff_utc},
        {"passive_weight", risk.static_weight ? Json(1.0) : Json(nullptr)}, {"intraday_weight", opt(risk.intraday_weight)}, {"daily_weight", opt(risk.daily_weight)}, {"static_weight", opt(risk.static_weight)},
        {"h_daily", risk.estimate ? Json(risk.estimate->daily) : Json(nullptr)},
        {"h_intraday", risk.estimate ? opt(risk.estimate->intraday) : Json(nullptr)},
        {"missing_rv5_dates", risk.estimate ? Json(risk.estimate->missing_dates) : Json::array()}};
    Json reversion = nullptr;
    if (day.reversion) {
        const auto& d = *day.reversion; const auto& f = d.prior.fit;
        reversion = {{"history_first", format_utc_seconds(d.prior.oldest)}, {"history_last", format_utc_seconds(d.prior.latest)},
            {"complete", d.prior.complete}, {"fit_status", std::string{name(f.status)}}, {"pairs", f.pairs},
            {"intercept", opt(f.intercept)}, {"slope", opt(f.slope)}, {"innovation_variance", opt(f.innovation_variance)},
            {"equilibrium", opt(f.equilibrium)}, {"speed_per_minute", opt(f.speed_per_minute)},
            {"current", d.current ? Json(d.current->values.back()) : Json(nullptr)}, {"expected_move", opt(d.expected_move)}};
    }
    const auto& a = day.availability;
    return {{"date", day.date}, {"symbol", day.symbol}, {"session_open_utc", format_utc_seconds(CalendarSession{day.date, "09:30", "16:00"}.open_utc())}, {"scored_period", day.scored_period}, {"common_operational", day.common_operational},
        {"availability", {{"operational", a.operational}, {"momentum_features", a.momentum_features}, {"reversion_features", a.reversion_features},
            {"continuation_prefixes", a.continuation_prefixes}, {"missing_execution_slots", a.missing_execution_slots}}},
        {"momentum_signal", day.momentum ? Json(day.momentum->signal_return) : Json(nullptr)}, {"policies", paths},
        {"risk", risk_record}, {"measurement", measurement}, {"reversion", reversion}};
}
PanelReplayReport::PanelReplayReport(const std::filesystem::path& root, Json provenance, Json parity, std::filesystem::path dataset)
    : directory_(create_unique_directory(root, "panel_replay")), summary_{{"schema_version", 1}, {"kind", "causal_panel_replay"},
        {"provenance", std::move(provenance)}, {"reference_parity", std::move(parity)}, {"input_dataset", dataset.generic_string()},
        {"primary_cost_bps_per_side", 1}, {"primary_delay_minutes", 1}, {"strategy_scored", true}, {"reserved_strategy_inputs_opened", false}, {"network_requests", 0}, {"broker_events", 0},
        {"shared_capital_replayed", false}, {"daily_risk_accounts_replayed", false}, {"simultaneous_inference_completed", false},
        {"source_sha256", TRADING_SOURCE_SHA256}, {"compiler", TRADING_COMPILER_ID}, {"build_configuration", TRADING_BUILD_CONFIGURATION}} {}
void PanelReplayReport::append(std::vector<PanelReplayDay> day) {
    if (finished_) { throw std::logic_error("panel report is already complete"); }
    for (const auto& row : day) {
        if (row.date >= "2024-01-01") { throw std::invalid_argument("reserved record cannot enter the M3 report"); }
        const auto record = panel_replay_record(row);
        const auto line = record.dump() + '\n';
        auto& output = outputs_[row.symbol];
        if (!output.stream.is_open()) {
            output.stream.exceptions(std::ios::failbit | std::ios::badbit);
            output.stream.open(directory_ / (row.symbol + ".jsonl.tmp"), std::ios::binary | std::ios::trunc);
        }
        if (line.size() > 256U * 1024U || output.bytes + line.size() > 128U * 1024U * 1024U) {
            throw std::invalid_argument("panel replay output exceeds its explicit record/file bound");
        }
        output.stream.write(line.data(), static_cast<std::streamsize>(line.size()));
        output.bytes += line.size(); ++output.records;
        output.scored += row.scored_period ? 1U : 0U;
        output.common += row.scored_period && row.common_operational ? 1U : 0U;
        output.risk_ready += row.scored_period && row.risk.estimate ? 1U : 0U;
        if (row.scored_period && row.common_operational && std::none_of(samples_.begin(), samples_.end(), [&](const auto& item) { return item.at("symbol").template get<std::string>() == row.symbol; })) {
            samples_.push_back(record);
        }
    }
}
std::filesystem::path PanelReplayReport::finish() {
    if (finished_ || outputs_.size() != 5) { throw std::logic_error("panel report requires five complete instrument streams"); }
    summary_["evidence_origin"] = summary_.at("provenance").at("evidence_origin");
    Json artifacts = Json::array(), counts = Json::array();
    for (auto& [symbol, output] : outputs_) {
        output.stream.close(); const auto name = symbol + ".jsonl";
        std::filesystem::rename(directory_ / (name + ".tmp"), directory_ / name);
        artifacts.push_back({{"path", name}, {"sha256", sha256_file(directory_ / name, 128U * 1024U * 1024U)}, {"bytes", output.bytes}});
        counts.push_back({{"symbol", symbol}, {"calendar_records", output.records}, {"development_records", output.scored},
            {"common_operational_records", output.common}, {"risk_forecasts", output.risk_ready}});
    }
    summary_["instruments"] = counts; summary_["artifacts"] = artifacts;
    std::ostringstream html;
    html << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        << "<title>Causal panel replay</title><style>body{font:16px/1.5 system-ui;color:#173347;margin:32px}main{max-width:1100px;margin:auto}"
        << "table{border-collapse:collapse;width:100%}td,th{padding:9px;border-bottom:1px solid #ccd5dc;text-align:left}svg{width:100%;background:#f1f5f8}"
        << "pre{white-space:pre-wrap;overflow-wrap:anywhere}select{padding:8px}</style><main><h1>Causal five-instrument replay</h1>"
        << "<p><strong>Evidence origin: " << summary_.at("evidence_origin").get<std::string>() << "</strong>.</p>"
        << "<p>Primary one-minute, one-basis-point-per-side bar-price reference replay through 2023. Targets use completed observations and prior sessions only. "
        << "Missing execution outcomes are NA. Intraday comparisons use the common operational mask after decisions; it is not a trading signal.</p>"
        << "<p>Daily risk forecasts and sizing targets are recorded. Shared-capital ledgers, daily-risk account paths, cost research and simultaneous inference remain later milestones.</p>"
        << "<table><tr><th>Instrument</th><th>Calendar records</th><th>Development</th><th>Common operational</th><th>Prior-only risk forecasts</th></tr>";
    for (const auto& row : counts) { html << "<tr><td>" << row.at("symbol").get<std::string>() << "</td><td>" << row.at("calendar_records")
        << "</td><td>" << row.at("development_records") << "</td><td>" << row.at("common_operational_records") << "</td><td>" << row.at("risk_forecasts") << "</td></tr>"; }
    html << "</table><h2>First observable development-date targets</h2><p>Each instrument shows its first common operational development date. "
        << "Steps show intended long/cash targets at decision time, not broker acknowledgments or observed order flow.</p><select id=\"instrument\"></select>"
        << "<svg id=\"chart\" viewBox=\"0 0 1000 300\" role=\"img\" aria-label=\"Intraday policy targets\"></svg><p id=\"caption\"></p>"
        << "<script>const samples=" << samples_.dump() << ";const select=document.getElementById('instrument');const svg=document.getElementById('chart');"
        << "samples.forEach((s,i)=>{const o=document.createElement('option');o.value=i;o.textContent=s.symbol;select.append(o)});"
        << "function draw(){const s=samples[Number(select.value)];if(!s)return;const names=['momentum','continuation_volume','reversion_candidate'];"
        << "const colors=['#1570a6','#a24912','#5a4191'];let h='';names.forEach((n,j)=>{const p=s.policies.find(x=>x.policy===n);if(!p)return;"
        << "const y=60+j*85;h+=`<text x=\"10\" y=\"${y-15}\" font-size=\"15\">${n}</text><line x1=\"240\" x2=\"960\" y1=\"${y}\" y2=\"${y}\" stroke=\"#b4c1cc\"/>`;"
        << "let lastX=240,lastY=y; p.decisions.forEach(d=>{const t=new Date(d.at);const utc=t.getUTCHours()*60+t.getUTCMinutes();const start=new Date(s.session_open_utc);const open=start.getUTCHours()*60+start.getUTCMinutes();"
        << "const x=240+(utc-open)/390*720;const next=d.target_long===null?y+12:d.target_long?y-28:y;"
        << "h+=`<path d=\"M${lastX},${lastY} H${x} V${next}\" fill=\"none\" stroke=\"${colors[j]}\" stroke-width=\"3\"/>`;lastX=x;lastY=next});"
        << "h+=`<path d=\"M${lastX},${lastY} H960\" stroke=\"${colors[j]}\" fill=\"none\" stroke-width=\"3\"/>`});[0,120,240,390].forEach(m=>{const x=240+m/390*720;const v=570+m;h+=`<text x=\"${x}\" y=\"290\" text-anchor=\"middle\" font-size=\"14\">${Math.floor(v/60)}:${String(v%60).padStart(2,'0')}</text>`});svg.innerHTML=h;"
        << "document.getElementById('caption').textContent=s.symbol+' | '+s.date+' | horizontal session time 09:30–16:00 New York; raised step = long, baseline = cash.';}"
        << "select.addEventListener('change',draw);draw();</script><h2>Reference parity and reproduction</h2><pre>"
        << ReportText{summary_.dump(2)}.html() << "</pre><p><a href=\"summary.json\">Evidence manifest</a></p></main></html>";
    write_new_text_file(directory_ / "report.html", html.str());
    summary_["report_sha256"] = sha256_file(directory_ / "report.html", 16U * 1024U * 1024U);
    write_new_text_file(directory_ / "summary.json", summary_.dump(2) + '\n');
    finished_ = true; return directory_;
}
} // namespace trading
