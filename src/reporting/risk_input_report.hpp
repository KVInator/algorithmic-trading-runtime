#pragma once

#include "risk_input_dataset.hpp"
#include "risk_preflight.hpp"

#include <filesystem>

namespace trading {

// The result must outlive the report; export does not modify its inputs.
class RiskInputReport {
public:
    explicit RiskInputReport(const RiskInputInventory& result) : result_{result} {}
    [[nodiscard]] std::filesystem::path write(const RiskPreflightResult& preflight,
        const std::filesystem::path& output_root) const;

private:
    const RiskInputInventory& result_;
};

} // namespace trading
