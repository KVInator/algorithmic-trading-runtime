#pragma once
#include "shared_capital_diagnostic.hpp"

namespace trading {
class SharedCapitalReport {
public:
    void write(const std::filesystem::path& directory, const execution::SharedDiagnosticResult& result) const;
};
} // namespace trading
