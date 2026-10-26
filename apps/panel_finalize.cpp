#include "console_progress.hpp"
#include "development_study_dataset.hpp"
#include "historical_panel.hpp"
#include "panel_input_finalizer.hpp"
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
        const auto dataset_root = root / "scratch/datasets/extension_panel_2016_2025";
        const auto issuer_file = dataset_root / "issuer/reference.json";
        const trading::ConsoleProgress progress;
        progress("Historical panel | issuer reconciliation, vintage bridge and study registration");
        if (!std::filesystem::exists(issuer_file)) {
            std::cout << "Status: issuer_reference_required\nRequired file: " << issuer_file.string()
                << "\nReference schema and primary-source download routes: research/extension_protocol.md\n"
                << "No strategy was fitted or scored. The acquired archive is preserved.\n";
            return 2;
        }
        const trading::HistoricalPanelDataset dataset{dataset_root, contract, trading::PanelOrigin::provider};
        const auto audit = dataset.audit(progress);
        const auto actions = trading::PanelActionReference{}.reconcile(dataset_root, issuer_file, contract, trading::PanelOrigin::provider);
        if (!actions.at("issuer_reconciled").get<bool>()) {
            std::cout << "Status: action_discrepancies_require_review\n";
            for (const auto& item : actions.at("blockers")) { std::cout << item.get<std::string>() << '\n'; }
            return 2;
        }
        const auto legacy = trading::DevelopmentStudyDataset{root}.load(progress);
        const auto risk_root = root / "scratch/datasets/SPY_RISK_01_inputs";
        const auto risk = trading::RiskInputDataset{risk_root, root / "research/spy_distribution_reference.json"}.load();
        std::vector<trading::SessionDownload> calibration;
        for (const auto& input : risk.prices) { calibration.push_back(trading::load_session_download(input.directory, input.input.request)); }
        const auto result = trading::PanelInputFinalizer{dataset_root, contract, trading::PanelOrigin::provider}.finalize(
            audit, actions, legacy, calibration, trading::sha256_file(risk_root / "acquisition_manifest.json"));
        std::cout << "Status: registered_inputs_ready\nStudy manifest: " << (dataset_root / "study_manifest.json").string()
            << "\nReserved strategy inputs opened: " << result.at("reserved_strategy_inputs_opened")
            << "\nNo strategy was fitted or scored.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Panel finalization stopped: " << error.what() << '\n';
        return 1;
    }
}
