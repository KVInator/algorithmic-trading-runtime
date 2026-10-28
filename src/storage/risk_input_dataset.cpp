#include "risk_input_dataset.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include "session_storage.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {
using Json = nlohmann::json;

std::string month_id(unsigned month) { return "2021-" + std::string{month < 10 ? "0" : ""} + std::to_string(month); }
Json envelope(const std::string& endpoint, const std::string& path, const MarketDataResponse& response,
              const std::string& retrieved) {
    static_cast<void>(parse_utc_seconds(retrieved));
    return {{"schema_version", 1}, {"endpoint", endpoint}, {"request_path", path},
        {"http_status", response.status}, {"retrieved_utc", retrieved},
        {"body_sha256", sha256(response.body)}, {"body", response.body}};
}
template<class Fetch>
Json response_evidence(const std::filesystem::path& file, const std::string& endpoint,
                       const std::string& path, bool acquire, Fetch fetch) {
    if (!std::filesystem::exists(file)) {
        if (!acquire) { throw std::runtime_error("missing risk input response: " + file.string()); }
        const auto response = fetch();
        if (response.body.size() > maximum_market_data_page_bytes) { throw std::invalid_argument("risk response exceeds size bound"); }
        const auto now = format_utc_seconds(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
        write_new_text_file(file, envelope(endpoint, path, response, now).dump(2) + '\n');
    }
    const auto doc = parse_json_document(read_text_file(file, 512U * 1024U));
    const MarketDataResponse response{doc.at("http_status").get<std::uint32_t>(), doc.at("body").get<std::string>()};
    if (response.body.size() > maximum_market_data_page_bytes ||
        doc != envelope(endpoint, path, response, doc.at("retrieved_utc").get<std::string>())) {
        throw std::invalid_argument("risk input response provenance or hash mismatch");
    }
    return doc;
}

Json price_record(const std::filesystem::path& root, const RiskPriceInput& input,
                  const std::filesystem::path& run, const SessionDownload& download) {
    const auto relative = std::filesystem::relative(run, root).generic_string();
    static_cast<void>(contained_path(root, relative));
    Json files = Json::array();
    auto add = [&](const std::string& name) { files.push_back({{"file", name}, {"sha256", sha256_file(contained_path(run, name))}}); };
    add("metadata.json"); add("bars.csv");
    for (std::size_t i = 0; i < download.pages.size(); ++i) { add(session_page_filename(i)); }
    return {{"schema_version", 1}, {"date", input.date}, {"role", input.role},
        {"request_path", input.request.path()}, {"session_directory", relative}, {"files", files}};
}
SessionDownload read_price(const std::filesystem::path& root, const RiskPriceInput& input) {
    const auto record = parse_json_document(read_text_file(contained_path(root, "prices/" + input.date + "/evidence.json")));
    const auto run = contained_path(root, record.at("session_directory").get<std::string>());
    const auto download = load_session_download(run, input.request);
    if (record != price_record(root, input, run, download)) { throw std::invalid_argument("risk price input hash or request mismatch"); }
    return download;
}
} // namespace

std::vector<CalendarRange> risk_calibration_ranges() {
    std::vector<CalendarRange> ranges{{"2020-12-31", "2020-12-31"}};
    for (unsigned month = 1; month <= 12; ++month) {
        const auto end = std::chrono::year_month_day{std::chrono::year{2021}/std::chrono::month{month}/std::chrono::last};
        ranges.emplace_back(month_id(month) + "-01", month_id(month) + "-" + std::to_string(static_cast<unsigned>(end.day())));
    }
    return ranges;
}
std::vector<RiskPriceInput> risk_price_input_plan() {
    std::vector<CalendarSession> calendar;
    for (const auto& range : risk_calibration_ranges()) {
        const auto month = risk_calibration_calendar(range);
        calendar.insert(calendar.end(), month.begin(), month.end());
    }
    if (calendar.size() != 253) { throw std::logic_error("risk calibration requires 252 sessions and one preceding close"); }
    std::vector<RiskPriceInput> plan;
    for (std::size_t i = 0; i < calendar.size(); ++i) {
        const auto& session = calendar[i];
        const bool full = i >= calendar.size() - 20;
        plan.push_back({session.date(), i == 0 ? "boundary_close" : full ? "warmup_full" : "daily_close",
            HistoricalBarRequest{"SPY", full ? session.open_utc() : session.close_utc() - std::chrono::minutes{1}, session.close_utc(), 100}});
    }
    for (const auto* date : {"2022-11-25", "2023-07-03", "2023-11-24"}) {
        const auto day = development_calendar(CalendarRange{date, date}).at(0);
        plan.push_back({date, "shortened_session", day.bar_request()});
    }
    return plan;
}

RiskInputDataset::RiskInputDataset(std::filesystem::path directory, std::filesystem::path issuer_reference)
    : directory_{std::move(directory)}, issuer_reference_{std::move(issuer_reference)} {
    if (directory_.empty()) { throw std::invalid_argument("risk input directory is empty"); }
}
RiskInputInventory RiskInputDataset::collect(const SessionBatch::CalendarFetcher& calendar,
    const BarPageFetcher& bars, const ActionFetcher& actions, const SessionBatch::Progress& progress) const {
    if (!calendar || !bars || !actions) { throw std::invalid_argument("risk acquisition requires all read-only data fetchers"); }
    return inspect(true, calendar, bars, actions, progress);
}
RiskInputInventory RiskInputDataset::load() const { return inspect(false, {}, {}, {}, {}); }

RiskInputInventory RiskInputDataset::inspect(bool acquire, const SessionBatch::CalendarFetcher& calendar_fetcher,
    const BarPageFetcher& bar_fetcher, const ActionFetcher& action_fetcher, const SessionBatch::Progress& progress) const {
    RiskInputInventory result;
    auto report = [&](const std::string& message) { if (progress) { progress(message); } };
    for (const auto& range : risk_calibration_ranges()) {
        const auto relative = "calendars/" + range.first().substr(0, 7) + ".json";
        const auto file = contained_path(directory_, relative);
        const auto doc = response_evidence(file, "https://paper-api.alpaca.markets/v2/calendar", range.path(), acquire,
            [&] { return calendar_fetcher(range); });
        if (doc.at("http_status") != 200) { throw std::runtime_error("risk calibration calendar access rejected"); }
        const auto observed = decode_market_calendar(doc.at("body").get<std::string>(), range);
        const auto expected = risk_calibration_calendar(range);
        if (observed.size() != expected.size()) { throw std::invalid_argument("risk calibration calendar count mismatch"); }
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (observed[i].date() != expected[i].date() || observed[i].open_utc() != expected[i].open_utc() ||
                observed[i].close_utc() != expected[i].close_utc()) { throw std::invalid_argument("risk calendar differs from NYSE reference"); }
        }
        result.evidence_hashes.emplace_back(relative, sha256_file(file));
        report("Verified calibration calendar " + range.first().substr(0, 7));
    }
    const auto plan = risk_price_input_plan();
    for (const auto& input : plan) {
        const auto root = contained_path(directory_, "prices/" + input.date);
        const auto marker = contained_path(root, "evidence.json");
        if (!std::filesystem::exists(marker)) {
            if (!acquire) { throw std::runtime_error("missing risk price evidence on " + input.date); }
            const auto runs = contained_path(root, "runs");
            auto run = find_latest_session_run(runs, input.request);
            if (!run) {
                report("Acquiring " + input.date + " | " + input.role + " | " + std::to_string(input.request.expected_minutes()) + " minute references");
                run = save_session_download(SessionDownloader{input.request}.download(bar_fetcher), runs);
            }
            const auto download = load_session_download(*run, input.request);
            write_new_text_file(marker, price_record(directory_, input, *run, download).dump(2) + '\n');
        }
        const auto download = read_price(directory_, input);
        const auto record = parse_json_document(read_text_file(marker));
        result.prices.push_back({input, contained_path(directory_, record.at("session_directory").get<std::string>()),
            sha256_file(marker), download.audit.observed_bars});
        result.evidence_hashes.emplace_back("prices/" + input.date + "/evidence.json", sha256_file(marker));
        if (!download.audit.complete()) { result.blockers.push_back("Incomplete " + input.role + " on " + input.date); }
        report(input.date + " verified | " + std::to_string(download.audit.observed_bars) + "/" +
            std::to_string(input.request.expected_minutes()) + " references");
    }
    for (int year = 2021; year <= 2024; ++year) {
        const auto first = std::to_string(year) + "-01-01";
        const auto last = std::to_string(year) + (year == 2024 ? "-03-31" : "-12-31");
        const CorporateActionRequest request{first, last};
        std::optional<std::string> token;
        std::set<std::string> tokens;
        for (std::size_t page = 0; ; ++page) {
            if (page >= 32) { throw std::invalid_argument("corporate-action pagination exceeds 32 pages per interval"); }
            const auto relative = "actions/" + std::to_string(year) + "/" + session_page_filename(page);
            const auto file = contained_path(directory_, relative);
            const auto doc = response_evidence(file, "https://data.alpaca.markets/v1/corporate-actions", request.path(token), acquire,
                [&] { report("Acquiring corporate actions | process year " + std::to_string(year)); return action_fetcher(request, token); });
            result.evidence_hashes.emplace_back(relative, sha256_file(file));
            if (doc.at("http_status") != 200) {
                result.blockers.push_back("Corporate-action access rejected for process year " + std::to_string(year) +
                    "; HTTP " + std::to_string(doc.at("http_status").get<std::uint32_t>()));
                break;
            }
            token = result.actions.append_page(doc.at("body").get<std::string>(), first, last);
            if (!token) { break; }
            if (!tokens.insert(*token).second) { throw std::invalid_argument("corporate-action pagination token repeated"); }
        }
    }
    for (const auto& kind : result.actions.unsupported_types()) { result.blockers.push_back("Unsupported corporate action: " + kind); }
    if (issuer_reference_.empty()) {
        result.blockers.push_back("Independent issuer distribution reconciliation is required; no reference was supplied.");
    } else {
        const auto ref = parse_json_document(read_text_file(issuer_reference_));
        if (ref.at("schema_version") != 1 || ref.at("symbol") != "SPY" || ref.at("cusip") != "78462F103" ||
            ref.at("ex_date_first") != "2021-01-01" || ref.at("ex_date_last") != "2023-12-31" ||
            ref.at("cash_distributions").size() != 12 ||
            ref.at("source_url") != "https://www.ssga.com/library-content/products/fund-data/etfs/us/spdr-etf-historical-distributions.xlsx") {
            throw std::invalid_argument("issuer distribution reference contract mismatch");
        }
        std::vector<CashDistribution> expected;
        for (const auto& row : ref.at("cash_distributions")) {
            expected.push_back({"", row.at("ex_date").get<std::string>(), row.at("record_date").get<std::string>(),
                row.at("payable_date").get<std::string>(), "", row.at("usd_per_share").get<double>()});
        }
        const auto differences = result.actions.reconcile(expected);
        result.blockers.insert(result.blockers.end(), differences.begin(), differences.end());
        result.cash_distributions_reconciled = differences.empty();
        result.issuer_reference_sha256 = sha256_file(issuer_reference_);
    }
    result.required_evidence_complete = result.blockers.empty();
    result.blockers.push_back("The 2023-06-05 RV5 gap and the missing-risk policy remain unresolved; no strategy is registered or scored.");
    Json evidence = Json::array();
    for (const auto& [file, digest] : result.evidence_hashes) { evidence.push_back({{"file", file}, {"sha256", digest}}); }
    const Json manifest{{"schema_version", 1}, {"kind", "risk_input_acquisition_inventory"},
        {"strategy_scored", false}, {"planned_price_inputs", plan.size()}, {"evidence", evidence}, {"blockers", result.blockers},
        {"issuer_reference_sha256", result.issuer_reference_sha256}, {"cash_distributions_reconciled", result.cash_distributions_reconciled}};
    const auto marker = contained_path(directory_, "acquisition_manifest.json");
    if (!std::filesystem::exists(marker)) {
        if (!acquire) { throw std::runtime_error("risk acquisition inventory is not published"); }
        write_new_text_file(marker, manifest.dump(2) + '\n');
    }
    const auto published = parse_json_document(read_text_file(marker));
    if (published != manifest) {
        // Report field locations without exposing retained financial responses.
        std::string detail{"risk acquisition inventory changed at"};
        const auto differences = Json::diff(published, manifest);
        const auto count = std::min<std::size_t>(differences.size(), 5);
        for (std::size_t i = 0; i < count; ++i) { detail += " " + differences.at(i).at("path").get<std::string>(); }
        if (published.contains("issuer_reference_sha256") && published.at("issuer_reference_sha256") != result.issuer_reference_sha256) {
            detail += "; issuer reference bytes differ from the inventory (build.py restores only registered LF/CRLF checkout conversions)";
        }
        throw std::invalid_argument(detail);
    }
    result.evidence_hashes.emplace_back("acquisition_manifest.json", sha256_file(marker));
    return result;
}

std::vector<SessionDownload> RiskInputDataset::supplementary_sessions() const {
    std::vector<SessionDownload> result;
    for (const auto& input : risk_price_input_plan()) {
        if (input.role != "shortened_session") { continue; }
        const auto file = contained_path(directory_, "prices/" + input.date + "/evidence.json");
        if (std::filesystem::exists(file)) { result.push_back(read_price(directory_, input)); }
    }
    return result;
}

} // namespace trading
