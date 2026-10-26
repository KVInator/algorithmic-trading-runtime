#include "panel_input_finalizer.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include "daily_risk_rules.hpp"
#include "risk_input_dataset.hpp"
#include "response_bytes.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {
using Json = nlohmann::json;
Json checked(const std::filesystem::path& root, const Json& ref) {
    const auto file = contained_path(root, ref.at("file").get<std::string>());
    if (sha256_file(file) != ref.at("sha256").get<std::string>()) { throw std::invalid_argument("panel finalization evidence identity mismatch"); }
    return parse_json_document(read_text_file(file, 8U * 1024U * 1024U));
}
std::string date(const Json& item, const char* field) {
    const auto value = item.at(field).get<std::string>();
    if (value.size() != 10) { throw std::invalid_argument("issuer date requires YYYY-MM-DD"); }
    static_cast<void>(parse_utc_seconds(value + "T00:00:00Z"));
    return value;
}
double number(const Json& item, const char* field) {
    const auto& value = item.at(field);
    if (!value.is_number()) { throw std::invalid_argument("issuer financial field must be numeric"); }
    const auto result = value.get<double>();
    if (!std::isfinite(result) || result <= 0) { throw std::invalid_argument("issuer financial field must be positive and finite"); }
    return result;
}
bool in_horizon(std::string_view ex) { return ex >= "2016-01-01" && ex <= "2025-12-31"; }
void publish(const std::filesystem::path& file, const Json& doc) {
    if (!std::filesystem::exists(file)) { write_new_text_file(file, doc.dump(2) + '\n'); }
    if (parse_json_document(read_text_file(file, 16U * 1024U * 1024U)) != doc) {
        throw std::invalid_argument("refusing to replace a published panel finalization record");
    }
}
} // namespace

nlohmann::json PanelActionReference::reconcile(const std::filesystem::path& dataset,
    const std::filesystem::path& reference_file, const ExtensionStudyContract& contract, PanelOrigin origin) const {
    if (!std::filesystem::exists(reference_file)) {
        return {{"issuer_reconciled", false}, {"blockers", {"Independent issuer action reference is missing."}}};
    }
    const auto doc = parse_json_document(read_text_file(reference_file, 8U * 1024U * 1024U));
    const std::string expected_origin = origin == PanelOrigin::provider ? "provider" : "synthetic";
    if (doc.at("schema_version").get<int>() != 1 || doc.at("evidence_origin").get<std::string>() != expected_origin ||
        doc.at("first_ex_date").get<std::string>() != "2016-01-01" || doc.at("last_ex_date").get<std::string>() != "2025-12-31") {
        throw std::invalid_argument("issuer reference origin or horizon mismatch");
    }
    const auto symbols = contract.symbols();
    std::map<std::string, std::set<std::string>> authority;
    std::set<std::string> source_files;
    for (const auto& source : doc.at("sources")) {
        const auto file = source.at("file").get<std::string>(), url = source.at("url").get<std::string>();
        const auto raw = contained_path(reference_file.parent_path(), file);
        static_cast<void>(parse_utc_seconds(source.at("retrieved_utc").get<std::string>()));
        if (!source_files.insert(file).second || source.at("sha256").get<std::string>() != sha256_file(raw) ||
            std::filesystem::file_size(raw) > 100U * 1024U * 1024U) {
            throw std::invalid_argument("issuer source identity or size mismatch");
        }
        bool official = false;
        for (const auto host : {"https://www.ssga.com/", "https://www.ishares.com/", "https://www.invesco.com/",
                "https://dng-api.invesco.com/", "https://www.spdrgoldshares.com/", "https://www.sec.gov/"}) { official = official || url.starts_with(host); }
        if (origin == PanelOrigin::provider && !official) { throw std::invalid_argument("issuer reference requires an identified primary-source URL"); }
    }
    for (const auto& coverage : doc.at("coverage")) {
        const auto symbol = coverage.at("symbol").get<std::string>();
        if (std::find(symbols.begin(), symbols.end(), symbol) == symbols.end() || authority.contains(symbol)) {
            throw std::invalid_argument("issuer reference requires distinct registered symbols");
        }
        if (!coverage.at("cash_complete").get<bool>() || !coverage.at("splits_complete").get<bool>()) {
            return {{"issuer_reconciled", false}, {"issuer_reference_sha256", sha256_file(reference_file)},
                {"blockers", {"Independent full-horizon cash/split coverage remains unverified for " + symbol + "."}}};
        }
        auto& files = authority[symbol];
        for (const auto& source : coverage.at("source_files")) {
            const auto file = source.get<std::string>();
            if (!source_files.contains(file)) { throw std::invalid_argument("issuer coverage source is not authenticated"); }
            files.insert(file);
        }
        if (files.empty() || coverage.at("coverage_locator").get<std::string>().empty()) {
            throw std::invalid_argument("issuer coverage requires a source location and supporting files");
        }
    }
    if (authority.size() != symbols.size()) { throw std::invalid_argument("issuer reference must cover all five symbols including empty histories"); }
    std::map<std::string, Json> cash, splits;
    auto validate_source = [&](const Json& row) {
        const auto symbol = row.at("symbol").get<std::string>();
        if (!authority.contains(symbol) || !authority.at(symbol).contains(row.at("source_file").get<std::string>()) ||
            row.at("source_locator").get<std::string>().empty()) { throw std::invalid_argument("issuer action source locator is unavailable"); }
        const auto ex = date(row, "ex_date");
        if (!in_horizon(ex)) { throw std::invalid_argument("issuer action lies outside the registered ex-date horizon"); }
        return symbol + '/' + ex;
    };
    for (const auto& row : doc.at("cash_distributions")) {
        const auto key = validate_source(row);
        const auto ex = date(row, "ex_date"), record = date(row, "record_date"), pay = date(row, "payable_date");
        static_cast<void>(number(row, "usd_per_share"));
        if (record < ex || pay < record || !cash.emplace(key, row).second) {
            throw std::invalid_argument("issuer cash action is duplicated or has invalid chronology");
        }
    }
    for (const auto& row : doc.at("splits")) {
        const auto key = validate_source(row);
        static_cast<void>(number(row, "old_rate")); static_cast<void>(number(row, "new_rate"));
        if (!splits.emplace(key, row).second) { throw std::invalid_argument("ambiguous issuer split on the same ex-date"); }
    }
    Json blockers = Json::array();
    std::set<std::string> ids, provider_cash, provider_splits;
    std::map<std::string, double> rates;
    std::size_t completed_dates = 0;
    const auto manifest = parse_json_document(read_text_file(dataset / "acquisition_manifest.json"));
    for (const auto& interval_ref : manifest.at("actions")) {
        const auto interval = checked(dataset, interval_ref);
        for (const auto& page_ref : interval.at("pages")) {
            const auto envelope = checked(dataset, page_ref);
            const auto body_text = decode_response_bytes(envelope.at("body_hex").get<std::string>());
            if (sha256(body_text) != envelope.at("body_sha256").get<std::string>()) { throw std::invalid_argument("action page body changed"); }
            const auto body = parse_json_document(body_text);
            for (const auto& [kind, rows] : body.at("corporate_actions").items()) {
                for (const auto& row : rows) {
                    if (!ids.insert(row.at("id").get<std::string>()).second) { throw std::invalid_argument("duplicate action identity across process intervals"); }
                    const auto ex = row.contains("ex_date") && row.at("ex_date").is_string() ? date(row, "ex_date") : std::string{};
                    if (!in_horizon(ex)) {
                        if (ex.empty()) { blockers.push_back("Unsupported action without an ex-date."); }
                        continue;
                    }
                    const auto key = row.at("symbol").get<std::string>() + '/' + ex;
                    if (kind == "cash_dividends") {
                        if (!provider_cash.insert(key).second) { blockers.push_back("Multiple provider cash records require explicit reconciliation: " + key); }
                        rates[key] += number(row, "rate");
                        if (!cash.contains(key)) { blockers.push_back("Provider-only cash action: " + key); continue; }
                        for (const auto* field : {"record_date", "payable_date"}) {
                            if (!row.contains(field) || row.at(field).is_null() || row.at(field) == "") { ++completed_dates; }
                            else if (date(row, field) != cash.at(key).at(field).get<std::string>()) { blockers.push_back("Issuer/provider date disagreement: " + key); }
                        }
                    } else if (kind == "forward_splits" || kind == "reverse_splits" || kind == "unit_splits") {
                        provider_splits.insert(key);
                        if (!splits.contains(key) || std::abs(number(row, "new_rate") / number(row, "old_rate") -
                            number(splits.at(key), "new_rate") / number(splits.at(key), "old_rate")) > 1e-12) {
                            blockers.push_back("Issuer/provider split disagreement: " + key);
                        }
                    } else { blockers.push_back("Unsupported action type: " + kind); }
                }
            }
        }
    }
    for (const auto& [key, rate] : rates) {
        if (cash.contains(key) && std::abs(rate - number(cash.at(key), "usd_per_share")) >
            contract.document().at("corporate_actions").at("cash_rate_absolute_tolerance_usd_per_share").get<double>()) {
            blockers.push_back("Issuer/provider cash amount disagreement: " + key);
        }
    }
    std::size_t issuer_only_cash = 0, issuer_only_splits = 0;
    Json canonical_cash = Json::array(), canonical_splits = Json::array();
    for (const auto& [key, row] : cash) { canonical_cash.push_back(row); issuer_only_cash += provider_cash.contains(key) ? 0U : 1U; }
    for (const auto& [key, row] : splits) { canonical_splits.push_back(row); issuer_only_splits += provider_splits.contains(key) ? 0U : 1U; }
    Json result{{"issuer_reconciled", blockers.empty()}, {"blockers", blockers}, {"issuer_reference_sha256", sha256_file(reference_file)},
        {"cash_distributions", cash.size()}, {"splits", splits.size()}, {"issuer_only_cash_records", issuer_only_cash},
        {"issuer_only_split_records", issuer_only_splits}, {"issuer_completed_date_fields", completed_dates}};
    if (blockers.empty()) {
        // Financial fields remain local; the review record carries counts only.
        const Json canonical{{"schema_version", 1}, {"protocol_sha256", contract.identity()},
            {"evidence_origin", expected_origin},
            {"issuer_reference_sha256", result.at("issuer_reference_sha256")}, {"cash_distributions", canonical_cash}, {"splits", canonical_splits}};
        publish(dataset / "sealed/accounting_actions.json", canonical);
        result["canonical_actions_sha256"] = sha256_file(dataset / "sealed/accounting_actions.json");
    }
    return result;
}

PanelInputFinalizer::PanelInputFinalizer(std::filesystem::path dataset, ExtensionStudyContract contract, PanelOrigin origin)
    : directory_{std::move(dataset)}, contract_{std::move(contract)}, origin_{origin} {}

nlohmann::json PanelInputFinalizer::finalize(const Json& audited_panel, const Json& action_assessment,
    const FrozenDevelopmentDataset& legacy, std::span<const SessionDownload> legacy_calibration, std::string_view legacy_risk_identity) const {
    const auto manifest_file = directory_ / "acquisition_manifest.json";
    const std::string expected_origin = origin_ == PanelOrigin::provider ? "provider" : "synthetic";
    if (audited_panel.at("acquisition_manifest_sha256").get<std::string>() != sha256_file(manifest_file) ||
        audited_panel.at("evidence_origin").get<std::string>() != expected_origin ||
        audited_panel.at("protocol_sha256").get<std::string>() != contract_.identity() || !action_assessment.at("issuer_reconciled").get<bool>()) {
        throw std::invalid_argument("panel finalization requires authenticated prices and reconciled issuer actions");
    }
    if (sha256_file(directory_ / "sealed/accounting_actions.json") != action_assessment.at("canonical_actions_sha256").get<std::string>()) {
        throw std::invalid_argument("canonical accounting action identity changed");
    }
    const auto canonical = parse_json_document(read_text_file(directory_ / "sealed/accounting_actions.json", 8U * 1024U * 1024U));
    if (canonical.at("protocol_sha256").get<std::string>() != contract_.identity() ||
        canonical.at("issuer_reference_sha256") != action_assessment.at("issuer_reference_sha256")) {
        throw std::invalid_argument("canonical action reference provenance changed");
    }
    if (origin_ == PanelOrigin::provider && (legacy.manifest_sha256 != risk_rules::development_digest ||
        legacy_risk_identity != risk_rules::acquisition_digest || legacy_calibration.size() != risk_price_input_plan().size())) {
        throw std::invalid_argument("vintage bridge requires the original registered SPY study snapshot");
    }
    const HistoricalPanelDataset panel{directory_, contract_, origin_};
    Json bridge = Json::array(), masks = Json::array();
    const auto acquisition_sha = sha256_file(manifest_file);
    const auto compare = [&](const SessionDownload& old, const std::string& role) {
        const auto day = format_utc_seconds(old.request.start()).substr(0, 10);
        const auto current = panel.load_development_session("SPY", day, acquisition_sha);
        std::map<std::chrono::sys_seconds, const MinuteBar*> newer;
        for (const auto& bar : current.bars) { newer.emplace(bar.start(), &bar); }
        std::size_t changed = 0, missing = 0;
        for (const auto& bar : old.bars) {
            const auto found = newer.find(bar.start());
            if (found == newer.end()) { ++missing; continue; }
            const auto& next = *found->second;
            changed += bar.prices().open != next.prices().open || bar.prices().high != next.prices().high ||
                bar.prices().low != next.prices().low || bar.prices().close != next.prices().close || bar.volume() != next.volume() ? 1U : 0U;
        }
        bridge.push_back({{"date", day}, {"role", role}, {"old_window_start", format_utc_seconds(old.request.start())},
            {"old_window_end", format_utc_seconds(old.request.end_exclusive())}, {"old_minutes", old.bars.size()}, {"new_minutes", current.bars.size()},
            {"changed_common_bars", changed}, {"missing_old_minutes_in_new", missing}});
    };
    for (const auto& old : legacy.sessions) { compare(old, "development"); }
    for (const auto& old : legacy_calibration) { compare(old, "risk_calibration_or_supplement"); }
    if (origin_ == PanelOrigin::provider && legacy.sessions.size() != 496) { throw std::invalid_argument("vintage bridge must retain all 496 original scored sessions"); }
    std::set<std::string> common_years;
    std::map<std::string, std::pair<std::size_t, std::size_t>> annual_coverage;
    bool endpoint_gap = false;
    struct HistoryState { std::size_t eligible{}, consecutive_closes{}; std::deque<bool> regular_slots; };
    std::map<std::string, HistoryState> histories;
    Json first_scored = Json::object();
    const auto history_length = contract_.document().at("history").at("primary_sessions").get<std::size_t>();
    const auto risk_closes = contract_.document().at("history").at("risk_and_overlay_returns").get<std::size_t>() + 1;
    const auto initialization_last = contract_.document().at("archive").at("initialization_last").get<std::string>();
    const auto development_last = contract_.document().at("archive").at("development_last").get<std::string>();
    for (const auto& row : audited_panel.at("coverage")) {
        if (!row.at("open_available").get<bool>() || !row.at("close_available").get<bool>()) { endpoint_gap = true; }
        const auto day = row.at("date").get<std::string>();
        const auto symbol = row.at("symbol").get<std::string>();
        auto& history = histories[symbol];
        auto mask = row;
        const bool base = row.at("intraday_eligible").get<bool>();
        const bool scored_period = day > initialization_last;
        mask["momentum_inputs_ready"] = scored_period && base;
        mask["continuation_inputs_ready"] = scored_period && base && history.eligible >= history_length;
        mask["reversion_inputs_ready"] = scored_period && base && history.regular_slots.size() == history_length;
        mask["reversion_history_complete"] = history.regular_slots.size() == history_length &&
            std::all_of(history.regular_slots.begin(), history.regular_slots.end(), [](bool valid) { return valid; });
        mask["daily_risk_inputs_ready"] = scored_period && history.consecutive_closes >= risk_closes &&
            row.at("open_available").get<bool>() && row.at("close_available").get<bool>();
        for (const auto* key : {"momentum_inputs_ready", "continuation_inputs_ready", "reversion_inputs_ready", "daily_risk_inputs_ready"}) {
            if (mask.at(key).get<bool>() && !first_scored[symbol].contains(key)) { first_scored[symbol][key] = day; }
        }
        masks.push_back(std::move(mask));
        history.eligible += base ? 1U : 0U;
        history.consecutive_closes = row.at("close_available").get<bool>() ? history.consecutive_closes + 1 : 0;
        if (row.at("regular").get<bool>()) {
            history.regular_slots.push_back(base);
            if (history.regular_slots.size() > history_length) { history.regular_slots.pop_front(); }
        }
        if (day > initialization_last && day <= development_last && row.at("symbol").get<std::string>() == "SPY" && row.at("regular").get<bool>()) {
            auto& count = annual_coverage[day.substr(0, 4)];
            ++count.second;
            count.first += row.at("common_intraday_eligible").get<bool>() ? 1U : 0U;
        }
    }
    for (const auto& [year, counts] : annual_coverage) {
        if (counts.first >= contract_.document().at("eligibility").at("minimum_common_development_dates_per_qualified_year").get<std::size_t>() &&
            static_cast<double>(counts.first) / static_cast<double>(counts.second) >=
                contract_.document().at("eligibility").at("minimum_common_development_calendar_coverage").get<double>()) { common_years.insert(year); }
    }
    if (endpoint_gap || (origin_ == PanelOrigin::provider && common_years.size() < contract_.document().at("eligibility").at("minimum_scored_years").get<std::size_t>())) {
        throw std::invalid_argument("panel is input-limited: missing continuous daily marks or fewer than five development years");
    }
    publish(directory_ / "vintage_bridge.json", {{"schema_version", 1}, {"legacy_manifest_sha256", legacy.manifest_sha256},
        {"legacy_risk_acquisition_sha256", std::string{legacy_risk_identity}}, {"new_acquisition_sha256", acquisition_sha}, {"rows", bridge}, {"strategy_returns_compared", false}});
    publish(directory_ / "eligibility_masks.json", {{"schema_version", 1}, {"protocol_sha256", contract_.identity()}, {"rows", masks}});
    const Json result{{"schema_version", 1}, {"kind", "extension_study_manifest"}, {"protocol_sha256", contract_.identity()},
        {"evidence_origin", expected_origin},
        {"acquisition_manifest_sha256", acquisition_sha}, {"canonical_actions_sha256", action_assessment.at("canonical_actions_sha256")},
        {"issuer_reference_sha256", action_assessment.at("issuer_reference_sha256")},
        {"vintage_bridge_sha256", sha256_file(directory_ / "vintage_bridge.json")},
        {"eligibility_masks_sha256", sha256_file(directory_ / "eligibility_masks.json")},
        {"first_scored_input_dates", first_scored}, {"model_fit_availability_evaluated", false},
        {"source_sha256", TRADING_SOURCE_SHA256}, {"compiler", TRADING_COMPILER_ID}, {"build_configuration", TRADING_BUILD_CONFIGURATION},
        {"reserved_strategy_inputs_opened", false}, {"strategy_scored", false}, {"research_inputs_ready", true}};
    publish(directory_ / "study_manifest.json", result);
    return result;
}
} // namespace trading
