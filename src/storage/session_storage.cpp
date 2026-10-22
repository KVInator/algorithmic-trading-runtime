#include "session_storage.hpp"
#include "minute_bar_csv.hpp"
#include "artifact_io.hpp"
#include "json_input.hpp"
#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace trading {
namespace {

std::filesystem::path new_run_directory(const SessionDownload& download,
                                       const std::filesystem::path& output_root) {
    auto stamp = format_utc_seconds(download.finished_utc);
    stamp.erase(std::remove(stamp.begin(), stamp.end(), ':'), stamp.end());
    const auto stem = download.request.symbol() + "_" +
                      format_utc_seconds(download.request.start()).substr(0, 10) + "_" + stamp;
    return create_unique_directory(output_root, stem);
}

nlohmann::json session_metadata(const SessionDownload& download, const std::string& csv) {
    auto page_records = nlohmann::json::array();
    for (std::size_t index = 0; index < download.pages.size(); ++index) {
        const auto& page = download.pages[index];
        page_records.push_back({{"file", session_page_filename(index)}, {"request_path", page.request_path},
                                {"http_status", 200}, {"bar_count", page.bar_count},
                                {"response_bytes", page.raw_json.size()}});
    }
    auto missing = nlohmann::json::array();
    for (const auto time : download.audit.missing_minutes) {
        missing.push_back(format_utc_seconds(time));
    }
    nlohmann::json metadata{
        {"schema_version", 1},
        {"source", "Alpaca Market Data API"},
        {"endpoint", "https://data.alpaca.markets/v2/stocks/bars"},
        {"symbol", download.request.symbol()},
        {"timeframe", "1Min"}, {"feed", "sip"}, {"adjustment", "raw"},
        {"currency", "USD"}, {"asof", "-"}, {"sort", "asc"},
        {"start_inclusive_utc", format_utc_seconds(download.request.start())},
        {"end_exclusive_utc", format_utc_seconds(download.request.end_exclusive())},
        {"provider_end_inclusive_utc", format_utc_seconds(
            download.request.end_exclusive() - std::chrono::minutes{1})},
        {"session_definition", "explicit UTC minute window; no automatic calendar lookup"},
        {"retrieval_started_utc", format_utc_seconds(download.started_utc)},
        {"retrieval_finished_utc", format_utc_seconds(download.finished_utc)},
        {"page_size", download.request.page_size()},
        {"pagination_complete", true}, {"pages", page_records},
        {"bars_file", "bars.csv"}, {"bars_file_bytes", csv.size()},
        {"volume_unit", "shares"}, {"price_unit", "USD per share"},
        {"audit", {{"expected_minutes", download.audit.expected_minutes},
               {"observed_bars", download.audit.observed_bars},
               {"missing_minutes_utc", std::move(missing)},
               {"minute_grid_complete", download.audit.complete()},
               {"total_volume_shares", download.audit.total_volume_shares},
               {"first_bar_utc", nullptr}, {"last_bar_utc", nullptr}}}};
    if (!download.bars.empty()) {
        metadata["audit"]["first_bar_utc"] = format_utc_seconds(download.bars.front().start());
        metadata["audit"]["last_bar_utc"] = format_utc_seconds(download.bars.back().start());
    }
    return metadata;
}

} // namespace

std::string session_page_filename(std::size_t index) {
    std::ostringstream name;
    name.imbue(std::locale::classic());
    name << "page_" << std::setfill('0') << std::setw(3) << index + 1 << ".json";
    return name.str();
}

std::filesystem::path save_session_download(const SessionDownload& download,
                                           const std::filesystem::path& output_root) {
    // Recompute the report from the current bars through the authoritative audit.
    const auto audit = audit_session(download.request, download.bars);
    const SessionDownload checked{download.request, download.bars, download.pages, audit,
                                  download.started_utc, download.finished_utc};
    if (download.pages.empty()) {
        throw std::invalid_argument("session storage requires a completed page download");
    }
    const auto directory = new_run_directory(download, output_root);
    try {
        for (std::size_t index = 0; index < download.pages.size(); ++index) {
            const auto& page = download.pages[index];
            const auto filename = session_page_filename(index);
            write_new_text_file(directory / filename, page.raw_json);
        }
        const auto csv = format_minute_bars_csv(download.bars);
        write_new_text_file(directory / "bars.csv", csv);
        const auto metadata = session_metadata(checked, csv);
        write_new_text_file(directory / "metadata.json", metadata.dump(2) + '\n');
    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error("session file write failed in " + directory.string() + ": " + error.what());
    }
    return directory;
}

SessionDownload load_session_download(const std::filesystem::path& directory,
                                      const HistoricalBarRequest& expected) {
    const auto metadata = parse_json_document(read_text_file(contained_path(directory, "metadata.json")));
    const auto& pages = metadata.at("pages");
    if (!pages.is_array() || pages.empty() || pages.size() > 16) {
        throw std::invalid_argument("stored session requires 1 to 16 raw pages");
    }
    std::size_t index = 0;
    auto download = SessionDownloader{expected}.download(
        [&](const HistoricalBarRequest& request, const std::optional<std::string>& token) {
            if (index >= pages.size()) { throw std::invalid_argument("stored pagination ended prematurely"); }
            const auto filename = session_page_filename(index);
            const auto& record = pages.at(index++);
            if (record.at("file") != filename || record.at("request_path") != request.path(token) ||
                record.at("http_status") != 200) {
                throw std::invalid_argument("stored page does not match the expected request");
            }
            return MarketDataResponse{200, read_text_file(contained_path(directory, filename),
                                                          maximum_market_data_page_bytes)};
        });
    if (index != pages.size()) { throw std::invalid_argument("stored session contains unused raw pages"); }
    download.started_utc = parse_utc_seconds(metadata.at("retrieval_started_utc").get<std::string>());
    download.finished_utc = parse_utc_seconds(metadata.at("retrieval_finished_utc").get<std::string>());
    if (download.finished_utc < download.started_utc) {
        throw std::invalid_argument("stored retrieval timestamps descend");
    }
    const auto csv = read_text_file(contained_path(directory, "bars.csv"), 1024U * 1024U);
    const auto csv_bars = parse_minute_bars_csv(csv);
    if (format_minute_bars_csv(csv_bars) != format_minute_bars_csv(download.bars) ||
        metadata != session_metadata(download, csv)) {
        throw std::invalid_argument("stored CSV or metadata disagrees with retained raw pages");
    }
    return download;
}

SessionDownload load_session_download(const std::filesystem::path& directory) {
    const auto metadata = parse_json_document(read_text_file(contained_path(directory, "metadata.json")));
    const HistoricalBarRequest request{metadata.at("symbol").get<std::string>(),
        parse_utc_seconds(metadata.at("start_inclusive_utc").get<std::string>()),
        parse_utc_seconds(metadata.at("end_exclusive_utc").get<std::string>()),
        metadata.at("page_size").get<std::size_t>()};
    return load_session_download(directory, request);
}

std::optional<std::filesystem::path> find_latest_session_run(
    const std::filesystem::path& root, const HistoricalBarRequest& request) {
    if (!std::filesystem::is_directory(root)) { return std::nullopt; }
    const auto prefix = request.symbol() + "_" + format_utc_seconds(request.start()).substr(0, 10) + "_";
    std::optional<std::filesystem::path> selected;
    std::filesystem::file_time_type newest{};
    for (const auto& entry : std::filesystem::directory_iterator{root}) {
        if (!entry.is_directory() || !entry.path().filename().string().starts_with(prefix)) { continue; }
        const auto marker = contained_path(root, entry.path().filename().generic_string() + "/metadata.json");
        if (!std::filesystem::is_regular_file(marker)) { continue; }
        const auto written = std::filesystem::last_write_time(marker);
        if (!selected || written > newest || (written == newest && entry.path() > *selected)) {
            newest = written;
            selected = entry.path();
        }
    }
    return selected;
}

} // namespace trading
