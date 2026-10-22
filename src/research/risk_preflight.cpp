#include "risk_preflight.hpp"
#include "alpaca_corporate_action_request.hpp"

#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include "minute_bar_csv.hpp"
#include "session_storage.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace trading {

std::vector<RiskSessionCoverage> audit_risk_session_coverage(
    std::span<const CalendarSession> calendar, std::span<const SessionDownload> downloads) {
    if (calendar.empty()) { throw std::invalid_argument("risk coverage requires an exchange calendar"); }
    std::map<std::string, const SessionDownload*> by_date;
    std::set<std::string> dates;
    std::string previous;
    for (const auto& session : calendar) {
        if (session.date() <= previous || session.date() < "2022-01-01" || session.date() > "2023-12-31") {
            throw std::invalid_argument("risk preflight calendar requires ascending unique 2022-2023 sessions");
        }
        previous = session.date();
        dates.insert(session.date());
    }
    for (const auto& download : downloads) {
        const auto date = format_utc_seconds(download.request.start()).substr(0, 10);
        if (!dates.contains(date) || !by_date.emplace(date, &download).second) {
            throw std::invalid_argument("risk evidence date is outside the calendar or duplicated");
        }
    }
    std::vector<RiskSessionCoverage> result;
    for (const auto& session : calendar) {
        const auto request = session.bar_request();
        RiskSessionCoverage row{session.date(), request.expected_minutes(), 0, false, false, {}, "", "absent"};
        std::set<std::chrono::sys_seconds> starts;
        if (const auto it = by_date.find(session.date()); it != by_date.end()) {
            const auto& download = *it->second;
            if (download.request.symbol() != "SPY" || download.request.start() != request.start() ||
                download.request.end_exclusive() != request.end_exclusive()) {
                throw std::invalid_argument("risk evidence request differs from the exchange session");
            }
            const auto audit = audit_session(request, download.bars);
            row.observed_minutes = audit.observed_bars;
            row.evidence_sha256 = sha256(format_minute_bars_csv(download.bars));
            row.source = audit.missing_minutes.empty() ? "verified_complete" : "verified_incomplete";
            for (const auto& bar : download.bars) { starts.insert(bar.start()); }
        }
        row.account_references_complete = starts.contains(request.start()) &&
            starts.contains(request.start() + std::chrono::minutes{1}) &&
            starts.contains(request.start() + std::chrono::minutes{2}) &&
            starts.contains(request.end_exclusive() - std::chrono::minutes{1});
        // Close of each five-minute interval, represented by its final minute
        // bar start. The opening price is a separate endpoint.
        if (!starts.contains(request.start())) { row.missing_rv5_bar_starts.push_back(format_utc_seconds(request.start())); }
        for (auto endpoint = request.start() + std::chrono::minutes{5}; endpoint <= request.end_exclusive();
             endpoint += std::chrono::minutes{5}) {
            const auto last_bar = endpoint - std::chrono::minutes{1};
            if (!starts.contains(last_bar)) { row.missing_rv5_bar_starts.push_back(format_utc_seconds(last_bar)); }
        }
        row.rv5_endpoints_complete = row.missing_rv5_bar_starts.empty();
        result.push_back(std::move(row));
    }
    return result;
}

CorporateActionProbe inspect_corporate_action_probe(int year, const MarketDataResponse& response) {
    CorporateActionProbe result{year, CorporateActionProbeRequest{year}.path(), response.status, response.body,
        sha256(response.body), false, false,
        format_utc_seconds(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()))};
    // Even a valid complete page proves access only. Ex/pay dates, unsupported
    // action types, fund distributions and independent totals still need reconciliation.
    try {
        const auto doc = parse_json_document(response.body);
        result.json_object = doc.is_object();
        if (result.json_object && doc.contains("next_page_token")) {
            const auto& token = doc.at("next_page_token");
            result.more_pages = !token.is_null() && (!token.is_string() || !token.get<std::string>().empty());
        }
    } catch (const nlohmann::json::exception&) { result.json_object = false; }
      catch (const std::invalid_argument&) { result.json_object = false; }
    return result;
}

RiskPreflight::RiskPreflight(std::filesystem::path root) : project_root_{std::move(root)} {}
RiskPreflightResult RiskPreflight::run(const DevelopmentStudyDataset::Progress& progress) const {
    auto dataset = DevelopmentStudyDataset{project_root_}.load(progress);
    RiskPreflightResult result;
    result.input_manifest_sha256 = dataset.manifest_sha256;
    if (progress) { progress("Auditing all exchange sessions and required raw-price endpoints"); }
    std::vector<CalendarSession> calendar;
    for (const auto& month : development_months()) {
        const auto file = dataset.directory / "months" / month.id / "calendar_snapshot.json";
        const auto doc = parse_json_document(read_text_file(file));
        const auto body = doc.at("body").get<std::string>();
        if (doc.at("http_status") != 200 || sha256(body) != doc.at("body_sha256").get<std::string>()) {
            throw std::invalid_argument("risk preflight calendar evidence hash/status mismatch");
        }
        const auto sessions = decode_market_calendar(body, month.range);
        calendar.insert(calendar.end(), sessions.begin(), sessions.end());
        result.calendar_hashes.emplace_back(month.id, sha256_file(file));
    }
    if (calendar.size() != 501) { throw std::invalid_argument("risk preflight requires all 501 development exchange sessions"); }
    const auto policy = parse_json_document(read_text_file(project_root_ / "research/development_data_policy.json"));
    const auto gap_root = contained_path(project_root_, policy.at("data_exclusions").at(0).at("session_directory").get<std::string>());
    dataset.sessions.push_back(load_session_download(gap_root));
    // Retired diagnostic evidence can be reused only after raw/CSV/metadata
    // verification. A missing retained run remains absent, never a cash day.
    for (const auto& session : calendar) {
        const auto found = std::find_if(dataset.sessions.begin(), dataset.sessions.end(), [&](const auto& item) {
            return item.request.start() == session.open_utc();
        });
        if (found != dataset.sessions.end()) { continue; }
        const auto request = session.bar_request();
        auto saved = find_latest_session_run(project_root_ / "scratch/runs", request);
        if (!saved) { saved = find_latest_session_run(dataset.directory / "months" / session.date().substr(0, 7) / "sessions", request); }
        if (saved) { dataset.sessions.push_back(load_session_download(*saved, request)); }
    }
    result.coverage = audit_risk_session_coverage(calendar, dataset.sessions);
    const auto missing_references = std::count_if(result.coverage.begin(), result.coverage.end(), [](const auto& x) {
        return !x.account_references_complete;
    });
    const auto missing_risk = std::count_if(result.coverage.begin(), result.coverage.end(), [](const auto& x) {
        return !x.rv5_endpoints_complete;
    });
    if (missing_references != 0) { result.blockers.push_back(std::to_string(missing_references) + " sessions lack required account price references."); }
    if (missing_risk != 0) { result.blockers.push_back(std::to_string(missing_risk) + " sessions lack required five-minute risk endpoints; a missing-data policy is unresolved."); }
    result.blockers.push_back("Full 2021 daily calibration and leading 20-session intraday history are not part of the verified input contract.");
    result.blockers.push_back("Raw-price dividend/split/distribution ledger, ex/pay dates and independent distribution totals are not reconciled.");
    result.blockers.push_back("The candidate protocol, missing-risk policy and same-date action ordering must be registered before scoring.");
    if (progress) { progress("Checking synthetic account, dividend, payment and split identities"); }
    result.accounting = demonstrate_long_cash_account();
    return result;
}

} // namespace trading
