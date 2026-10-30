#include "extension_contract.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include "market_calendar.hpp"
#include <stdexcept>
#include <utility>

namespace trading {
PanelEvaluationAccess::PanelEvaluationAccess(int year, bool provider, std::string authority, bool excess)
    : year_(year), provider_(provider), size_excess_(excess), authority_(std::move(authority)) {
    if ((year != 2024 && year != 2025) || authority_.size() != 64)
        throw std::invalid_argument("invalid evaluation capability");
    const auto calendar = extension_calendar_years(year - 2, year - 1);
    unit_first_ = calendar.at(calendar.size() - 252).date();
}
bool PanelEvaluationAccess::scored(std::string_view date) const {
    return date >= (development() ? "2017-01-01" : std::to_string(year_) + "-01-01") &&
           date <= last_input();
}
bool PanelEvaluationAccess::unit_active(std::string_view date) const {
    return date >= unit_first_ && date <= last_input();
}
std::string PanelEvaluationAccess::first_input() const {
    return !development() && !provider_ ? "2022-01-01" : "2016-01-01";
}
std::string PanelEvaluationAccess::last_input() const { return std::to_string(year_) + "-12-31"; }
void PanelEvaluationAccess::require_price(std::string_view date) const {
    static_cast<void>(parse_session_date(date));
    if (date < first_input() || date > last_input())
        throw std::invalid_argument("price date exceeds the authorized evaluation window");
}
void PanelEvaluationAccess::require_origin(bool provider) const {
    if (!development() && provider != provider_)
        throw std::invalid_argument("constructed capability cannot authorize provider prices");
}
nlohmann::json PanelEvaluationAccess::document() const {
    return {{"stage", development()   ? "development"
                      : year_ == 2024 ? "M8_validation"
                                      : "M9_final"},
            {"year", year_},
            {"first_input", first_input()},
            {"last_input", last_input()},
            {"first_scored", development() ? "2017-01-01" : std::to_string(year_) + "-01-01"},
            {"unit_reference_first", unit_first_},
            {"evidence_origin", provider_ ? "provider" : "synthetic"},
            {"authority_sha256", authority_},
            {"calibration_size_excess_detected", size_excess_},
            {"fresh_evaluation_capital", !development()},
            {"primary_rules_replaced", false}};
}

ExtensionStudyContract::ExtensionStudyContract(nlohmann::json document, std::string identity)
    : document_(std::move(document)), identity_{std::move(identity)} {}

ExtensionStudyContract ExtensionStudyContract::load(const std::filesystem::path &path) {
    const auto text = read_text_file(path, 64U * 1024U);
    const auto identity = sha256(text);
    if (identity != TRADING_EXTENSION_PROTOCOL_SHA256) {
        throw std::invalid_argument("extension protocol differs from its registered build identity");
    }
    return {parse_json_document(text), identity};
}
const nlohmann::json &ExtensionStudyContract::document() const noexcept { return document_; }
const std::string &ExtensionStudyContract::identity() const noexcept { return identity_; }
std::vector<std::string> ExtensionStudyContract::symbols() const {
    return document_.at("symbols").get<std::vector<std::string>>();
}
bool ExtensionStudyContract::reserved(std::string_view date) const {
    static_cast<void>(parse_session_date(date));
    return date > document_.at("archive").at("development_last").get<std::string>();
}
bool ExtensionStudyContract::diagnostic_excluded(std::string_view symbol, std::string_view date) const {
    const auto &exclusion = document_.at("eligibility").at("diagnostic_exclusion");
    return symbol == exclusion.at("symbol").get<std::string>() &&
           date == exclusion.at("date").get<std::string>();
}
void ExtensionStudyContract::require_development_access(std::string_view date) const {
    if (reserved(date)) {
        throw std::invalid_argument("reserved strategy inputs require the later milestone access gate");
    }
}
} // namespace trading
