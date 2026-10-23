#pragma once

#include "intraday_reversion.hpp"

#include <string>

namespace trading {

struct ReversionControl {
    std::string label;
    double declared_intercept;
    double declared_slope;
    std::vector<std::vector<double>> sequences;
    PooledReversionFit fitted;
};
struct ReversionCheck { std::string label; bool passed; };
struct ReversionCoverage {
    std::string date;
    std::size_t earlier_training_sessions;
    bool complete_session;
    bool complete_prefix;
    bool positive_anchor_volume;
    bool reference_clocks_present;
    bool input_ready;
};
struct ReversionPreflightResult {
    std::vector<ReversionCheck> checks;
    std::vector<ReversionControl> controls;
    std::vector<MinuteBar> synthetic_prefix;
    OpeningAnchorResidual synthetic_measurement;
    std::vector<ReversionCoverage> coverage;
    std::optional<std::string> study_manifest_sha256;
};

// Constructed controls and input availability only; no historical model fitting.
class ReversionPreflight {
public:
    ReversionPreflight() : measurement_{}, estimator_{} {}
    [[nodiscard]] ReversionPreflightResult controls() const;
    [[nodiscard]] std::vector<ReversionCoverage> audit_coverage(std::span<const SessionDownload> sessions) const;

private:
    OpeningAnchorMeasurement measurement_;
    PooledReversionEstimator estimator_;
};

} // namespace trading
