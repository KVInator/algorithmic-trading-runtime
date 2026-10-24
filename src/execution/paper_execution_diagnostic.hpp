#pragma once
#include "paper_execution.hpp"
#include <filesystem>

namespace trading::execution {
struct PaperBridgeResult {
    std::vector<std::string> checks;
    nlohmann::json frames = nlohmann::json::array();
};

class PaperExecutionDiagnostic {
public:
    explicit PaperExecutionDiagnostic(std::filesystem::path directory) : directory_{std::move(directory)} {}

    [[nodiscard]] PaperBridgeResult run() const;

private:
    std::filesystem::path directory_;
};
} // namespace trading::execution
