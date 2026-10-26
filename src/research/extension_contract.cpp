#include "extension_contract.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include "market_calendar.hpp"
#include <stdexcept>
#include <utility>

namespace trading {
ExtensionStudyContract::ExtensionStudyContract(nlohmann::json document, std::string identity)
    : document_(std::move(document)), identity_{std::move(identity)} {}

ExtensionStudyContract ExtensionStudyContract::load(const std::filesystem::path& path) {
    const auto text = read_text_file(path, 64U * 1024U);
    const auto identity = sha256(text);
    if (identity != TRADING_EXTENSION_PROTOCOL_SHA256) {
        throw std::invalid_argument("extension protocol differs from its registered build identity");
    }
    return {parse_json_document(text), identity};
}
const nlohmann::json& ExtensionStudyContract::document() const noexcept { return document_; }
const std::string& ExtensionStudyContract::identity() const noexcept { return identity_; }
std::vector<std::string> ExtensionStudyContract::symbols() const {
    return document_.at("symbols").get<std::vector<std::string>>();
}
bool ExtensionStudyContract::reserved(std::string_view date) const {
    static_cast<void>(parse_session_date(date));
    return date > document_.at("archive").at("development_last").get<std::string>();
}
bool ExtensionStudyContract::diagnostic_excluded(std::string_view symbol, std::string_view date) const {
    const auto& exclusion = document_.at("eligibility").at("diagnostic_exclusion");
    return symbol == exclusion.at("symbol").get<std::string>() && date == exclusion.at("date").get<std::string>();
}
void ExtensionStudyContract::require_development_access(std::string_view date) const {
    if (reserved(date)) {
        throw std::invalid_argument("reserved strategy inputs require the later milestone access gate");
    }
}
} // namespace trading
