#pragma once

#include "artifact_io.hpp"
#include "console_progress.hpp"
#include "content_digest.hpp"
#include "development_study_dataset.hpp"
#include "reversion_study.hpp"
#include "strategy_execution_report.hpp"

#include <iostream>

namespace trading::execution {

class StrategyExecutionApplication final {
  public:
    explicit StrategyExecutionApplication(std::filesystem::path root) : root_{std::move(root)} {}

    void run() const {
        const trading::ConsoleProgress progress;
        progress("Execution integration | strategy adapters, parity and event replay");
        const auto directory = trading::create_unique_directory(root_ / "scratch/runs", "strategy_execution");
        const trading::execution::StrategyExecutionDiagnostic diagnostic{directory};
        auto result = diagnostic.run();
        trading::ReversionStudy::verify_registration(root_);
        if (trading::sha256_file(root_ / "research/intraday_continuation_protocol.md") !=
            trading::continuation_protocol_sha256()) {
            throw std::invalid_argument("continuation protocol differs from compiled source");
        }
        const auto dataset = trading::DevelopmentStudyDataset{root_}.load(progress);
        progress("Replaying first eligible scored date using explicit constructed bar-price quotes");
        diagnostic.historical(result, dataset);
        trading::execution::StrategyExecutionReport{result}.write(directory);
        std::cout << "Execution checks passed: " << result.checks.size() << "\nCases: " << result.cases.size()
                  << "\nInteractive report: " << (directory / "report.html").string()
                  << "\nManifest: " << (directory / "summary.json").string()
                  << "\nOffline execution evidence completed; historical study results were not changed.\n";
    }

  private:
    std::filesystem::path root_;
};

} // namespace trading::execution
