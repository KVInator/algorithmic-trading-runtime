#pragma once
#include "risk_input_dataset.hpp"
#include "risk_preflight.hpp"

namespace trading {
[[nodiscard]] std::filesystem::path save_risk_input_report(const RiskInputInventory& inputs,
    const RiskPreflightResult& preflight, const std::filesystem::path& output_root);
} // namespace trading
