#pragma once

#include "paper_contract_diagnostic.hpp"
#include <filesystem>

namespace trading {
class PaperContractReport {
public:
    explicit PaperContractReport(const PaperContractResult& result) : result_{result} {}

    void write(const std::filesystem::path& directory) const;

private:
    const PaperContractResult& result_;
};
} // namespace trading
