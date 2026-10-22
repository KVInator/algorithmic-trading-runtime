#include "development_dataset.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include "session_storage.hpp"
#include "utc_timestamp.hpp"

#include <iterator>
#include <stdexcept>
#include <utility>

namespace trading {

std::vector<DevelopmentMonth> development_months() {
    std::vector<DevelopmentMonth> result;
    for (int year = 2022; year <= 2023; ++year) {
        for (unsigned int month = 1; month <= 12; ++month) {
            const auto first = std::chrono::sys_days{std::chrono::year{year} / std::chrono::month{month} / 1};
            const auto last = std::chrono::sys_days{std::chrono::year{year} / std::chrono::month{month} / std::chrono::last};
            const auto start = format_utc_seconds(first).substr(0, 10);
            result.push_back({start.substr(0, 7), {start, format_utc_seconds(last).substr(0, 10)}});
        }
    }
    return result;
}

namespace {

using Json = nlohmann::json;

SessionBatch month_batch(const DevelopmentMonth& month, const std::filesystem::path& root) {
    return {month.range, development_calendar(month.range).size(), contained_path(root, "months/" + month.id),
            month.id == "2022-03" ? BatchPurpose::engineering_pilot : BatchPurpose::development};
}

std::vector<SessionExclusion> expected_exclusions() {
    return {{"2022-11-25", "shortened calendar session"}, {"2023-01-03", "diagnostic fixture"},
            {"2023-07-03", "shortened calendar session"}, {"2023-11-24", "shortened calendar session"}};
}

void check_month(const DevelopmentMonth& month, const FrozenSessionBatch& batch) {
    std::size_t index = 0;
    std::vector<std::string> excluded;
    for (const auto& session : development_calendar(month.range)) {
        if (!session.regular() || session.date() == "2023-01-03") { excluded.push_back(session.date()); continue; }
        if (index >= batch.sessions.size() || batch.sessions[index].request.start() != session.open_utc() ||
            batch.sessions[index].request.end_exclusive() != session.close_utc()) {
            throw std::invalid_argument("month does not match the complete scored development date list: " + month.id);
        }
        ++index;
    }
    if (index != batch.sessions.size() || excluded != batch.excluded_dates) {
        throw std::invalid_argument("development month has unexpected eligible/excluded dates: " + month.id);
    }
}

Json manifest_record(const std::filesystem::path& root, const std::string& march_digest) {
    Json records = Json::array();
    for (const auto& month : development_months()) {
        const auto file = "months/" + month.id + "/manifest.json";
        records.push_back({{"month", month.id}, {"file", file},
            {"sha256", sha256_file(contained_path(root, file))},
            {"origin", month.id == "2022-03" ? "frozen_engineering_pilot" : "development_acquisition"}});
    }
    Json exclusions = Json::array();
    for (const auto& entry : expected_exclusions()) { exclusions.push_back({{"date", entry.date}, {"reason", entry.reason}}); }
    return {{"schema_version", 1}, {"protocol", "SPY-IM-01"}, {"purpose", "development"},
            {"start_date", "2022-01-01"}, {"end_date", "2023-12-31"}, {"symbol", "SPY"},
            {"timeframe", "1Min"}, {"feed", "sip"}, {"adjustment", "raw"}, {"currency", "USD"},
            {"asof", "-"}, {"sort", "asc"}, {"calendar_reference", "NYSE_2022_2023_v1"},
            {"calendar_sessions", 501}, {"scored_sessions", 497}, {"validated_scored_bars", 193830},
            {"frozen_march_manifest_sha256", march_digest},
            {"unresolved_sessions", 0}, {"months", std::move(records)}, {"exclusions", std::move(exclusions)}};
}

} // namespace

std::vector<SessionExclusion> development_calendar_exclusions() { return expected_exclusions(); }

DevelopmentDataset::DevelopmentDataset(std::filesystem::path directory, std::filesystem::path frozen_march,
                                       std::string expected_march_sha256)
    : directory_{std::move(directory)}, frozen_march_{std::move(frozen_march)},
      expected_march_sha256_{std::move(expected_march_sha256)} {
    if (directory_.empty() || frozen_march_.empty()) {
        throw std::invalid_argument("development dataset requires output and frozen March paths");
    }
    if (expected_march_sha256_.size() != 64 || expected_march_sha256_.find_first_not_of("0123456789abcdef") != std::string::npos) {
        throw std::invalid_argument("expected March digest requires 64 lowercase hexadecimal digits");
    }
}

FrozenDevelopmentDataset DevelopmentDataset::load() const {
    const auto manifest = contained_path(directory_, "manifest.json");
    if (!std::filesystem::is_regular_file(manifest)) {
        throw std::runtime_error("development acquisition is incomplete: the final dataset manifest is absent. "
            "Review the unresolved-session inventory or first failure reported by alpaca_development. "
            "Existing completed checkpoints are reused; performance evaluation remains blocked.");
    }
    const auto text = read_text_file(manifest);
    if (parse_json_document(text) != manifest_record(directory_, expected_march_sha256_)) {
        throw std::invalid_argument("development manifest conventions, counts or monthly hashes disagree");
    }
    FrozenDevelopmentDataset result{directory_, sha256(text), {}, {}, expected_exclusions()};
    for (const auto& month : development_months()) {
        auto batch = month_batch(month, directory_).load();
        check_month(month, batch);
        if (month.id == "2022-03") {
            if (batch.manifest_sha256 != expected_march_sha256_) { throw std::invalid_argument("March manifest differs from the frozen pilot identity"); }
            result.march_manifest_sha256 = batch.manifest_sha256;
        }
        result.sessions.insert(result.sessions.end(), std::make_move_iterator(batch.sessions.begin()),
                               std::make_move_iterator(batch.sessions.end()));
    }
    if (result.sessions.size() != 497) { throw std::invalid_argument("development dataset requires 497 scored sessions"); }
    return result;
}

FrozenDevelopmentDataset DevelopmentDataset::acquire(const SessionBatch::CalendarFetcher& calendar_fetcher,
                                                     const BarPageFetcher& page_fetcher,
                                                     const SessionBatch::Progress& progress) const {
    static_cast<void>(acquire_months(calendar_fetcher, page_fetcher, progress, false));
    return load();
}

DevelopmentCollection DevelopmentDataset::acquire_months(const SessionBatch::CalendarFetcher& calendar_fetcher,
                                                         const BarPageFetcher& page_fetcher,
                                                         const SessionBatch::Progress& progress,
                                                         bool retain_gaps) const {
    if (std::filesystem::exists(contained_path(directory_, "manifest.json"))) { return {load().sessions.size(), {}, {}}; }
    DevelopmentCollection result{0, {}, {}};
    const auto plan = development_months();
    const auto march = month_batch(plan.at(2), directory_);
    if (!std::filesystem::exists(contained_path(directory_, "months/2022-03/manifest.json"))) {
        if (sha256_file(contained_path(frozen_march_, "manifest.json")) != expected_march_sha256_) {
            throw std::invalid_argument("March source manifest differs from the frozen pilot identity");
        }
        if (progress) { progress("Importing verified frozen March 2022 files"); }
        const auto imported = march.import_from(frozen_march_);
        check_month(plan.at(2), imported);
        if (progress) { progress("March manifest preserved: " + imported.manifest_sha256); }
    }
    for (const auto& month : plan) {
        if (progress) { progress("Month " + month.id); }
        const auto batch = month_batch(month, directory_);
        bool counted = false;
        if (retain_gaps && !std::filesystem::exists(contained_path(directory_, "months/" + month.id + "/manifest.json"))) {
            auto collected = batch.collect(calendar_fetcher, page_fetcher, progress);
            result.complete_sessions += collected.complete_sessions;
            counted = true;
            result.unresolved.insert(result.unresolved.end(),
                std::make_move_iterator(collected.unresolved.begin()), std::make_move_iterator(collected.unresolved.end()));
            if (!collected.unresolved.empty()) { continue; }
        }
        const auto frozen = retain_gaps ? batch.load() : batch.acquire(calendar_fetcher, page_fetcher, progress);
        check_month(month, frozen);
        if (!counted) { result.complete_sessions += frozen.sessions.size(); }
        if (month.id == "2022-03" && frozen.manifest_sha256 != expected_march_sha256_) {
            throw std::invalid_argument("imported March manifest differs from the frozen pilot identity");
        }
    }
    if (result.complete_sessions + result.unresolved.size() != 497) {
        throw std::invalid_argument("development collection does not cover all 497 planned eligible sessions");
    }
    if (result.unresolved.empty()) {
        write_new_text_file(contained_path(directory_, "manifest.json"), manifest_record(directory_, expected_march_sha256_).dump(2) + '\n');
    }
    return result;
}

DevelopmentCollection DevelopmentDataset::collect(const SessionBatch::CalendarFetcher& calendar_fetcher,
                                                  const BarPageFetcher& page_fetcher,
                                                  const std::filesystem::path& report_root,
                                                  const SessionBatch::Progress& progress) const {
    if (report_root.empty()) { throw std::invalid_argument("development collection audit requires an output root"); }
    auto result = acquire_months(calendar_fetcher, page_fetcher, progress, true);
    const auto audit = create_unique_directory(report_root, "SPY_development_acquisition_audit");
    Json unresolved = Json::array();
    Json completed_months = Json::array();
    Json checkpoints = Json::array();
    for (const auto& month : development_months()) {
        const auto month_root = contained_path(directory_, "months/" + month.id);
        const auto manifest = month_root / "manifest.json";
        if (std::filesystem::is_regular_file(manifest)) {
            completed_months.push_back({{"month", month.id}, {"manifest_sha256", sha256_file(manifest)}});
        }
        for (const auto& session : development_calendar(month.range)) {
            if (!session.regular() || session.date() == "2023-01-03") { continue; }
            const auto checkpoint = month_root / "checkpoints" / (session.date() + ".json");
            if (std::filesystem::is_regular_file(checkpoint)) {
                checkpoints.push_back({{"date", session.date()},
                    {"file", std::filesystem::relative(checkpoint, directory_).generic_string()},
                    {"sha256", sha256_file(checkpoint)}});
            }
        }
    }
    for (const auto& session : result.unresolved) {
        Json missing = Json::array();
        for (const auto time : session.audit.missing_minutes) { missing.push_back(format_utc_seconds(time)); }
        Json files = Json::array();
        const auto saved = load_session_download(session.directory);
        auto add = [&](const std::string& file) { files.push_back({{"file", file}, {"sha256", sha256_file(session.directory / file)}}); };
        add("metadata.json");
        add("bars.csv");
        for (std::size_t page = 0; page < saved.pages.size(); ++page) { add(session_page_filename(page)); }
        unresolved.push_back({{"date", session.date},
            {"session_directory", std::filesystem::relative(session.directory, directory_).generic_string()},
            {"expected_minutes", session.audit.expected_minutes}, {"observed_bars", session.audit.observed_bars},
            {"missing_minutes_utc", std::move(missing)}, {"files", std::move(files)}});
    }
    if (checkpoints.size() != result.complete_sessions) {
        throw std::runtime_error("checkpoint inventory changed during development collection");
    }
    write_new_text_file(audit / "summary.json", Json{
        {"schema_version", 1}, {"purpose", "acquisition audit; not a performance input"},
        {"dataset_directory", directory_.generic_string()}, {"protocol", "SPY-IM-01"},
        {"planned_eligible_sessions", 497}, {"complete_sessions", result.complete_sessions},
        {"unresolved_sessions", result.unresolved.size()}, {"unresolved", std::move(unresolved)},
        {"completed_months", std::move(completed_months)}, {"checkpoints", std::move(checkpoints)},
        {"frozen_march_manifest_sha256", expected_march_sha256_},
        {"performance_ready", result.unresolved.empty()}, {"data_exclusions_added", 0}}.dump(2) + '\n');
    result.audit_directory = audit;
    return result;
}

} // namespace trading
