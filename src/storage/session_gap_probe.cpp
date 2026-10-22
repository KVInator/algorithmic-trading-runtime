#include "session_gap_probe.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "session_storage.hpp"
#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {

struct ProbeWindow {
    std::string kind;
    HistoricalBarRequest request;
};

bool same_bar(const MinuteBar& left, const MinuteBar& right) {
    const auto& a = left.prices();
    const auto& b = right.prices();
    return left.symbol() == right.symbol() && left.start() == right.start() &&
        a.open == b.open && a.high == b.high && a.low == b.low && a.close == b.close &&
        left.volume() == right.volume();
}

template <typename Times>
nlohmann::json time_records(const Times& times) {
    auto values = nlohmann::json::array();
    for (const auto time : times) { values.push_back(format_utc_seconds(time)); }
    return values;
}

} // namespace

SessionGapProbe::SessionGapProbe(std::filesystem::path source_directory)
    : source_directory_{std::move(source_directory)} {}

GapProbeResult SessionGapProbe::run(const std::filesystem::path& output_root,
                                   const BarPageFetcher& fetch_page,
                                   const Progress& progress) const {
    const auto source = load_session_download(source_directory_);
    const auto& missing = source.audit.missing_minutes;
    if (missing.empty()) { throw std::invalid_argument("gap probe requires an incomplete saved download"); }
    if (!fetch_page) { throw std::invalid_argument("gap probe requires a page fetcher"); }
    std::vector<ProbeWindow> windows;
    for (std::size_t first = 0; first < missing.size();) {
        auto last = first;
        while (last + 1 < missing.size() && missing[last + 1] - missing[last] == std::chrono::minutes{1}) { ++last; }
        windows.push_back({"context", {source.request.symbol(),
            std::max(source.request.start(), missing[first] - std::chrono::minutes{1}),
            std::min(source.request.end_exclusive(), missing[last] + std::chrono::minutes{2})}});
        for (auto index = first; index <= last; ++index) {
            windows.push_back({"minute", {source.request.symbol(), missing[index],
                missing[index] + std::chrono::minutes{1}, 1}});
        }
        if (windows.size() > 16) { throw std::invalid_argument("gap probe exceeds its 16-window bound"); }
        first = last + 1;
    }

    const auto metadata_hash = sha256_file(source_directory_ / "metadata.json");
    const auto bars_hash = sha256_file(source_directory_ / "bars.csv");
    auto pages = nlohmann::json::array();
    for (std::size_t index = 0; index < source.pages.size(); ++index) {
        const auto file = session_page_filename(index);
        pages.push_back({{"file", file}, {"sha256", sha256(source.pages[index].raw_json)}});
    }
    const auto stamp = format_utc_seconds(source.request.start());
    const auto directory = create_unique_directory(output_root,
        "gap_probe_" + source.request.symbol() + "_" + stamp.substr(0, 10));
    write_new_text_file(directory / "source.json", nlohmann::json{
        {"schema_version", 1}, {"source_directory", source_directory_.generic_string()},
        {"metadata_sha256", metadata_hash}, {"bars_sha256", bars_hash}, {"pages", pages},
        {"request_path", source.request.path()}, {"missing_minutes_utc", time_records(missing)},
        {"use", "diagnostic evidence; no dataset repair or performance evaluation"}}.dump(2) + '\n');
    if (progress) { progress("Gap probe evidence: " + directory.string()); }

    std::map<std::chrono::sys_seconds, MinuteBar> reference;
    std::map<std::chrono::sys_seconds, MinuteBar> candidates;
    std::set<std::chrono::sys_seconds> conflicts;
    for (const auto& bar : source.bars) { reference.emplace(bar.start(), bar); }
    auto records = nlohmann::json::array();
    for (const auto& window : windows) {
        const auto download = SessionDownloader{window.request}.download(fetch_page);
        const auto saved = save_session_download(download, directory / "probes");
        std::size_t matched = 0;
        for (const auto& bar : download.bars) {
            const auto existing = reference.find(bar.start());
            if (existing != reference.end()) {
                if (same_bar(existing->second, bar)) { ++matched; }
                else { conflicts.insert(bar.start()); }
            } else {
                const auto [candidate, inserted] = candidates.emplace(bar.start(), bar);
                if (!inserted && !same_bar(candidate->second, bar)) { conflicts.insert(bar.start()); }
            }
        }
        records.push_back({{"kind", window.kind},
            {"directory", std::filesystem::relative(saved, directory).generic_string()},
            {"metadata_sha256", sha256_file(saved / "metadata.json")},
            {"expected_minutes", download.audit.expected_minutes},
            {"observed_bars", download.bars.size()}, {"matching_reference_bars", matched},
            {"missing_minutes_utc", time_records(download.audit.missing_minutes)}});
        if (progress) {
            progress(window.kind + " " + format_utc_seconds(window.request.start()) + " -> " +
                format_utc_seconds(window.request.end_exclusive()) + ": " +
                std::to_string(download.bars.size()) + "/" + std::to_string(download.audit.expected_minutes) +
                " bars; matching reference bars: " + std::to_string(matched));
        }
    }
    if (sha256_file(source_directory_ / "metadata.json") != metadata_hash ||
        sha256_file(source_directory_ / "bars.csv") != bars_hash) {
        throw std::runtime_error("source download changed during gap diagnosis");
    }
    for (std::size_t index = 0; index < source.pages.size(); ++index) {
        if (sha256_file(source_directory_ / session_page_filename(index)) != sha256(source.pages[index].raw_json)) {
            throw std::runtime_error("source raw page changed during gap diagnosis");
        }
    }
    std::vector<std::chrono::sys_seconds> found;
    for (const auto& [time, bar] : candidates) { static_cast<void>(bar); found.push_back(time); }
    write_new_text_file(directory / "summary.json", nlohmann::json{
        {"schema_version", 1}, {"source_record_sha256", sha256_file(directory / "source.json")},
        {"probe_windows", windows.size()}, {"missing_minutes", missing.size()},
        {"candidate_minutes_found", time_records(found)}, {"conflicting_minutes_utc", time_records(conflicts)},
        {"probes", records}, {"dataset_modified", false}}.dump(2) + '\n');
    return {directory, windows.size(), missing.size(), candidates.size(), conflicts.size()};
}

} // namespace trading
