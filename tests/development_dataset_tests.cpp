#include "development_study.hpp"
#include "development_study_dataset.hpp"
#include "session_gap_probe.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include "session_storage.hpp"
#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

using Json = nlohmann::json;

void require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }
template <typename Operation> void rejects(Operation operation, const char* message) {
    try { operation(); } catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}

trading::MarketDataResponse calendar_response(const trading::CalendarRange& range) {
    Json records = Json::array();
    for (const auto& session : trading::development_calendar(range)) {
        records.push_back({{"date", session.date()}, {"open", "09:30"}, {"close", session.regular() ? "16:00" : "13:00"}});
    }
    return {200, records.dump()};
}

trading::MarketDataResponse synthetic_page(const trading::HistoricalBarRequest& request,
                                          const std::optional<std::string>& token) {
    const auto begin = token ? std::stoi(*token) : 0;
    const auto end = std::min(begin + 100, 390);
    const auto date = trading::format_utc_seconds(request.start()).substr(0, 10);
    const bool active = (trading::parse_session_date(date).time_since_epoch().count() % 2) == 0;
    Json bars = Json::array();
    for (int minute = begin; minute < end; ++minute) {
        double open = 100;
        if (minute == 386) { open = active ? 100.1 : 99.9; }
        if (minute == 387) { open = active ? 100.08 : 99.9; }
        const double close = minute == 359 ? (active ? 101 : 99) : 100;
        bars.push_back({{"t", trading::format_utc_seconds(request.start() + std::chrono::minutes{minute})},
            {"o", open}, {"h", std::max(open, close) + 1}, {"l", std::min(open, close) - 1}, {"c", close}, {"v", 1000}});
    }
    return {200, Json{{"bars", {{"SPY", bars}}},
                      {"next_page_token", end < 390 ? Json(std::to_string(end)) : Json(nullptr)}}.dump()};
}

void test_calendar_reference() {
    // Independent published monthly session totals, including early closes.
    const std::size_t expected[]{20, 19, 23, 20, 21, 21, 20, 23, 21, 21, 21, 21,
                                 20, 19, 23, 19, 22, 21, 20, 23, 20, 22, 21, 20};
    std::size_t all = 0;
    std::size_t scored = 0;
    for (int year = 2022; year <= 2023; ++year) {
        for (unsigned int month = 1; month <= 12; ++month) {
            const auto first = std::chrono::sys_days{std::chrono::year{year} / std::chrono::month{month} / 1};
            const auto last = std::chrono::sys_days{std::chrono::year{year} / std::chrono::month{month} / std::chrono::last};
            const trading::CalendarRange range{trading::format_utc_seconds(first).substr(0, 10),
                                                trading::format_utc_seconds(last).substr(0, 10)};
            const auto records = trading::development_calendar(range);
            const auto index = static_cast<std::size_t>(year - 2022) * 12 + month - 1;
            require(records.size() == expected[index], "published monthly session count mismatch");
            all += records.size();
            for (const auto& record : records) { scored += record.regular() && record.date() != "2023-01-03" ? 1U : 0U; }
        }
    }
    require(all == 501 && scored == 497, "full development counts mismatch");
    rejects([] { static_cast<void>(trading::development_calendar({"2024-01-01", "2024-01-31"})); },
            "development calendar must not expose the validation year");
}

void test_workflow(const std::filesystem::path& output) {
    const auto project = output / "synthetic-project";
    const auto datasets = project / "scratch/datasets";
    const auto march_path = datasets / "SPY_1Min_SIP_2022-03";
    const auto march = trading::SessionBatch{{"2022-03-01", "2022-03-31"}, 23, march_path}.acquire(calendar_response, synthetic_page);
    const auto march_bytes = trading::read_text_file(march_path / "manifest.json");
    const auto root = datasets / "SPY_1Min_SIP_development_2022-2023";
    const trading::DevelopmentDataset dataset{root, march_path, march.manifest_sha256};
    std::size_t calendar_calls = 0;
    std::size_t first_day_calls = 0;
    const auto fetch_calendar = [&](const trading::CalendarRange& range) {
        ++calendar_calls;
        require(!range.first().starts_with("2022-03"), "frozen March calendar was requested again");
        require(range.last() <= "2023-12-31", "acquisition accessed a held-out period");
        return calendar_response(range);
    };
    rejects([&] { static_cast<void>(dataset.acquire(fetch_calendar, [&](const auto& request, const auto& token) {
        const auto date = trading::format_utc_seconds(request.start()).substr(0, 10);
        if (date != "2022-01-03") { throw std::runtime_error("injected interruption after first scored session"); }
        ++first_day_calls;
        return synthetic_page(request, token);
    })); }, "interrupted development acquisition was published");
    require(first_day_calls == 4 && calendar_calls == 1 && !std::filesystem::exists(root / "manifest.json"),
            "interruption checkpoint contract mismatch");
    require(trading::read_text_file(root / "months/2022-03/manifest.json") == march_bytes, "March manifest bytes changed during import");
    const auto collection = dataset.collect(fetch_calendar, [&](const auto& request, const auto& token) {
        const auto date = trading::format_utc_seconds(request.start()).substr(0, 10);
        require(!date.starts_with("2022-03") && date != "2023-01-03" && date <= "2023-12-31",
                "collection must preserve frozen March and the development date boundary");
        auto response = synthetic_page(request, token);
        if (!token && (date == "2022-02-01" || date == "2023-06-05")) {
            auto document = trading::parse_json_document(response.body);
            if (date == "2023-06-05") {
                for (int omitted = 0; omitted < 4; ++omitted) { document["bars"]["SPY"].erase(22); }
            } else { document["bars"]["SPY"].erase(17); }
            response.body = document.dump();
        }
        return response;
    }, project / "scratch/runs");
    require(collection.complete_sessions == 495 && collection.unresolved.size() == 2 &&
            collection.unresolved[0].date == "2022-02-01" && collection.unresolved[1].date == "2023-06-05" &&
            !std::filesystem::exists(root / "manifest.json") && std::filesystem::exists(root / "months/2023-12/manifest.json"),
            "full-period collection must continue across incomplete months and block the final manifest");
    const auto audit = trading::parse_json_document(trading::read_text_file(collection.audit_directory / "summary.json"));
    require(audit.at("complete_sessions") == 495 && audit.at("unresolved_sessions") == 2 &&
            audit.at("completed_months").size() == 22 && audit.at("checkpoints").size() == 495 &&
            audit.at("data_exclusions_added") == 0 && audit.at("performance_ready") == false &&
            audit.at("unresolved").at(0).at("files").size() == 6 &&
            audit.at("frozen_march_manifest_sha256") == march.manifest_sha256,
            "collection audit must bind complete checkpoints, incomplete evidence and March without adding exclusions");
    rejects([&] { static_cast<void>(dataset.load()); }, "an acquisition inventory must not enable performance evaluation");
    const auto repeated = dataset.collect({}, {}, project / "scratch/runs");
    require(repeated.complete_sessions == 495 && repeated.unresolved.size() == 2 && repeated.audit_directory != collection.audit_directory,
            "completed inventory must verify offline without retrying saved data gaps or replacing its report");
    rejects([&] { static_cast<void>(dataset.acquire(fetch_calendar, [&](const auto& request, const auto& token) {
        const auto date = trading::format_utc_seconds(request.start()).substr(0, 10);
        require(date == "2022-02-01" || date == "2023-06-05", "strict repair must request only saved incomplete sessions");
        auto response = synthetic_page(request, token);
        if (date == "2023-06-05" && !token) {
            auto document = trading::parse_json_document(response.body);
            for (int omitted = 0; omitted < 4; ++omitted) { document["bars"]["SPY"].erase(22); }
            response.body = document.dump();
        }
        return response;
    })); }, "persistent June gap must block the original 497-session acquisition");
    const auto pending = dataset.collect({}, {}, project / "scratch/runs");
    require(pending.complete_sessions == 496 && pending.unresolved.size() == 1 && calendar_calls == 23 && first_day_calls == 4,
            "repair must preserve all completed dates and leave only the declared June gap");
    const auto source = trading::load_session_download(pending.unresolved[0].directory);
    const auto probe = trading::SessionGapProbe{pending.unresolved[0].directory}.run(project / "scratch/runs",
        [&](const auto& request, const auto&) {
            Json records = Json::array();
            for (const auto& bar : source.bars) {
                if (bar.start() >= request.start() && bar.start() < request.end_exclusive()) {
                    records.push_back({{"t", trading::format_utc_seconds(bar.start())}, {"o", bar.prices().open},
                        {"h", bar.prices().high}, {"l", bar.prices().low}, {"c", bar.prices().close}, {"v", bar.volume()}});
                }
            }
            return trading::MarketDataResponse{200, Json{{"bars", {{"SPY", records}}}, {"next_page_token", nullptr}}.dump()};
        });
    auto policy = trading::parse_json_document(trading::read_text_file(
        std::filesystem::path{TRADING_PROJECT_ROOT} / "research/development_data_policy.json"));
    policy["acquisition_audit"] = {{"file", std::filesystem::relative(pending.audit_directory / "summary.json", project).generic_string()},
        {"sha256", trading::sha256_file(pending.audit_directory / "summary.json")}};
    policy["gap_probe"] = {{"file", std::filesystem::relative(probe.directory / "summary.json", project).generic_string()},
        {"sha256", trading::sha256_file(probe.directory / "summary.json")}};
    const auto inventory = trading::parse_json_document(trading::read_text_file(pending.audit_directory / "summary.json"));
    policy["frozen_march_manifest_sha256"] = march.manifest_sha256;
    auto exclusion = inventory.at("unresolved").at(0);
    exclusion["session_directory"] = std::filesystem::relative(pending.unresolved[0].directory, project).generic_string();
    exclusion["reason"] = "synthetic coverage failure; full session excluded under protocol revision 2";
    policy["data_exclusions"] = Json::array({exclusion});
    const auto policy_text = policy.dump(2) + '\n';
    trading::write_new_text_file(project / "research/development_data_policy.json", policy_text);
    trading::write_new_text_file(project / "research/intraday_momentum_protocol.md", trading::read_text_file(
        std::filesystem::path{TRADING_PROJECT_ROOT} / "research/intraday_momentum_protocol.md"));
    const trading::DevelopmentStudyDataset frozen_study{project, trading::sha256(policy_text)};
    const auto saved = frozen_study.finalize();
    const auto manifest_bytes = trading::read_text_file(root / "study_manifest.json");
    require(saved.sessions.size() == 496 && saved.exclusions.size() == 5 && saved.protocol_revision == 2 &&
        saved.march_manifest_sha256 == march.manifest_sha256 &&
        trading::read_text_file(march_path / "manifest.json") == march_bytes &&
        !std::filesystem::exists(root / "manifest.json") && frozen_study.finalize().manifest_sha256 == saved.manifest_sha256 &&
        trading::read_text_file(root / "study_manifest.json") == manifest_bytes,
        "finalization must bind the revised sample without changing source evidence or publishing the original collection manifest");
    const auto offline = frozen_study.load();
    require(offline.manifest_sha256 == saved.manifest_sha256, "frozen study must verify offline");
    const auto study = trading::DevelopmentStudy{}.run(offline);
    require(study.cases.size() == 10 && study.primary_bootstrap.size() == 2 &&
        study.statistics[2].calendar_years[0].statistics.sessions == 250 &&
        study.statistics[2].calendar_years[1].statistics.sessions == 246 &&
        study.decision == trading::DevelopmentDecision::eligible_for_unchanged_validation,
        "development risk statistics, annual counts or screening mismatch");
    const auto report = trading::save_development_report(offline, study, project / "scratch/runs");
    const auto summary = trading::parse_json_document(trading::read_text_file(report / "summary.json"));
    require(summary.at("daily_returns_sha256") == trading::sha256_file(report / "daily_returns.csv") &&
            summary.at("input_manifest_sha256") == offline.manifest_sha256 &&
            summary.at("frozen_march_manifest_sha256") == march.manifest_sha256 && summary.at("exclusions").size() == 5,
            "development report provenance mismatch");
    const auto checkpoint = root / "months/2022-03/checkpoints/2022-03-01.json";
    const auto original = trading::read_text_file(checkpoint);
    { std::ofstream file{checkpoint, std::ios::binary | std::ios::app}; file << ' '; }
    rejects([&] { static_cast<void>(frozen_study.load()); }, "changed nested checkpoint passed the full dataset verification");
    { std::ofstream file{checkpoint, std::ios::binary | std::ios::trunc}; file << original; }
    require(frozen_study.load().sessions.size() == 496, "restored dataset no longer verifies");
    { std::ofstream file{project / "research/development_data_policy.json", std::ios::binary | std::ios::app}; file << ' '; }
    rejects([&] { static_cast<void>(frozen_study.load()); }, "changed policy must invalidate the frozen study");
    { std::ofstream file{project / "research/development_data_policy.json", std::ios::binary | std::ios::trunc}; file << policy_text; }
    const auto probe_metadata = probe.directory / "summary.json";
    const auto original_probe = trading::read_text_file(probe_metadata);
    { std::ofstream file{probe_metadata, std::ios::binary | std::ios::app}; file << ' '; }
    rejects([&] { static_cast<void>(frozen_study.load()); }, "changed diagnostic evidence must invalidate the frozen study");
    { std::ofstream file{probe_metadata, std::ios::binary | std::ios::trunc}; file << original_probe; }
    std::cout << "Synthetic full-period project: " << project.string() << '\n';
}

void test_collection(const std::filesystem::path& output) {
    const auto root = output / "coverage-inventory";
    const trading::SessionBatch batch{{"2023-06-01", "2023-06-07"}, 5, root, trading::BatchPurpose::development};
    std::size_t requests = 0;
    const auto collection = batch.collect(calendar_response, [&](const auto& request, const auto& token) {
        ++requests;
        auto response = synthetic_page(request, token);
        const auto date = trading::format_utc_seconds(request.start()).substr(0, 10);
        if (!token && (date == "2023-06-02" || date == "2023-06-05")) {
            auto document = trading::parse_json_document(response.body);
            document["bars"]["SPY"].erase(17);
            response.body = document.dump();
        }
        return response;
    });
    require(requests == 20 && collection.complete_sessions == 3 && collection.unresolved.size() == 2 &&
            collection.unresolved[0].date == "2023-06-02" && collection.unresolved[1].date == "2023-06-05" &&
            collection.unresolved[0].audit.missing_minutes.size() == 1 &&
            std::filesystem::exists(root / "checkpoints/2023-06-07.json") &&
            !std::filesystem::exists(root / "checkpoints/2023-06-02.json") &&
            !std::filesystem::exists(root / "manifest.json"),
            "collection must retain gaps and continue to later sessions without publishing a complete batch");
    const auto original = trading::read_text_file(collection.unresolved[0].directory / "metadata.json");
    const auto reused = batch.collect({}, {});
    require(reused.complete_sessions == 3 && reused.unresolved.size() == 2 &&
            reused.unresolved[0].directory == collection.unresolved[0].directory,
            "repeated collection must verify completed and unresolved evidence without network requests");
    const auto corrupted = collection.unresolved[1].directory / "metadata.json";
    const auto original_metadata = trading::read_text_file(corrupted);
    auto document = trading::parse_json_document(original_metadata);
    document["audit"]["observed_bars"] = 390;
    { std::ofstream file{corrupted, std::ios::binary | std::ios::trunc}; file << document.dump(); }
    rejects([&] { static_cast<void>(batch.collect({}, {})); },
            "corrupt saved evidence must stop collection instead of becoming a coverage gap");
    { std::ofstream file{corrupted, std::ios::binary | std::ios::trunc}; file << original_metadata; }
    const auto repaired = batch.acquire({}, synthetic_page);
    require(repaired.sessions.size() == 5 &&
            trading::read_text_file(collection.unresolved[0].directory / "metadata.json") == original &&
            batch.collect({}, {}).complete_sessions == 5,
            "strict acquisition must still support same-source repair and retain prior gap evidence");

    const auto failed_root = output / "transport-failure";
    const trading::SessionBatch failed{{"2023-06-01", "2023-06-07"}, 5, failed_root, trading::BatchPurpose::development};
    requests = 0;
    rejects([&] { static_cast<void>(failed.collect(calendar_response, [&](const auto& request, const auto& token) {
        const auto date = trading::format_utc_seconds(request.start()).substr(0, 10);
        require(date <= "2023-06-02", "HTTP failures must stop further collection");
        ++requests;
        return date == "2023-06-02" ? trading::MarketDataResponse{403, "forbidden"} : synthetic_page(request, token);
    })); }, "HTTP failure must not be treated as a saved coverage gap");
    require(requests == 5 && std::filesystem::exists(failed_root / "checkpoints/2023-06-01.json") &&
            !std::filesystem::exists(failed_root / "manifest.json"),
            "transport failure must preserve earlier checkpoints and prevent publication");
}

void test_import_and_calendar_errors(const std::filesystem::path& output) {
    const auto source = output / "synthetic-project/scratch/datasets/SPY_1Min_SIP_2022-03";
    rejects([&] { static_cast<void>(trading::DevelopmentDataset{output / "wrong-frozen-identity", source}.acquire({}, {})); },
            "a different valid March snapshot must not substitute for the frozen production identity");
    const auto partial = output / "partial-import";
    trading::write_new_text_file(partial / "calendar_snapshot.json", trading::read_text_file(source / "calendar_snapshot.json"));
    require(trading::SessionBatch{{"2022-03-01", "2022-03-31"}, 23, partial}.import_from(source).sessions.size() == 23,
            "partial frozen import did not resume");
    const auto invalid = output / "conflicting-import";
    trading::write_new_text_file(invalid / "calendar_snapshot.json", "{}");
    rejects([&] { static_cast<void>(trading::SessionBatch{{"2022-03-01", "2022-03-31"}, 23, invalid}.import_from(source)); },
            "conflicting partial import was overwritten");
    const trading::SessionBatch july{{"2023-07-01", "2023-07-31"}, 20, output / "wrong-calendar", trading::BatchPurpose::development};
    rejects([&] { static_cast<void>(july.acquire([](const auto& range) {
        auto records = trading::parse_json_document(calendar_response(range).body);
        records[0]["close"] = "16:00"; // July 3 must close at 13:00.
        return trading::MarketDataResponse{200, records.dump()};
    }, {})); }, "calendar with the right count and wrong early-close hours was accepted");
    require(!std::filesystem::exists(output / "wrong-calendar/calendar_snapshot.json"), "invalid calendar was committed");
}

void test_small_workflow(const std::filesystem::path& output) {
    // Three sessions cover checkpoint preservation, a real grid failure and a
    // successful retry without constructing the full two-year disk fixture.
    const auto root = output / "resumable-fragment";
    const trading::SessionBatch batch{{"2023-06-01", "2023-06-05"}, 3, root, trading::BatchPurpose::development};
    std::size_t good_calls = 0;
    std::string failure;
    try {
        static_cast<void>(batch.acquire(calendar_response, [&](const auto& request, const auto& token) {
            auto response = synthetic_page(request, token);
            const auto date = trading::format_utc_seconds(request.start()).substr(0, 10);
            if (date != "2023-06-05") { ++good_calls; }
            else if (!token) {
                auto page = trading::parse_json_document(response.body);
                page["bars"]["SPY"].erase(17);
                response.body = page.dump();
            }
            return response;
        }));
    } catch (const std::runtime_error& error) { failure = error.what(); }
    require(failure.find("389/390 validated bars; 1 missing minute slots") != std::string::npos &&
            failure.find("2023-06-05T13:47:00Z") != std::string::npos &&
            failure.find("metadata.json and bars.csv") != std::string::npos,
            "gap diagnostics must identify the exact missing UTC start, coverage and evidence files");
    require(good_calls == 8 && !std::filesystem::exists(root / "manifest.json"),
            "missing data must preserve earlier days and block publication");
    const auto first_checkpoint = trading::read_text_file(root / "checkpoints/2023-06-01.json");
    const trading::HistoricalBarRequest failed_request = trading::CalendarSession{"2023-06-05", "09:30", "16:00"}.bar_request();
    const auto failed_run = trading::find_latest_session_run(root / "sessions", failed_request);
    require(failed_run.has_value(), "incomplete session evidence was not saved");
    const auto retained = trading::read_text_file(*failed_run / "metadata.json");
    require(trading::load_session_download(*failed_run, failed_request).audit.missing_minutes.size() == 1,
            "retained evidence must reconstruct the missing grid");
    const auto complete = batch.acquire({}, [&](const auto& request, const auto& token) {
        if (trading::format_utc_seconds(request.start()).substr(0, 10) != "2023-06-05") { ++good_calls; }
        return synthetic_page(request, token);
    });
    require(complete.sessions.size() == 3 && good_calls == 8 &&
            trading::read_text_file(root / "checkpoints/2023-06-01.json") == first_checkpoint &&
            trading::read_text_file(*failed_run / "metadata.json") == retained,
            "retry must reuse completed checkpoints and preserve original gap evidence");
    require(batch.acquire({}, {}).manifest_sha256 == complete.manifest_sha256, "complete fragment must verify offline");

    const auto imported_root = output / "partial-import";
    trading::write_new_text_file(imported_root / "calendar_snapshot.json", trading::read_text_file(root / "calendar_snapshot.json"));
    const auto imported = trading::SessionBatch{{"2023-06-01", "2023-06-05"}, 3, imported_root,
        trading::BatchPurpose::development}.import_from(root);
    require(imported.manifest_sha256 == complete.manifest_sha256, "partial import must preserve source manifest bytes");
    const auto conflict_root = output / "conflicting-import";
    trading::write_new_text_file(conflict_root / "calendar_snapshot.json", "{}");
    rejects([&] { static_cast<void>(trading::SessionBatch{{"2023-06-01", "2023-06-05"}, 3, conflict_root,
        trading::BatchPurpose::development}.import_from(root)); }, "conflicting import was overwritten");

    const trading::SessionBatch january{{"2023-01-03", "2023-01-04"}, 2, output / "diagnostic-exclusion", trading::BatchPurpose::development};
    const auto eligible = january.acquire(calendar_response, [](const auto& request, const auto& token) {
        require(trading::format_utc_seconds(request.start()).starts_with("2023-01-04"), "diagnostic fixture was requested");
        return synthetic_page(request, token);
    });
    require(eligible.sessions.size() == 1 && eligible.excluded_dates == std::vector<std::string>{"2023-01-03"},
            "diagnostic exclusion must be logged before bar requests");

    const trading::DevelopmentDataset unfinished{output / "unfinished", output / "missing-March"};
    failure.clear();
    try { static_cast<void>(unfinished.load()); }
    catch (const std::runtime_error& error) { failure = error.what(); }
    require(failure.find("acquisition is incomplete") != std::string::npos && failure.find("alpaca_development") != std::string::npos,
            "replay must explain acquisition readiness instead of exposing a file_size failure");
    rejects([&] { static_cast<void>(unfinished.acquire({}, {})); }, "missing frozen source must fail before HTTP access");
    rejects([&] { static_cast<void>(trading::DevelopmentStudy{}.run(
        trading::FrozenDevelopmentDataset{root, complete.manifest_sha256, {}, complete.sessions, {}})); },
        "a small complete fragment must not count as a complete development study");
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc < 2 || argc > 3 || (argc == 3 && std::string_view{argv[2]} != "--full")) {
            throw std::invalid_argument("usage: development_dataset_tests <artifact root> [--full]");
        }
        const auto output = trading::create_unique_directory(argv[1], "contracts");
        test_calendar_reference();
        if (argc == 3) { test_workflow(output); test_import_and_calendar_errors(output); }
        else { test_small_workflow(output); test_collection(output); }
        std::cout << "Development dataset contracts passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "Development test failed: " << error.what() << '\n'; return 1; }
}
