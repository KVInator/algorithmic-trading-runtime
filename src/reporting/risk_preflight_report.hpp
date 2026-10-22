#pragma once

#include "risk_preflight.hpp"

namespace trading {

[[nodiscard]] std::filesystem::path save_risk_preflight_report(
    const RiskPreflightResult& result, std::span<const CorporateActionProbe> probes,
    const std::string& access_status, const std::filesystem::path& output_root);

} // namespace trading
