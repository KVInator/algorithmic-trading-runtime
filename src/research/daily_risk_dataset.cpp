#include "daily_risk_study.hpp"
#include "daily_risk_rules.hpp"
#include "risk_preflight.hpp"
#include "content_digest.hpp"
#include "session_storage.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

namespace trading {
namespace risk_rules {
std::vector<CalendarSession> calibration_calendar() {
    std::vector<CalendarSession> sessions;
    for (const auto& range : risk_calibration_ranges()) {
        if (range.first().starts_with("2020")) { continue; }
        const auto month = risk_calibration_calendar(range);
        sessions.insert(sessions.end(), month.begin(), month.end());
    }
    return sessions;
}
std::vector<CalendarSession> development_calendar() {
    std::vector<CalendarSession> sessions;
    for (const auto& month : development_months()) {
        const auto rows = trading::development_calendar(month.range);
        sessions.insert(sessions.end(), rows.begin(), rows.end());
    }
    return sessions;
}
} // namespace risk_rules

DailyRiskDataset::DailyRiskDataset(std::filesystem::path root) : root_{std::move(root)} {}
RiskStudyInputs DailyRiskDataset::load(const SessionBatch::Progress& progress) const {
    if (progress) { progress("Verifying registered protocol and immutable acquisition identities"); }
    RiskStudyInputs result{};
    result.protocol_sha256 = sha256_file(root_ / "research/daily_risk_protocol.md") + ":" +
        sha256_file(root_ / "research/daily_risk_protocol.json");
    const auto directory = root_ / "scratch/datasets/SPY_RISK_01_inputs";
    result.acquisition_manifest_sha256 = sha256_file(directory / "acquisition_manifest.json");
    result.issuer_reference_sha256 = sha256_file(root_ / "research/spy_distribution_reference.json");
    if (result.protocol_sha256 != risk_rules::protocol_identity ||
        result.acquisition_manifest_sha256 != risk_rules::acquisition_digest ||
        result.issuer_reference_sha256 != risk_rules::issuer_digest) {
        throw std::invalid_argument("registered risk protocol or acquisition identity mismatch; scoring stopped");
    }
    const auto inventory = RiskInputDataset{directory, root_ / "research/spy_distribution_reference.json"}.load();
    if (!inventory.required_evidence_complete || !inventory.cash_distributions_reconciled || !inventory.actions.unsupported_types().empty()) {
        throw std::invalid_argument("risk acquisition is incomplete or corporate actions are not reconciled");
    }
    result.distributions = inventory.actions.distributions();
    for (const auto& [file, digest] : inventory.evidence_hashes) { result.input_evidence.emplace_back("risk_acquisition/" + file, digest); }
    if (progress) { progress("Loading 252 calibration closes and twenty full warm-up sessions"); }
    std::map<std::string, const RiskInputRecord*> calibration;
    for (const auto& row : inventory.prices) {
        if (row.input.role == "boundary_close") {
            const auto download = load_session_download(row.directory, row.input.request);
            if (download.bars.size() != 1) { throw std::invalid_argument("invalid 2020 closing boundary"); }
            result.boundary_close = download.bars.back().prices().close;
        } else if (row.input.date.starts_with("2021")) { calibration.emplace(row.input.date, &row); }
    }
    for (const auto& session : risk_rules::calibration_calendar()) {
        const auto it = calibration.find(session.date());
        if (it == calibration.end()) { throw std::invalid_argument("missing calibration session " + session.date()); }
        auto download = load_session_download(it->second->directory, it->second->input.request);
        result.calibration.emplace_back(session, std::move(download.bars));
    }
    auto verified = RiskPreflight{root_}.run(progress);
    result.development_manifest_sha256 = verified.input_manifest_sha256;
    if (result.development_manifest_sha256 != risk_rules::development_digest || verified.coverage.size() != 501 ||
        std::any_of(verified.coverage.begin(), verified.coverage.end(), [](const auto& x) { return !x.account_references_complete; })) {
        throw std::invalid_argument("registered development identity or required account references do not match");
    }
    for (const auto& row : verified.coverage) {
        if (row.date == "2023-01-03" && row.evidence_sha256 != risk_rules::diagnostic_digest) {
            throw std::invalid_argument("retained diagnostic session differs from the completed acquisition audit");
        }
        result.input_evidence.emplace_back("development/canonical_bars/" + row.date, row.evidence_sha256);
    }
    for (const auto& [month, digest] : verified.calendar_hashes) { result.input_evidence.emplace_back("development/calendar/" + month, digest); }
    std::map<std::string, SessionDownload*> downloads;
    for (auto& session : verified.sessions) {
        const auto date = std::find_if(verified.calendar.begin(), verified.calendar.end(), [&](const auto& day) {
            return day.open_utc() == session.request.start();
        });
        if (date == verified.calendar.end() || !downloads.emplace(date->date(), &session).second) {
            throw std::invalid_argument("duplicate or unbounded development session");
        }
    }
    for (const auto& session : verified.calendar) {
        const auto it = downloads.find(session.date());
        if (it == downloads.end()) { throw std::invalid_argument("missing account session " + session.date()); }
        result.development.emplace_back(session, std::move(it->second->bars));
    }
    return result;
}
} // namespace trading
