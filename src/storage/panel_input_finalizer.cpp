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
Json registered_coverage(const Json& coverage, const ExtensionStudyContract& contract, PanelOrigin origin) {
    std::map<std::string, std::pair<std::size_t, std::size_t>> counts;
    Json endpoints = Json::array(), years = Json::array();
    const auto& rules = contract.document().at("eligibility");
    const auto& archive = contract.document().at("archive");
    for (const auto& row : coverage) {
        const auto day = row.at("date").get<std::string>();
        if (!row.at("open_available").get<bool>() || !row.at("close_available").get<bool>()) {
            endpoints.push_back({{"symbol", row.at("symbol")}, {"date", day},
                {"open_available", row.at("open_available")}, {"close_available", row.at("close_available")}});
        }
        if (day > archive.at("initialization_last").get<std::string>() && day <= archive.at("development_last").get<std::string>() &&
            row.at("symbol").get<std::string>() == "SPY" && row.at("regular").get<bool>()) {
            auto& count = counts[day.substr(0, 4)];
            ++count.second;
            count.first += row.at("common_intraday_eligible").get<bool>() ? 1U : 0U;
        }
    }
    std::size_t qualified = 0;
    for (const auto& [year, count] : counts) {
        const double fraction = static_cast<double>(count.first) / static_cast<double>(count.second);
        const bool passes = count.first >= rules.at("minimum_common_development_dates_per_qualified_year").get<std::size_t>() &&
            fraction >= rules.at("minimum_common_development_calendar_coverage").get<double>();
        qualified += passes ? 1U : 0U;
        years.push_back({{"year", year}, {"common_dates", count.first}, {"regular_dates", count.second},
            {"coverage_fraction", fraction}, {"qualifies", passes}});
    }
    const auto minimum = rules.at("minimum_scored_years").get<std::size_t>();
    return {{"development_years", years}, {"qualified_development_years", qualified}, {"minimum_development_years", minimum},
        {"year_gate_applies", origin == PanelOrigin::provider}, {"missing_daily_endpoints", endpoints},
        {"coverage_gates_passed", endpoints.empty() && (origin == PanelOrigin::synthetic || qualified >= minimum)}};
}
bool recovered_close(std::span<const MinuteBar> recovered, std::string_view symbol, std::string_view date) {
    return std::any_of(recovered.begin(), recovered.end(), [&](const auto& bar) {
        return bar.symbol() == symbol && format_utc_seconds(bar.start()).substr(0, 10) == date;
    });
}
Json remaining_endpoints(const Json& original, std::span<const MinuteBar> recovered) {
    Json remaining = Json::array();
    for (const auto& item : original) {
        if (!item.at("open_available").get<bool>() ||
            (!item.at("close_available").get<bool>() && !recovered_close(recovered, item.at("symbol").get<std::string>(), item.at("date").get<std::string>()))) {
            remaining.push_back(item);
        }
    }
    return remaining;
}
void validate_secondary_events(const std::filesystem::path& raw, const std::string& symbol) {
    const auto data = parse_json_document(read_text_file(raw, 8U * 1024U * 1024U));
    const auto& chart = data.at("chart");
    if (!chart.at("error").is_null() || chart.at("result").size() != 1) {
        throw std::invalid_argument("secondary event response is incomplete");
    }
    const auto& result = chart.at("result").at(0);
    const auto& meta = result.at("meta");
    std::set<std::string> months;
    for (const auto& timestamp : result.at("timestamp")) {
        if (!timestamp.is_number_integer()) { throw std::invalid_argument("secondary coverage timestamp must be an integer"); }
        const auto at = std::chrono::sys_seconds{std::chrono::seconds{timestamp.get<std::int64_t>()}};
        months.insert(format_utc_seconds(at).substr(0, 7));
    }
    bool full_horizon = result.at("timestamp").size() == 120 && months.size() == 120;
    for (int year = 2016; year <= 2025; ++year) {
        for (int month = 1; month <= 12; ++month) {
            full_horizon = full_horizon && months.contains(std::to_string(year) + '-' + (month < 10 ? "0" : "") + std::to_string(month));
        }
    }
    if (meta.at("symbol") != symbol || meta.at("currency") != "USD" || meta.at("dataGranularity") != "1mo" || !full_horizon) {
        throw std::invalid_argument("secondary event symbol, currency or horizon mismatch");
    }
    const auto events = result.value("events", Json::object());
    if (!events.value("splits", Json::object()).empty() || (symbol == "GLD" &&
        (!events.value("dividends", Json::object()).empty() || !events.value("capitalGains", Json::object()).empty()))) {
        throw std::invalid_argument("secondary event history contradicts registered zero-event coverage");
    }
}
} // namespace

nlohmann::json PanelActionReference::reconcile(const std::filesystem::path& dataset,
    const std::filesystem::path& reference_file, const ExtensionStudyContract& contract, PanelOrigin origin, const PanelSourceResolution* resolution) const {
    if (!std::filesystem::exists(reference_file)) {
        return {{"issuer_reconciled", false}, {"blockers", {resolution == nullptr ? "Independent issuer action reference is missing." :
            "Resolved corporate-action reference is missing; run py -3 -B research/prepare_panel_actions.py."}}};
    }
    const auto doc = parse_json_document(read_text_file(reference_file, 8U * 1024U * 1024U));
    const std::string expected_origin = origin == PanelOrigin::provider ? "provider" : "synthetic";
    if (doc.at("schema_version").get<int>() != (resolution == nullptr ? 1 : 2) ||
        (resolution != nullptr && doc.at("source_resolution_sha256") != resolution->identity()) || doc.at("evidence_origin").get<std::string>() != expected_origin ||
        doc.at("first_ex_date").get<std::string>() != "2016-01-01" || doc.at("last_ex_date").get<std::string>() != "2025-12-31") {
        throw std::invalid_argument("issuer reference origin or horizon mismatch");
    }
    const auto symbols = contract.symbols();
    Json blockers = Json::array(), source_coverage = Json::array(), resolved_conflicts = Json::array();
    std::map<std::string, std::string> secondary_files;
    std::set<std::string> secondary_source_files;
    std::string gld_income_source;
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
        if (resolution != nullptr && source.value("kind", std::string{}) == "secondary_events") {
            const auto symbol = source.at("symbol").get<std::string>();
            auto expected_url = resolution->document().at("secondary_event_url").get<std::string>();
            expected_url.replace(expected_url.find("{symbol}"), 8, symbol);
            if (std::find(symbols.begin(), symbols.end(), symbol) == symbols.end() || url != expected_url ||
                !secondary_files.emplace(symbol, file).second) { throw std::invalid_argument("secondary event source identity mismatch"); }
            validate_secondary_events(raw, symbol);
            secondary_source_files.insert(file);
            continue;
        }
        if (resolution != nullptr && url == resolution->document().at("gld_issuer_url").get<std::string>()) {
            if (read_text_file(raw, 20U * 1024U * 1024U).find(
                resolution->document().at("gld_issuer_statement").get<std::string>()) != std::string::npos) { gld_income_source = file; }
        }
        bool official = false;
        for (const auto host : {"https://www.ssga.com/", "https://www.ishares.com/", "https://www.invesco.com/",
                "https://dng-api.invesco.com/", "https://www.spdrgoldshares.com/", "https://www.sec.gov/"}) { official = official || url.starts_with(host); }
        if (origin == PanelOrigin::provider && !official) { throw std::invalid_argument("issuer reference requires an identified primary-source URL"); }
    }
    if (resolution != nullptr && (secondary_files.size() != symbols.size() || gld_income_source.empty())) {
        throw std::invalid_argument("resolved reference requires five authenticated event feeds and GLD issuer evidence");
    }
    for (const auto& coverage : doc.at("coverage")) {
        const auto symbol = coverage.at("symbol").get<std::string>();
        if (std::find(symbols.begin(), symbols.end(), symbol) == symbols.end() || authority.contains(symbol)) {
            throw std::invalid_argument("issuer reference requires distinct registered symbols");
        }
        if (!coverage.at("cash_complete").get<bool>() || !coverage.at("splits_complete").get<bool>()) {
            blockers.push_back("Independent full-horizon cash/split coverage remains unverified for " + symbol + ".");
        }
        source_coverage.push_back({{"symbol", symbol}, {"cash_complete", coverage.at("cash_complete")},
            {"splits_complete", coverage.at("splits_complete")}});
        auto& files = authority[symbol];
        for (const auto& source : coverage.at("source_files")) {
            const auto file = source.get<std::string>();
            if (!source_files.contains(file)) { throw std::invalid_argument("issuer coverage source is not authenticated"); }
            files.insert(file);
        }
        if (resolution != nullptr && (!files.contains(secondary_files.at(symbol)) || (symbol == "GLD" && !files.contains(gld_income_source)))) {
            throw std::invalid_argument("split coverage does not reference its authenticated event feed");
        }
        if ((coverage.at("cash_complete").get<bool>() || coverage.at("splits_complete").get<bool>()) &&
            (files.empty() || coverage.at("coverage_locator").get<std::string>().empty())) {
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
        if (secondary_source_files.contains(row.at("source_file").get<std::string>()) ||
            (resolution != nullptr && row.at("symbol") == "GLD")) {
            throw std::invalid_argument("canonical cash event contradicts source authority");
        }
        const auto ex = date(row, "ex_date"), record = date(row, "record_date"), pay = date(row, "payable_date");
        static_cast<void>(number(row, "usd_per_share"));
        if (record < ex || pay < record || !cash.emplace(key, row).second) {
            throw std::invalid_argument("issuer cash action is duplicated or has invalid chronology");
        }
    }
    if (resolution != nullptr && !doc.at("splits").empty()) {
        throw std::invalid_argument("canonical splits contradict zero-event cross-checks");
    }
    for (const auto& row : doc.at("splits")) {
        const auto key = validate_source(row);
        static_cast<void>(number(row, "old_rate")); static_cast<void>(number(row, "new_rate"));
        if (!splits.emplace(key, row).second) { throw std::invalid_argument("ambiguous issuer split on the same ex-date"); }
    }
    std::set<std::string> ids, provider_cash, provider_splits;
    std::map<std::string, double> rates;
    std::map<std::string, Json> development_provider_cash;
    std::size_t completed_dates = 0;
    const auto cash_conflict = [&](const std::string& reason, const std::string& key) {
        if (resolution == nullptr) { blockers.push_back(reason + ": " + key); }
        else { resolved_conflicts.push_back({{"symbol_ex_date", key}, {"reason", reason}, {"authority", "issuer"}}); }
    };
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
                        if (!provider_cash.insert(key).second) { cash_conflict("Multiple provider cash records", key); }
                        rates[key] += number(row, "rate");
                        if (!contract.reserved(ex)) {
                            if (!development_provider_cash.contains(key)) { development_provider_cash[key] = Json::array(); }
                            Json detail{{"id", row.at("id")}, {"rate", row.at("rate")}};
                            for (const auto* field : {"process_date", "record_date", "payable_date"}) {
                                detail[field] = row.contains(field) ? row.at(field) : Json(nullptr);
                            }
                            development_provider_cash[key].push_back(std::move(detail));
                        }
                        if (!cash.contains(key)) { blockers.push_back("Provider-only cash action: " + key); continue; }
                        if (resolution != nullptr && std::abs(number(row, "rate") - number(cash.at(key), "usd_per_share")) >
                            contract.document().at("corporate_actions").at("cash_rate_absolute_tolerance_usd_per_share").get<double>()) {
                            cash_conflict("Issuer/provider cash amount disagreement", key);
                        }
                        for (const auto* field : {"record_date", "payable_date"}) {
                            if (!row.contains(field) || row.at(field).is_null() || row.at(field) == "") { ++completed_dates; }
                            else if (date(row, field) != cash.at(key).at(field).get<std::string>()) { cash_conflict("Issuer/provider date disagreement", key); }
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
        if (resolution == nullptr && cash.contains(key) && std::abs(rate - number(cash.at(key), "usd_per_share")) >
            contract.document().at("corporate_actions").at("cash_rate_absolute_tolerance_usd_per_share").get<double>()) {
            blockers.push_back("Issuer/provider cash amount disagreement: " + key);
        }
    }
    std::size_t issuer_only_cash = 0, issuer_only_splits = 0;
    Json canonical_cash = Json::array(), canonical_splits = Json::array();
    for (const auto& [key, row] : cash) { canonical_cash.push_back(row); issuer_only_cash += provider_cash.contains(key) ? 0U : 1U; }
    for (const auto& [key, row] : splits) { canonical_splits.push_back(row); issuer_only_splits += provider_splits.contains(key) ? 0U : 1U; }
    Json comparisons = Json::array();
    for (const auto& [key, rows] : development_provider_cash) {
        const auto issuer = cash.find(key);
        const auto tolerance = contract.document().at("corporate_actions").at("cash_rate_absolute_tolerance_usd_per_share").get<double>();
        const bool amount_differs = issuer != cash.end() && (resolution == nullptr ?
            std::abs(rates.at(key) - number(issuer->second, "usd_per_share")) > tolerance :
            std::any_of(rows.begin(), rows.end(), [&](const auto& row) {
                return std::abs(number(row, "rate") - number(issuer->second, "usd_per_share")) > tolerance;
            }));
        bool date_differs = false;
        if (issuer != cash.end()) {
            for (const auto& row : rows) {
                for (const auto* field : {"record_date", "payable_date"}) {
                    if (!row.at(field).is_null() && !row.at(field).get<std::string>().empty() && row.at(field) != issuer->second.at(field)) { date_differs = true; }
                }
            }
        }
        if (issuer == cash.end() || rows.size() > 1 || amount_differs || date_differs) {
            comparisons.push_back({{"symbol_ex_date", key}, {"provider_records", rows},
                {"issuer_record", issuer == cash.end() ? Json(nullptr) : issuer->second},
                {"amount_disagreement", amount_differs}, {"date_disagreement", date_differs}});
        }
    }
    Json result{{"issuer_reconciled", blockers.empty()}, {"blockers", blockers}, {"issuer_reference_sha256", sha256_file(reference_file)},
        {"cash_distributions", cash.size()}, {"splits", splits.size()}, {"issuer_only_cash_records", issuer_only_cash},
        {"issuer_only_split_records", issuer_only_splits}, {"issuer_completed_date_fields", completed_dates},
        {"source_coverage", source_coverage}, {"development_cash_discrepancies", comparisons}};
    if (resolution != nullptr) {
        result["source_resolution_sha256"] = resolution->identity();
        result["resolved_provider_cash_conflicts"] = resolved_conflicts;
        result["split_coverage_basis"] = "secondary_event_feed_cross_check_not_issuer_split_history";
    }
    if (blockers.empty()) {
        // Canonical financial fields remain local; reserved disagreements export keys only.
        const Json canonical{{"schema_version", 1}, {"protocol_sha256", contract.identity()},
            {"evidence_origin", expected_origin},
            {"issuer_reference_sha256", result.at("issuer_reference_sha256")}, {"cash_distributions", canonical_cash}, {"splits", canonical_splits}};
        auto published = canonical;
        if (resolution != nullptr) { published["source_resolution_sha256"] = resolution->identity(); }
        publish(dataset / "sealed/accounting_actions.json", published);
        result["canonical_actions_sha256"] = sha256_file(dataset / "sealed/accounting_actions.json");
    }
    return result;
}

PanelInputFinalizer::PanelInputFinalizer(std::filesystem::path dataset, ExtensionStudyContract contract, PanelOrigin origin)
    : directory_{std::move(dataset)}, contract_{std::move(contract)}, origin_{origin} {}

nlohmann::json PanelInputFinalizer::assess(const Json& audited_panel, const Json& action_assessment, const PanelInputPolicy* input_policy, const PanelSourceResolution* resolution) const {
    if (resolution != nullptr && (input_policy == nullptr || (action_assessment.at("issuer_reconciled").get<bool>() &&
        (!action_assessment.contains("source_resolution_sha256") || action_assessment.at("source_resolution_sha256") != resolution->identity())))) {
        throw std::invalid_argument("readiness requires consistent input and action resolution identities");
    }
    const auto acquisition_sha = sha256_file(directory_ / "acquisition_manifest.json");
    const std::string expected_origin = origin_ == PanelOrigin::provider ? "provider" : "synthetic";
    if (audited_panel.at("acquisition_manifest_sha256").get<std::string>() != acquisition_sha ||
        audited_panel.at("protocol_sha256").get<std::string>() != contract_.identity() ||
        audited_panel.at("evidence_origin").get<std::string>() != expected_origin) {
        throw std::invalid_argument("input assessment requires an authenticated panel of the declared origin");
    }
    auto result = registered_coverage(audited_panel.at("coverage"), contract_, origin_);
    Json gaps = Json::array();
    std::size_t reserved_gap_days = 0;
    const HistoricalPanelDataset panel{directory_, contract_, origin_};
    for (const auto& row : audited_panel.at("coverage")) {
        if (row.at("missing_minutes").get<std::size_t>() == 0) { continue; }
        const auto day = row.at("date").get<std::string>();
        if (contract_.reserved(day)) { ++reserved_gap_days; continue; }
        const auto symbol = row.at("symbol").get<std::string>();
        const auto session = panel.load_development_session(symbol, day, acquisition_sha);
        const auto& missing = session.audit.missing_minutes;
        if (missing.size() != row.at("missing_minutes").get<std::size_t>()) { throw std::invalid_argument("gap assessment disagrees with authenticated session"); }
        for (std::size_t first = 0; first < missing.size();) {
            auto end = first + 1;
            while (end < missing.size() && missing.at(end) == missing.at(end - 1) + std::chrono::minutes{1}) { ++end; }
            gaps.push_back({{"symbol", symbol}, {"date", day}, {"regular", row.at("regular")},
                {"start_utc", format_utc_seconds(missing.at(first))},
                {"end_exclusive_utc", format_utc_seconds(missing.at(end - 1) + std::chrono::minutes{1})},
                {"missing_minutes", end - first}, {"cause", "undetermined"}});
            first = end;
        }
    }
    result["development_gap_intervals"] = std::move(gaps);
    result["reserved_gapped_symbol_days_details_omitted"] = reserved_gap_days;
    result["actions"] = action_assessment;
    result["schema_version"] = 1;
    result["kind"] = "historical_panel_readiness";
    result["evidence_origin"] = expected_origin;
    result["protocol_sha256"] = contract_.identity();
    result["acquisition_manifest_sha256"] = acquisition_sha;
    result["source_sha256"] = TRADING_SOURCE_SHA256;
    result["compiler"] = TRADING_COMPILER_ID;
    result["build_configuration"] = TRADING_BUILD_CONFIGURATION;
    result["input_gates_passed"] = result.at("coverage_gates_passed").get<bool>() && action_assessment.at("issuer_reconciled").get<bool>();
    result["research_inputs_ready"] = false;
    result["strategy_scored"] = false;
    result["reserved_strategy_inputs_opened"] = false;
    result["vintage_bridge_verified"] = false;
    if (input_policy != nullptr) {
        auto assessment = panel.assess_input_policy(*input_policy, acquisition_sha);
        assessment.erase("rows");
        PanelClosingRecovery recovery{Json{{"present", false}}, {}};
        if (std::filesystem::exists(directory_ / "resolutions/closing_marks/manifest.json")) {
            recovery = panel.recover_closing_marks(*input_policy, audited_panel, {}, [](auto) {}, false);
        }
        result["closing_mark_recovery"] = recovery.summary;
        result["remaining_daily_endpoints"] = remaining_endpoints(result.at("missing_daily_endpoints"), recovery.bars);
        const bool missing_allowed = resolution != nullptr && resolution->permits_missing_closes(result.at("remaining_daily_endpoints"), recovery.summary);
        if (resolution != nullptr) {
            result["source_resolution_sha256"] = resolution->identity();
            result["unavailable_daily_marks_accepted"] = missing_allowed;
            result["daily_mark_handling"] = resolution->document().at("unavailable_close_handling");
        }
        result["active_coverage_gates_passed"] = (result.at("remaining_daily_endpoints").empty() || missing_allowed) &&
            (origin_ == PanelOrigin::synthetic || assessment.at("operational_year_gate_passed").get<bool>());
        result["active_input_gates_passed"] = result.at("active_coverage_gates_passed").get<bool>() && action_assessment.at("issuer_reconciled").get<bool>();
        result["input_policy_assessment"] = std::move(assessment);
    }
    return result;
}

nlohmann::json PanelInputFinalizer::finalize(const Json& audited_panel, const Json& action_assessment,
    const FrozenDevelopmentDataset& legacy, std::span<const SessionDownload> legacy_calibration, std::string_view legacy_risk_identity,
    const PanelInputPolicy* input_policy, const PanelSourceResolution* resolution) const {
    if (resolution != nullptr && (input_policy == nullptr || !action_assessment.contains("source_resolution_sha256") ||
        action_assessment.at("source_resolution_sha256") != resolution->identity())) {
        throw std::invalid_argument("registration requires consistent input and action resolution identities");
    }
    const auto manifest_file = directory_ / "acquisition_manifest.json";
    const std::string expected_origin = origin_ == PanelOrigin::provider ? "provider" : "synthetic";
    if (audited_panel.at("acquisition_manifest_sha256").get<std::string>() != sha256_file(manifest_file) ||
        audited_panel.at("evidence_origin").get<std::string>() != expected_origin ||
        audited_panel.at("protocol_sha256").get<std::string>() != contract_.identity() || !action_assessment.at("issuer_reconciled").get<bool>()) {
        throw std::invalid_argument("panel finalization requires authenticated prices and reconciled issuer actions");
    }
    const auto parent_coverage = registered_coverage(audited_panel.at("coverage"), contract_, origin_);
    const HistoricalPanelDataset panel{directory_, contract_, origin_};
    Json amended = input_policy == nullptr ? Json(nullptr) : panel.assess_input_policy(*input_policy, sha256_file(manifest_file));
    PanelClosingRecovery recovery{Json{{"present", false}}, {}};
    if (input_policy != nullptr && std::filesystem::exists(directory_ / "resolutions/closing_marks/manifest.json")) {
        recovery = panel.recover_closing_marks(*input_policy, audited_panel, {}, [](auto) {}, false);
    }
    const bool year_gate = input_policy == nullptr ? parent_coverage.at("coverage_gates_passed").get<bool>() :
        (remaining_endpoints(parent_coverage.at("missing_daily_endpoints"), recovery.bars).empty() ||
            (resolution != nullptr && resolution->permits_missing_closes(
                remaining_endpoints(parent_coverage.at("missing_daily_endpoints"), recovery.bars), recovery.summary))) &&
            (origin_ == PanelOrigin::synthetic || amended.at("operational_year_gate_passed").get<bool>());
    if (!year_gate) {
        throw std::invalid_argument("panel is input-limited: inspect the readiness report for daily-mark and development-year failures");
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
    struct HistoryState { std::size_t eligible{}, consecutive_closes{}; std::deque<bool> regular_slots; };
    std::map<std::string, HistoryState> histories;
    Json first_scored = Json::object();
    const auto history_length = contract_.document().at("history").at("primary_sessions").get<std::size_t>();
    const auto risk_closes = contract_.document().at("history").at("risk_and_overlay_returns").get<std::size_t>() + 1;
    const auto initialization_last = contract_.document().at("archive").at("initialization_last").get<std::string>();
    std::size_t row_index = 0;
    for (const auto& row : audited_panel.at("coverage")) {
        const auto day = row.at("date").get<std::string>();
        const auto symbol = row.at("symbol").get<std::string>();
        auto& history = histories[symbol];
        auto mask = row;
        const bool has_recovered_close = recovered_close(recovery.bars, symbol, day);
        const bool daily_close_available = row.at("close_available").get<bool>() || has_recovered_close;
        mask["daily_close_available"] = daily_close_available;
        mask["daily_close_source"] = !daily_close_available ? "unavailable" : has_recovered_close ? "authenticated_closing_recovery" : "original_archive";
        const bool original_complete = row.at("intraday_eligible").get<bool>();
        bool base = original_complete, reversion_history_valid = original_complete;
        bool momentum_feature_valid = true, continuation_feature_valid = true, reversion_feature_valid = true;
        if (input_policy != nullptr) {
            const auto& availability = amended.at("rows").at(row_index);
            if (availability.at("symbol") != row.at("symbol") || availability.at("date") != row.at("date")) {
                throw std::invalid_argument("input-policy rows do not match the authenticated coverage order");
            }
            mask["input_availability"] = availability;
            base = availability.at("operational").get<bool>();
            momentum_feature_valid = availability.at("momentum_features").get<bool>();
            reversion_feature_valid = availability.at("reversion_features").get<bool>();
            continuation_feature_valid = std::any_of(availability.at("continuation_prefixes").begin(), availability.at("continuation_prefixes").end(),
                [](const auto& valid) { return valid.template get<bool>(); });
            reversion_history_valid = availability.at("reversion_features").get<bool>() && !contract_.diagnostic_excluded(symbol, day);
        }
        ++row_index;
        const bool scored_period = day > initialization_last;
        mask["momentum_inputs_ready"] = scored_period && base && momentum_feature_valid;
        mask["continuation_inputs_ready"] = scored_period && base && continuation_feature_valid && history.eligible >= history_length;
        mask["reversion_inputs_ready"] = scored_period && base && reversion_feature_valid && history.regular_slots.size() == history_length;
        mask["reversion_history_complete"] = history.regular_slots.size() == history_length &&
            std::all_of(history.regular_slots.begin(), history.regular_slots.end(), [](bool valid) { return valid; });
        mask["daily_risk_inputs_ready"] = scored_period && history.consecutive_closes >= risk_closes &&
            row.at("open_available").get<bool>() && daily_close_available;
        for (const auto* key : {"momentum_inputs_ready", "continuation_inputs_ready", "reversion_inputs_ready", "daily_risk_inputs_ready"}) {
            if (mask.at(key).get<bool>() && !first_scored[symbol].contains(key)) { first_scored[symbol][key] = day; }
        }
        masks.push_back(std::move(mask));
        history.eligible += original_complete ? 1U : 0U;
        history.consecutive_closes = daily_close_available ? history.consecutive_closes + 1 : 0;
        if (row.at("regular").get<bool>()) {
            history.regular_slots.push_back(reversion_history_valid);
            if (history.regular_slots.size() > history_length) { history.regular_slots.pop_front(); }
        }
    }
    publish(directory_ / "vintage_bridge.json", {{"schema_version", 1}, {"legacy_manifest_sha256", legacy.manifest_sha256},
        {"legacy_risk_acquisition_sha256", std::string{legacy_risk_identity}}, {"new_acquisition_sha256", acquisition_sha}, {"rows", bridge}, {"strategy_returns_compared", false}});
    publish(directory_ / "eligibility_masks.json", {{"schema_version", 1}, {"protocol_sha256", contract_.identity()}, {"rows", masks}});
    Json result{{"schema_version", 1}, {"kind", "extension_study_manifest"}, {"protocol_sha256", contract_.identity()},
        {"input_policy_sha256", input_policy == nullptr ? Json(nullptr) : Json(input_policy->identity())},
        {"parent_coverage_assessment", parent_coverage},
        {"closing_recovery_manifest_sha256", recovery.summary.contains("manifest_sha256") ? recovery.summary.at("manifest_sha256") : Json(nullptr)},
        {"evidence_origin", expected_origin},
        {"acquisition_manifest_sha256", acquisition_sha}, {"canonical_actions_sha256", action_assessment.at("canonical_actions_sha256")},
        {"issuer_reference_sha256", action_assessment.at("issuer_reference_sha256")},
        {"vintage_bridge_sha256", sha256_file(directory_ / "vintage_bridge.json")},
        {"eligibility_masks_sha256", sha256_file(directory_ / "eligibility_masks.json")},
        {"first_scored_input_dates", first_scored}, {"model_fit_availability_evaluated", false},
        {"source_sha256", TRADING_SOURCE_SHA256}, {"compiler", TRADING_COMPILER_ID}, {"build_configuration", TRADING_BUILD_CONFIGURATION},
        {"reserved_strategy_inputs_opened", false}, {"strategy_scored", false}, {"research_inputs_ready", true}};
    if (resolution != nullptr) {
        result["source_resolution_sha256"] = resolution->identity();
        result["unavailable_daily_endpoints"] = remaining_endpoints(parent_coverage.at("missing_daily_endpoints"), recovery.bars);
        result["daily_mark_handling"] = resolution->document().at("unavailable_close_handling");
    }
    publish(directory_ / "study_manifest.json", result);
    return result;
}
} // namespace trading
