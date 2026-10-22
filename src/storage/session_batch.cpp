#include "session_batch.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include "session_storage.hpp"
#include "utc_timestamp.hpp"

#include <stdexcept>
#include <sstream>
#include <utility>

namespace trading {
namespace {

using Json = nlohmann::json;

std::string coverage_failure(const CalendarSession& session, const SessionDownload& download,
                             const std::filesystem::path& directory) {
    std::ostringstream message;
    message << "incomplete minute grid on " << session.date() << ": " << download.audit.observed_bars
            << '/' << download.audit.expected_minutes << " validated bars; "
            << download.audit.missing_minutes.size() << " missing minute slots\nMissing UTC starts:";
    constexpr std::size_t displayed = 8;
    for (std::size_t i = 0; i < download.audit.missing_minutes.size() && i < displayed; ++i) {
        message << ' ' << format_utc_seconds(download.audit.missing_minutes[i]);
    }
    if (download.audit.missing_minutes.size() > displayed) { message << " (remaining starts in metadata.json)"; }
    message << "\nSaved evidence: " << directory.string()
            << "\nInspect metadata.json and bars.csv; completed checkpoints are preserved.";
    return message.str();
}

Json snapshot_record(const CalendarRange& range, const std::string& body, const std::string& retrieved) {
    static_cast<void>(parse_utc_seconds(retrieved));
    return {{"schema_version", 1}, {"endpoint", "https://paper-api.alpaca.markets/v2/calendar"},
            {"request_path", range.path()}, {"http_status", 200}, {"retrieved_utc", retrieved},
            {"body_sha256", sha256(body)}, {"body", body}};
}

std::string exclusion_reason(const CalendarSession& session, BatchPurpose purpose) {
    if (!session.regular()) { return "calendar session is not 09:30-16:00"; }
    if (purpose == BatchPurpose::development && session.date() == "2023-01-03") {
        return "diagnostic fixture excluded by SPY-IM-01";
    }
    return {};
}

void validate_development_calendar(const std::vector<CalendarSession>& calendar,
                                   const CalendarRange& range, BatchPurpose purpose) {
    if (purpose != BatchPurpose::development) { return; }
    const auto reference = development_calendar(range);
    if (reference.size() != calendar.size()) { throw std::invalid_argument("development calendar count mismatch"); }
    for (std::size_t i = 0; i < reference.size(); ++i) {
        if (reference[i].date() != calendar[i].date() || reference[i].open_utc() != calendar[i].open_utc() ||
            reference[i].close_utc() != calendar[i].close_utc()) {
            throw std::invalid_argument("provider calendar disagrees with the NYSE development reference");
        }
    }
}

std::vector<CalendarSession> load_calendar(const std::filesystem::path& root,
                                         const CalendarRange& range, std::size_t expected) {
    const auto snapshot = parse_json_document(read_text_file(contained_path(root, "calendar_snapshot.json")));
    const auto body = snapshot.at("body").get<std::string>();
    if (body.size() > maximum_market_data_page_bytes ||
        snapshot != snapshot_record(range, body, snapshot.at("retrieved_utc").get<std::string>())) {
        throw std::invalid_argument("calendar snapshot provenance is inconsistent");
    }
    auto calendar = decode_market_calendar(body, range);
    if (calendar.size() != expected) {
        throw std::invalid_argument("calendar session count differs from the independently specified count");
    }
    return calendar;
}

std::string checkpoint_name(const CalendarSession& session) {
    return "checkpoints/" + session.date() + ".json";
}

Json checkpoint_record(const std::filesystem::path& root, const std::string& calendar_digest,
                       const CalendarSession& session, const std::string& relative_directory,
                       const SessionDownload& download) {
    const auto relative = std::filesystem::path{relative_directory};
    if (relative.parent_path() != "sessions" ||
        !relative.filename().string().starts_with("SPY_" + session.date() + "_")) {
        throw std::invalid_argument("checkpoint must reference its own date under sessions/");
    }
    const auto directory = contained_path(root, relative_directory);
    Json files = Json::array();
    auto add = [&](const std::string& filename) {
        const auto path = contained_path(directory, filename);
        files.push_back({{"file", filename}, {"bytes", std::filesystem::file_size(path)},
                         {"sha256", sha256_file(path)}});
    };
    add("metadata.json");
    add("bars.csv");
    for (std::size_t index = 0; index < download.pages.size(); ++index) { add(session_page_filename(index)); }
    return {{"schema_version", 1}, {"calendar_snapshot_sha256", calendar_digest},
            {"date", session.date()}, {"session_directory", relative_directory},
            {"start_inclusive_utc", format_utc_seconds(session.open_utc())},
            {"end_exclusive_utc", format_utc_seconds(session.close_utc())}, {"files", std::move(files)}};
}

SessionDownload load_checkpoint(const std::filesystem::path& root, const std::string& calendar_digest,
                                const CalendarSession& session) {
    const auto record = parse_json_document(read_text_file(contained_path(root, checkpoint_name(session))));
    const auto relative = record.at("session_directory").get<std::string>();
    auto download = load_session_download(contained_path(root, relative), session.bar_request());
    if (!download.audit.complete() ||
        record != checkpoint_record(root, calendar_digest, session, relative, download)) {
        throw std::invalid_argument("session checkpoint hash or coverage mismatch on " + session.date());
    }
    return download;
}

Json manifest_record(const std::filesystem::path& root, const CalendarRange& range,
                     const std::vector<CalendarSession>& calendar, const std::string& calendar_digest, BatchPurpose purpose) {
    Json checkpoints = Json::array();
    Json excluded = Json::array();
    std::size_t rows = 0;
    for (const auto& session : calendar) {
        if (exclusion_reason(session, purpose).empty()) {
            const auto name = checkpoint_name(session);
            checkpoints.push_back({{"date", session.date()}, {"file", name},
                                   {"sha256", sha256_file(contained_path(root, name))}});
            rows += session.bar_request().expected_minutes();
        } else {
            excluded.push_back({{"date", session.date()}, {"reason", exclusion_reason(session, purpose)},
                                {"open_utc", format_utc_seconds(session.open_utc())},
                                {"close_utc", format_utc_seconds(session.close_utc())}});
        }
    }
    return {{"schema_version", 1}, {"purpose", purpose == BatchPurpose::engineering_pilot ? "engineering_pilot" : "development"}, {"protocol", "SPY-IM-01"},
            {"start_date", range.first()}, {"end_date", range.last()},
            {"symbol", "SPY"}, {"timeframe", "1Min"}, {"feed", "sip"}, {"adjustment", "raw"},
            {"currency", "USD"}, {"asof", "-"}, {"sort", "asc"},
            {"calendar_sessions", calendar.size()}, {"eligible_sessions", checkpoints.size()},
            {"validated_bars", rows}, {"unresolved_sessions", 0},
            {"calendar_snapshot_file", "calendar_snapshot.json"},
            {"calendar_snapshot_sha256", calendar_digest},
            {"checkpoints", std::move(checkpoints)}, {"excluded_sessions", std::move(excluded)}};
}

} // namespace

SessionBatch::SessionBatch(CalendarRange range, std::size_t expected_calendar_sessions,
                           std::filesystem::path directory, BatchPurpose purpose)
    : range_{std::move(range)}, expected_calendar_sessions_{expected_calendar_sessions},
      directory_{std::move(directory)}, purpose_{purpose} {
    if (directory_.empty() || expected_calendar_sessions_ == 0 || expected_calendar_sessions_ > 31) {
        throw std::invalid_argument("batch requires a directory and 1 to 31 expected calendar sessions");
    }
}

FrozenSessionBatch SessionBatch::load() const {
    const auto manifest_text = read_text_file(contained_path(directory_, "manifest.json"));
    const auto manifest = parse_json_document(manifest_text);
    const auto calendar = load_calendar(directory_, range_, expected_calendar_sessions_);
    validate_development_calendar(calendar, range_, purpose_);
    const auto calendar_digest = sha256_file(contained_path(directory_, "calendar_snapshot.json"));
    if (manifest != manifest_record(directory_, range_, calendar, calendar_digest, purpose_)) {
        throw std::invalid_argument("dataset manifest hash, count or convention mismatch");
    }
    FrozenSessionBatch batch{directory_, sha256(manifest_text), {}, {}};
    for (const auto& session : calendar) {
        if (exclusion_reason(session, purpose_).empty()) { batch.sessions.push_back(load_checkpoint(directory_, calendar_digest, session)); }
        else { batch.excluded_dates.push_back(session.date()); }
    }
    if (batch.sessions.empty()) { throw std::invalid_argument("batch contains no eligible full sessions"); }
    return batch;
}

FrozenSessionBatch SessionBatch::acquire(const CalendarFetcher& calendar_fetcher,
                                        const BarPageFetcher& page_fetcher, const Progress& progress) const {
    if (std::filesystem::exists(contained_path(directory_, "manifest.json"))) { return load(); }
    static_cast<void>(acquire_sessions(calendar_fetcher, page_fetcher, progress, GapPolicy::stop_and_retry));
    return load();
}

SessionDownload SessionBatch::load_session(std::string_view date) const {
    const auto calendar = load_calendar(directory_, range_, expected_calendar_sessions_);
    validate_development_calendar(calendar, range_, purpose_);
    for (const auto& session : calendar) {
        if (session.date() == date && exclusion_reason(session, purpose_).empty()) {
            return load_checkpoint(directory_, sha256_file(contained_path(directory_, "calendar_snapshot.json")), session);
        }
    }
    throw std::invalid_argument("date is not an eligible session in the saved batch calendar");
}

BatchCollection SessionBatch::collect(const CalendarFetcher& calendar_fetcher,
                                      const BarPageFetcher& page_fetcher, const Progress& progress) const {
    return acquire_sessions(calendar_fetcher, page_fetcher, progress, GapPolicy::retain_and_continue);
}

BatchCollection SessionBatch::acquire_sessions(const CalendarFetcher& calendar_fetcher,
                                               const BarPageFetcher& page_fetcher, const Progress& progress,
                                               GapPolicy policy) const {
    if (std::filesystem::exists(contained_path(directory_, "manifest.json"))) { return {load().sessions.size(), {}}; }
    BatchCollection result{0, {}};
    auto report = [&](const std::string& message) { if (progress) { progress(message); } };
    std::filesystem::create_directories(directory_);
    const auto snapshot_path = contained_path(directory_, "calendar_snapshot.json");
    if (!std::filesystem::exists(snapshot_path)) {
        if (!calendar_fetcher) { throw std::invalid_argument("calendar fetcher is missing"); }
        const auto response = calendar_fetcher(range_);
        if (response.status != 200 || response.body.size() > maximum_market_data_page_bytes) {
            throw std::runtime_error("calendar request failed or exceeded its size bound; HTTP " +
                                     std::to_string(response.status));
        }
        const auto decoded = decode_market_calendar(response.body, range_);
        validate_development_calendar(decoded, range_, purpose_);
        if (decoded.size() != expected_calendar_sessions_) {
            throw std::invalid_argument("calendar count differs from the independently specified count");
        }
        const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        write_new_text_file(snapshot_path, snapshot_record(range_, response.body, format_utc_seconds(now)).dump(2) + '\n');
    }
    const auto calendar = load_calendar(directory_, range_, expected_calendar_sessions_);
    validate_development_calendar(calendar, range_, purpose_);
    const auto calendar_digest = sha256_file(snapshot_path);
    for (const auto& session : calendar) {
        const auto reason = exclusion_reason(session, purpose_);
        if (!reason.empty()) { report(session.date() + " excluded: " + reason); continue; }
        const auto checkpoint = contained_path(directory_, checkpoint_name(session));
        if (std::filesystem::exists(checkpoint)) {
            static_cast<void>(load_checkpoint(directory_, calendar_digest, session));
            ++result.complete_sessions;
            report(session.date() + " verified and reused");
            continue;
        }
        const auto request = session.bar_request();
        const auto sessions_root = contained_path(directory_, "sessions");
        auto run = find_latest_session_run(sessions_root, request);
        std::optional<SessionDownload> download;
        if (run) {
            download = load_session_download(*run, request);
            if (!download->audit.complete()) {
                report(coverage_failure(session, *download, *run));
                if (policy == GapPolicy::stop_and_retry) {
                    report(session.date() + " retrying the incomplete session; prior evidence retained");
                    download.reset();
                }
            }
        }
        if (!download) {
            report(session.date() + " downloading " + format_utc_seconds(session.open_utc()) + " -> " +
                   format_utc_seconds(session.close_utc()));
            download = SessionDownloader{request}.download(page_fetcher);
            run = save_session_download(*download, sessions_root);
        }
        if (!download->audit.complete()) {
            if (policy == GapPolicy::retain_and_continue) {
                result.unresolved.push_back({session.date(), *run, download->audit});
                report(session.date() + " retained as unresolved; continuing collection without a checkpoint");
                continue;
            }
            throw std::runtime_error(coverage_failure(session, *download, *run) +
                                     "\nMissing data blocks completion; the final manifest has not been published.");
        }
        const auto relative = std::filesystem::relative(*run, directory_).generic_string();
        write_new_text_file(checkpoint, checkpoint_record(directory_, calendar_digest, session, relative, *download).dump(2) + '\n');
        ++result.complete_sessions;
        report(session.date() + " checkpoint complete: 390 bars");
    }
    if (result.unresolved.empty()) {
        write_new_text_file(contained_path(directory_, "manifest.json"),
                            manifest_record(directory_, range_, calendar, calendar_digest, purpose_).dump(2) + '\n');
    }
    return result;
}

FrozenSessionBatch SessionBatch::import_from(const std::filesystem::path& source) const {
    const auto frozen = SessionBatch{range_, expected_calendar_sessions_, source, purpose_}.load();
    auto copy = [&](const std::string& relative) {
        const auto text = read_text_file(contained_path(source, relative));
        const auto destination = contained_path(directory_, relative);
        if (std::filesystem::exists(destination)) {
            if (read_text_file(destination) != text) {
                throw std::invalid_argument("partial import disagrees with frozen source: " + relative);
            }
        } else { write_new_text_file(destination, text); }
    };
    copy("calendar_snapshot.json");
    for (const auto& download : frozen.sessions) {
        const auto date = format_utc_seconds(download.request.start()).substr(0, 10);
        const auto checkpoint = "checkpoints/" + date + ".json";
        const auto record = parse_json_document(read_text_file(contained_path(source, checkpoint)));
        const auto run = record.at("session_directory").get<std::string>();
        copy(run + "/metadata.json");
        copy(run + "/bars.csv");
        for (std::size_t i = 0; i < download.pages.size(); ++i) { copy(run + "/" + session_page_filename(i)); }
        copy(checkpoint);
    }
    if (sha256_file(contained_path(source, "manifest.json")) != frozen.manifest_sha256) {
        throw std::runtime_error("frozen source manifest changed during import");
    }
    copy("manifest.json");
    auto imported = load();
    if (imported.manifest_sha256 != frozen.manifest_sha256) {
        throw std::runtime_error("import did not preserve the frozen manifest bytes");
    }
    return imported;
}

} // namespace trading
