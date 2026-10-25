#include "runtime_benchmark_report.hpp"
#include "runtime_benchmark_build.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "journal_frame.hpp"
#include "report_text.hpp"
#include "trading_source_inventory.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <set>

namespace trading {
namespace {
using Json = nlohmann::json;
class BenchmarkFigure {
  public:
    explicit BenchmarkFigure(const Json &rows) : rows_{rows} {}
    std::string render(bool recovery = false) const {
        std::ostringstream svg;
        svg << "<svg viewBox=\"0 0 1000 380\" role=\"img\" aria-label=\"Benchmark latency by state "
               "size\"><rect width=\"1000\" height=\"380\" fill=\"white\"/>"
               "<text x=\"20\" y=\"22\">"
            << (recovery ? "Median recovery-operation latency, microseconds; linear scales"
                         : "Median of trial p50 input latencies, microseconds; linear scales")
            << "</text>";
        const std::vector<std::string> modes =
            recovery ? std::vector<std::string>{"journal_recovery", "paper_recovery"}
                     : std::vector<std::string>{"core",         "copy_candidate", "journal_memory",
                                                "journal_sync", "paper_memory",   "paper_sync"};
        const std::vector<std::string> colors{"#196b77", "#3d8c49", "#bb7b18",
                                              "#b74839", "#6851a6", "#375d9e"};
        double maximum = 1;
        for (const auto &r : rows_)
            if ((r.at("sample_unit") == "recovery_operation") == recovery)
                maximum = std::max(maximum, r.at("latency").at("p50_ns").get<double>() / 1000.0);
        for (std::size_t j = 0; j < modes.size(); ++j) {
            svg << "<text x=\"" << 20 + (j % 3) * 310 << "\" y=\"" << 45 + (j / 3) * 20 << "\" fill=\""
                << colors[j] << "\" font-size=\"12\">" << modes[j] << "</text>";
            std::map<std::size_t, std::vector<double>> points;
            for (const auto &r : rows_)
                if (r.at("mode") == modes[j])
                    points[r.at("round_trips").get<std::size_t>()].push_back(
                        r.at("latency").at("p50_ns").get<double>() / 1000.0);
            svg << "<polyline fill=\"none\" stroke-width=\"2\" stroke=\"" << colors[j] << "\" points=\"";
            for (auto &[size, values] : points) {
                std::sort(values.begin(), values.end());
                const auto median = values[(values.size() - 1) / 2];
                svg << 80.0 + static_cast<double>(size) * 6.5 << ',' << 300.0 - median * 200.0 / maximum
                    << ' ';
            }
            svg << "\"/>";
        }
        for (int i = 0; i <= 4; ++i)
            svg << "<text x=\"5\" y=\"" << 300 - i * 50 << "\" font-size=\"11\">"
                << maximum * static_cast<double>(i) / 4 << " us</text>";
        svg << "<path d=\"M80 100V300H930\" stroke=\"#aab9c5\" fill=\"none\"/>";
        std::set<std::size_t> sizes;
        for (const auto &r : rows_)
            sizes.insert(r.at("round_trips").get<std::size_t>());
        for (auto size : sizes)
            svg << "<text x=\"" << 80.0 + static_cast<double>(size) * 6.5 << "\" y=\"325\" font-size=\"12\">"
                << size << "</text>";
        svg << "<text x=\"290\" y=\"355\">Round trips in retained state (two terminal orders "
               "each)</text></svg>";
        return svg.str();
    }

  private:
    const Json &rows_;
};
} // namespace
void RuntimeBenchmarkReport::write(const std::filesystem::path &directory) const {
    const Json settings{{"common_cxx_flags", build::benchmark_common_flags},
                        {"configuration_cxx_flags", build::benchmark_configuration_flags},
                        {"cxx_standard", 20},
                        {"project_warnings_as_errors", build::benchmark_warnings_as_errors}};
    const auto save = [&](std::string_view name, const std::string &bytes) {
        write_new_text_file(directory / name, bytes);
    };
    save("benchmark_evidence.json", Json{{"rows", result_.rows},
                                         {"workloads", result_.workloads},
                                         {"environment", result_.environment},
                                         {"clock_probe", result_.clock_probe},
                                         {"build_settings", settings}}
                                            .dump() +
                                        '\n');
    save("source_inventory.txt", std::string{build::source_records});
    std::ostringstream csv;
    csv << "mode,round_trips,trial,sample_unit,count,elapsed_ns,p50_ns,p95_ns,p99_ns,max_ns,operations_per_"
           "second,journal_bytes,journal_records,largest_frame_bytes,rss_before_bytes,rss_after_bytes,"
           "process_peak_bytes\n";
    for (const auto &r : result_.rows) {
        csv << r.at("mode").get<std::string>() << ',' << r.at("round_trips") << ',' << r.at("trial") << ','
            << r.at("sample_unit").get<std::string>() << ',' << r.at("latency").at("count") << ','
            << r.at("elapsed_ns");
        for (const auto key : {"p50_ns", "p95_ns", "p99_ns", "max_ns"})
            csv << ',' << r.at("latency").at(key);
        csv << ',' << r.at("operations_per_second") << ',' << r.at("journal_bytes") << ','
            << r.at("journal_records") << ',' << r.at("largest_frame_bytes");
        for (const auto &value :
             {r.at("resources_before").at("resident_bytes"), r.at("resources_after").at("resident_bytes"),
              r.at("resources_after").at("peak_resident_bytes")})
            csv << ',' << (value.is_null() ? "" : value.dump());
        csv << '\n';
    }
    save("benchmark_metrics.csv", csv.str());
    std::ostringstream html;
    html << std::fixed << std::setprecision(3);
    html << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" "
            "content=\"width=device-width,initial-scale=1\"><title>Runtime benchmark</title>"
            "<style>body{font:15px system-ui;max-width:1250px;margin:2rem auto;padding:0 "
            "1rem;background:#f3f6fa;color:#193348}aside{background:#fff0d2;padding:1rem}table{width:100%;"
            "border-collapse:collapse;background:white}td,th{padding:.5rem;border-bottom:1px solid "
            "#dbe3ec;text-align:left}.scroll{overflow-x:auto}svg{width:100%}details{padding:1rem;background:"
            "white;margin:1rem "
            "0}summary{cursor:pointer}pre{white-space:pre-wrap;overflow-wrap:anywhere}</style>"
            "<h1>C++ runtime benchmark</h1><aside>Measured local processing and platform file "
            "synchronization. Synthetic round trips; no broker/network delay or strategy return. "
            "Debug/sanitized builds are diagnostics only; Release measurements are the "
            "reference.</aside><p>Build: "
         << ReportText{TRADING_COMPILER_ID}.html() << " / " << ReportText{TRADING_BUILD_CONFIGURATION}.html()
         << ".</p>"
            "<details><summary>Environment and clock probe</summary><pre>"
         << ReportText{result_.environment.dump(2)}.html() << "</pre><pre>"
         << ReportText{result_.clock_probe.dump(2)}.html() << "</pre><pre>"
         << ReportText{settings.dump(2)}.html()
         << "</pre></details>"
            "<p>Core applies typed inputs directly. Copy candidate adds the journal owner's outer "
            "transaction copy. Journal memory retains serialization, hashing and volatile storage without "
            "physical synchronization. "
            "Journal sync uses the existing exclusive platform writer. Paper modes use schema 3 with JSON "
            "messages and nanodollar accounting; their payloads differ from the schema-2 typed path. "
            "Recovery includes opening, reading, validation, replay and a synchronized routing-close record, "
            "after an untimed copy to a fresh file. Reads are warm-cache.</p>"
         << BenchmarkFigure{result_.rows}.render() << BenchmarkFigure{result_.rows}.render(true)
         << "<h2>Trial variability</h2><p>Min / median / max of trial p50 input latency, or whole-operation "
            "recovery latency. "
            "Nearest-rank percentiles are retained per trial; no confidence interval or universal speedup is "
            "implied.</p><table><thead><tr><th>Mode</th><th>Round trips</th><th>Trials</th><th>Trial p50 min "
            "/ median / max, us</th></tr></thead><tbody>";
    std::map<std::pair<std::string, std::size_t>, std::vector<std::int64_t>> groups;
    for (const auto &r : result_.rows)
        groups[{r.at("mode").get<std::string>(), r.at("round_trips").get<std::size_t>()}].push_back(
            r.at("latency").at("p50_ns").get<std::int64_t>());
    for (auto &[key, values] : groups) {
        std::sort(values.begin(), values.end());
        html << "<tr><td>" << ReportText{key.first}.html() << "</td><td>" << key.second << "</td><td>"
             << values.size() << "</td><td>" << static_cast<double>(values.front()) / 1000 << " / "
             << static_cast<double>(values[(values.size() - 1) / 2]) / 1000 << " / "
             << static_cast<double>(values.back()) / 1000 << "</td></tr>";
    }
    html << "</tbody></table><h2>Per-trial timing and resources</h2><p>Throughput is input calls per summed "
            "call time, or recoveries per second for recovery rows. Creation, fixture construction, "
            "validation, report writing and resource sampling are outside measured calls. "
            "Append/sync timings are nested inside journal call timings and include instrumentation "
            "overhead. Process RSS/peak are shared across the sequential benchmark, not isolated allocations "
            "or per-case peaks. "
            "A flat/noisy memory delta does not prove constant space. Histories/maps and candidate copies "
            "grow with retained inputs; no optimization has been applied.</p><div "
            "class=\"scroll\"><table><thead><tr><th>Mode / size / trial</th><th>Samples</th><th>p50 / p95 / "
            "p99, us</th><th>Operations/s</th><th>Append/sync p50, us</th><th>Journal bytes / "
            "records</th><th>Process RSS after / peak, MiB</th></tr></thead><tbody>";
    for (const auto &r : result_.rows) {
        html << "<tr><td><details><summary>" << r.at("mode").get<std::string>() << " / "
             << r.at("round_trips") << " / " << r.at("trial") << "</summary><pre>";
        auto metadata = r;
        metadata.erase("latencies_ns");
        metadata.erase("append_sync_ns");
        html << ReportText{metadata.dump(2)}.html() << "</pre></details></td><td>"
             << r.at("latency").at("count") << "</td><td>";
        for (const auto key : {"p50_ns", "p95_ns", "p99_ns"})
            html << static_cast<double>(r.at("latency").at(key).get<std::int64_t>()) / 1000 << ' ';
        html << "</td><td>" << r.at("operations_per_second").get<double>() << "</td><td>"
             << (r.at("append_latency").is_null()
                     ? std::string{"unavailable"}
                     : std::to_string(r.at("append_latency").at("p50_ns").get<double>() / 1000.0))
             << "</td><td>" << r.at("journal_bytes") << " / " << r.at("journal_records") << "</td><td>";
        for (const auto key : {"resident_bytes", "peak_resident_bytes"}) {
            const auto value = r.at("resources_after").at(key);
            if (value.is_null())
                html << "unavailable ";
            else
                html << value.get<double>() / 1048576 << ' ';
        }
        html << "</td></tr>";
    }
    html << "</tbody></table></div><h2>Bounds and interpretation</h2><p>Journal caps: "
         << execution::maximum_input_records << " inputs, " << execution::maximum_journal_bytes
         << " bytes and " << execution::maximum_frame_bytes
         << " bytes per frame. Reported occupancy is measured; maximum-capacity throughput is not "
            "extrapolated. Platform flush completion is not a physical power-loss guarantee. "
            "One untimed two-round-trip warm-up precedes measured trials. Two schema blocks and core/copy "
            "order alternate by trial. Samples retain state-growth effects rather than repeating one "
            "empty-account operation. "
            "Host scheduling, virtualization, caching, antivirus and storage affect results.</p><p><a "
            "href=\"benchmark_metrics.csv\">Metrics CSV</a> | <a href=\"benchmark_evidence.json\">Raw "
            "timings and fixtures</a> | <a href=\"summary.json\">Manifest</a></p></html>\n";
    save("report.html", html.str());
    Json artifacts = Json::array();
    std::vector<std::filesystem::path> files;
    for (const auto &f : std::filesystem::recursive_directory_iterator(directory))
        if (f.is_regular_file())
            files.push_back(f.path());
    std::sort(files.begin(), files.end());
    for (const auto &f : files)
        artifacts.push_back(
            {{"path", std::filesystem::relative(f, directory).generic_string()}, {"sha256", sha256_file(f)}});
    save("summary.json",
         Json{{"schema_version", 1},
              {"evidence_origin", "measured_processing_synthetic_inputs"},
              {"rows", result_.rows.size()},
              {"compiler", TRADING_COMPILER_ID},
              {"build_configuration", TRADING_BUILD_CONFIGURATION},
              {"build_settings", settings},
              {"release_reference", std::string_view{TRADING_BUILD_CONFIGURATION} == "Release"},
              {"source_sha256", build::source_sha256},
              {"limits",
               {{"input_records", execution::maximum_input_records},
                {"journal_bytes", execution::maximum_journal_bytes},
                {"frame_bytes", execution::maximum_frame_bytes}}},
              {"artifacts", artifacts}}
                 .dump(2) +
             '\n');
}
} // namespace trading
