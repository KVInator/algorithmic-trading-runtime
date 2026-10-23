#pragma once

#include "risk_preflight.hpp"

#include <filesystem>

namespace trading {

// The result must outlive the report; export does not modify its inputs.
class RiskPreflightReport {
public:
    explicit RiskPreflightReport(const RiskPreflightResult& result) : result_{result} {}
    [[nodiscard]] std::filesystem::path write(std::span<const CorporateActionProbe> probes,
        const std::string& access_status, const std::filesystem::path& output_root) const;

private:
    const RiskPreflightResult& result_;
};

} // namespace trading
