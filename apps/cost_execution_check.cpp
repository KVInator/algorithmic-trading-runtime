#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "cost_quote_diagnostic_report.hpp"
#include "cost_quote_diagnostics.hpp"
#include "cost_quote_report.hpp"
#include "cost_quote_sample.hpp"
#include "json_input.hpp"
#include "utc_timestamp.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <set>

namespace {
using Json = nlohmann::json;
class Checks {
  public:
    explicit Checks(std::filesystem::path directory) : directory_(std::move(directory)) {}
    void require(std::string label, bool value) {
        if (!value) {
            throw std::runtime_error("cost execution check failed: " + label);
        }
        checks_.push_back(std::move(label));
    }
    template <class F> void rejects(std::string label, F &&f) {
        bool stopped = false;
        try {
            f();
        } catch (const std::exception &) {
            stopped = true;
        }
        require(std::move(label), stopped);
    }
    Json run(const std::filesystem::path &panel) {
        const auto plan = trading::CostQuotePlan::constructed();
        require("one constructed date produces 130 fixed quote windows", plan.windows().size() == 130);
        std::set<std::string> identities;
        for (const auto &w : plan.windows()) {
            require("window is pre-2024 and one second long",
                    w.date < "2024-01-01" && w.request.end() - w.request.start() == std::chrono::seconds{1});
            identities.insert(w.id);
        }
        require("quote windows have unique identities", identities.size() == plan.windows().size());
        rejects("reserved dates cannot enter a quote plan",
                [] { static_cast<void>(trading::CostQuotePlan::constructed({"2024-01-02"})); });
        rejects("short sessions cannot enter the quote sample",
                [] { static_cast<void>(trading::CostQuotePlan::constructed({"2022-11-25"})); });
        const auto matrix = trading::cost_execution_workloads();
        require("registered one-factor matrix contains 170 workloads and 680 policy cases",
                matrix.size() == 170 && matrix.size() * 4 == 680);
        std::set<std::string> workloads;
        for (const auto &w : matrix) {
            workloads.insert(w.id);
            const int changed =
                static_cast<int>(w.path != "steady") + static_cast<int>(w.requested_shares != 10) +
                static_cast<int>(w.displayed_shares != 10) + static_cast<int>(w.spread_cents != 2) +
                static_cast<int>(w.outbound_seconds != 0) + static_cast<int>(w.receipt_seconds != 0) +
                static_cast<int>(w.cancel_processing_seconds != 1) +
                static_cast<int>(w.source_age_seconds != 0);
            require("synthetic secondary parameters are not crossed", changed <= 1);
        }
        require("synthetic workload identities are unique", workloads.size() == matrix.size());
        const auto basic = sample("basic", "regular", plan);
        require("all complete regular quote windows are eligible",
                basic.at("status_counts").at("eligible").get<int>() == 130);
        require("two-page windows consume 260 request callbacks",
                basic.at("request_callbacks_this_invocation").get<int>() == 260);
        const auto &measurement = basic.at("windows").at(0).at("measurement");
        require("pre-2024 quote sizes convert round lots to shares",
                measurement.at("bid_shares").get<int>() == 200 &&
                    measurement.at("ask_shares").get<int>() == 300);
        require("latest quote is used without looking beyond reference",
                measurement.at("age_nanoseconds").get<std::int64_t>() == 0 &&
                    measurement.at("bid_units").get<std::int64_t>() == 100010000000);
        const auto resumed = trading::CostQuoteSample{directory_ / "basic/archive", plan}.run(
            true,
            [](const auto &, const auto &) -> trading::MarketDataResponse {
                throw std::runtime_error("completed quote cache unexpectedly fetched again");
            },
            [](auto) {});
        require("completed acquisition resumes without another request",
                resumed.at("request_callbacks_this_invocation").get<int>() == 0 &&
                    resumed.at("windows") == basic.at("windows"));
        const auto unavailable = sample("forbidden", "forbidden", plan);
        require("forbidden access is retained and not repeatedly retried",
                unavailable.at("request_callbacks_this_invocation").get<int>() == 1 &&
                    unavailable.at("status_counts").at("http_403").get<int>() == 1 &&
                    unavailable.at("status_counts").at("not_attempted_after_denial").get<int>() == 129);
        const auto capped = sample("capped", "capped", plan);
        require("capped windows remain unestimated",
                capped.at("windows").at(0).at("status").get<std::string>() == "incomplete_page_limit" &&
                    capped.at("windows").at(0).at("measurement").is_null());
        const auto nonregular = sample("nonregular", "nonregular", plan);
        require("invalid latest quote is not replaced by an older regular quote",
                nonregular.at("status_counts").at("nonregular").get<int>() == 130);
        const auto ambiguous = sample("ambiguous", "ambiguous", plan);
        require("same-timestamp conflicting latest quotes remain ambiguous",
                ambiguous.at("status_counts").at("ambiguous_latest").get<int>() == 130);
        const auto duplicate = sample("duplicate", "duplicate", plan);
        require("identical latest duplicates do not discard eligible quotes",
                duplicate.at("status_counts").at("eligible").get<int>() == 130);
        const auto geometry = sample("geometry", "geometry", plan);
        for (const auto *status : {"locked", "crossed", "missing_side", "missing_size", "missing_exchange"}) {
            require("invalid latest quote geometry remains separately unestimated",
                    geometry.at("status_counts").at(status).get<int>() == 26);
        }
        const auto fractional = sample("nanoseconds", "nanoseconds", plan);
        require("source age retains nanosecond precision",
                fractional.at("windows").at(0).at("measurement").at("age_nanoseconds").get<std::int64_t>() ==
                    876543211);
        const auto empty = sample("empty", "empty", plan);
        require("complete empty windows remain explicitly unestimated",
                empty.at("status_counts").at("empty").get<int>() == 130);
        const auto continued = sample("empty_page", "empty_page", plan);
        require("empty first page does not terminate a continued window",
                continued.at("status_counts").at("eligible").get<int>() == 130 &&
                    continued.at("request_callbacks_this_invocation").get<int>() == 260);
        const auto transient = sample("retry", "retry", plan);
        require("rate-limit retry evidence retains both HTTP attempts",
                transient.at("retained_http_attempts").get<int>() == 261 &&
                    transient.at("windows").at(0).at("pages").at(0).at("attempts").size() == 2);
        std::size_t calls = 0;
        const auto interrupted = directory_ / "interrupted/archive";
        rejects("transport interruption preserves completed quote pages", [&] {
            static_cast<void>(trading::CostQuoteSample{interrupted, plan}.run(
                true,
                [&](const auto &r, const auto &token) {
                    if (++calls == 2) {
                        throw std::runtime_error("constructed interrupted quote transport");
                    }
                    return response("regular", r, token, 0);
                },
                [](auto) {}));
        });
        auto recovered = trading::CostQuoteSample{interrupted, plan}.run(
            true, [&](const auto &r, const auto &token) { return response("regular", r, token, 0); },
            [](auto) {});
        require("resumed acquisition reuses the committed first page",
                recovered.at("request_callbacks_this_invocation").get<int>() == 259 &&
                    recovered.at("status_counts").at("eligible").get<int>() == 130);
        save("interrupted", recovered);
        const auto badclock = directory_ / "future/archive";
        rejects("future quotes are rejected after preserving their raw page", [&] {
            static_cast<void>(trading::CostQuoteSample{badclock, plan}.run(
                true, [&](const auto &r, const auto &token) { return response("future", r, token, 0); },
                [](auto) {}));
        });
        rejected_.push_back("future/archive");
        const auto corrupt = directory_ / "tampered/archive";
        std::filesystem::create_directories(corrupt.parent_path());
        std::filesystem::copy(directory_ / "basic/archive", corrupt,
                              std::filesystem::copy_options::recursive);
        const auto file = corrupt / basic.at("windows").at(0).at("pages").at(0).at("file").get<std::string>();
        auto doc = trading::parse_json_document(trading::read_text_file(file));
        doc["body_sha256"] = std::string(64, '0');
        {
            std::ofstream stream{file, std::ios::binary | std::ios::trunc};
            stream << doc.dump(2) << '\n';
        }
        rejects("tampered retained response is rejected without a refetch", [&] {
            static_cast<void>(trading::CostQuoteSample{corrupt, plan}.run(false, {}, [](auto) {}));
        });
        rejected_.push_back("tampered/archive");
        Json provider = nullptr;
        if (std::filesystem::exists(panel / "study_manifest.json")) {
            const auto registered = trading::CostQuotePlan::from_panel(panel);
            require("accepted M2 masks generate exactly 910 provider windows",
                    registered.windows().size() == 910);
            provider = registered.document();
        }
        for (const auto *mode : {"metadata", "size", "conditions", "unsupported", "oversized"}) {
            static_cast<void>(sample(mode, mode, plan));
        }
        Json diagnostic_cases = Json::array();
        std::map<std::string, Json> diagnoses;
        const auto inventory = [](const std::filesystem::path &root) {
            Json files = Json::object();
            for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
                if (entry.is_regular_file()) {
                    files[std::filesystem::relative(entry.path(), root).generic_string()] =
                        trading::sha256_file(entry.path());
                }
            }
            return files;
        };
        const auto before = inventory(directory_ / "basic/archive");
        for (const auto &case_file : cases_) {
            const auto case_path = std::filesystem::path{case_file.get<std::string>()};
            const auto saved = directory_ / case_path;
            const auto digest = trading::sha256_file(saved);
            auto diagnosis = trading::CostQuoteDiagnostics{panel}.inspect(saved);
            require("read-only diagnosis retains primary decisions and never adopts alternative eligibility",
                    diagnosis.at("primary_replay_identical").get<bool>() &&
                        !diagnosis.at("revised_eligibility_adopted").get<bool>() &&
                        diagnosis.at("network_requests").get<int>() == 0 &&
                        trading::sha256_file(saved) == digest);
            const auto output = directory_ / case_path.parent_path() / "diagnostics";
            trading::CostQuoteDiagnosticReport{}.write(output, diagnosis);
            diagnostic_cases.push_back(
                (case_path.parent_path() / "diagnostics/summary.json").generic_string());
            diagnoses[case_path.parent_path().generic_string()] = std::move(diagnosis);
        }
        require("inspection changes no cached filenames or bytes",
                before == inventory(directory_ / "basic/archive"));
        require("price conflicts are distinct from metadata conflicts",
                diagnoses.at("ambiguous").at("ambiguous_latest_relations").at("price_conflict").get<int>() ==
                    130);
        require("size conflicts are distinct from price conflicts",
                diagnoses.at("size").at("ambiguous_latest_relations").at("size_conflict").get<int>() == 130);
        require("exchange-only differences remain primary ambiguous without reclassification",
                diagnoses.at("metadata").at("ambiguous_latest_relations").at("metadata_only").get<int>() ==
                        130 &&
                    diagnoses.at("metadata").at("condition_test_removal_only_count").get<int>() == 0);
        require("unsupported ambiguous fields remain explicit", diagnoses.at("unsupported")
                                                                        .at("ambiguous_latest_relations")
                                                                        .at("unsupported_fields")
                                                                        .get<int>() == 130);
        require("condition-only relaxation is counted separately from primary eligibility",
                diagnoses.at("conditions").at("primary_status_counts").at("eligible").get<int>() == 26 &&
                    diagnoses.at("conditions").at("condition_test_removal_only_count").get<int>() == 130);
        require("oversized condition metadata is identified by digest without exporting its bytes",
                diagnoses.at("oversized")
                            .at("metadata_frequencies")
                            .at(0)
                            .at("conditions")
                            .at("representation")
                            .get<std::string>() == "sha256_only_over_512_bytes" &&
                    diagnoses.at("oversized").dump().find(std::string(600, 'A')) == std::string::npos);
        const auto false_source = directory_ / "false_source.json";
        auto altered = basic;
        altered["windows"][0]["status"] = "empty";
        trading::write_new_text_file(false_source, altered.dump(2) + '\n');
        rejects("inspection rejects a summary that differs from its authenticated cache",
                [&] { static_cast<void>(trading::CostQuoteDiagnostics{panel}.inspect(false_source)); });
        return {{"kind", "cost_execution_check"},
                {"evidence_origin", "synthetic"},
                {"network_requests", 0},
                {"historical_strategy_scored", false},
                {"reserved_strategy_inputs_opened", false},
                {"policy_sha256", TRADING_COST_EXECUTION_PROTOCOL_SHA256},
                {"checks", checks_},
                {"cases", cases_},
                {"diagnostic_cases", diagnostic_cases},
                {"rejected_datasets", rejected_},
                {"provider_plan", provider},
                {"synthetic_workloads_registered", matrix.size()},
                {"synthetic_account_cases_registered", matrix.size() * 4},
                {"synthetic_account_cases_executed", 0},
                {"source_sha256", TRADING_SOURCE_SHA256},
                {"compiler", TRADING_COMPILER_ID},
                {"build_configuration", TRADING_BUILD_CONFIGURATION}};
    }

  private:
    trading::MarketDataResponse response(const std::string &mode,
                                         const trading::HistoricalTickRequest &request,
                                         const std::optional<std::string> &token, std::size_t calls) {
        if (mode == "forbidden") {
            return {403, "{}", {}};
        }
        if (mode == "retry" && calls == 1) {
            return {429, "{}", std::string{"0"}};
        }
        Json rows = Json::array();
        auto q = Json{{"t", trading::format_utc_seconds(token ? request.end() : request.start())},
                      {"bp", token ? 100.01 : 100.0},
                      {"ap", token ? 100.03 : 100.02},
                      {"bs", token ? 2 : 1},
                      {"as", token ? 3 : 2},
                      {"bx", "P"},
                      {"ax", "P"},
                      {"c", Json::array({"R"})},
                      {"z", "B"}};
        if (mode == "future") {
            q["t"] = trading::format_utc_seconds(request.end() + std::chrono::seconds{1});
        }
        if (mode == "nonregular" && token) {
            q["c"] = Json::array({"N"});
        }
        if (mode == "oversized" && token) {
            q["c"] = Json::array({std::string(600, 'A')});
        }
        if (mode == "conditions" && token) {
            if (request.symbol() == "SPY") {
                q["c"] = Json::array({""});
            }
            if (request.symbol() == "QQQ") {
                q["c"] = Json::array();
            }
            if (request.symbol() == "IWM") {
                q["c"] = Json::array({"R", "R"});
            }
            if (request.symbol() == "TLT") {
                q["c"] = Json::array({"R", "N"});
            }
            if (request.symbol() == "GLD") {
                q["c"] = nullptr;
            }
        }
        if (mode == "geometry" && token) {
            if (request.symbol() == "SPY") {
                q["ap"] = q.at("bp");
            }
            if (request.symbol() == "QQQ") {
                q["ap"] = 99.0;
            }
            if (request.symbol() == "IWM") {
                q["bp"] = 0;
            }
            if (request.symbol() == "TLT") {
                q["bs"] = 0;
            }
            if (request.symbol() == "GLD") {
                q["bx"] = "";
            }
        }
        if (mode == "nanoseconds" && token) {
            auto stamp = trading::format_utc_seconds(request.start());
            stamp.pop_back();
            q["t"] = stamp + ".123456789Z";
        }
        if (mode != "empty" && !(mode == "empty_page" && !token)) {
            rows.push_back(q);
        }
        if (mode == "ambiguous" && token) {
            q["bp"] = 100.02;
            rows.push_back(q);
        }
        if (token && (mode == "metadata" || mode == "size" || mode == "unsupported")) {
            if (mode == "metadata") {
                q["ax"] = "Q";
            }
            if (mode == "size") {
                q["as"] = 4;
            }
            if (mode == "unsupported") {
                q.erase("c");
            }
            rows.push_back(q);
        }
        if (mode == "duplicate" && token) {
            rows.push_back(q);
        }
        Json next = token ? Json(nullptr) : Json("next");
        if (mode == "empty") {
            next = nullptr;
        }
        if (mode == "capped" && request.symbol() == "SPY" &&
            request.end() == trading::parse_utc_seconds("2022-01-04T15:01:00Z")) {
            const int page = token ? std::stoi(token->substr(1)) : 0;
            next = "p" + std::to_string(page + 1);
            q["t"] = trading::format_utc_seconds(request.end());
            rows = Json::array({q});
        }
        return {200, Json{{"quotes", {{request.symbol(), rows}}}, {"next_page_token", next}}.dump(), {}};
    }
    Json sample(const std::string &name, const std::string &mode, const trading::CostQuotePlan &plan) {
        std::size_t calls = 0;
        auto result = trading::CostQuoteSample{directory_ / name / "archive", plan}.run(
            true, [&](const auto &r, const auto &token) { return response(mode, r, token, ++calls); },
            [](auto) {});
        save(name, result);
        return result;
    }
    void save(const std::string &name, const Json &result) {
        trading::CostQuoteReport{}.write(directory_ / name, result);
        cases_.push_back(name + "/summary.json");
    }
    std::filesystem::path directory_;
    std::vector<std::string> checks_;
    Json cases_ = Json::array(), rejected_ = Json::array();
};
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc > 2) {
            throw std::invalid_argument("usage: cost_execution_check [registered-panel-directory]");
        }
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        const auto output = trading::create_unique_directory(root / "scratch/runs", "cost_execution_check");
        std::cout
            << "M6 | constructed quote pagination, eligibility and immutable resume\nEvidence directory: "
            << output.string() << std::endl;
        const auto summary = Checks{output}.run(
            argc == 2 ? std::filesystem::path{argv[1]} : root / "scratch/datasets/extension_panel_2016_2025");
        trading::write_new_text_file(output / "summary.json", summary.dump(2) + '\n');
        std::cout << "Checks passed: " << summary.at("checks").size()
                  << "\nConstructed sample cases: " << summary.at("cases").size()
                  << "\nNetwork requests sent: 0\nSynthetic execution cases registered: 680 | executed: "
                     "0\nManifest: "
                  << (output / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Cost execution checks stopped: " << e.what() << '\n';
        return 1;
    }
}
