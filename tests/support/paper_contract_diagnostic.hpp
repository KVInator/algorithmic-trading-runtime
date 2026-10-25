#pragma once

#include "alpaca_paper_contract.hpp"

namespace trading {
struct PaperContractCheck {
    std::string name;
    bool passed;
};

struct PaperContractCase {
    std::string name;
    std::string payload;
    bool accepted;
    std::string detail;
};

struct PaperDecimalExample {
    std::string input;
    std::string canonical;
    std::optional<std::int64_t> cents;
};

struct PaperContractResult {
    std::vector<PaperContractCheck> checks;
    std::vector<PaperContractCase> cases;
    std::vector<PaperRequest> requests;
    std::vector<PaperDecimalExample> decimals;
};

class PaperContractDiagnostic {
  public:
    [[nodiscard]] PaperContractResult run() const;
};
} // namespace trading
