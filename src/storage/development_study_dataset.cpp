#include "development_study_dataset.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include "session_storage.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {

using Json = nlohmann::json;
constexpr std::string_view collection_directory = "scratch/datasets/SPY_1Min_SIP_development_2022-2023";
constexpr std::string_view policy_file = "research/development_data_policy.json";
constexpr std::string_view protocol_file = "research/intraday_momentum_protocol.md";

Json checked_record(const std::filesystem::path& root, const Json& reference) {
    const auto file = reference.at("file").get<std::string>();
    const auto text = read_text_file(contained_path(root, file));
    if (sha256(text) != reference.at("sha256").get<std::string>()) { throw std::invalid_argument("study evidence digest mismatch: " + file); }
    return parse_json_document(text);
}

Json time_records(const std::vector<std::chrono::sys_seconds>& times) {
    auto result = Json::array();
    for (const auto time : times) { result.push_back(format_utc_seconds(time)); }
    return result;
}

void verify_probe(const std::filesystem::path& project, const Json& policy, const Json& exclusion,
                  const SessionDownload& source) {
    const auto probe = checked_record(project, policy.at("gap_probe"));
    const auto root = contained_path(project, policy.at("gap_probe").at("file").get<std::string>()).parent_path();
    const auto origin_text = read_text_file(root / "source.json");
    const auto origin = parse_json_document(origin_text);
    if (sha256(origin_text) != probe.at("source_record_sha256").get<std::string>() ||
        probe.at("dataset_modified") != false || !probe.at("candidate_minutes_found").empty() ||
        !probe.at("conflicting_minutes_utc").empty() || probe.at("missing_minutes") != source.audit.missing_minutes.size() ||
        origin.at("missing_minutes_utc") != exclusion.at("missing_minutes_utc")) {
        throw std::invalid_argument("gap probe does not establish the declared unresolved exclusion");
    }
    const auto source_root = contained_path(project, exclusion.at("session_directory").get<std::string>());
    if (origin.at("metadata_sha256") != sha256_file(source_root / "metadata.json") ||
        origin.at("bars_sha256") != sha256_file(source_root / "bars.csv")) {
        throw std::invalid_argument("gap probe references a different source download");
    }
    std::set<std::chrono::sys_seconds> individually_probed;
    std::size_t context_checks = 0;
    if (probe.at("probe_windows") != probe.at("probes").size()) { throw std::invalid_argument("gap probe count mismatch"); }
    for (const auto& record : probe.at("probes")) {
        const auto directory = contained_path(root, record.at("directory").get<std::string>());
        if (sha256_file(directory / "metadata.json") != record.at("metadata_sha256").get<std::string>()) {
            throw std::invalid_argument("gap probe metadata changed");
        }
        const auto download = load_session_download(directory);
        if (download.request.symbol() != source.request.symbol() || download.request.start() < source.request.start() ||
            download.request.end_exclusive() > source.request.end_exclusive()) {
            throw std::invalid_argument("gap probe falls outside the excluded session");
        }
        if (record.at("kind") == "minute") {
            if (download.request.expected_minutes() != 1 || !download.bars.empty() ||
                !individually_probed.insert(download.request.start()).second) {
                throw std::invalid_argument("individual missing-minute probe is inconsistent");
            }
        } else if (record.at("kind") == "context") {
            std::size_t matched = 0;
            for (const auto& bar : download.bars) {
                const auto original = std::find_if(source.bars.begin(), source.bars.end(),
                    [&](const auto& value) { return value.start() == bar.start(); });
                if (original == source.bars.end() || original->prices().open != bar.prices().open ||
                    original->prices().high != bar.prices().high || original->prices().low != bar.prices().low ||
                    original->prices().close != bar.prices().close || original->volume() != bar.volume()) {
                    throw std::invalid_argument("context probe conflicts with the excluded source");
                }
                ++matched;
            }
            if (matched < 2) { throw std::invalid_argument("context probe lacks matching neighbouring bars"); }
            ++context_checks;
        } else { throw std::invalid_argument("unknown gap probe kind"); }
    }
    const std::set<std::chrono::sys_seconds> missing{source.audit.missing_minutes.begin(), source.audit.missing_minutes.end()};
    if (individually_probed != missing || context_checks == 0) { throw std::invalid_argument("gap probes do not cover every missing minute"); }
}

} // namespace

struct DevelopmentStudyDataset::VerifiedInput {
    FrozenDevelopmentDataset dataset;
    Json manifest;
};

DevelopmentStudyDataset::DevelopmentStudyDataset(std::filesystem::path project_root,
                                                 std::optional<std::string> expected_policy_sha256)
    : project_root_{std::move(project_root)},
      expected_policy_sha256_{expected_policy_sha256.value_or(TRADING_DATA_POLICY_SHA256)} {
    if (project_root_.empty() || expected_policy_sha256_.size() != 64 ||
        expected_policy_sha256_.find_first_not_of("0123456789abcdef") != std::string::npos) {
        throw std::invalid_argument("study dataset requires a project root and a SHA-256 policy identity");
    }
}

DevelopmentStudyDataset::VerifiedInput DevelopmentStudyDataset::verify(const Progress& progress) const {
    if (progress) { progress("Verifying data policy and acquisition audit"); }
    const auto policy_text = read_text_file(contained_path(project_root_, policy_file));
    if (sha256(policy_text) != expected_policy_sha256_) { throw std::invalid_argument("data policy changed; rebuild or restore the declared policy"); }
    const auto policy = parse_json_document(policy_text);
    if (policy.at("schema_version") != 1 || policy.at("protocol") != "SPY-IM-01" || policy.at("protocol_revision") != 2 ||
        !policy.at("data_exclusions").is_array() || policy.at("data_exclusions").size() != 1) {
        throw std::invalid_argument("protocol revision 2 requires exactly one declared development data exclusion");
    }
    const auto audit = checked_record(project_root_, policy.at("acquisition_audit"));
    if (audit.at("planned_eligible_sessions") != 497 || audit.at("complete_sessions") != 496 ||
        audit.at("unresolved_sessions") != 1 || audit.at("unresolved").size() != 1 ||
        audit.at("performance_ready") != false || audit.at("data_exclusions_added") != 0 ||
        audit.at("frozen_march_manifest_sha256") != policy.at("frozen_march_manifest_sha256")) {
        throw std::invalid_argument("acquisition inventory does not match the declared revision-2 sample");
    }
    const auto data_root = contained_path(project_root_, collection_directory);
    if (progress) { progress("Verifying excluded-session evidence and gap probes"); }
    const auto& exclusion = policy.at("data_exclusions").at(0);
    const auto excluded_date = exclusion.at("date").get<std::string>();
    const CalendarSession excluded_session{excluded_date, "09:30", "16:00"};
    if (excluded_date < "2022-01-01" || excluded_date > "2023-12-31") { throw std::invalid_argument("data exclusion is outside development"); }
    const auto source_root = contained_path(project_root_, exclusion.at("session_directory").get<std::string>());
    const auto source = load_session_download(source_root, excluded_session.bar_request());
    if (source.audit.complete() || source.audit.expected_minutes != exclusion.at("expected_minutes") ||
        source.audit.observed_bars != exclusion.at("observed_bars") ||
        time_records(source.audit.missing_minutes) != exclusion.at("missing_minutes_utc") ||
        audit.at("unresolved").at(0).at("date") != excluded_date ||
        audit.at("unresolved").at(0).at("missing_minutes_utc") != exclusion.at("missing_minutes_utc")) {
        throw std::invalid_argument("excluded source disagrees with the recorded coverage failure");
    }
    std::set<std::string> expected_files{"metadata.json", "bars.csv"};
    for (std::size_t i = 0; i < source.pages.size(); ++i) { expected_files.insert(session_page_filename(i)); }
    std::set<std::string> observed_files;
    for (const auto& file : exclusion.at("files")) {
        const auto name = file.at("file").get<std::string>();
        if (!observed_files.insert(name).second || sha256_file(contained_path(source_root, name)) != file.at("sha256").get<std::string>()) {
            throw std::invalid_argument("excluded source file digest mismatch");
        }
    }
    if (observed_files != expected_files) { throw std::invalid_argument("excluded source file list is incomplete"); }
    verify_probe(project_root_, policy, exclusion, source);

    FrozenDevelopmentDataset dataset{data_root, {}, policy.at("frozen_march_manifest_sha256").get<std::string>(), {},
        development_calendar_exclusions(), "study_manifest.json", expected_policy_sha256_,
        sha256_file(contained_path(project_root_, protocol_file)), 2};
    dataset.exclusions.push_back({excluded_date, exclusion.at("reason").get<std::string>()});
    std::sort(dataset.exclusions.begin(), dataset.exclusions.end(), [](const auto& a, const auto& b) { return a.date < b.date; });
    std::map<std::string, Json> audit_checkpoints;
    for (const auto& item : audit.at("checkpoints")) {
        if (!audit_checkpoints.emplace(item.at("date").get<std::string>(), item).second) {
            throw std::invalid_argument("duplicate checkpoint in acquisition audit");
        }
    }
    Json selected = Json::array();
    Json calendars = Json::array();
    Json completed_months = Json::array();
    bool excluded_date_in_calendar = false;
    for (const auto& month : development_months()) {
        if (progress) { progress("Verifying " + month.id + " raw pages, CSV and checkpoints"); }
        const auto calendar = development_calendar(month.range);
        const auto relative_month = "months/" + month.id;
        const auto month_root = contained_path(data_root, relative_month);
        const SessionBatch batch{month.range, calendar.size(), month_root,
            month.id == "2022-03" ? BatchPurpose::engineering_pilot : BatchPurpose::development};
        std::optional<FrozenSessionBatch> frozen;
        if (std::filesystem::is_regular_file(month_root / "manifest.json")) {
            frozen = batch.load();
            completed_months.push_back({{"month", month.id}, {"manifest_sha256", frozen->manifest_sha256}});
        }
        if (month.id == "2022-03" && (!frozen || frozen->manifest_sha256 != dataset.march_manifest_sha256)) {
            throw std::invalid_argument("frozen March identity changed during study finalization");
        }
        calendars.push_back({{"file", relative_month + "/calendar_snapshot.json"},
            {"sha256", sha256_file(month_root / "calendar_snapshot.json")}});
        std::size_t session_index = 0;
        for (const auto& session : calendar) {
            if (!session.regular() || session.date() == "2023-01-03") { continue; }
            if (session.date() == excluded_date) {
                excluded_date_in_calendar = true;
                if (std::filesystem::exists(month_root / "checkpoints" / (excluded_date + ".json"))) {
                    throw std::invalid_argument("excluded date unexpectedly has a complete checkpoint");
                }
                continue;
            }
            const auto file = relative_month + "/checkpoints/" + session.date() + ".json";
            const auto digest = sha256_file(contained_path(data_root, file));
            const auto found = audit_checkpoints.find(session.date());
            const Json checkpoint{{"date", session.date()}, {"file", file}, {"sha256", digest}};
            if (found == audit_checkpoints.end() || found->second != checkpoint) {
                throw std::invalid_argument("selected checkpoint differs from acquisition audit on " + session.date());
            }
            auto download = frozen ? std::move(frozen->sessions.at(session_index++)) : batch.load_session(session.date());
            if (download.request.start() != session.open_utc() || download.request.end_exclusive() != session.close_utc()) {
                throw std::invalid_argument("selected session differs from the development calendar");
            }
            dataset.sessions.push_back(std::move(download));
            selected.push_back(checkpoint);
        }
        if (frozen && session_index != frozen->sessions.size()) { throw std::invalid_argument("unexpected session in frozen month"); }
        if (progress) { progress("Verified " + month.id + " | " + std::to_string(dataset.sessions.size()) + "/496 scored sessions"); }
    }
    if (!excluded_date_in_calendar || dataset.sessions.size() != 496 || audit_checkpoints.size() != 496 ||
        completed_months != audit.at("completed_months")) {
        throw std::invalid_argument("study date coverage or completed-month inventory mismatch");
    }
    Json exclusions = Json::array();
    for (const auto& value : dataset.exclusions) { exclusions.push_back({{"date", value.date}, {"reason", value.reason}}); }
    Json manifest{{"schema_version", 2}, {"protocol", "SPY-IM-01"}, {"protocol_revision", 2},
        {"protocol_sha256", dataset.protocol_sha256}, {"data_policy_sha256", expected_policy_sha256_},
        {"data_policy", policy}, {"start_date", "2022-01-01"}, {"end_date", "2023-12-31"},
        {"symbol", "SPY"}, {"timeframe", "1Min"}, {"feed", "sip"}, {"adjustment", "raw"}, {"currency", "USD"},
        {"asof", "-"}, {"sort", "asc"}, {"calendar_sessions", 501}, {"scored_sessions", 496},
        {"validated_scored_bars", 193440}, {"unresolved_scored_sessions", 0},
        {"frozen_march_manifest_sha256", dataset.march_manifest_sha256}, {"exclusions", std::move(exclusions)},
        {"calendar_snapshots", std::move(calendars)}, {"completed_months", std::move(completed_months)},
        {"checkpoints", std::move(selected)}};
    return {std::move(dataset), std::move(manifest)};
}

FrozenDevelopmentDataset DevelopmentStudyDataset::finalize(const Progress& progress) const {
    const auto file = contained_path(project_root_, std::string{collection_directory} + "/study_manifest.json");
    if (std::filesystem::exists(file)) { return load(progress); }
    auto verified = verify(progress);
    const auto text = verified.manifest.dump(2) + '\n';
    write_new_text_file(file, text);
    verified.dataset.manifest_sha256 = sha256(text);
    if (progress) { progress("Study manifest published after complete verification"); }
    return std::move(verified.dataset);
}

FrozenDevelopmentDataset DevelopmentStudyDataset::load(const Progress& progress) const {
    const auto file = contained_path(project_root_, std::string{collection_directory} + "/study_manifest.json");
    if (!std::filesystem::is_regular_file(file)) {
        throw std::runtime_error("study dataset is not finalized; run development_finalize before development_replay");
    }
    const auto text = read_text_file(file);
    auto verified = verify(progress);
    if (parse_json_document(text) != verified.manifest) { throw std::invalid_argument("frozen study manifest changed or no longer matches its evidence"); }
    verified.dataset.manifest_sha256 = sha256(text);
    if (progress) { progress("Frozen study manifest verified"); }
    return std::move(verified.dataset);
}

} // namespace trading
