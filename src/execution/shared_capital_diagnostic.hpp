#pragma once
#include "journaled_shared_capital.hpp"
#include <filesystem>

namespace trading::execution {
struct SharedSegmentEvidence {
    std::string path;
    SharedJournalIdentity identity;
};
struct SharedCaseEvidence {
    std::string name;
    std::vector<SharedSegmentEvidence> segments;
    SharedCapitalConfiguration configuration;
    SharedCapitalSnapshot final_state;
};
struct SharedDiagnosticResult {
    std::vector<std::string> checks;
    std::vector<SharedCaseEvidence> cases;
    nlohmann::json evidence;
};
class SharedCapitalDiagnostic {
public:
    SharedCapitalDiagnostic(std::filesystem::path directory, SharedCapitalConfiguration configuration);
    [[nodiscard]] SharedDiagnosticResult run() const;
private:
    std::filesystem::path directory_;
    SharedCapitalConfiguration configuration_;
};
} // namespace trading::execution
