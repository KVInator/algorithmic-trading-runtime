#include "development_study.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>
#include <stdexcept>

namespace trading {
namespace {

using Json = nlohmann::json;

Json optional_number(const std::optional<double>& value) { return value ? Json(*value) : Json(nullptr); }

Json series_json(const ReturnStatistics& stats) {
    return {{"mean_daily_return", stats.mean}, {"compounded_return", stats.compounded},
            {"maximum_daily_close_drawdown_loss", stats.maximum_drawdown},
            {"sample_daily_standard_deviation", optional_number(stats.sample_standard_deviation)},
            {"annualized_daily_sharpe", optional_number(stats.annualized_daily_sharpe)}};
}

Json period_json(const PeriodStatistics& stats) {
    Json winners = Json::array();
    for (const auto& winner : stats.largest_five_winners) {
        winners.push_back({{"date", winner.date}, {"net_return", winner.net_return}});
    }
    return {{"sessions", stats.sessions}, {"active_days", stats.active_days}, {"fraction_active", stats.fraction_active},
        {"strategy_net", series_json(stats.strategy_net)}, {"always_long_net", series_json(stats.always_long_net)},
        {"cash", series_json(stats.cash)}, {"mean_gross_active_return", optional_number(stats.mean_gross_active)},
        {"mean_net_active_return", optional_number(stats.mean_net_active)},
        {"mean_gross_nonpositive_signal_window_return", optional_number(stats.mean_gross_nonpositive_signal)},
        {"signal_contrast_gross_return", optional_number(stats.signal_contrast_gross)},
        {"break_even_cost_bps_per_side", optional_number(stats.break_even_cost_bps_per_side)},
        {"break_even_condition", stats.mean_gross_active && *stats.mean_gross_active > 0 ?
            "positive gross active mean" : "no positive gross active mean; no nonnegative profitable cost allowance"},
        {"concentration", {{"largest_five_winners", std::move(winners)},
            {"arithmetic_net_sum", stats.arithmetic_net_sum}, {"largest_five_net_sum", stats.largest_five_net_sum},
            {"largest_five_share_of_arithmetic_net", optional_number(stats.largest_five_share_of_arithmetic_net)},
            {"largest_five_share_of_positive_returns", optional_number(stats.largest_five_share_of_positive_returns)}}}};
}

Json interval_json(const PercentileInterval& value) { return {{"lower", value.lower}, {"upper", value.upper}}; }

} // namespace

DevelopmentDecision assess_development(const PeriodStatistics& primary, const PeriodStatistics& delay_stress) {
    for (const auto* stats : {&primary, &delay_stress}) {
        if (stats->active_days > stats->sessions) { throw std::invalid_argument("active-day count exceeds session count"); }
        for (const auto& value : {stats->mean_gross_active, stats->mean_net_active, stats->signal_contrast_gross}) {
            if (value && !std::isfinite(*value)) { throw std::invalid_argument("development screen requires finite statistics"); }
        }
    }
    if (primary.sessions != delay_stress.sessions || primary.active_days != delay_stress.active_days) {
        throw std::invalid_argument("development screen requires the same dates and active-day count across delays");
    }
    if (primary.active_days < 100) { return DevelopmentDecision::insufficient_active_days; }
    if (!primary.mean_gross_active || *primary.mean_gross_active <= 0) { return DevelopmentDecision::no_gross_edge; }
    if (!primary.mean_net_active || *primary.mean_net_active <= 0) { return DevelopmentDecision::cost_dominated; }
    if (!delay_stress.mean_net_active || *delay_stress.mean_net_active <= 0) { return DevelopmentDecision::delay_stress_failed; }
    if (!primary.signal_contrast_gross) { return DevelopmentDecision::undefined_signal_contrast; }
    if (*primary.signal_contrast_gross <= 0) { return DevelopmentDecision::nonpositive_signal_contrast; }
    return DevelopmentDecision::eligible_for_unchanged_validation;
}

std::string development_decision_name(DevelopmentDecision decision) {
    switch (decision) {
    case DevelopmentDecision::insufficient_active_days: return "insufficient_active_days";
    case DevelopmentDecision::no_gross_edge: return "no_gross_edge";
    case DevelopmentDecision::cost_dominated: return "cost_dominated";
    case DevelopmentDecision::delay_stress_failed: return "delay_stress_failed";
    case DevelopmentDecision::undefined_signal_contrast: return "undefined_signal_contrast";
    case DevelopmentDecision::nonpositive_signal_contrast: return "nonpositive_signal_contrast";
    case DevelopmentDecision::eligible_for_unchanged_validation: return "eligible_for_unchanged_validation";
    }
    throw std::invalid_argument("unknown development decision");
}

DevelopmentStudyResult DevelopmentStudy::run(const FrozenDevelopmentDataset& dataset, const Progress& progress) const {
    if (dataset.protocol_revision != 2 || dataset.sessions.size() != 496 || dataset.exclusions.size() != 5 ||
        dataset.protocol_sha256 != TRADING_PROTOCOL_SHA256 || dataset.manifest_file != "study_manifest.json" ||
        dataset.data_policy_sha256.size() != 64 ||
        dataset.sessions.front().request.start() < parse_utc_seconds("2022-01-01T00:00:00Z") ||
        dataset.sessions.back().request.end_exclusive() > parse_utc_seconds("2024-01-01T00:00:00Z")) {
        throw std::invalid_argument("development study requires the frozen revision-2 496-session 2022-2023 dataset");
    }
    if (progress) { progress("Replaying ten fixed cost/delay cases"); }
    DevelopmentStudyResult result{BatchMomentumReplay{}.run(dataset.sessions), {}, {}, DevelopmentDecision::insufficient_active_days};
    if (progress) { progress("Calculating period and annual return statistics"); }
    std::vector<DailyResearchReturn> primary_days;
    const PeriodStatistics* primary = nullptr;
    const PeriodStatistics* stress = nullptr;
    result.statistics.reserve(result.cases.size());
    for (const auto& replay : result.cases) {
        const auto days = research_returns(replay.days);
        CaseStatistics statistics{describe_returns(days), {}};
        for (const auto* year : {"2022", "2023"}) {
            std::vector<DailyResearchReturn> selected;
            std::copy_if(days.begin(), days.end(), std::back_inserter(selected),
                         [&](const auto& day) { return day.date.starts_with(year); });
            statistics.calendar_years.push_back({year, describe_returns(selected)});
        }
        result.statistics.push_back(std::move(statistics));
        if (replay.cost_bps_per_side == 1.0) {
            if (replay.delay == std::chrono::minutes{1}) { primary_days = days; primary = &result.statistics.back().overall; }
            else if (replay.delay == std::chrono::minutes{2}) { stress = &result.statistics.back().overall; }
        }
    }
    if (!primary || !stress) { throw std::logic_error("fixed protocol cases are missing"); }
    result.decision = assess_development(*primary, *stress);
    for (const auto length : {5U, 10U}) {
        if (progress) { progress("Running " + std::to_string(length) + "-day block bootstrap | 10000 resamples"); }
        result.primary_bootstrap.push_back(MovingBlockBootstrap{length}.run(primary_days));
    }
    if (progress) { progress("Development statistics complete"); }
    return result;
}

std::filesystem::path save_development_report(const FrozenDevelopmentDataset& dataset,
                                               const DevelopmentStudyResult& study,
                                               const std::filesystem::path& output_root) {
    if (study.cases.size() != 10 || study.statistics.size() != study.cases.size() || study.primary_bootstrap.size() != 2 ||
        dataset.protocol_revision != 2 || dataset.protocol_sha256 != TRADING_PROTOCOL_SHA256 ||
        sha256_file(contained_path(dataset.directory, dataset.manifest_file)) != dataset.manifest_sha256) {
        throw std::invalid_argument("development report requires complete protocol cases and an unchanged input manifest");
    }
    const auto csv = format_momentum_daily_csv(study.cases);
    Json cases = Json::array();
    for (std::size_t i = 0; i < study.cases.size(); ++i) {
        const auto& replay = study.cases[i];
        Json years = Json::array();
        for (const auto& year : study.statistics[i].calendar_years) {
            years.push_back({{"year", year.year}, {"statistics", period_json(year.statistics)}});
        }
        cases.push_back({{"delay_minutes", replay.delay.count()}, {"cost_bps_per_side", replay.cost_bps_per_side},
                         {"overall", period_json(study.statistics[i].overall)}, {"calendar_years", std::move(years)}});
    }
    Json bootstrap = Json::array();
    for (const auto& value : study.primary_bootstrap) {
        bootstrap.push_back({{"block_length", value.block_length}, {"replicates", value.replicates}, {"seed", value.seed},
            {"mean_net_daily_95", interval_json(value.mean_net_daily_95)},
            {"signal_contrast_gross_95", value.signal_contrast_gross_95 ? interval_json(*value.signal_contrast_gross_95) : Json(nullptr)},
            {"undefined_contrast_replicates", value.undefined_contrast_replicates}});
    }
    Json exclusions = Json::array();
    for (const auto& value : dataset.exclusions) { exclusions.push_back({{"date", value.date}, {"reason", value.reason}}); }
    const Json summary{{"schema_version", 3}, {"protocol", "SPY-IM-01"}, {"protocol_revision", dataset.protocol_revision}, {"purpose", "development"},
        {"protocol_sha256_at_build", TRADING_PROTOCOL_SHA256},
        {"input_manifest_sha256", dataset.manifest_sha256}, {"frozen_march_manifest_sha256", dataset.march_manifest_sha256},
        {"input_manifest_file", dataset.manifest_file}, {"data_policy_sha256", dataset.data_policy_sha256},
        {"start_date", "2022-01-01"}, {"end_date", "2023-12-31"}, {"calendar_sessions", 501},
        {"scored_sessions", dataset.sessions.size()}, {"validated_scored_bars", dataset.sessions.size() * 390}, {"unresolved_sessions", 0},
        {"exclusions", std::move(exclusions)}, {"return_unit", "decimal fraction; 0.0001 equals one basis point"},
        {"maximum_drawdown_unit", "nonnegative daily-close wealth loss fraction; initial wealth is one"},
        {"concentration_unit", "arithmetic return sums; shares of net may exceed one or be negative"},
        {"daily_returns_file", "daily_returns.csv"}, {"daily_returns_sha256", sha256(csv)},
        {"bootstrap_method", {{"sampling", "non-circular moving blocks; complete paired days; truncate to N"},
            {"random_engine", "mt19937_64"}, {"integer_mapping", "unsigned rejection then modulo; threshold=(-bound)%bound"},
            {"quantile", "linear interpolation at (M-1)*p; p=0.025,0.975"},
            {"missing_signal_group", "contrast interval null if any replicate has a missing group"},
            {"primary_delay_minutes", 1}, {"primary_cost_bps_per_side", 1}}},
        {"primary_bootstrap", std::move(bootstrap)}, {"cases", std::move(cases)},
        {"development_decision", development_decision_name(study.decision)},
        {"decision_rule", "at least 100 active days; positive primary gross/net, two-minute net and gross signal contrast"},
        {"bootstrap_role", "reported uncertainty; development does not require positive interval lower bounds"},
        {"execution_model", "continuous-notional delayed minute-open price proxies; hypothetical two-sided all-in costs"}};
    const auto directory = create_unique_directory(output_root, "SPY_IM_01_development_2022-2023");
    write_new_text_file(directory / "daily_returns.csv", csv);
    write_new_text_file(directory / "summary.json", summary.dump(2) + '\n');
    return directory;
}

} // namespace trading
