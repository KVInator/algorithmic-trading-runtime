#include "stability_study.hpp"
#include "content_digest.hpp"
#include "return_statistics.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace trading {
namespace {
using Json = nlohmann::json;
using Moment = std::array<long double, 5>;
void require(bool value, const char *reason) {
    if (!value) {
        throw std::invalid_argument(reason);
    }
}
Json optional(std::optional<double> value) { return value ? Json(*value) : Json(nullptr); }
double quantile(std::vector<double> values, double p) {
    require(!values.empty(), "quantile requires observations");
    std::sort(values.begin(), values.end());
    const double x = p * static_cast<double>(values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(x)),
               upper = static_cast<std::size_t>(std::ceil(x));
    return values[lower] + (x - static_cast<double>(lower)) * (values[upper] - values[lower]);
}
class EndpointMoments {
  public:
    explicit EndpointMoments(const StabilityEndpoint &endpoint) : endpoint_(endpoint) {
        for (const auto &pair : endpoint.days) {
            if (pair) {
                require(std::isfinite(pair->candidate) && std::isfinite(pair->control),
                        "nonfinite endpoint input");
                ++count_;
                mean_a_ += pair->candidate;
                mean_b_ += pair->control;
            }
        }
        if (count_) {
            mean_a_ /= count_;
            mean_b_ /= count_;
            const auto first = std::find_if(endpoint.days.begin(), endpoint.days.end(),
                                            [](const auto &pair) { return pair.has_value(); });
            if (std::all_of(endpoint.days.begin(), endpoint.days.end(), [&](const auto &pair) {
                    return !pair || pair->candidate == (*first)->candidate;
                })) {
                mean_a_ = (*first)->candidate;
            }
            if (std::all_of(endpoint.days.begin(), endpoint.days.end(), [&](const auto &pair) {
                    return !pair || pair->control == (*first)->control;
                })) {
                mean_b_ = (*first)->control;
            }
        }
        prefix_.push_back({});
        for (const auto &pair : endpoint.days) {
            auto next = prefix_.back();
            if (pair) {
                const long double a = pair->candidate - mean_a_, b = pair->control - mean_b_;
                ++next[0];
                next[1] += a;
                next[2] += b;
                next[3] += a * a;
                next[4] += b * b;
            }
            prefix_.push_back(next);
        }
        estimate_ = statistic(prefix_.back());
    }
    [[nodiscard]] std::optional<double> statistic(const Moment &m) const {
        if (m[0] < (endpoint_.cer ? 2 : 1)) {
            return {};
        }
        long double result = mean_a_ - mean_b_ + (m[1] - m[2]) / m[0];
        if (endpoint_.cer) {
            const auto a = (m[3] - m[1] * m[1] / m[0]) / (m[0] - 1);
            const auto b = (m[4] - m[2] * m[2] / m[0]) / (m[0] - 1);
            if (a < 0 || b < 0) {
                return {};
            }
            result = 252 * (result - 1.5L * (a - b));
        }
        const double value = static_cast<double>(result);
        return std::isfinite(value) ? std::optional<double>{value} : std::nullopt;
    }
    [[nodiscard]] std::optional<double> standard_error(std::size_t block) const {
        if (!estimate_ || count_ < 2) {
            return {};
        }
        const auto total = endpoint_.days.size();
        std::vector<long double> influence(total);
        for (std::size_t i = 0; i < total; ++i) {
            if (!endpoint_.days[i]) {
                continue;
            }
            const auto a = endpoint_.days[i]->candidate - mean_a_,
                       b = endpoint_.days[i]->control - mean_b_;
            auto value = a - b;
            if (endpoint_.cer) {
                const auto factor = static_cast<long double>(count_) / (count_ - 1);
                value = 252 *
                        (value - 1.5L * factor *
                                     (a * a - b * b - (prefix_.back()[3] - prefix_.back()[4]) / count_));
            }
            influence[i] = value * static_cast<long double>(total) / count_;
        }
        long double variance = 0;
        for (auto value : influence) {
            variance += value * value;
        }
        for (std::size_t lag = 1; lag < block; ++lag) {
            long double cross = 0;
            for (std::size_t i = lag; i < total; ++i) {
                cross += influence[i] * influence[i - lag];
            }
            variance += 2 * (1 - static_cast<long double>(lag) / block) * cross;
        }
        variance /= static_cast<long double>(total) * total;
        if (!std::isfinite(variance) || variance <= 0) {
            return {};
        }
        return static_cast<double>(std::sqrt(variance));
    }
    [[nodiscard]] Moment block(std::size_t start, std::size_t length) const {
        Moment result{};
        for (std::size_t i = 0; i < result.size(); ++i) {
            result[i] = prefix_[start + length][i] - prefix_[start][i];
        }
        return result;
    }
    [[nodiscard]] std::size_t count() const { return count_; }
    [[nodiscard]] std::optional<double> estimate() const { return estimate_; }

  private:
    const StabilityEndpoint &endpoint_;
    std::size_t count_{};
    long double mean_a_{}, mean_b_{};
    std::vector<Moment> prefix_;
    std::optional<double> estimate_;
};

std::string band(const StabilityObservation &row) {
    if (!row.holding_minutes) {
        return "unavailable";
    }
    const double value = *row.holding_minutes;
    require(value >= 0, "holding duration must be nonnegative");
    if (value == 0) {
        return "cash";
    }
    for (int upper : {15, 30, 60, 120, 390}) {
        if (value <= upper) {
            return "up_to_" + std::to_string(upper);
        }
    }
    throw std::invalid_argument("holding duration exceeds a regular session");
}
Json describe(const StabilitySeries &series, const std::vector<std::size_t> &indices) {
    std::vector<double> values;
    double exposure = 0, turnover = 0, cost = 0, trades = 0;
    std::size_t active = 0, exposure_n = 0, turnover_n = 0, cost_n = 0, trade_n = 0;
    for (auto i : indices) {
        const auto &row = series.days[i];
        if (row.cost) {
            cost += *row.cost;
            ++cost_n;
        }
        if (row.turnover) {
            turnover += *row.turnover;
            ++turnover_n;
        }
        if (!row.net) {
            continue;
        }
        require(std::isfinite(*row.net) && *row.net > -1, "invalid observed strategy return");
        values.push_back(*row.net);
        active += row.exposure && *row.exposure > 0;
        if (row.exposure) {
            exposure += *row.exposure;
            ++exposure_n;
        }
        if (row.trades) {
            trades += *row.trades;
            ++trade_n;
        }
    }
    Json stats = nullptr;
    if (!values.empty()) {
        const auto v = describe_return_series(values);
        stats = {{"mean", v.mean},
                 {"selected_observation_product", v.compounded},
                 {"selected_observation_drawdown", v.maximum_drawdown},
                 {"sample_sd", optional(v.sample_standard_deviation)}};
    }
    return {
        {"selected_calendar_dates", indices.size()},
        {"observed_dates", values.size()},
        {"active_dates", active},
        {"statistics", stats},
        {"mean_exposure", exposure_n ? Json(exposure / static_cast<double>(exposure_n)) : Json(nullptr)},
        {"turnover_initial_equity_units", turnover_n ? Json(turnover) : Json(nullptr)},
        {"cost_initial_equity_units", cost_n ? Json(cost) : Json(nullptr)},
        {"trade_count_or_mean_component_count", trade_n ? Json(trades) : Json(nullptr)}};
}
Json removals(const StabilitySeries &series, const std::vector<std::size_t> &indices,
              const std::vector<std::string> &dates) {
    std::vector<std::size_t> positive;
    long double sum = 0, wins = 0;
    std::size_t valid = 0;
    for (auto i : indices) {
        if (!series.days[i].net) {
            continue;
        }
        const auto value = *series.days[i].net;
        sum += value;
        ++valid;
        if (value > 0) {
            wins += value;
            positive.push_back(i);
        }
    }
    std::sort(positive.begin(), positive.end(), [&](auto a, auto b) {
        const auto x = *series.days[a].net, y = *series.days[b].net;
        return x == y ? dates[a] < dates[b] : x > y;
    });
    Json rows = Json::array();
    for (int removal : {1, 5, 10, 0}) {
        const auto count = removal ? static_cast<std::size_t>(removal) : (positive.size() + 99) / 100;
        const auto n = std::min(count, positive.size());
        long double removed = 0;
        Json selected = Json::array();
        for (std::size_t i = 0; i < n; ++i) {
            removed += *series.days[positive[i]].net;
            selected.push_back(dates[positive[i]]);
        }
        rows.push_back(
            {{"rule", removal ? "top_" + std::to_string(removal) : "top_one_percent_positive_dates"},
             {"positive_dates", positive.size()},
             {"removed_dates", selected},
             {"removed_arithmetic_return", static_cast<double>(removed)},
             {"share_of_positive_sum",
              wins > 0 ? Json(static_cast<double>(removed / wins)) : Json(nullptr)},
             {"mean_after_zeroing", valid && (removal || !positive.empty())
                                        ? Json(static_cast<double>((sum - removed) / valid))
                                        : Json(nullptr)}});
    }
    return rows;
}
} // namespace

FullCalendarInference::FullCalendarInference(const ExtensionStudyContract &contract)
    : settings_(contract.document().at("inference")),
      definitions_(contract.document().at("primary_endpoints")) {}
Json FullCalendarInference::run(const std::vector<StabilityEndpoint> &endpoints,
                                std::size_t block) const {
    require(endpoints.size() == 6, "joint inference requires the complete six-endpoint family");
    const auto total = endpoints.front().days.size();
    require(total >= block && (block == 5 || block == 10 || block == 20 || block == 40),
            "unsupported inference calendar/block");
    std::set<std::string> ids;
    std::vector<EndpointMoments> moments;
    for (const auto &endpoint : endpoints) {
        const auto index = moments.size();
        require(endpoint.id == definitions_.at(index).at("id").get<std::string>() &&
                    endpoint.cer == (index == 4) &&
                    endpoint.material == definitions_.at(index).at("material_increment").get<double>(),
                "unregistered endpoint definition or family order");
        require(endpoint.days.size() == total && ids.insert(endpoint.id).second,
                "endpoint calendar or identities differ");
        moments.emplace_back(endpoint);
    }
    const auto replicates = settings_.at("resamples").get<std::size_t>();
    const auto seed = settings_.at("seed").get<std::uint64_t>();
    MovingBlockIndexSampler sampler{total, block, seed};
    Sha256 mapping;
    std::array<std::vector<double>, 6> draws;
    std::array<std::size_t, 6> undefined{};
    std::array<std::optional<double>, 6> errors;
    bool studentizers = true;
    for (std::size_t j = 0; j < 6; ++j) {
        errors[j] = moments[j].standard_error(block);
        studentizers = studentizers && errors[j].has_value();
    }
    std::vector<double> maxima;
    std::size_t undefined_family = 0;
    for (std::size_t replicate = 0; replicate < replicates; ++replicate) {
        std::array<Moment, 6> sums{};
        for (const auto &[start, length] : sampler.next_blocks()) {
            mapping.update(std::to_string(start) + ":" + std::to_string(length) + ";");
            for (std::size_t j = 0; j < 6; ++j) {
                const auto v = moments[j].block(start, length);
                for (std::size_t k = 0; k < 5; ++k) {
                    sums[j][k] += v[k];
                }
            }
        }
        mapping.update("\n");
        bool complete = studentizers;
        double maximum = -std::numeric_limits<double>::infinity();
        for (std::size_t j = 0; j < 6; ++j) {
            const auto value = moments[j].statistic(sums[j]);
            if (value) {
                draws[j].push_back(*value);
                if (errors[j] && moments[j].estimate()) {
                    maximum = std::max(maximum, (*value - *moments[j].estimate()) / *errors[j]);
                }
            } else {
                ++undefined[j];
                complete = false;
            }
        }
        if (complete && std::isfinite(maximum)) {
            maxima.push_back(maximum);
        } else {
            ++undefined_family;
        }
    }
    const double limit = settings_.at("undefined_resample_fraction_limit").get<double>();
    const bool simultaneous = block == 20 || block == 40;
    const bool available =
        simultaneous && studentizers &&
        static_cast<double>(undefined_family) / static_cast<double>(replicates) <= limit &&
        !maxima.empty();
    const std::optional<double> critical =
        available ? std::optional<double>{quantile(maxima, .95)} : std::nullopt;
    Json rows = Json::array();
    bool coverage = true;
    for (std::size_t j = 0; j < 6; ++j) {
        const auto n = moments[j].count();
        const bool adequate = n >= settings_.at("minimum_valid_dates").get<std::size_t>() &&
                              static_cast<double>(n) / static_cast<double>(total) >=
                                  settings_.at("minimum_calendar_coverage").get<double>();
        coverage = coverage && adequate;
        const bool marginal = (j < 4 && (block == 5 || block == 10)) || (j >= 4 && simultaneous);
        const bool interval =
            marginal && !draws[j].empty() &&
            static_cast<double>(undefined[j]) / static_cast<double>(replicates) <= limit;
        const auto lower = critical && errors[j] && moments[j].estimate()
                               ? std::optional<double>{*moments[j].estimate() - *critical * *errors[j]}
                               : std::nullopt;
        rows.push_back(
            {{"id", endpoints[j].id},
             {"observed_dates", n},
             {"calendar_dates", total},
             {"estimate", optional(moments[j].estimate())},
             {"hac_standard_error", optional(errors[j])},
             {"undefined_resamples", undefined[j]},
             {"marginal_requested", marginal},
             {"marginal_lower", interval ? Json(quantile(draws[j], .025)) : Json(nullptr)},
             {"marginal_upper", interval ? Json(quantile(draws[j], .975)) : Json(nullptr)},
             {"family_lower", optional(lower)},
             {"material_increment", endpoints[j].material},
             {"adequate_coverage", adequate},
             {"bound_above_material", lower ? Json(*lower > endpoints[j].material) : Json(nullptr)}});
    }
    return {{"block_length", block},
            {"replicates", replicates},
            {"seed", seed},
            {"sampling_sha256", mapping.finish()},
            {"endpoints", rows},
            {"family_requested", simultaneous},
            {"family_available", available},
            {"family_critical", optional(critical)},
            {"undefined_family_resamples", undefined_family},
            {"all_endpoints_adequate", coverage},
            {"advancement_assessed", false},
            {"assessment", !coverage ? "input_limited"
                           : !available
                               ? "family_unavailable_or_not_requested"
                               : "development_bounds_pending_risk_matching_and_inference_validation"}};
}

StabilityStudy::StabilityStudy(const ExtensionStudyContract &contract) : contract_(contract) {}
Json StabilityStudy::plan() const {
    return {{"protocol_sha256", contract_.identity()},
            {"stage", "M7_primary_diagnostics"},
            {"primary_cost_bps", 1},
            {"primary_delay_minutes", 1},
            {"inference", contract_.document().at("inference")},
            {"stability", contract_.document().at("stability")},
            {"condition", "sample_sd_of_60_consecutive_prior_dividend_aware_log_returns_annualized_252_"
                          "cutoffs_from_252_prior_calendar_slots_all_required"},
            {"condition_ties", "low_if_vol_at_or_below_lower_high_if_above_upper_otherwise_middle"},
            {"pooled_returns", "equal_weight_on_all_five_observed_same_dates_then_nonlinear_CER"},
            {"holding_bands",
             "cash_then_right_closed_0_15_30_60_120_390_total_minutes_invested_per_session"},
            {"concentration", "posthoc_zero_selected_positive_date_contributions_preserve_valid_count_"
                              "not_a_replayed_funded_ledger"},
            {"descriptive_groups",
             "selected_observation_products_and_drawdowns_no_group_significance_or_trading_filters"},
            {"secondary_variants_executed", false},
            {"risk_matching_executed", false},
            {"inference_calibration_validated", false},
            {"reserved_strategy_inputs_opened", false}};
}
Json StabilityStudy::run(const StabilityInputs &inputs, const Progress &progress) const {
    require(!inputs.dates.empty() && inputs.series.size() == 77,
            "primary diagnostic matrix must contain 77 series");
    Json groups = Json::array();
    std::set<std::string> identities;
    for (const auto &series : inputs.series) {
        require(series.days.size() == inputs.dates.size() && identities.insert(series.id).second,
                "diagnostic series calendar or identity differs");
        std::map<std::pair<std::string, std::string>, std::vector<std::size_t>> selections;
        for (std::size_t i = 0; i < inputs.dates.size(); ++i) {
            selections[{"period", "all"}].push_back(i);
            selections[{"year", inputs.dates[i].substr(0, 4)}].push_back(i);
            selections[{"condition", series.days[i].condition}].push_back(i);
            if (series.layer == "intraday") {
                selections[{"holding", band(series.days[i])}].push_back(i);
            }
        }
        for (const auto &[key, indices] : selections) {
            auto row = describe(series, indices);
            row.update({{"series", series.id},
                        {"dimension", key.first},
                        {"group", key.second},
                        {"concentration", removals(series, indices, inputs.dates)}});
            groups.push_back(std::move(row));
        }
    }
    Json inference = Json::array();
    for (std::size_t block : {5U, 10U, 20U, 40U}) {
        if (progress) {
            progress("Paired full-calendar uncertainty | block " + std::to_string(block));
        }
        inference.push_back(FullCalendarInference{contract_}.run(inputs.endpoints, block));
    }
    return {{"groups", groups},
            {"inference", inference},
            {"series_count", inputs.series.size()},
            {"calendar_dates", inputs.dates.size()},
            {"primary_endpoint_count", 6},
            {"group_count", groups.size()},
            {"secondary_variants_executed", false},
            {"risk_matching_executed", false},
            {"inference_calibration_validated", false}};
}
Json StabilityStudy::numerical_checks() const {
    std::vector<StabilityEndpoint> endpoints;
    const auto &registered = contract_.document().at("primary_endpoints");
    for (std::size_t j = 0; j < 6; ++j) {
        StabilityEndpoint endpoint{registered[j].at("id").get<std::string>(),
                                   j == 4,
                                   registered[j].at("material_increment").get<double>(),
                                   {}};
        for (std::size_t i = 0; i < 360; ++i) {
            const double a =
                .0003 + .001 * std::sin(.17 * static_cast<double>(i) + static_cast<double>(j));
            const double b = .0001 + .0008 * std::sin(.17 * static_cast<double>(i) + .3);
            endpoint.days.push_back((j == 5 && i % 17 == 0) ? std::nullopt
                                                            : std::optional<StabilityPair>{{a, b}});
        }
        endpoints.push_back(std::move(endpoint));
    }
    Json output{{"input", Json::array()}, {"results", Json::array()}};
    for (const auto &endpoint : endpoints) {
        output["input"].push_back(stability_endpoint_json(endpoint));
    }
    for (std::size_t block : {5U, 10U, 20U, 40U}) {
        output["results"].push_back(FullCalendarInference{contract_}.run(endpoints, block));
    }
    auto constant = endpoints;
    for (auto &endpoint : constant) {
        for (auto &pair : endpoint.days) {
            pair = StabilityPair{0, 0};
        }
    }
    const auto zero = FullCalendarInference{contract_}.run(constant, 40);
    require(!zero.at("family_available").get<bool>() && zero.at("family_critical").is_null(),
            "zero studentizers must disable the family");
    output["zero_studentizer_result"] = zero;
    for (auto &endpoint : constant) {
        for (auto &pair : endpoint.days) {
            pair = StabilityPair{.003, .001};
        }
    }
    const auto constant_increment = FullCalendarInference{contract_}.run(constant, 40);
    require(!constant_increment.at("family_available").get<bool>(),
            "constant nonzero increments must not acquire artificial precision");
    output["constant_increment_result"] = constant_increment;
    Json rejected = Json::array();
    const auto rejects = [&](const char *name, auto invalid) {
        bool caught = false;
        try {
            static_cast<void>(FullCalendarInference{contract_}.run(invalid, 40));
        } catch (const std::invalid_argument &) {
            caught = true;
        }
        require(caught, "malformed inference family was accepted");
        rejected.push_back(name);
    };
    auto invalid = endpoints;
    invalid.pop_back();
    rejects("incomplete_family", invalid);
    invalid = endpoints;
    std::swap(invalid[0], invalid[1]);
    rejects("reordered_family", invalid);
    invalid = endpoints;
    invalid[0].material = 0;
    rejects("changed_material_increment", invalid);
    invalid = endpoints;
    invalid[0].days.pop_back();
    rejects("unequal_calendars", invalid);
    output["rejected_inputs"] = rejected;
    return output;
}
Json stability_observation_json(const StabilityObservation &row) {
    return {{"net", optional(row.net)},           {"gross", optional(row.gross)},
            {"exposure", optional(row.exposure)}, {"turnover", optional(row.turnover)},
            {"cost", optional(row.cost)},         {"holding_minutes", optional(row.holding_minutes)},
            {"trades", optional(row.trades)},     {"condition", row.condition}};
}
Json stability_endpoint_json(const StabilityEndpoint &endpoint) {
    Json days = Json::array();
    for (const auto &pair : endpoint.days) {
        days.push_back(pair ? Json::array({pair->candidate, pair->control}) : Json(nullptr));
    }
    return {{"id", endpoint.id}, {"cer", endpoint.cer}, {"material", endpoint.material}, {"days", days}};
}
} // namespace trading
