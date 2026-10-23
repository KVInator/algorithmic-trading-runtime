#include "reversion_preflight.hpp"

#include "market_calendar.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trading {
namespace {
std::vector<std::vector<double>> control(double intercept, double slope) {
    const std::array<double, 5> errors{0.0001, -0.0002, 0.0002, -0.0002, 0.0001};
    std::vector<std::vector<double>> result;
    for (std::size_t i = 0; i < errors.size(); ++i) {
        const double x = (static_cast<double>(i) - 2) * 0.001;
        result.push_back({x, intercept + slope * x + errors[i]});
    }
    return result;
}
bool near(double a, double b) { return std::abs(a - b) <= 1e-12; }
} // namespace

ReversionPreflightResult ReversionPreflight::controls() const {
    const auto session = CalendarSession{"2022-02-01", "09:30", "16:00"}.bar_request();
    std::vector<MinuteBar> prefix;
    for (std::size_t i = 0; i < reversion_prefix_minutes; ++i) {
        const double price = i < 30 ? 100 : 100 * std::exp(-0.004 * static_cast<double>(i - 29) / 120);
        prefix.emplace_back("SPY", session.start() + std::chrono::minutes{static_cast<std::int64_t>(i)},
            Ohlc{price, price, price, price}, 100);
    }
    const auto& measurement = measurement_;
    ReversionPreflightResult result{{}, {}, prefix, measurement.measure(session, prefix), {}, {}};
    auto check = [&](std::string label, bool passed) {
        if (!passed) { throw std::logic_error("reversion control failed: " + label); }
        result.checks.push_back({std::move(label), passed});
    };
    const auto& estimator = estimator_;
    auto add = [&](std::string label, double intercept, double slope, ReversionFitStatus expected) {
        auto sequences = control(intercept, slope);
        auto fitted = estimator.fit(sequences);
        check(label + " recovers the declared intercept", fitted.intercept && near(*fitted.intercept, intercept));
        check(label + " recovers the declared slope", fitted.slope && near(*fitted.slope, slope));
        check(label + " has the declared admissibility status", fitted.status == expected);
        result.controls.push_back({std::move(label), intercept, slope, std::move(sequences), std::move(fitted)});
    };
    add("Mean-reverting control", 0.0002, 0.8, ReversionFitStatus::admissible);
    add("Unit-root control", 0.0002, 1, ReversionFitStatus::unit_root_boundary);
    add("Explosive control", 0.0002, 1.05, ReversionFitStatus::nonstationary_slope);
    add("Alternating control", 0.0002, -0.5, ReversionFitStatus::nonpositive_slope);
    const auto& fitted = result.controls.front().fitted;
    check("Only within-sequence transitions are fitted", fitted.pairs == 5 && fitted.sequences == 5);
    check("Residual degrees of freedom are explicit", fitted.innovation_variance && near(*fitted.innovation_variance, 14e-8 / 3));
    check("OU equilibrium agrees with the analytical mapping", fitted.equilibrium && near(*fitted.equilibrium, 0.001));
    check("OU half-life agrees with the analytical mapping", fitted.half_life_minutes && near(*fitted.half_life_minutes, -5 * std::log(2.0) / std::log(0.8)));
    const double gain = estimator.expected_log_move(fitted, -0.004, 1, 30);
    check("Delayed expected log move agrees with the closed form", near(gain, 0.005 * std::pow(0.8, 0.2) * (1 - std::pow(0.8, 6))));
    check("Longer dispatch delay reduces this positive model move", estimator.expected_log_move(fitted, -0.004, 2, 30) < gain);
    check("Above-equilibrium state forecasts a negative log move", estimator.expected_log_move(fitted, 0.004, 1, 30) < 0);
    check("Opening reference is fixed at the declared price", result.synthetic_measurement.anchor_price == 100);
    check("Prefix contains 25 five-minute residual observations", result.synthetic_measurement.values.size() == 25);
    check("Latest residual uses the last completed minute", near(result.synthetic_measurement.values.back(), -0.004));
    return result;
}

std::vector<ReversionCoverage> ReversionPreflight::audit_coverage(std::span<const SessionDownload> sessions) const {
    std::vector<ReversionCoverage> result;
    std::size_t prior = 0;
    std::optional<std::chrono::sys_seconds> previous_end;
    const auto& measurement = measurement_;
    for (const auto& item : sessions) {
        const auto decision = measurement.decision_time(item.request);
        const auto date = format_utc_seconds(item.request.start()).substr(0, 10);
        if (date < "2022-01-01" || date > "2023-12-31" ||
            (previous_end && item.request.start() <= *previous_end)) {
            throw std::invalid_argument("reversion preflight accepts only chronological 2022-2023 development sessions");
        }
        previous_end = item.request.end_exclusive();
        const bool complete = audit_session(item.request, item.bars).complete();
        const auto end = std::lower_bound(item.bars.begin(), item.bars.end(), decision,
            [](const MinuteBar& bar, auto at) { return bar.start() < at; });
        const std::span<const MinuteBar> prefix{item.bars.data(), static_cast<std::size_t>(end - item.bars.begin())};
        const HistoricalBarRequest request{item.request.symbol(), item.request.start(), decision};
        const bool prefix_complete = audit_session(request, prefix).complete();
        bool positive_volume = false;
        for (const auto& bar : prefix) {
            if (bar.start() < item.request.start() + std::chrono::minutes{30} && bar.volume() > 0) { positive_volume = true; }
        }
        bool references = true;
        for (const auto minute : {151, 152, 181, 182}) {
            const auto at = item.request.start() + std::chrono::minutes{minute};
            const auto found = std::lower_bound(item.bars.begin(), item.bars.end(), at,
                [](const MinuteBar& bar, auto time) { return bar.start() < time; });
            references = references && found != item.bars.end() && found->start() == at;
        }
        result.push_back({date, std::min(prior, reversion_history_sessions), complete, prefix_complete, positive_volume,
            references, prior >= reversion_history_sessions && complete && prefix_complete && positive_volume && references});
        if (complete && prefix_complete && positive_volume) { ++prior; }
    }
    return result;
}

} // namespace trading
