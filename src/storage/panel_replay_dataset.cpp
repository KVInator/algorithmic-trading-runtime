#include "panel_replay_dataset.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace trading {
PanelReplayDataset::PanelReplayDataset(std::filesystem::path directory, ExtensionStudyContract contract,
    PanelInputPolicy policy, PanelSourceResolution resolution, PanelOrigin origin, std::string expected)
    : directory_(std::move(directory)), contract_(std::move(contract)), policy_(std::move(policy)), origin_(origin) {
    const auto file = directory_ / "study_manifest.json";
    if (sha256_file(file) != expected) { throw std::invalid_argument("panel study registration differs from the accepted M2 identity"); }
    manifest_ = parse_json_document(read_text_file(file));
    const std::string label = origin == PanelOrigin::provider ? "provider" : "synthetic";
    if (manifest_.at("kind").get<std::string>() != "extension_study_manifest" || manifest_.at("evidence_origin").get<std::string>() != label ||
        manifest_.at("protocol_sha256").get<std::string>() != contract_.identity() || manifest_.at("input_policy_sha256").get<std::string>() != policy_.identity() ||
        manifest_.at("source_resolution_sha256").get<std::string>() != resolution.identity() || !manifest_.at("research_inputs_ready").get<bool>() ||
        manifest_.at("strategy_scored").get<bool>() || manifest_.at("reserved_strategy_inputs_opened").get<bool>()) {
        throw std::invalid_argument("panel registration contract or access boundary changed");
    }
    for (const auto& item : std::vector<std::pair<std::string, std::string>>{{"acquisition_manifest.json", "acquisition_manifest_sha256"},
        {"eligibility_masks.json", "eligibility_masks_sha256"}, {"vintage_bridge.json", "vintage_bridge_sha256"},
        {"sealed/accounting_actions.json", "canonical_actions_sha256"}}) {
        const auto bound = item.first == "eligibility_masks.json" ? 16U * 1024U * 1024U : 4U * 1024U * 1024U;
        if (sha256_file(directory_ / item.first, bound) != manifest_.at(item.second).get<std::string>()) { throw std::invalid_argument("registered panel artifact changed: " + item.first); }
    }
    masks_ = parse_json_document(read_text_file(directory_ / "eligibility_masks.json", 16U * 1024U * 1024U));
    if (masks_.at("protocol_sha256").get<std::string>() != contract_.identity()) { throw std::invalid_argument("mask protocol identity changed"); }
    actions_ = parse_json_document(read_text_file(directory_ / "sealed/accounting_actions.json", 8U * 1024U * 1024U));
    const auto& actions = actions_;
    if (actions.at("protocol_sha256").get<std::string>() != contract_.identity() || actions.at("source_resolution_sha256").get<std::string>() != resolution.identity() ||
        actions.at("issuer_reference_sha256").get<std::string>() != manifest_.at("issuer_reference_sha256").get<std::string>() || !actions.at("splits").empty()) {
        throw std::invalid_argument("panel action identities or registered no-split basis changed");
    }
    const auto issuer_file = directory_ / (origin == PanelOrigin::provider ? "issuer/reference_resolved.json" : "../issuer/resolved.json");
    if (sha256_file(issuer_file, 8U * 1024U * 1024U) != manifest_.at("issuer_reference_sha256").get<std::string>()) {
        throw std::invalid_argument("panel issuer reference identity changed");
    }
    const auto reference = parse_json_document(read_text_file(issuer_file, 8U * 1024U * 1024U));
    for (const auto& source : reference.at("sources")) {
        if (sha256_file(contained_path(issuer_file.parent_path(), source.at("file").get<std::string>()), 20U * 1024U * 1024U) != source.at("sha256").get<std::string>()) {
            throw std::invalid_argument("retained action source changed");
        }
    }
    for (const auto& row : actions.at("cash_distributions")) {
        const auto day = row.at("ex_date").get<std::string>();
        if (contract_.reserved(day)) { continue; }
        const auto value = row.at("usd_per_share").get<double>();
        if (!std::isfinite(value) || value <= 0 || !dividends_.emplace(row.at("symbol").get<std::string>() + '/' + day, value).second) {
            throw std::invalid_argument("canonical cash event is invalid or duplicated");
        }
    }
    if (origin == PanelOrigin::synthetic) {
        const auto settings = parse_json_document(read_text_file(directory_ / "dataset_contract.json"));
        for (const auto& row : settings.at("ranges")) { ranges_.emplace_back(row.at("first").get<std::string>(), row.at("last").get<std::string>()); }
    }
    const HistoricalPanelDataset panel{directory_, contract_, origin_, ranges_};
    if (!manifest_.at("closing_recovery_manifest_sha256").is_null()) {
        if (sha256_file(directory_ / "resolutions/closing_marks/manifest.json") != manifest_.at("closing_recovery_manifest_sha256").get<std::string>()) {
            throw std::invalid_argument("closing recovery identity changed");
        }
        auto audit = manifest_; audit["coverage"] = masks_.at("rows");
        const auto recovered = panel.recover_closing_marks(policy_, audit, {}, [](auto) {}, false);
        for (const auto& bar : recovered.bars) { recovered_.emplace(bar.symbol() + '/' + format_utc_seconds(bar.start()).substr(0, 10), bar); }
    }
    provenance_ = manifest_;
    provenance_["study_manifest_sha256"] = expected;
}
const nlohmann::json& PanelReplayDataset::provenance() const noexcept { return provenance_; }
const nlohmann::json& PanelReplayDataset::accounting_actions() const noexcept { return actions_; }
std::optional<MinuteBar> PanelReplayDataset::recovered_close(const std::string& symbol, const std::string& date) const {
    const auto it = recovered_.find(symbol + '/' + date);
    return it == recovered_.end() ? std::nullopt : std::optional<MinuteBar>{it->second};
}
void PanelReplayDataset::replay(const Consumer& consume, const HistoricalPanelDataset::Progress& progress) const {
    if (!consume) { throw std::invalid_argument("panel replay requires a record consumer"); }
    replay_with_sessions([&](auto day, auto) { consume(std::move(day)); }, progress);
}
void PanelReplayDataset::replay_with_sessions(const MarketConsumer& consume, const HistoricalPanelDataset::Progress& progress) const {
    if (!consume) { throw std::invalid_argument("panel replay requires a market-record consumer"); }
    const HistoricalPanelDataset panel{directory_, contract_, origin_, ranges_};
    std::map<std::string, PanelReplayState> states;
    const auto symbols = contract_.symbols();
    for (const auto& symbol : symbols) { states.emplace(symbol, PanelReplayState{symbol, policy_}); }
    std::vector<PanelReplayDay> day;
    std::vector<SessionDownload> sessions;
    std::size_t dates = 0;
    std::string previous;
    for (const auto& mask : masks_.at("rows")) {
        const auto date = mask.at("date").get<std::string>(), symbol = mask.at("symbol").get<std::string>();
        if (contract_.reserved(date)) { continue; }
        contract_.require_development_access(date);
        if (date != previous && !day.empty()) { throw std::invalid_argument("registered panel masks do not group complete five-symbol dates"); }
        if (std::find(symbols.begin(), symbols.end(), symbol) == symbols.end()) { throw std::invalid_argument("unexpected registered mask symbol"); }
        auto session = panel.load_development_session(symbol, date, manifest_.at("acquisition_manifest_sha256").get<std::string>());
        const auto key = symbol + '/' + date;
        const auto recovered = recovered_.find(key);
        auto row = states.at(symbol).advance(session.request, session.bars, dividends_.contains(key) ? dividends_.at(key) : 0,
            recovered == recovered_.end() ? std::nullopt : std::optional<MinuteBar>{recovered->second});
        if (row.availability.operational != mask.at("input_availability").at("operational").get<bool>() ||
            row.availability.momentum_features != mask.at("input_availability").at("momentum_features").get<bool>() ||
            row.availability.reversion_features != mask.at("input_availability").at("reversion_features").get<bool>() ||
            row.availability.continuation_prefixes != mask.at("input_availability").at("continuation_prefixes").get<std::vector<bool>>()) {
            throw std::invalid_argument("replayed availability differs from registered masks");
        }
        day.push_back(std::move(row)); previous = date;
        sessions.push_back(std::move(session));
        if (day.size() == symbols.size()) {
            std::set<std::string> seen;
            for (const auto& item : day) { seen.insert(item.symbol); }
            if (seen.size() != symbols.size()) { throw std::invalid_argument("a symbol was duplicated in a registered panel date"); }
            const bool common = std::all_of(day.begin(), day.end(), [](const auto& item) { return item.availability.operational; });
            for (auto& item : day) { item.common_operational = common; }
            consume(std::move(day), std::move(sessions)); day.clear(); sessions.clear(); ++dates;
            if (progress && dates % 25 == 0) { progress("Prior-only panel replay | " + std::to_string(dates) + " dates through " + date); }
        }
    }
    if (!day.empty()) { throw std::invalid_argument("registered replay ended inside a five-symbol date"); }
}
} // namespace trading
