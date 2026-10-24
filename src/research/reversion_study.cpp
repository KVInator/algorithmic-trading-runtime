#include "reversion_study.hpp"

#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "market_calendar.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trading {
namespace {
std::array<std::optional<double>, 88> values(const ReversionStudyDay& day) {
    std::array<std::optional<double>, 88> result{};
    for (std::size_t i = 0; i < 40; ++i) {
        result[i] = day.accounts[i].net;
        result[40 + i] = day.accounts[i].gross;
    }
    for (std::size_t p = 1; p < 4; ++p) {
        result[78 + 2 * p] = day.accounts[2].net - day.accounts[10 * p + 2].net;
        result[79 + 2 * p] = day.accounts[2].gross - day.accounts[10 * p + 2].gross;
    }
    result[86] = day.squared_loss_contrast;
    result[87] = day.absolute_loss_contrast;
    return result;
}

std::array<std::string, 88> labels() {
    std::array<std::string, 88> result;
    const auto& cases = reversion_cases();
    for (std::size_t i = 0; i < 40; ++i) {
        const auto& c = cases[i];
        const auto suffix = std::string{name(c.policy)} + "_delay_" + std::to_string(c.delay) + "_cost_" +
                            std::to_string(c.cost);
        result[i] = "net_mean_" + suffix;
        result[40 + i] = "gross_mean_" + suffix;
    }
    for (std::size_t p = 1; p < 4; ++p) {
        const auto suffix = std::string{name(static_cast<ReversionPolicy>(p))};
        result[78 + 2 * p] = "paired_net_vs_" + suffix;
        result[79 + 2 * p] = "paired_gross_vs_" + suffix;
    }
    result[86] = "drift_minus_candidate_squared_loss";
    result[87] = "drift_minus_candidate_absolute_loss";
    return result;
}

ReversionPeriod describe(std::span<const ReversionStudyDay> days, std::size_t index) {
    std::vector<DailyResearchReturn> paired;
    std::vector<double> gross;
    std::vector<WinningContribution> contributions;
    for (const auto& day : days) {
        const auto& c = reversion_cases()[index];
        const auto window =
            evaluate_long_prices(day.entry_prices[static_cast<std::size_t>(c.delay - 1)],
                                 day.exit_prices[static_cast<std::size_t>(c.delay - 1)], c.cost);
        paired.push_back({day.date, day.decision.actions[static_cast<std::size_t>(c.policy)].enter,
                          window.gross_return, window.net_return});
        gross.push_back(day.accounts[index].gross);
        contributions.push_back({day.date, day.accounts[index].net});
    }
    const auto returns = describe_returns(paired);
    return {returns,
            describe_return_series(gross),
            describe_return_concentration(contributions),
            returns.active_days * 30,
            2 * returns.fraction_active,
            returns.fraction_active * 30 / 390};
}

void validate_days(std::span<const ReversionStudyDay> days) {
    if (days.size() < 10 || days.size() > 492) {
        throw std::invalid_argument("reversion comparison requires 10..492 scored dates");
    }
    std::string previous;
    for (const auto& day : days) {
        static_cast<void>(parse_utc_seconds(day.date + "T00:00:00Z"));
        if (day.date <= previous) {
            throw std::invalid_argument("reversion comparison dates must be unique and ascending");
        }
        previous = day.date;
        for (std::size_t i = 0; i < 40; ++i) {
            const auto& c = reversion_cases()[i];
            const auto r =
                evaluate_long_prices(day.entry_prices[static_cast<std::size_t>(c.delay - 1)],
                                     day.exit_prices[static_cast<std::size_t>(c.delay - 1)], c.cost);
            const bool active = day.decision.actions[static_cast<std::size_t>(c.policy)].enter;
            if (day.accounts[i].gross != (active ? r.gross_return : 0) ||
                day.accounts[i].net != (active ? r.net_return : 0)) {
                throw std::invalid_argument(
                    "account return does not match its fixed decision and reference prices");
            }
        }
        for (const auto& value : values(day)) {
            if (value && !std::isfinite(*value)) {
                throw std::invalid_argument("nonfinite comparison statistic");
            }
        }
        if (day.squared_loss_contrast.has_value() != day.absolute_loss_contrast.has_value()) {
            throw std::invalid_argument("forecast loss masks differ");
        }
    }
}

bool passes(const std::vector<ReversionGate>& gates) {
    return std::all_of(gates.begin(), gates.end(), [](const auto& g) { return g.passed; });
}
} // namespace

const std::array<ReversionCase, 40>& reversion_cases() {
    static const auto cases = [] {
        std::array<ReversionCase, 40> result{};
        std::size_t i = 0;
        for (const auto p : {ReversionPolicy::candidate, ReversionPolicy::sign_only, ReversionPolicy::drift,
                             ReversionPolicy::always_long}) {
            for (const int delay : {1, 2}) {
                for (const double cost : {0.0, 0.5, 1.0, 2.0, 5.0}) {
                    result[i++] = {p, delay, cost};
                }
            }
        }
        return result;
    }();
    return cases;
}

ReversionStudy::ReversionStudy(ReversionStudySettings settings) : settings_{settings} {
    if (settings.parameter_refits < 20 || settings.parameter_refits > 100000 ||
        settings.outcome_resamples < 20 || settings.outcome_resamples > 100000) {
        throw std::invalid_argument("invalid reversion research budgets");
    }
}

void ReversionStudy::verify_registration(const std::filesystem::path& root) {
    if (sha256_file(root / "research/intraday_reversion_protocol.md") != reversion_protocol_digest ||
        sha256_file(root / "research/intraday_reversion_protocol.json") != reversion_settings_digest) {
        throw std::invalid_argument("SPY-MR-01 revision 1 protocol/settings identity changed");
    }
}

ReversionStudyResult ReversionStudy::replay(std::span<const SessionDownload> sessions,
                                            const ReversionProgress& progress) const {
    if (sessions.size() < 30 || sessions.size() > 512) {
        throw std::invalid_argument("reversion replay requires 30..512 eligible sessions");
    }
    ReversionStudyResult result{};
    ReversionHistory history{};
    for (const auto& session : sessions) {
        if (session.bars.size() != 390 || !audit_session(session.request, session.bars).complete()) {
            throw std::invalid_argument("reversion replay requires every regular-session minute");
        }
        const auto date = format_utc_seconds(session.request.start()).substr(0, 10);
        if (history.ready()) {
            auto decision = strategy_.decide(session.request, std::span{session.bars}.first(150), history);
            ReversionStudyDay day{date, std::move(decision), {}, {}, {}, 0, {}, {}, {}};
            for (std::size_t d = 0; d < 2; ++d) {
                day.entry_prices[d] = session.bars[151 + d].prices().open;
                day.exit_prices[d] = session.bars[181 + d].prices().open;
            }
            for (std::size_t i = 0; i < 40; ++i) {
                const auto& c = reversion_cases()[i];
                const auto r =
                    evaluate_long_prices(day.entry_prices[static_cast<std::size_t>(c.delay - 1)],
                                         day.exit_prices[static_cast<std::size_t>(c.delay - 1)], c.cost);
                if (day.decision.actions[static_cast<std::size_t>(c.policy)].enter) {
                    day.accounts[i] = {r.gross_return, r.net_return};
                }
            }
            day.realized_log_move = std::log(day.exit_prices[0]) - std::log(day.entry_prices[0]);
            if (day.decision.expected_move && day.decision.prior.drift) {
                const double a = day.realized_log_move - *day.decision.expected_move,
                             b = day.realized_log_move - *day.decision.prior.drift;
                day.squared_loss_contrast = b * b - a * a;
                day.absolute_loss_contrast = std::abs(b) - std::abs(a);
            }
            if (day.decision.prior.complete) {
                auto encoded = date;
                encoded.erase(std::remove(encoded.begin(), encoded.end(), '-'), encoded.end());
                const auto seed = std::uint64_t{20261023} + std::stoull(encoded);
                for (const std::size_t block : {1U, 5U}) {
                    const std::optional<double> current =
                        day.decision.current ? std::optional{day.decision.current->values.back()}
                                             : std::nullopt;
                    day.parameter_ranges.push_back(
                        ReversionUncertainty{{settings_.parameter_refits, block, seed}}.run(
                            day.decision.prior.sequences, current, 1, 30));
                }
            }
            result.days.push_back(std::move(day));
            if (progress && result.days.size() % 25 == 0) {
                progress("Prior-only replay and parameter ranges | " + std::to_string(result.days.size()) +
                         " dates");
            }
        }
        // Commit this history slot only after every current decision and window.
        result.observations.push_back(
            {date, history.complete(session.request, session.bars, session.request.end_exclusive())});
    }
    return result;
}

void ReversionStudy::analyze(ReversionStudyResult& result, const ReversionProgress& progress) const {
    validate_days(result.days);
    result.statistics.clear();
    result.uncertainty.clear();
    for (std::size_t i = 0; i < 40; ++i) {
        std::vector<double> net;
        for (const auto& day : result.days) {
            net.push_back(day.accounts[i].net);
        }
        const auto compounded = compound_return_path(net);
        auto& ledger = result.paths[i];
        ledger.clear();
        const auto& c = reversion_cases()[i];
        for (std::size_t d = 0; d < result.days.size(); ++d) {
            const double before = d == 0 ? 1 : compounded[d - 1].wealth;
            const auto& day = result.days[d];
            const double quantity = day.decision.actions[static_cast<std::size_t>(c.policy)].enter
                                        ? before / (day.entry_prices[static_cast<std::size_t>(c.delay - 1)] *
                                                    (1 + c.cost / 10000))
                                        : 0;
            if (!std::isfinite(quantity)) {
                throw std::overflow_error("fractional account quantity overflow");
            }
            ledger.push_back({before, quantity, compounded[d].wealth, compounded[d].drawdown});
        }
        ReversionCaseStatistics stats{describe(result.days, i), {}};
        for (auto begin = result.days.begin(); begin != result.days.end();) {
            const auto year = begin->date.substr(0, 4);
            const auto end = std::find_if(begin, result.days.end(),
                                          [&](const auto& d) { return !d.date.starts_with(year); });
            stats.years.emplace(year, describe(std::span{begin, end}, i));
            begin = end;
        }
        result.statistics.push_back(std::move(stats));
    }
    const auto names = labels();
    std::vector<std::array<std::optional<double>, 88>> records;
    for (const auto& d : result.days) {
        records.push_back(values(d));
    }
    for (std::size_t m = 0; m < 88; ++m) {
        long double sum = 0;
        std::size_t count = 0;
        for (const auto& row : records) {
            if (row[m]) {
                sum += *row[m];
                ++count;
            }
        }
        result.metrics[m] = {names[m], count,
                             count ? std::optional{static_cast<double>(sum / static_cast<long double>(count))}
                                   : std::nullopt};
    }
    for (const std::size_t block : {5U, 10U}) {
        if (progress) {
            progress("Paired full-date outcome uncertainty | block " + std::to_string(block));
        }
        MovingBlockIndexSampler sampler{result.days.size(), block, 20261023};
        std::array<std::vector<double>, 88> samples;
        ReversionOutcomeUncertainty output{block, settings_.outcome_resamples, 20261023, {}};
        for (std::size_t b = 0; b < settings_.outcome_resamples; ++b) {
            std::array<long double, 88> sums{};
            std::array<std::size_t, 88> counts{};
            for (const auto index : sampler.next()) {
                for (std::size_t m = 0; m < 88; ++m) {
                    if (records[index][m]) {
                        sums[m] += *records[index][m];
                        ++counts[m];
                    }
                }
            }
            for (std::size_t m = 0; m < 88; ++m) {
                if (counts[m] == 0) {
                    ++output.metrics[m].undefined_replicates;
                } else {
                    const double v = static_cast<double>(sums[m] / static_cast<long double>(counts[m]));
                    if (!std::isfinite(v)) {
                        throw std::overflow_error("outcome resample overflow");
                    }
                    samples[m].push_back(v);
                }
            }
        }
        for (std::size_t m = 0; m < 88; ++m) {
            if (output.metrics[m].undefined_replicates == 0) {
                output.metrics[m].range = percentile_interval(std::move(samples[m]));
            }
        }
        result.uncertainty.push_back(std::move(output));
    }
    assess(result);
}

void ReversionStudy::assess(ReversionStudyResult& result) const {
    if (result.statistics.size() != 40 || result.uncertainty.size() != 2 ||
        result.uncertainty[0].block_length != 5 || result.uncertainty[1].block_length != 10) {
        throw std::invalid_argument("assessment requires all fixed cases and both outcome blocks");
    }
    const auto& s = result.statistics[2];
    const auto& p = s.overall;
    const auto positive = [&](std::size_t m) {
        return result.metrics[m].mean && *result.metrics[m].mean > 0;
    };
    result.economic_gates = {
        {"at_least_100_active_dates", p.returns.active_days >= 100},
        {"positive_primary_gross", p.gross.mean > 0},
        {"positive_primary_net", p.returns.strategy_net.mean > 0},
        {"positive_net_2022", s.years.contains("2022") && s.years.at("2022").returns.strategy_net.mean > 0},
        {"positive_net_2023", s.years.contains("2023") && s.years.at("2023").returns.strategy_net.mean > 0},
        {"positive_one_minute_2bp_net", result.statistics[3].overall.returns.strategy_net.mean > 0},
        {"positive_two_minute_1bp_net", result.statistics[7].overall.returns.strategy_net.mean > 0},
        {"top_five_share_at_most_half", p.concentration.largest_five_share_of_positive_returns &&
                                            *p.concentration.largest_five_share_of_positive_returns <= .5},
        {"positive_net_sum_without_top_five",
         p.concentration.arithmetic_net_sum - p.concentration.largest_five_net_sum > 0}};
    for (const auto m : {80U, 82U}) {
        result.economic_gates.push_back({"positive_" + result.metrics[m].label, positive(m)});
    }
    for (const auto m : {81U, 83U}) {
        result.economic_gates.push_back({"nonnegative_" + result.metrics[m].label,
                                         result.metrics[m].mean && *result.metrics[m].mean >= 0});
    }
    result.forecast_gates = {{"positive_squared_loss_improvement", positive(86)},
                             {"positive_absolute_loss_improvement", positive(87)}};
    for (const auto& u : result.uncertainty) {
        const auto suffix = "_block_" + std::to_string(u.block_length);
        for (const auto m : {2U, 80U, 82U}) {
            const auto& r = u.metrics[m].range;
            result.economic_gates.push_back(
                {"positive_lower_" + result.metrics[m].label + suffix, r && r->lower > 0});
        }
        for (const auto m : {86U, 87U}) {
            const auto& r = u.metrics[m].range;
            result.forecast_gates.push_back(
                {"positive_lower_" + result.metrics[m].label + suffix, r && r->lower > 0});
        }
    }
    result.status = p.gross.mean <= 0                  ? "no_gross_edge"
                    : p.returns.strategy_net.mean <= 0 ? "cost_dominated"
                    : passes(result.economic_gates)    ? "passes_development_screens"
                                                       : "failed_registered_screens";
}

ReversionStudyResult ReversionStudy::run_frozen(const FrozenDevelopmentDataset& dataset,
                                                const std::filesystem::path& root,
                                                const ReversionProgress& progress) const {
    verify_registration(root);
    const std::array<std::string_view, 5> excluded{"2022-11-25", "2023-01-03", "2023-06-05", "2023-07-03",
                                                   "2023-11-24"};
    if (settings_.parameter_refits != 499 || settings_.outcome_resamples != 10000 ||
        dataset.manifest_sha256 != reversion_manifest_digest ||
        dataset.manifest_file != "study_manifest.json" || dataset.protocol_revision != 2 ||
        dataset.sessions.size() != 496 || dataset.march_manifest_sha256 != frozen_march_pilot_sha256 ||
        dataset.exclusions.size() != 5 ||
        sha256_file(contained_path(dataset.directory, dataset.manifest_file)) != reversion_manifest_digest) {
        throw std::invalid_argument(
            "registered reversion requires authenticated 496-session snapshot and full fixed budgets");
    }
    for (std::size_t i = 0; i < 5; ++i) {
        if (dataset.exclusions[i].date != excluded[i]) {
            throw std::invalid_argument("registered exclusions changed");
        }
    }
    std::size_t index = 0;
    for (const auto& month : development_months()) {
        for (const auto& date : development_calendar(month.range)) {
            if (!date.regular() ||
                std::find(excluded.begin(), excluded.end(), date.date()) != excluded.end()) {
                continue;
            }
            if (index >= dataset.sessions.size() ||
                dataset.sessions[index].request.start() != date.open_utc() ||
                dataset.sessions[index].request.end_exclusive() != date.close_utc()) {
                throw std::invalid_argument("registered calendar sequence changed");
            }
            ++index;
        }
    }
    if (index != 496) {
        throw std::invalid_argument("registered calendar count changed");
    }
    auto result = replay(dataset.sessions, progress);
    analyze(result, progress);
    return result;
}
} // namespace trading
