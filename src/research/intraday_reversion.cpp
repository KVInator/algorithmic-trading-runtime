#include "intraday_reversion.hpp"

#include "market_calendar.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace trading {
namespace {
double as_finite(long double value) {
    const double result = static_cast<double>(value);
    if (!std::isfinite(result)) { throw std::overflow_error("reversion calculation exceeds finite double units"); }
    return result;
}
void validate_session(const HistoricalBarRequest& session, ResearchInputMode mode) {
    if (mode == ResearchInputMode::registered_panel) { require_panel_regular_session(session); return; }
    const auto date = format_utc_seconds(session.start()).substr(0, 10);
    const auto calendar = development_calendar(CalendarRange{date, date});
    if (calendar.size() != 1 || !calendar.front().regular() || session.symbol() != "SPY" ||
        session.start() != calendar.front().open_utc() || session.end_exclusive() != calendar.front().close_utc()) {
        throw std::invalid_argument("reversion measurement requires a calendar-verified full SPY session");
    }
}
} // namespace

OpeningAnchorMeasurement::OpeningAnchorMeasurement(ResearchInputMode mode) : mode_{mode} {}

std::chrono::sys_seconds OpeningAnchorMeasurement::decision_time(const HistoricalBarRequest& session) const {
    validate_session(session, mode_);
    return session.start() + std::chrono::minutes{reversion_prefix_minutes};
}
OpeningAnchorResidual OpeningAnchorMeasurement::measure(const HistoricalBarRequest& session,
    std::span<const MinuteBar> completed) const {
    const auto at = decision_time(session);
    const HistoricalBarRequest prefix{session.symbol(), session.start(), at, session.page_size()};
    if (completed.size() != reversion_prefix_minutes || !audit_session(prefix, completed).complete()) {
        throw std::invalid_argument("reversion requires exactly 150 completed contiguous minutes");
    }
    const auto result = observe(session, completed);
    if (!result) { throw std::invalid_argument("opening anchor or fixed-grid observations are unavailable"); }
    return *result;
}
std::optional<OpeningAnchorResidual> OpeningAnchorMeasurement::observe(const HistoricalBarRequest& session,
    std::span<const MinuteBar> completed) const {
    const auto at = decision_time(session);
    const HistoricalBarRequest prefix{session.symbol(), session.start(), at, session.page_size()};
    const ResearchSessionView observed{prefix, completed};
    long double volume = 0, weighted_close = 0;
    for (std::size_t i = 0; i < 30; ++i) {
        const auto* bar = observed.at(i);
        if (bar == nullptr) { return {}; }
        const long double v = bar->volume();
        if (v == 0) { continue; }
        const long double next_volume = volume + v;
        weighted_close += v / next_volume * (static_cast<long double>(bar->prices().close) - weighted_close);
        volume = next_volume;
    }
    if (volume <= 0) { return {}; }
    const auto anchor = as_finite(weighted_close);
    if (anchor <= 0) { throw std::invalid_argument("opening reference is not positive"); }
    OpeningAnchorResidual result{at, anchor, {}};
    for (std::size_t i = 0; i < result.values.size(); ++i) {
        const auto* bar = observed.at(29 + 5 * i);
        if (bar == nullptr) { return {}; }
        result.values[i] = std::log(bar->prices().close) - std::log(anchor);
    }
    return result;
}

std::string_view name(ReversionFitStatus status) {
    switch (status) {
    case ReversionFitStatus::admissible: return "admissible";
    case ReversionFitStatus::insufficient_pairs: return "insufficient_pairs";
    case ReversionFitStatus::singular_design: return "singular_design";
    case ReversionFitStatus::nonpositive_slope: return "nonpositive_slope";
    case ReversionFitStatus::unit_root_boundary: return "unit_root_boundary";
    case ReversionFitStatus::nonstationary_slope: return "nonstationary_slope";
    case ReversionFitStatus::zero_innovation_variance: return "zero_innovation_variance";
    }
    throw std::invalid_argument("unknown reversion fit status");
}

PooledReversionFit PooledReversionEstimator::fit(std::span<const std::vector<double>> sequences) const {
    PooledReversionFit result{ReversionFitStatus::insufficient_pairs, sequences.size(), 0,
        {}, {}, {}, {}, {}, {}};
    long double mean_x = 0, mean_y = 0, xx = 0, xy = 0, scale = 0;
    for (const auto& sequence : sequences) {
        if (sequence.size() < 2) { throw std::invalid_argument("each reversion sequence requires at least two observations"); }
        for (const double value : sequence) {
            if (!std::isfinite(value)) { throw std::invalid_argument("reversion observations must be finite"); }
        }
        for (std::size_t i = 1; i < sequence.size(); ++i) {
            const long double x = sequence[i - 1], y = sequence[i];
            ++result.pairs;
            const auto count = static_cast<long double>(result.pairs);
            const auto dx = x - mean_x, dy = y - mean_y;
            mean_x += dx / count; mean_y += dy / count;
            xx += dx * (x - mean_x); xy += dx * (y - mean_y);
            scale = std::max(scale, std::abs(x));
        }
    }
    if (result.pairs < 3) { return result; }
    // Scale-relative rejection makes the numerical rank decision invariant to
    // changing residual units. It does not replace an empirical stationarity test.
    const auto tolerance = 64 * static_cast<long double>(std::numeric_limits<double>::epsilon());
    if (!std::isfinite(xx) || !std::isfinite(xy)) { throw std::overflow_error("reversion normal equations overflow"); }
    if (xx <= tolerance * scale * scale * static_cast<long double>(result.pairs)) {
        result.status = ReversionFitStatus::singular_design; return result;
    }
    const long double slope = xy / xx, intercept = mean_y - slope * mean_x;
    result.slope = as_finite(slope); result.intercept = as_finite(intercept);
    long double sse = 0;
    for (const auto& sequence : sequences) {
        for (std::size_t i = 1; i < sequence.size(); ++i) {
            const auto error = static_cast<long double>(sequence[i]) - intercept - slope * sequence[i - 1];
            sse += error * error;
        }
    }
    result.innovation_variance = as_finite(sse / static_cast<long double>(result.pairs - 2));
    if (slope <= 0) { result.status = ReversionFitStatus::nonpositive_slope; return result; }
    if (std::abs(slope - 1) <= tolerance) { result.status = ReversionFitStatus::unit_root_boundary; return result; }
    if (slope > 1) { result.status = ReversionFitStatus::nonstationary_slope; return result; }
    if (*result.innovation_variance == 0) { result.status = ReversionFitStatus::zero_innovation_variance; return result; }
    const auto speed = -std::log(slope) / 5;
    result.equilibrium = as_finite(intercept / (1 - slope));
    result.speed_per_minute = as_finite(speed);
    result.half_life_minutes = as_finite(std::log(2.L) / speed);
    result.status = ReversionFitStatus::admissible;
    return result;
}

double PooledReversionEstimator::expected_log_move(const PooledReversionFit& fitted, double current,
    double delay, double horizon) const {
    if (fitted.status != ReversionFitStatus::admissible || !fitted.equilibrium || !fitted.speed_per_minute ||
        !std::isfinite(*fitted.equilibrium) || !std::isfinite(*fitted.speed_per_minute) || *fitted.speed_per_minute <= 0 ||
        !std::isfinite(current) || !std::isfinite(delay) || !std::isfinite(horizon) || delay < 0 || horizon <= 0) {
        throw std::invalid_argument("expected log move requires an admissible fit and finite causal horizons");
    }
    return as_finite((static_cast<long double>(*fitted.equilibrium) - current) *
        std::exp(-static_cast<long double>(*fitted.speed_per_minute) * delay) *
        -std::expm1(-static_cast<long double>(*fitted.speed_per_minute) * horizon));
}

} // namespace trading
