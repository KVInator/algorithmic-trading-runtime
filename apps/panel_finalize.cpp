#include "console_progress.hpp"
#include "development_study_dataset.hpp"
#include "historical_panel.hpp"
#include "panel_input_finalizer.hpp"
#include "historical_panel_report.hpp"
#include "risk_input_dataset.hpp"
#include "session_storage.hpp"
#include "content_digest.hpp"
#include "artifact_io.hpp"
#include <iostream>
#include <vector>

int main() {
    try {
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        const auto contract = trading::ExtensionStudyContract::load(root / "research/extension_protocol.json");
        const auto input_policy = trading::PanelInputPolicy::load(root / "research/extension_input_policy.json", contract);
        const auto dataset_root = root / "scratch/datasets/extension_panel_2016_2025";
        const auto issuer_file = dataset_root / "issuer/reference.json";
        const trading::ConsoleProgress progress;
        progress("Historical panel | issuer reconciliation, vintage bridge and study registration");
        const trading::HistoricalPanelDataset dataset{dataset_root, contract, trading::PanelOrigin::provider};
        const auto audit = dataset.audit(progress);
        const auto actions = trading::PanelActionReference{}.reconcile(dataset_root, issuer_file, contract, trading::PanelOrigin::provider);
        const trading::PanelInputFinalizer finalizer{dataset_root, contract, trading::PanelOrigin::provider};
        progress("Assessing registered gates and development-only gap intervals");
        const auto readiness = finalizer.assess(audit, actions, &input_policy);
        const auto report = trading::HistoricalPanelReport{}.write_readiness(readiness, root / "scratch/runs");
        std::cout << "Readiness summary: " << (report / "summary.json").string()
            << "\nVisual report: " << (report / "report.html").string() << '\n';
        std::cout << "Operational development years under input amendment: "
            << readiness.at("input_policy_assessment").at("qualified_operational_development_years") << '\n';
        std::cout << "Daily endpoints still unavailable after recovery: " << readiness.at("remaining_daily_endpoints").size() << '\n';
        if (!readiness.at("active_input_gates_passed").get<bool>()) {
            std::cout << "Status: input_limited; registration not attempted\nParent complete-grid qualified development years: "
                << readiness.at("qualified_development_years") << " / " << readiness.at("minimum_development_years")
                << "\nMissing daily endpoints: " << readiness.at("missing_daily_endpoints").size() << '\n';
            for (const auto& item : actions.at("blockers")) { std::cout << item.get<std::string>() << '\n'; }
            std::cout << "Exit 2 denotes unresolved input prerequisites. No strategy was fitted or scored.\n";
            return 2;
        }
        const auto legacy = trading::DevelopmentStudyDataset{root}.load(progress);
        const auto risk_root = root / "scratch/datasets/SPY_RISK_01_inputs";
        const auto risk = trading::RiskInputDataset{risk_root, root / "research/spy_distribution_reference.json"}.load();
        std::vector<trading::SessionDownload> calibration;
        for (const auto& input : risk.prices) { calibration.push_back(trading::load_session_download(input.directory, input.input.request)); }
        const auto result = finalizer.finalize(
            audit, actions, legacy, calibration, trading::sha256_file(risk_root / "acquisition_manifest.json"), &input_policy);
        std::cout << "Status: registered_inputs_ready\nStudy manifest: " << (dataset_root / "study_manifest.json").string()
            << "\nReserved strategy inputs opened: " << result.at("reserved_strategy_inputs_opened")
            << "\nNo strategy was fitted or scored.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Panel finalization stopped: " << error.what() << '\n';
        return 1;
    }
}
