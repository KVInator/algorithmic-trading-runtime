#include "continuation_study.hpp"
#include "content_digest.hpp"
#include "artifact_io.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {

constexpr std::size_t price_primary = 2;
constexpr std::size_t volume_primary = 12;
constexpr std::size_t resamples = 10000;
constexpr std::uint64_t bootstrap_seed = 20261021;

double finite(double value) {
    if (!std::isfinite(value)) { throw std::overflow_error("continuation aggregation exceeds the finite range"); }
    return value;
}

ContinuationCaseDay recost(const IntradayContinuationReplay& replay, double cost) {
    ContinuationCaseDay result;
    double wealth = 1;
    result.minutes_in_market = replay.minutes_in_market;
    result.gross_return = replay.strategy_gross_return;
    for (auto trade : replay.trades) {
        trade.returns = evaluate_long_prices(trade.entry_reference_price, trade.exit_reference_price, cost);
        result.turnover = finite(result.turnover + wealth *
            (trade.entry_reference_price / trade.returns.buy_after_cost +
             trade.exit_reference_price / trade.returns.buy_after_cost));
        wealth = finite(wealth * (1 + trade.returns.net_return));
        if (wealth <= 0) { throw std::range_error("continuation wealth is non-positive"); }
        result.trades.push_back(std::move(trade));
    }
    result.net_return = wealth - 1;
    const auto& control = replay.always_long_control;
    const auto returns = evaluate_long_prices(control.entry_reference_price, control.exit_reference_price, cost);
    result.control_gross_return = returns.gross_return;
    result.control_net_return = returns.net_return;
    return result;
}

void add_groups(std::array<ClockGroups, 12>& groups, const ContinuationDay& day) {
    for (std::size_t i = 0; i < groups.size(); ++i) {
        auto& group = groups[i];
        const auto& opportunity = day.opportunities[i];
        const double forward = opportunity.forward[0].returns.gross_return;
        if (opportunity.features.move > opportunity.features.typical_absolute_move) {
            ++group.price_true_count;
            group.price_true_sum = finite(group.price_true_sum + forward);
            if (opportunity.features.relative_volume > 1) {
                ++group.high_volume_count;
                group.high_volume_sum = finite(group.high_volume_sum + forward);
            } else {
                ++group.other_volume_count;
                group.other_volume_sum = finite(group.other_volume_sum + forward);
            }
        } else {
            ++group.price_false_count;
            group.price_false_sum = finite(group.price_false_sum + forward);
        }
    }
}

ContinuationContrasts contrasts(std::array<ClockGroups, 12> groups) {
    ContinuationContrasts result{groups, 0.0, 0.0};
    for (const auto& group : groups) {
        if (group.price_true_count == 0 || group.price_false_count == 0) { result.price.reset(); }
        else if (result.price) {
            *result.price = finite(*result.price + (group.price_true_sum / static_cast<double>(group.price_true_count) -
                group.price_false_sum / static_cast<double>(group.price_false_count)) / 12.0);
        }
        if (group.high_volume_count == 0 || group.other_volume_count == 0) { result.volume.reset(); }
        else if (result.volume) {
            *result.volume = finite(*result.volume + (group.high_volume_sum / static_cast<double>(group.high_volume_count) -
                group.other_volume_sum / static_cast<double>(group.other_volume_count)) / 12.0);
        }
    }
    return result;
}

ContinuationPeriodStatistics describe(std::span<const ContinuationDay> days, std::size_t case_index) {
    std::vector<double> gross, net, control;
    std::vector<WinningContribution> contributions;
    std::size_t active = 0, trades = 0;
    double minutes = 0, turnover = 0;
    for (const auto& day : days) {
        const auto& value = day.cases[case_index];
        gross.push_back(value.gross_return);
        net.push_back(value.net_return);
        control.push_back(value.control_net_return);
        contributions.push_back({day.date, value.net_return});
        active += value.trades.empty() ? 0U : 1U;
        trades += value.trades.size();
        minutes = finite(minutes + static_cast<double>(value.minutes_in_market.count()));
        turnover = finite(turnover + value.turnover);
    }
    const double count = static_cast<double>(days.size());
    return {days.size(), active, trades, minutes / (390 * count), turnover / count,
        describe_return_series(gross), describe_return_series(net), describe_return_series(control),
        describe_return_concentration(contributions)};
}

ContinuationBootstrapStatistics bootstrap(std::span<const ContinuationDay> days, std::size_t length,
                                          const StudyProgress& progress) {
    MovingBlockIndexSampler sampler{days.size(), length, bootstrap_seed};
    std::array<std::vector<double>, 7> samples;
    for (auto& sample : samples) { sample.reserve(resamples); }
    std::size_t undefined_price = 0, undefined_volume = 0;
    const double count = static_cast<double>(days.size());
    for (std::size_t replicate = 0; replicate < resamples; ++replicate) {
        std::array<double, 5> sums{};
        std::array<ClockGroups, 12> groups{};
        for (const auto index : sampler.next()) {
            const auto& day = days[index];
            const auto& price = day.cases[price_primary];
            const auto& volume = day.cases[volume_primary];
            const std::array<double, 5> values{price.net_return, volume.net_return, price.control_net_return,
                volume.net_return - price.net_return, volume.gross_return - price.gross_return};
            for (std::size_t i = 0; i < values.size(); ++i) { sums[i] = finite(sums[i] + values[i]); }
            add_groups(groups, day);
        }
        for (std::size_t i = 0; i < sums.size(); ++i) { samples[i].push_back(sums[i] / count); }
        const auto contrast = contrasts(groups);
        if (contrast.price) { samples[5].push_back(*contrast.price); } else { ++undefined_price; }
        if (contrast.volume) { samples[6].push_back(*contrast.volume); } else { ++undefined_volume; }
        if (progress && (replicate + 1) % 2000 == 0) {
            progress(std::to_string(length) + "-session bootstrap: " + std::to_string(replicate + 1) + "/10000");
        }
    }
    return {length, resamples, bootstrap_seed, percentile_interval(std::move(samples[0])),
        percentile_interval(std::move(samples[1])), percentile_interval(std::move(samples[2])),
        percentile_interval(std::move(samples[3])), percentile_interval(std::move(samples[4])),
        undefined_price == 0 ? std::optional{percentile_interval(std::move(samples[5]))} : std::nullopt,
        undefined_volume == 0 ? std::optional{percentile_interval(std::move(samples[6]))} : std::nullopt,
        undefined_price, undefined_volume};
}

bool positive(const std::optional<double>& value) { return value && *value > 0; }
bool positive_lower(const std::optional<PercentileInterval>& value) { return value && value->lower > 0; }
bool all_pass(const std::vector<ContinuationGate>& gates) {
    return std::all_of(gates.begin(), gates.end(), [](const auto& gate) { return gate.passed; });
}

ContinuationAssessment assess_variant(const ContinuationStudyResult& result, std::size_t variant) {
    const auto primary = variant * 10 + price_primary;
    const auto& stats = result.statistics[primary];
    const auto& period = stats.overall;
    if (period.sessions == 0 || period.active_days > period.sessions) {
        throw std::invalid_argument("invalid continuation sample counts");
    }
    const auto& concentration = period.concentration;
    ContinuationAssessment assessment{{}, {
        {"at_least_100_active_days", period.active_days >= 100},
        {"positive_gross_daily_mean", period.gross.mean > 0},
        {"positive_primary_net_mean", period.net.mean > 0},
        {"positive_two_minute_1bp_net_mean", result.statistics[primary + 5].overall.net.mean > 0},
        {"positive_one_minute_2bp_net_mean", result.statistics[primary + 1].overall.net.mean > 0},
        {"positive_net_mean_in_2022_and_2023", stats.years.contains("2022") && stats.years.contains("2023") &&
            stats.years.at("2022").net.mean > 0 && stats.years.at("2023").net.mean > 0},
        {"positive_defined_price_information_contrast", positive(result.contrasts.price)},
        {"five_winners_at_most_half_positive_sum", concentration.largest_five_share_of_positive_returns &&
            *concentration.largest_five_share_of_positive_returns <= 0.5},
        {"positive_net_sum_without_five_winners", concentration.arithmetic_net_sum - concentration.largest_five_net_sum > 0}
    }, {}, false};
    for (const auto& value : result.bootstrap) {
        const auto& mean = variant == 0 ? value.price_net_mean_95 : value.volume_net_mean_95;
        const auto suffix = "_" + std::to_string(value.block_length) + "_session_blocks";
        assessment.uncertainty_gates.push_back({"positive_net_mean_lower_bound" + suffix, mean.lower > 0});
        assessment.uncertainty_gates.push_back({"positive_price_contrast_lower_bound" + suffix,
            positive_lower(value.price_contrast_95)});
    }
    assessment.later_evaluation_allowed = all_pass(assessment.point_gates) && all_pass(assessment.uncertainty_gates);
    if (period.gross.mean <= 0) { assessment.status = "no_gross_edge"; }
    else if (period.active_days < 100) { assessment.status = "insufficient_active_days"; }
    else if (period.net.mean <= 0) { assessment.status = "cost_dominated"; }
    else if (!all_pass(assessment.point_gates)) { assessment.status = "development_screen_failed"; }
    else if (!assessment.later_evaluation_allowed) { assessment.status = "inconclusive_uncertainty"; }
    else { assessment.status = "eligible_for_declared_later_evaluation"; }
    return assessment;
}

} // namespace

ContinuationAssessment assess_continuation_variant(const ContinuationStudyResult& result, ContinuationVariant variant) {
    if (result.statistics.size() != continuation_case_count || result.bootstrap.size() != 2 ||
        result.bootstrap[0].block_length != 5 || result.bootstrap[1].block_length != 10) {
        throw std::invalid_argument("continuation screens require all fixed cases and both block lengths");
    }
    if (variant != ContinuationVariant::price_only && variant != ContinuationVariant::volume_conditioned) {
        throw std::invalid_argument("unknown continuation screen variant");
    }
    return assess_variant(result, variant == ContinuationVariant::price_only ? 0U : 1U);
}

std::string_view continuation_variant_name(ContinuationVariant variant) {
    switch (variant) {
    case ContinuationVariant::price_only: return "price_only";
    case ContinuationVariant::volume_conditioned: return "volume_conditioned";
    }
    throw std::invalid_argument("unknown continuation variant");
}

const std::array<ContinuationCase, continuation_case_count>& continuation_cases() {
    static const auto cases = [] {
        std::array<ContinuationCase, continuation_case_count> result{};
        std::size_t index = 0;
        for (const auto variant : {ContinuationVariant::price_only, ContinuationVariant::volume_conditioned}) {
            for (const auto delay : {1, 2}) {
                for (const auto cost : {0.0, 0.5, 1.0, 2.0, 5.0}) {
                    result[index++] = {variant, std::chrono::minutes{delay}, cost};
                }
            }
        }
        return result;
    }();
    return cases;
}

std::size_t continuation_case_index(ContinuationVariant variant, std::chrono::minutes delay, double cost) {
    const auto& cases = continuation_cases();
    const auto found = std::find_if(cases.begin(), cases.end(), [&](const auto& value) {
        return value.variant == variant && value.delay == delay && value.cost_bps_per_side == cost;
    });
    if (found == cases.end()) { throw std::invalid_argument("undeclared continuation cost/delay case"); }
    return static_cast<std::size_t>(found - cases.begin());
}

std::vector<ContinuationDay> ContinuationStudy::replay(
    std::span<const SessionDownload> sessions, const StudyProgress& progress) const {
    if (sessions.size() <= continuation_history_sessions || sessions.size() > 512) {
        throw std::invalid_argument("continuation replay requires 21 to 512 eligible sessions");
    }
    IntradayContinuationHistory history;
    std::vector<ContinuationDay> result;
    for (const auto& session : sessions) {
        const auto date = format_utc_seconds(session.request.start()).substr(0, 10);
        if (history.ready()) {
            ContinuationDay day{};
            day.date = date;
            for (std::size_t variant = 0; variant < 2; ++variant) {
                const auto kind = variant == 0 ? ContinuationVariant::price_only : ContinuationVariant::volume_conditioned;
                const IntradayContinuationStrategy strategy{session.request, kind};
                for (int delay = 1; delay <= 2; ++delay) {
                    const auto replayed = IntradayContinuationReplayEngine{std::chrono::minutes{delay}, 0}.run(
                        strategy, history, session.bars);
                    for (const double cost : {0.0, 0.5, 1.0, 2.0, 5.0}) {
                        day.cases[continuation_case_index(kind, std::chrono::minutes{delay}, cost)] = recost(replayed, cost);
                    }
                    if (delay == 1) {
                        for (std::size_t i = 0; i < replayed.decisions.size(); ++i) {
                            day.actions[variant][i] = replayed.decisions[i].action;
                        }
                    }
                    if (variant == 0) {
                        for (std::size_t clock = 0; clock < day.opportunities.size(); ++clock) {
                            const auto& decision = replayed.decisions[clock];
                            auto& opportunity = day.opportunities[clock];
                            opportunity.features = *decision.features;
                            const auto end = replayed.decisions[clock + 1].at;
                            const auto start_index = static_cast<std::size_t>(std::chrono::duration_cast<std::chrono::minutes>(
                                decision.at - session.request.start()).count() + delay);
                            const auto end_index = static_cast<std::size_t>(std::chrono::duration_cast<std::chrono::minutes>(
                                end - session.request.start()).count() + delay);
                            const double entry = session.bars.at(start_index).prices().open;
                            const double exit = session.bars.at(end_index).prices().open;
                            opportunity.forward[static_cast<std::size_t>(delay - 1)] = {
                                decision.at, decision.at + std::chrono::minutes{delay}, end, end + std::chrono::minutes{delay},
                                entry, exit, evaluate_long_prices(entry, exit, 0)};
                        }
                    }
                }
            }
            result.push_back(std::move(day));
            if (progress && (result.size() % 20 == 0 || &session == &sessions.back())) {
                progress("Replayed through " + date + " | " + std::to_string(result.size()) + " scored sessions");
            }
        }
        // A session never enters its own feature history.
        history.append(session.request, session.bars);
    }
    return result;
}

ContinuationStudyResult ContinuationStudy::analyze(std::vector<ContinuationDay> days, const StudyProgress& progress) const {
    if (days.size() < 10 || days.size() > 492) { throw std::invalid_argument("continuation statistics require 10 to 492 scored sessions"); }
    std::string previous;
    std::array<ClockGroups, 12> groups{};
    double net_increment = 0, gross_increment = 0;
    for (const auto& day : days) {
        static_cast<void>(parse_utc_seconds(day.date + "T00:00:00Z"));
        if (day.date <= previous) { throw std::invalid_argument("continuation dates must be unique and ascending"); }
        previous = day.date;
        for (const auto& opportunity : day.opportunities) {
            if (!std::isfinite(opportunity.features.move) || !std::isfinite(opportunity.features.typical_absolute_move) ||
                !std::isfinite(opportunity.features.relative_volume) || opportunity.features.typical_absolute_move < 0 ||
                opportunity.features.relative_volume < 0) { throw std::invalid_argument("invalid continuation opportunity feature"); }
        }
        for (const auto& value : day.cases) {
            if (!std::isfinite(value.turnover) || value.turnover < 0 || value.minutes_in_market.count() < 0 ||
                value.minutes_in_market > std::chrono::minutes{355}) { throw std::invalid_argument("invalid continuation exposure"); }
        }
        add_groups(groups, day);
        net_increment = finite(net_increment + day.cases[volume_primary].net_return - day.cases[price_primary].net_return);
        gross_increment = finite(gross_increment + day.cases[volume_primary].gross_return - day.cases[price_primary].gross_return);
    }
    const auto count = static_cast<double>(days.size());
    ContinuationStudyResult result{std::move(days), {}, contrasts(groups), {}, net_increment / count, gross_increment / count, {}, {}};
    if (progress) { progress("Calculating twenty fixed cases, annual statistics and concentration"); }
    for (std::size_t i = 0; i < continuation_case_count; ++i) {
        ContinuationCaseStatistics stats{describe(result.days, i), {}};
        for (auto begin = result.days.begin(); begin != result.days.end();) {
            const auto year = begin->date.substr(0, 4);
            const auto end = std::find_if(begin, result.days.end(), [&](const auto& day) { return !day.date.starts_with(year); });
            stats.years.emplace(year, describe(std::span{begin, end}, i));
            begin = end;
        }
        result.statistics.push_back(std::move(stats));
    }
    for (const std::size_t length : {5U, 10U}) { result.bootstrap.push_back(bootstrap(result.days, length, progress)); }
    result.variants[0] = assess_continuation_variant(result, ContinuationVariant::price_only);
    result.variants[1] = assess_continuation_variant(result, ContinuationVariant::volume_conditioned);
    result.volume_increment = {"unproven_or_adverse", {
        {"positive_paired_net_increment", result.paired_net_increment > 0},
        {"nonnegative_paired_gross_increment", result.paired_gross_increment >= 0},
        {"positive_defined_volume_information_contrast", positive(result.contrasts.volume)}}, {}, false};
    for (const auto& value : result.bootstrap) {
        const auto suffix = "_" + std::to_string(value.block_length) + "_session_blocks";
        result.volume_increment.uncertainty_gates.push_back({"positive_paired_net_lower_bound" + suffix,
            value.paired_net_increment_95.lower > 0});
        result.volume_increment.uncertainty_gates.push_back({"positive_volume_contrast_lower_bound" + suffix,
            positive_lower(value.volume_contrast_95)});
    }
    result.volume_increment.later_evaluation_allowed = all_pass(result.volume_increment.point_gates) &&
        all_pass(result.volume_increment.uncertainty_gates) && result.variants[1].later_evaluation_allowed;
    if (result.volume_increment.later_evaluation_allowed) { result.volume_increment.status = "supported_for_declared_later_evaluation"; }
    else if (all_pass(result.volume_increment.point_gates)) { result.volume_increment.status = "positive_point_estimates_unproven"; }
    if (result.variants[1].later_evaluation_allowed && !result.volume_increment.later_evaluation_allowed) {
        result.variants[1].later_evaluation_allowed = false;
        result.variants[1].status = "incremental_volume_unproven";
    }
    return result;
}

ContinuationStudyResult ContinuationStudy::run_frozen(const FrozenDevelopmentDataset& dataset, const StudyProgress& progress) const {
    const std::array<std::string_view, 5> excluded{"2022-11-25", "2023-01-03", "2023-06-05", "2023-07-03", "2023-11-24"};
    if (dataset.manifest_sha256 != continuation_development_manifest_sha256 || dataset.sessions.size() != 496 ||
        dataset.march_manifest_sha256 != frozen_march_pilot_sha256 || dataset.protocol_revision != 2 ||
        dataset.manifest_file != "study_manifest.json" || dataset.exclusions.size() != excluded.size() ||
        sha256_file(contained_path(dataset.directory, dataset.manifest_file)) != dataset.manifest_sha256) {
        throw std::invalid_argument("SPY-TOD-01 requires the verified frozen 496-session development snapshot");
    }
    for (std::size_t i = 0; i < excluded.size(); ++i) {
        if (dataset.exclusions[i].date != excluded[i]) { throw std::invalid_argument("continuation exclusions changed"); }
    }
    for (const auto& session : dataset.sessions) {
        const auto date = format_utc_seconds(session.request.start()).substr(0, 10);
        if (date < "2022-01-01" || date > "2023-12-31" ||
            std::find(excluded.begin(), excluded.end(), date) != excluded.end()) {
            throw std::invalid_argument("session is outside the registered continuation development sample");
        }
    }
    return analyze(replay(dataset.sessions, progress), progress);
}

} // namespace trading
