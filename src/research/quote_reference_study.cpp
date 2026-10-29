#include "quote_reference_study.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace trading {
namespace {
using Json = nlohmann::json;
}
Json QuoteReferenceStudy::evaluate(const Json &sample) const {
    if (sample.at("kind") != "cost_quote_sample" ||
        sample.at("policy_sha256").get<std::string>() != TRADING_COST_EXECUTION_PROTOCOL_SHA256 ||
        sample.at("windows").size() > 910) {
        throw std::invalid_argument("quote reference requires the registered bounded sample");
    }
    Json rows = Json::array(), groups = Json::array(), pairs = Json::array();
    std::map<std::string, std::vector<double>> spread;
    std::map<std::string, std::pair<std::size_t, std::size_t>> coverage;
    for (const auto &w : sample.at("windows")) {
        const auto symbol = w.at("symbol").get<std::string>(), date = w.at("date").get<std::string>(),
                   key = symbol + '/' + date.substr(0, 4);
        if (date >= "2024") {
            throw std::invalid_argument("reserved quote reference date");
        }
        ++coverage[key].first;
        Json half = nullptr, differences = Json::array();
        if (w.at("status") == "eligible") {
            const auto &m = w.at("measurement");
            const auto bid = m.at("bid_units").get<std::int64_t>(),
                       ask = m.at("ask_units").get<std::int64_t>();
            if (bid <= 0 || ask <= bid || m.at("bid_shares").get<std::int64_t>() <= 0 ||
                m.at("ask_shares").get<std::int64_t>() <= 0) {
                throw std::invalid_argument("eligible quote geometry differs");
            }
            const double h = 10000.0 * static_cast<double>(ask - bid) /
                             (static_cast<double>(ask) + static_cast<double>(bid));
            half = h;
            spread[key].push_back(h);
            ++coverage[key].second;
            for (double cost : {0.0, .5, 1.0, 2.0, 5.0}) {
                differences.push_back(
                    {{"reference_cost_bps", cost}, {"crossing_minus_reference_bps", h - cost}});
            }
        }
        rows.push_back({{"id", w.at("id")},
                        {"symbol", symbol},
                        {"date", date},
                        {"decision_minute", w.at("decision_minute")},
                        {"delay_minutes", w.at("delay_minutes")},
                        {"status", w.at("status")},
                        {"crossing_cost_bps", half},
                        {"reference_differences", differences}});
    }
    for (const auto &[key, count] : coverage) {
        auto values = spread[key];
        std::sort(values.begin(), values.end());
        double sum = 0;
        for (double h : values) {
            sum += h;
        }
        Json fractions = Json::array();
        for (double cost : {0.0, .5, 1.0, 2.0, 5.0}) {
            fractions.push_back(
                {{"reference_cost_bps", cost},
                 {"eligible_at_or_below_reference",
                  std::count_if(values.begin(), values.end(), [&](double h) { return h <= cost; })}});
        }
        groups.push_back({{"group", key},
                          {"planned", count.first},
                          {"eligible", count.second},
                          {"mean_crossing_bps",
                           values.empty() ? Json(nullptr) : Json(sum / static_cast<double>(values.size()))},
                          {"minimum", values.empty() ? Json(nullptr) : Json(values.front())},
                          {"maximum", values.empty() ? Json(nullptr) : Json(values.back())},
                          {"reference_coverage", fractions}});
    }
    for (const auto &a : rows) {
        if (a.at("delay_minutes") != 1) {
            continue;
        }
        const auto b = std::find_if(rows.begin(), rows.end(), [&](const auto &x) {
            return x.at("symbol") == a.at("symbol") && x.at("date") == a.at("date") &&
                   x.at("decision_minute") == a.at("decision_minute") && x.at("delay_minutes") == 2;
        });
        if (b == rows.end()) {
            throw std::invalid_argument("paired quote delay omitted");
        }
        pairs.push_back({{"one_minute_id", a.at("id")},
                         {"two_minute_id", b->at("id")},
                         {"two_minus_one_crossing_bps",
                          a.at("crossing_cost_bps").is_null() || b->at("crossing_cost_bps").is_null()
                              ? Json(nullptr)
                              : Json(b->at("crossing_cost_bps").template get<double>() -
                                     a.at("crossing_cost_bps").get<double>())}});
    }
    return {{"windows", rows}, {"groups", groups}, {"delay_pairs", pairs}};
}
void QuoteReferenceStudy::write(const std::filesystem::path &dir, const Json &sample,
                                const Json &auth) const {
    auto s = evaluate(sample);
    s.update({{"kind", "quote_reference_comparison"},
              {"evidence_origin", sample.at("evidence_origin")},
              {"policy_sha256", TRADING_COST_EXECUTION_PROTOCOL_SHA256},
              {"source_sha256", TRADING_SOURCE_SHA256},
              {"compiler", TRADING_COMPILER_ID},
              {"build_configuration", TRADING_BUILD_CONFIGURATION},
              {"authentication", auth},
              {"sample_sha256", sha256(sample.dump())},
              {"network_requests", 0},
              {"historical_strategy_scored", false},
              {"reserved_strategy_inputs_opened", false}});
    write_new_text_file(dir / "sample.json", sample.dump(2) + '\n');
    std::ostringstream h;
    h << std::fixed << std::setprecision(6)
      << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" "
         "content=\"width=device-width,initial-scale=1\"><title>Quote crossing and fixed cost "
         "references</title><style>body{font:16px/1.5 system-ui;max-width:1180px;margin:28px "
         "auto;padding:15px;color:#193549}section{padding:20px;border:1px solid #cad7df;margin:20px "
         "0;overflow:auto}svg{width:100%;min-width:700px}table{border-collapse:collapse;width:100%;font-size:"
         "14px}td,th{padding:7px;border-bottom:1px solid "
         "#cad7df;text-align:right}td:first-child,th:first-child{text-align:left}</style><h1>Sampled quote "
         "crossing versus fixed charges</h1><p>Evidence origin: "
      << sample.at("evidence_origin").get<std::string>()
      << ". A hypothetical one-sided buy at ask or sell at bid is measured against that quote's midpoint. "
         "The quoted half-spread replaces the fixed all-in reference charge in this comparison. The two are "
         "never added.</p><p>No historical strategy return, realized fill, replenishment or client latency "
         "is estimated. Ineligible windows remain unavailable. The two delay samples are separate "
         "observations; their difference measures sampled spread variation, not the total cost of waiting or "
         "a trading advantage.</p>";
    for (const auto &symbol : sample.at("plan").at("symbols")) {
        double top = 5;
        for (const auto &g : s.at("groups")) {
            if (g.at("group").get<std::string>().starts_with(symbol.get<std::string>()) &&
                !g.at("mean_crossing_bps").is_null()) {
                top = std::max(top, g.at("mean_crossing_bps").get<double>());
            }
        }
        top *= 1.15;
        h << "<section><h2>" << symbol.get<std::string>()
          << " | mean sampled crossing cost</h2><svg data-symbol=\"" << symbol.get<std::string>()
          << "\" data-top=\"" << std::setprecision(12) << top
          << "\" viewBox=\"0 0 900 310\" role=\"img\" aria-label=\"Mean crossing cost basis points by "
             "sampled year\"><text x=\"60\" y=\"18\">Cost (bp per side)</text>";
        for (double c : {0.0, .5, 1.0, 2.0, 5.0}) {
            h << "<line x1=\"60\" x2=\"850\" y1=\"" << 260 - 220 * c / top << "\" y2=\""
              << 260 - 220 * c / top << "\" stroke=\"#aab7c2\"/><text x=\"855\" y=\"" << 264 - 220 * c / top
              << "\">" << c << "</text>";
        }
        std::size_t i = 0;
        for (const auto &g : s.at("groups")) {
            if (!g.at("group").get<std::string>().starts_with(symbol.get<std::string>() + '/')) {
                continue;
            }
            const auto x = 95 + 105 * i++;
            h << "<text x=\"" << x << "\" y=\"285\" text-anchor=\"middle\">"
              << g.at("group").get<std::string>().substr(4) << "</text>";
            if (g.at("mean_crossing_bps").is_null()) {
                h << "<text data-na=\"" << g.at("group").get<std::string>() << "\" x=\"" << x
                  << "\" y=\"250\" text-anchor=\"middle\">NA</text>";
            } else {
                h << "<circle data-group=\"" << g.at("group").get<std::string>() << "\" cx=\"" << x
                  << "\" cy=\"" << 260 - 220 * g.at("mean_crossing_bps").get<double>() / top
                  << "\" r=\"6\" fill=\"#127da2\"/>";
            }
        }
        h << "</svg></section>";
    }
    h << "<section><h2>Coverage and cost "
         "range</h2><table><tr><th>Instrument/year</th><th>Planned</th><th>Eligible</th><th>Mean "
         "bp</th><th>Minimum bp</th><th>Maximum bp</th></tr>";
    for (const auto &g : s.at("groups")) {
        h << std::setprecision(6) << "<tr data-group-row=\"" << g.at("group").get<std::string>() << "\"><td>"
          << g.at("group").get<std::string>() << "</td><td>" << g.at("planned") << "</td><td>"
          << g.at("eligible") << "</td>";
        for (const auto *k : {"mean_crossing_bps", "minimum", "maximum"}) {
            h << "<td>" << (g.at(k).is_null() ? "NA" : g.at(k).dump()) << "</td>";
        }
        h << "</tr>";
    }
    h << "</table></section><p><a href=\"summary.json\">Every window, fixed-charge difference and paired "
         "delay comparison</a></p>";
    write_new_text_file(dir / "report.html", h.str());
    s["report_sha256"] = sha256_file(dir / "report.html");
    write_new_text_file(dir / "summary.json", s.dump(2) + '\n');
}
} // namespace trading
