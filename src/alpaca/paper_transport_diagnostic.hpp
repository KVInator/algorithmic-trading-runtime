#pragma once
#include "paper_transport.hpp"

namespace trading {
struct PaperTransportResult {
    std::vector<std::string> checks;
    nlohmann::json cases = nlohmann::json::array();
    bool native_probe{false};
    bool successful{true};
    std::string detail;
};

class PaperTransportDiagnostic {
public:
    explicit PaperTransportDiagnostic(std::filesystem::path directory) : directory_{std::move(directory)} {}

    [[nodiscard]] PaperTransportResult run() const;

private:
    std::filesystem::path directory_;
};
} // namespace trading
