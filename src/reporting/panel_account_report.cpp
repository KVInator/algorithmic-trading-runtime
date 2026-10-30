#include "panel_account_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "report_text.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace trading {
namespace {
using Json = nlohmann::json;
std::string case_id(const Json &r, bool risk) {
    return risk ? r.at("symbol").get<std::string>() + "/" + r.at("account").get<std::string>()
                : r.at("variant").get<std::string>();
}
std::string html(const Json &s) {
    std::ostringstream h;
    h << std::fixed << std::setprecision(6);
    h << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" "
         "content=\"width=device-width,initial-scale=1\"><title>Continuous and shared-capital "
         "accounts</title><style>body{font:16px/1.5 "
         "system-ui;color:#193549;background:#f2f5f8;margin:24px}main{max-width:1180px;margin:auto}"
         "section{"
         "background:white;padding:20px;margin:20px 0;border:1px solid "
         "#d7e0e8;border-radius:8px;overflow:auto}svg{width:100%;min-width:650px}svg text{font:12px "
         "system-ui}table{border-collapse:"
         "collapse;font-size:13px;width:100%}td,th{padding:7px;border-bottom:1px solid "
         "#d7e0e8;text-align:right}td:first-child,th:first-child{text-align:left}pre{white-space:pre-"
         "wrap;"
         "overflow-wrap:anywhere}.legend span{display:inline-block;margin-right:18px}"
         "@page{size:A4 landscape;margin:12mm}@media "
         "print{body{margin:8px;font-size:12px}section{padding:12px;margin:12px 0;overflow:visible}"
         "svg{min-width:0}table{font-size:9px;table-layout:"
         "fixed}td,th{padding:3px;overflow-wrap:anywhere}details{display:none}}"
         "</style><main><h1>Continuous risk and shared-capital accounts</h1>";
    h << "<p>Evidence origin: " << s.at("provenance").at("evidence_origin").get<std::string>()
      << (s.contains("evaluation_scope")
              ? ". Fresh evaluation capital; prior-only histories remain continuous. Required missing "
                "observations stop an account without "
              : ". Frozen prior-only decisions through 2023. Required missing observations stop an "
                "account without ")
      << "liquidation, reset or restart. Later account outcomes remain unavailable.</p><p>Risk accounts "
         "are "
         "fractional, dividend-aware ledgers. Shared accounts are whole-share reference replays with "
         "immediate full fills at cost-shifted raw opens. No observed liquidity, broker execution or "
         "network "
         "latency is inferred.</p>";
    h << "<p>Primary figures: one-minute delay, one basis point per side. Lines end at the last "
         "observed account close; stopped paths are not extended. Every figure uses the complete "
      << (s.contains("evaluation_scope") ? "evaluation" : "development") << " date axis.</p>";
    const auto &calendar = s.at("development_dates");
    for (const auto &symbol : std::vector<std::string>{"SPY", "QQQ", "IWM", "TLT", "GLD", "shared"}) {
        const bool risk = symbol != "shared";
        const auto &rows = s.at(risk ? "risk_series" : "shared_series");
        double lo = 100000, hi = 100000;
        for (const auto &row : rows) {
            if (row.at("delay") == 1 && row.at("cost_bps") == 1 &&
                (!risk || row.at("symbol") == symbol)) {
                for (const auto &p : row.at("statistics").at("equity_path")) {
                    lo = std::min(lo, p[1].get<double>());
                    hi = std::max(hi, p[1].get<double>());
                }
            }
        }
        const auto pad = std::max(1.0, (hi - lo) * .08);
        lo -= pad;
        hi += pad;
        h << "<section><h2>" << symbol << " | observed account equity</h2><svg data-chart=\"" << symbol
          << "\" data-low=\"" << std::setprecision(12) << lo << "\" data-high=\"" << hi
          << "\" viewBox=\"0 0 900 320\" role=\"img\" aria-label=\"Observed account equity USD\"><text "
             "font-size=\"12\" x=\"80\" y=\"20\">Equity (USD)</text><line x1=\"80\" x2=\"865\" "
             "y1=\"270\" "
             "y2=\"270\" "
             "stroke=\"#999\"/>";
        for (int tick = 0; tick <= 4; ++tick) {
            const auto value = lo + (hi - lo) * tick / 4;
            h << std::setprecision(2) << "<text font-size=\"12\" x=\"74\" y=\"" << 274 - 230 * tick / 4.0
              << "\" text-anchor=\"end\">" << value << "</text>";
        }
        const std::vector<const char *> colors{"#127da2", "#c27624", "#7e5e9c", "#258569", "#bb5261"};
        std::size_t n = 0;
        std::ostringstream legend;
        for (const auto &row : rows) {
            if (row.at("delay") != 1 || row.at("cost_bps") != 1 ||
                (risk && row.at("symbol") != symbol)) {
                continue;
            }
            const auto id = case_id(row, risk);
            const auto color = colors[n++ % colors.size()];
            h << std::setprecision(6) << "<polyline data-series=\"" << id << "\" fill=\"none\" stroke=\""
              << color << "\" points=\"";
            for (const auto &p : row.at("statistics").at("equity_path")) {
                const auto found = std::find(calendar.begin(), calendar.end(), p[0]);
                const auto index = static_cast<double>(found - calendar.begin());
                const double x = 80 +
                                 785 * index /
                                     static_cast<double>(std::max<std::size_t>(2, calendar.size()) - 1),
                             y = 270 - 230 * (p[1].get<double>() - lo) / (hi - lo);
                h << x << ',' << y << ' ';
            }
            h << "\"/>";
            legend << "<span style=\"color:" << color << "\">"
                   << ReportText{risk ? row.at("account").get<std::string>() : id}.html() << "</span>";
        }
        h << "<text font-size=\"12\" x=\"80\" y=\"285\">"
          << (calendar.empty() ? "" : calendar.front().get<std::string>())
          << "</text><text font-size=\"12\" x=\"865\" y=\"285\" text-anchor=\"end\">"
          << (calendar.empty() ? "" : calendar.back().get<std::string>())
          << "</text></svg><p class=\"legend\">" << legend.str() << "</p></section>";
    }
    h << "<section><h2>Prior-risk forecast measurements</h2><p>Forecast outcomes are evaluated "
         "separately "
         "from stopped account paths. Positive control-minus-candidate QLIKE favours the intraday "
         "estimate. "
         "A missing observation or terminal horizon remains unavailable.</p><table><tr><th>Instrument / "
         "delay</th><th>Forecast dates</th><th>RV5 outcomes</th><th>Endpoint outcomes</th><th>QLIKE "
         "dates</th><th>Mean QLIKE increment</th></tr>";
    for (const auto &f : s.at("forecast_statistics")) {
        h << "<tr data-forecast=\"" << f.at("symbol").get<std::string>() << '/' << f.at("delay")
          << "\"><td>" << f.at("symbol").get<std::string>() << " / " << f.at("delay") << "</td>";
        for (const auto *key : {"forecast_dates", "future_rv5_dates", "endpoint_dates", "qlike_dates"}) {
            h << "<td>" << f.at(key) << "</td>";
        }
        h << "<td>"
          << (f.at("mean_control_minus_candidate_qlike").is_null()
                  ? "NA"
                  : f.at("mean_control_minus_candidate_qlike").dump())
          << "</td></tr>";
    }
    h << "</table></section><section><h2>Primary matched account contrasts</h2><p>One-minute delay, one "
         "basis point per side; candidate minus control on the common observed prefix. Other cost/delay "
         "contrasts remain in the manifest.</p><table><tr><th>Candidate / control</th><th>Common "
         "dates</th><th>Mean increment bp/day</th><th>Annual CER increment pp</th></tr>";
    for (const auto *key : {"risk_comparisons", "shared_comparisons"}) {
        for (const auto &p : s.at(key)) {
            if (p.at("delay") != 1 || p.at("cost_bps") != 1) {
                continue;
            }
            h << "<tr data-pair=\"" << p.at("candidate").get<std::string>() << "|"
              << p.at("control").get<std::string>() << "\"><td>" << p.at("candidate").get<std::string>()
              << " / " << p.at("control").get<std::string>() << "</td><td>"
              << p.at("common_observed_dates") << "</td><td>";
            if (p.at("mean_daily_increment").is_null()) {
                h << "NA";
            } else {
                h << 10000 * p.at("mean_daily_increment").get<double>();
            }
            h << "</td><td>";
            if (p.at("annual_cer_increment").is_null()) {
                h << "NA";
            } else {
                h << 100 * p.at("annual_cer_increment").get<double>();
            }
            h << "</td></tr>";
        }
    }
    h << "</table></section>";
    for (bool risk : {true, false}) {
        h << "<section><h2>" << (risk ? "Risk ledgers" : "Shared ledgers")
          << " | every registered case</h2><p>Observed-prefix statistics are incomplete when a stop "
             "date is "
             "present. Costs include executed activity on a stopping date. Exposure is closing liquid "
             "stock "
             "weight for risk ledgers and daily maximum marked gross fraction for shared "
             "ledgers.</p><table><tr><th>Account</th><th>Delay</th><th>Cost "
             "bp</th><th>Dates</th><th>Last "
             "equity USD</th><th>Drawdown %</th><th>Turnover / initial equity</th><th>Paid cost "
             "USD</th><th>Mean exposure %</th><th>Stop</th></tr>";
        for (const auto &row : s.at(risk ? "risk_series" : "shared_series")) {
            const auto &st = row.at("statistics");
            const auto id = case_id(row, risk) + "/" + std::to_string(row.at("delay").get<int>()) + "/" +
                            row.at("cost_bps").dump();
            h << "<tr data-case=\"" << id << "\"><td>" << case_id(row, risk) << "</td><td>"
              << row.at("delay") << "</td><td>" << row.at("cost_bps") << "</td><td>"
              << st.at("observed_dates") << "</td><td>" << st.at("last_observed_equity").get<double>()
              << "</td><td>" << 100 * st.at("observed_prefix_drawdown").get<double>() << "</td><td>"
              << st.at("traded_notional").get<double>() / 100000 << "</td><td>"
              << st.at("cost_paid").get<double>() << "</td><td>"
              << (st.at("mean_exposure").is_null()
                      ? "NA"
                      : std::to_string(100 * st.at("mean_exposure").get<double>()))
              << "</td><td>"
              << (st.at("stopped").is_null() ? "none" : st.at("stopped").get<std::string>())
              << "</td></tr>";
        }
        h << "</table></section>";
    }
    h << "<p>Paired contrasts use common observed calendar prefixes. They do not restore stopped "
         "portfolios or establish full-horizon benefit. "
      << (s.contains("evaluation_scope")
              ? "The integrated report records uncertainty, risk matching and calibration limitations."
              : "Statistical uncertainty and risk matching remain M7.")
      << "</p><p><a "
         "href=\"summary.json\">All paths, contrasts and provenance</a></p><details><summary>Executable "
         "conventions</summary><pre>"
      << ReportText{s.at("plan").dump(2)}.html() << "</pre></details></main></html>";
    return h.str();
}
Json contrasts(const Json &rows, bool risk) {
    Json result = Json::array();
    for (const auto &a : rows) {
        if ((risk && a.at("account") != "intraday_risk") || (!risk && a.at("variant") != "candidate")) {
            continue;
        }
        for (const auto &b : rows) {
            if (a.at("delay") != b.at("delay") || a.at("cost_bps") != b.at("cost_bps") ||
                (risk && (a.at("symbol") != b.at("symbol") || b.at("account") == "intraday_risk")) ||
                (!risk && b.at("variant") == "candidate")) {
                continue;
            }
            const auto &x = a.at("statistics").at("equity_path");
            const auto &y = b.at("statistics").at("equity_path");
            const auto n = std::min(x.size(), y.size());
            std::vector<double> ar, br;
            double ap = 100000, bp = 100000;
            for (std::size_t i = 0; i < n; ++i) {
                if (x[i][0] != y[i][0]) {
                    throw std::logic_error("paired account dates differ");
                }
                const auto ae = x[i][1].get<double>(), be = y[i][1].get<double>();
                ar.push_back(ae / ap - 1);
                br.push_back(be / bp - 1);
                ap = ae;
                bp = be;
            }
            double increment = 0;
            for (std::size_t i = 0; i < n; ++i) {
                increment += ar[i] - br[i];
            }
            result.push_back(
                {{"candidate", case_id(a, risk)},
                 {"control", case_id(b, risk)},
                 {"delay", a.at("delay")},
                 {"cost_bps", a.at("cost_bps")},
                 {"common_observed_dates", n},
                 {"mean_daily_increment", n ? Json(increment / static_cast<double>(n)) : Json(nullptr)},
                 {"annual_cer_increment",
                  n > 1 ? Json(certainty_equivalent(ar) - certainty_equivalent(br)) : Json(nullptr)}});
        }
    }
    return result;
}
} // namespace
PanelAccountReport::PanelAccountReport(const std::filesystem::path &root, Json provenance,
                                       std::filesystem::path dataset,
                                       const ExtensionStudyContract &contract, Json actions,
                                       PanelEvaluationAccess access)
    : directory_(create_unique_directory(root, "panel_accounts")),
      study_(contract, std::move(actions), access),
      summary_{{"kind", "panel_account_study"},
               {"provenance", std::move(provenance)},
               {"input_dataset", dataset.string()},
               {"policy_sha256", TRADING_COST_EXECUTION_PROTOCOL_SHA256},
               {"source_sha256", TRADING_SOURCE_SHA256},
               {"compiler", TRADING_COMPILER_ID},
               {"build_configuration", TRADING_BUILD_CONFIGURATION},
               {"network_requests", 0},
               {"reserved_strategy_inputs_opened", false},
               {"development_dates", Json::array()},
               {"artifacts", Json::array()}} {
    if (!access.development()) {
        summary_["evaluation_scope"] = access.document();
        summary_["validation_protocol_sha256"] = TRADING_VALIDATION_PROTOCOL_SHA256;
        summary_["reserved_strategy_inputs_opened"] = true;
        summary_["validation_strategy_inputs_opened"] = access.year() == 2024;
        summary_["final_strategy_inputs_opened"] = access.year() == 2025;
    }
    accounts_.exceptions(std::ios::failbit | std::ios::badbit);
    accounts_.open(directory_ / "account_days.jsonl", std::ios::binary);
    for (const auto &symbol : contract.symbols()) {
        auto &stream = primary_[symbol];
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        stream.open(directory_ / (symbol + ".jsonl"), std::ios::binary);
    }
    summary_["plan"] = study_.plan();
    summary_["plan_sha256"] = sha256(study_.plan().dump());
    write_new_text_file(directory_ / "plan.json", study_.plan().dump(2) + '\n');
}
const std::filesystem::path &PanelAccountReport::directory() const noexcept { return directory_; }
void PanelAccountReport::append(std::span<const PanelReplayDay> days,
                                std::span<const SessionDownload> sessions,
                                std::span<const std::optional<MinuteBar>> recovered, bool terminal) {
    if (terminal_) {
        throw std::invalid_argument("account report already received its terminal date");
    }
    const auto row = study_.append(days, sessions, recovered, terminal);
    const auto raw = row.dump() + '\n';
    if (raw.size() > 16U * 1024U * 1024U || bytes_ + raw.size() > 8ULL * 1024 * 1024 * 1024) {
        throw std::invalid_argument("account evidence exceeds its explicit bound");
    }
    accounts_.write(raw.data(), static_cast<std::streamsize>(raw.size()));
    bytes_ += raw.size();
    ++dates_;
    terminal_ = terminal;
    for (const auto &day : days) {
        primary_.at(day.symbol) << panel_replay_record(day).dump() << '\n';
    }
    if (days.front().scored_period) {
        summary_["development_dates"].push_back(days.front().date);
    }
}
std::filesystem::path PanelAccountReport::finish() {
    if (!terminal_ || dates_ == 0) {
        throw std::invalid_argument("account report requires an explicit last development date");
    }
    accounts_.close();
    for (auto &[s, out] : primary_) {
        out.close();
        static_cast<void>(s);
    }
    summary_.update(study_.finish());
    summary_["risk_comparisons"] = contrasts(summary_.at("risk_series"), true);
    summary_["shared_comparisons"] = contrasts(summary_.at("shared_series"), false);
    for (const auto &s : std::vector<std::string>{"SPY.jsonl", "QQQ.jsonl", "IWM.jsonl", "TLT.jsonl",
                                                  "GLD.jsonl", "account_days.jsonl", "plan.json"}) {
        summary_["artifacts"].push_back(
            {{"path", s},
             {"bytes", std::filesystem::file_size(directory_ / s)},
             {"sha256", sha256_file(directory_ / s, 8ULL * 1024 * 1024 * 1024)}});
    }
    summary_["calendar_dates"] = dates_;
    write_new_text_file(directory_ / "report.html", html(summary_));
    summary_["report_sha256"] = sha256_file(directory_ / "report.html", 32U * 1024 * 1024);
    write_new_text_file(directory_ / "summary.json", summary_.dump(2) + '\n');
    return directory_;
}
} // namespace trading
