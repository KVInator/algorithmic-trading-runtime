#pragma once
#include "historical_panel.hpp"
#include "development_dataset.hpp"
#include <span>

namespace trading {

// Issuer records supply absent provider dates/records with explicit provenance.
// Contradictory nonempty provider observations remain blocking discrepancies.
class PanelActionReference {
public:
    [[nodiscard]] nlohmann::json reconcile(const std::filesystem::path& dataset,
        const std::filesystem::path& reference_file, const ExtensionStudyContract& contract,
        PanelOrigin origin) const;
};

class PanelInputFinalizer {
public:
    PanelInputFinalizer(std::filesystem::path dataset, ExtensionStudyContract contract, PanelOrigin origin);
    // Diagnose every input gate before registration. Detailed gap times and cash
    // discrepancies are restricted to development; no strategy is evaluated.
    [[nodiscard]] nlohmann::json assess(const nlohmann::json& audited_panel,
        const nlohmann::json& action_assessment, const PanelInputPolicy* input_policy = nullptr) const;
    // The caller first authenticates the legacy snapshot with its original loader.
    // Publication is immutable and occurs only after all prerequisite checks pass.
    [[nodiscard]] nlohmann::json finalize(const nlohmann::json& audited_panel,
        const nlohmann::json& action_assessment, const FrozenDevelopmentDataset& legacy,
        std::span<const SessionDownload> legacy_calibration = {}, std::string_view legacy_risk_identity = {},
        const PanelInputPolicy* input_policy = nullptr) const;
private:
    std::filesystem::path directory_;
    ExtensionStudyContract contract_;
    PanelOrigin origin_;
};
} // namespace trading
