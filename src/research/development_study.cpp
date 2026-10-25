#include "development_study.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>
#include <stdexcept>

namespace trading {
DevelopmentDecision assess_development(const PeriodStatistics &primary,
                                       const PeriodStatistics &delay_stress) {
    for (const auto *stats : {&primary, &delay_stress}) {
        if (stats->active_days > stats->sessions) {
            throw std::invalid_argument("active-day count exceeds session count");
        }
        for (const auto &value :
             {stats->mean_gross_active, stats->mean_net_active, stats->signal_contrast_gross}) {
            if (value && !std::isfinite(*value)) {
                throw std::invalid_argument("development screen requires finite statistics");
            }
        }
    }
    if (primary.sessions != delay_stress.sessions || primary.active_days != delay_stress.active_days) {
        throw std::invalid_argument(
            "development screen requires the same dates and active-day count across delays");
    }
    if (primary.active_days < 100) {
        return DevelopmentDecision::insufficient_active_days;
    }
    if (!primary.mean_gross_active || *primary.mean_gross_active <= 0) {
        return DevelopmentDecision::no_gross_edge;
    }
    if (!primary.mean_net_active || *primary.mean_net_active <= 0) {
        return DevelopmentDecision::cost_dominated;
    }
    if (!delay_stress.mean_net_active || *delay_stress.mean_net_active <= 0) {
        return DevelopmentDecision::delay_stress_failed;
    }
    if (!primary.signal_contrast_gross) {
        return DevelopmentDecision::undefined_signal_contrast;
    }
    if (*primary.signal_contrast_gross <= 0) {
        return DevelopmentDecision::nonpositive_signal_contrast;
    }
    return DevelopmentDecision::eligible_for_unchanged_validation;
}

std::string development_decision_name(DevelopmentDecision decision) {
    switch (decision) {
    case DevelopmentDecision::insufficient_active_days:
        return "insufficient_active_days";
    case DevelopmentDecision::no_gross_edge:
        return "no_gross_edge";
    case DevelopmentDecision::cost_dominated:
        return "cost_dominated";
    case DevelopmentDecision::delay_stress_failed:
        return "delay_stress_failed";
    case DevelopmentDecision::undefined_signal_contrast:
        return "undefined_signal_contrast";
    case DevelopmentDecision::nonpositive_signal_contrast:
        return "nonpositive_signal_contrast";
    case DevelopmentDecision::eligible_for_unchanged_validation:
        return "eligible_for_unchanged_validation";
    }
    throw std::invalid_argument("unknown development decision");
}

DevelopmentStudyResult DevelopmentStudy::run(const FrozenDevelopmentDataset &dataset,
                                             const Progress &progress) const {
    if (dataset.protocol_revision != 2 || dataset.sessions.size() != 496 || dataset.exclusions.size() != 5 ||
        dataset.protocol_sha256 != TRADING_PROTOCOL_SHA256 ||
        dataset.manifest_file != "study_manifest.json" || dataset.data_policy_sha256.size() != 64 ||
        dataset.sessions.front().request.start() < parse_utc_seconds("2022-01-01T00:00:00Z") ||
        dataset.sessions.back().request.end_exclusive() > parse_utc_seconds("2024-01-01T00:00:00Z")) {
        throw std::invalid_argument(
            "development study requires the frozen revision-2 496-session 2022-2023 dataset");
    }
    if (progress) {
        progress("Replaying ten fixed cost/delay cases");
    }
    DevelopmentStudyResult result{
        BatchMomentumReplay{}.run(dataset.sessions), {}, {}, DevelopmentDecision::insufficient_active_days};
    if (progress) {
        progress("Calculating period and annual return statistics");
    }
    std::vector<DailyResearchReturn> primary_days;
    const PeriodStatistics *primary = nullptr;
    const PeriodStatistics *stress = nullptr;
    result.statistics.reserve(result.cases.size());
    for (const auto &replay : result.cases) {
        const auto days = research_returns(replay.days);
        CaseStatistics statistics{describe_returns(days), {}};
        for (const auto *year : {"2022", "2023"}) {
            std::vector<DailyResearchReturn> selected;
            std::copy_if(days.begin(), days.end(), std::back_inserter(selected),
                         [&](const auto &day) { return day.date.starts_with(year); });
            statistics.calendar_years.push_back({year, describe_returns(selected)});
        }
        result.statistics.push_back(std::move(statistics));
        if (replay.cost_bps_per_side == 1.0) {
            if (replay.delay == std::chrono::minutes{1}) {
                primary_days = days;
                primary = &result.statistics.back().overall;
            } else if (replay.delay == std::chrono::minutes{2}) {
                stress = &result.statistics.back().overall;
            }
        }
    }
    if (!primary || !stress) {
        throw std::logic_error("fixed protocol cases are missing");
    }
    result.decision = assess_development(*primary, *stress);
    for (const auto length : {5U, 10U}) {
        if (progress) {
            progress("Running " + std::to_string(length) + "-day block bootstrap | 10000 resamples");
        }
        result.primary_bootstrap.push_back(MovingBlockBootstrap{length}.run(primary_days));
    }
    if (progress) {
        progress("Development statistics complete");
    }
    return result;
}

} // namespace trading
