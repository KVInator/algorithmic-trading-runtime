#include "console_progress.hpp"
#include "panel_replay_dataset.hpp"
#include "panel_replay_report.hpp"
#include "development_study_dataset.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        const trading::ConsoleProgress progress;
        progress("M3 | registered causal panel replay and original-reference parity");
        const auto contract = trading::ExtensionStudyContract::load(root / "research/extension_protocol.json");
        const auto policy = trading::PanelInputPolicy::load(root / "research/extension_input_policy.json", contract);
        const auto resolution = trading::PanelSourceResolution::load(root / "research/extension_source_resolution.json", policy);
        const auto inputs = root / "scratch/datasets/extension_panel_2016_2025";
        const trading::PanelReplayDataset dataset{inputs, contract, policy, resolution};
        progress("Verifying the original SPY numerical path before expanded replay");
        const auto legacy = trading::DevelopmentStudyDataset{root}.load(progress);
        const auto risk = trading::DailyRiskDataset{root}.load(progress);
        const auto parity = trading::verify_panel_reference_parity(legacy.sessions, &risk);
        progress("Original-reference parity passed; opening pre-2024 instrument streams");
        trading::PanelReplayReport report{root / "scratch/runs", dataset.provenance(), parity, inputs};
        dataset.replay([&](auto day) { report.append(std::move(day)); }, progress);
        const auto output = report.finish();
        std::cout << "Status: causal_panel_replay_completed\nReference parity: passed\nReserved strategy inputs opened: false"
            << "\nVisual report: " << (output / "report.html").string() << "\nManifest: " << (output / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << "Panel replay stopped: " << error.what() << '\n'; return 1; }
}
