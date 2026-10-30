#include "inference_calibration.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include "return_statistics.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace trading {
namespace {
using Json = nlohmann::json;
void require(bool v, const char *m) {
    if (!v)
        throw std::invalid_argument(m);
}
} // namespace
ValidationProtocol ValidationProtocol::load(const std::filesystem::path &path) {
    const auto raw = read_text_file(path, 64U * 1024);
    const auto id = sha256(raw);
    require(id == TRADING_VALIDATION_PROTOCOL_SHA256,
            "validation protocol differs from build-pinned registration");
    auto d = parse_json_document(raw);
    require(d.at("kind").get<std::string>() == "registered_panel_validation" &&
                d.at("schema_version").get<int>() == 1 &&
                d.at("parent_protocol_sha256").get<std::string>() == TRADING_EXTENSION_PROTOCOL_SHA256,
            "validation parent/schema differs");
    return {std::move(d), id};
}
std::vector<InferenceCalibrationCase> InferenceCalibration::cases() const {
    std::vector<InferenceCalibrationCase> result;
    const auto &c = protocol_.document().at("calibration");
    for (const auto &s : c.at("daily_noise_basis_points"))
        for (const auto &p : c.at("ar1"))
            result.push_back(
                {"noise_" + s.dump() + "_phi_" + p.dump(), s.get<double>(), p.get<double>(), false});
    const auto &m = c.at("additional_mask_case");
    result.push_back({"fixed_exogenous_mask", m.at("daily_noise_basis_points").get<double>(),
                      m.at("ar1").get<double>(), true});
    return result;
}
std::vector<StabilityEndpoint> InferenceCalibration::sample(const InferenceCalibrationCase &scenario,
                                                            std::size_t trial) const {
    const auto all = cases();
    const auto it =
        std::find_if(all.begin(), all.end(), [&](const auto &c) { return c.id == scenario.id; });
    require(it != all.end() && it->noise_basis_points == scenario.noise_basis_points &&
                it->ar1 == scenario.ar1 && it->missing == scenario.missing,
            "unregistered calibration case");
    const auto &c = protocol_.document().at("calibration");
    require(trial < c.at("outer_trials").get<std::size_t>(), "trial out of range");
    ControlledNormalGenerator generator{
        c.at("generation_seed").get<std::uint64_t>() +
        static_cast<std::uint64_t>(it - all.begin()) * c.at("case_seed_stride").get<std::uint64_t>() +
        static_cast<std::uint64_t>(trial) * c.at("trial_seed_stride").get<std::uint64_t>()};
    std::array<double, 8> state{};
    for (auto &x : state)
        x = generator.next();
    const double sigma = scenario.noise_basis_points / 10000,
                 innovation = std::sqrt(1 - scenario.ar1 * scenario.ar1), common = std::sqrt(.5),
                 individual = std::sqrt(.5);
    std::set<std::size_t> missing;
    if (scenario.missing)
        for (const auto &i : c.at("additional_mask_case").at("missing_indices"))
            missing.insert(i.get<std::size_t>());
    std::vector<StabilityEndpoint> result;
    for (std::size_t j = 0; j < 6; ++j) {
        const auto &e = contract_.document().at("primary_endpoints").at(j);
        result.push_back(
            {e.at("id").get<std::string>(), j == 4, e.at("material_increment").get<double>(), {}});
    }
    for (std::size_t t = 0; t < 252; ++t) {
        if (t)
            for (auto &x : state)
                x = scenario.ar1 * x + innovation * generator.next();
        const double b = .5 * sigma * state[7];
        for (std::size_t j = 0; j < 6; ++j) {
            const double z = common * state[0] + individual * state[j + 1],
                         mu = j == 4 ? result[j].material / 252 + 1.5 * sigma * sigma
                                     : result[j].material,
                         a = j == 5 ? mu + 20 * sigma * z : b + mu + sigma * z;
            result[j].days.push_back(
                missing.contains(t) ? std::nullopt : std::optional<StabilityPair>{{a, j == 5 ? 0 : b}});
        }
    }
    return result;
}
std::array<double, 6> InferenceCalibration::alternative_shifts(const InferenceCalibrationCase &s) const {
    const auto &c = protocol_.document().at("calibration");
    long double sum = 252;
    for (std::size_t lag = 1; lag < 252; ++lag)
        sum += 2 * static_cast<long double>(252 - lag) * std::pow(s.ar1, static_cast<double>(lag));
    const double se = s.noise_basis_points / 10000 * static_cast<double>(std::sqrt(sum)) / 252,
                 shift = (c.at("planning_critical").get<double>() +
                          c.at("planning_power_quantile").get<double>()) *
                         se;
    std::array<double, 6> out{};
    out.fill(shift);
    out[4] *= 252;
    out[5] *= 20;
    return out;
}
Json binomial_diagnostic(std::size_t events, std::size_t trials, double probability, double z) {
    require(trials > 0 && events <= trials && probability > 0 && probability < 1 && std::isfinite(z) &&
                z > 0,
            "invalid binomial diagnostic");
    const double n = static_cast<double>(trials), p = static_cast<double>(events) / n,
                 den = 1 + z * z / n, center = (p + z * z / (2 * n)) / den,
                 half = z * std::sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / den;
    long double tail = events == 0 ? 1 : 0;
    if (events) {
        const long double k = static_cast<long double>(events), nn = static_cast<long double>(trials),
                          q = probability;
        long double term = std::exp(std::lgamma(nn + 1) - std::lgamma(k + 1) - std::lgamma(nn - k + 1) +
                                    k * std::log(q) + (nn - k) * std::log1p(-q));
        tail = term;
        for (std::size_t j = events; j < trials; ++j) {
            term *= static_cast<long double>(trials - j) / static_cast<long double>(j + 1) * q / (1 - q);
            tail += term;
        }
    }
    return {{"events", events},
            {"trials", trials},
            {"frequency", p},
            {"mc_standard_error", std::sqrt(p * (1 - p) / n)},
            {"wilson_lower", std::max(0., center - half)},
            {"wilson_upper", std::min(1., center + half)},
            {"size_excess_binomial_p", static_cast<double>(std::min(1.L, tail))}};
}
Json InferenceCalibration::trial_record(const InferenceCalibrationCase &scenario, std::size_t trial,
                                        const Json &r) const {
    const auto shifts = alternative_shifts(scenario);
    bool any = false;
    Json endpoints = Json::array();
    for (std::size_t j = 0; j < 6; ++j) {
        const auto &e = r.at("endpoints").at(j);
        const bool available = !e.at("family_lower").is_null(),
                   rejected = available && e.at("family_lower").get<double>() >
                                               e.at("material_increment").get<double>();
        any |= rejected;
        endpoints.push_back(
            {{"id", e.at("id")},
             {"estimate", e.at("estimate")},
             {"hac_standard_error", e.at("hac_standard_error")},
             {"family_lower", e.at("family_lower")},
             {"true_boundary", e.at("material_increment")},
             {"alternative_shift", shifts[j]},
             {"boundary_exceeded", rejected},
             {"alternative_detected", available && e.at("family_lower").get<double>() + shifts[j] >
                                                       e.at("material_increment").get<double>()}});
    }
    return {{"case", scenario.id},
            {"trial", trial},
            {"block_length", r.at("block_length")},
            {"family_available", r.at("family_available")},
            {"all_endpoints_adequate", r.at("all_endpoints_adequate")},
            {"sampling_sha256", r.at("sampling_sha256")},
            {"family_critical", r.at("family_critical")},
            {"family_boundary_exceeded", any},
            {"endpoints", endpoints}};
}
Json InferenceCalibration::aggregate(const InferenceCalibrationCase &scenario, std::size_t block,
                                     const std::vector<Json> &records) const {
    require(!records.empty(), "missing calibration trials");
    std::size_t events = 0, available = 0;
    std::array<std::size_t, 6> rejected{}, power{};
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto &r = records[i];
        require(r.at("case").get<std::string>() == scenario.id &&
                    r.at("trial").get<std::size_t>() == i &&
                    r.at("block_length").get<std::size_t>() == block,
                "trial matrix differs");
        events += r.at("family_boundary_exceeded").get<bool>();
        available += r.at("family_available").get<bool>();
        for (std::size_t j = 0; j < 6; ++j) {
            rejected[j] += r.at("endpoints").at(j).at("boundary_exceeded").get<bool>();
            power[j] += r.at("endpoints").at(j).at("alternative_detected").get<bool>();
        }
    }
    const double z = protocol_.document().at("calibration").at("mc_z").get<double>();
    const auto size = binomial_diagnostic(events, records.size(), .05, z);
    Json endpoints = Json::array();
    for (std::size_t j = 0; j < 6; ++j)
        endpoints.push_back(
            {{"id", records.front().at("endpoints").at(j).at("id")},
             {"boundary_rejection", binomial_diagnostic(rejected[j], records.size(), .05, z)},
             {"alternative_detection", binomial_diagnostic(power[j], records.size(), .8, z)}});
    return {{"case", scenario.id},
            {"daily_noise_basis_points", scenario.noise_basis_points},
            {"ar1", scenario.ar1},
            {"fixed_mask", scenario.missing},
            {"block_length", block},
            {"available_trials", available},
            {"trials", records.size()},
            {"family_boundary_exceedance", size},
            {"size_excess_adjusted_alpha", .05 / 20},
            {"size_excess_detected", size.at("size_excess_binomial_p").get<double>() <= .05 / 20},
            {"endpoint_diagnostics", endpoints},
            {"nominal_market_coverage_certified", false}};
}
Json InferenceCalibration::numerical_checks() const {
    const auto scenario = cases().at(4);
    const auto input = sample(scenario, 0);
    const InferenceDrawSchedule schedule{252, 40, 10000, 290026};
    const FullCalendarInference inference{contract_};
    const auto original = inference.run(input, 40);
    require(original == inference.run(input, 40, &schedule), "cached schedule changed inference");
    auto moved = input;
    const auto shifts = alternative_shifts(scenario);
    for (std::size_t j = 0; j < 6; ++j)
        for (auto &p : moved[j].days)
            if (p)
                p->candidate += shifts[j] / (j == 4 ? 252 : 1);
    const auto result = inference.run(moved, 40, &schedule);
    for (std::size_t j = 0; j < 6; ++j)
        require(std::abs(result.at("endpoints").at(j).at("family_lower").get<double>() -
                         original.at("endpoints").at(j).at("family_lower").get<double>() - shifts[j]) <
                    1e-11,
                "location equivariance differs");
    auto zero = input;
    for (auto &e : zero)
        for (auto &p : e.days)
            p = StabilityPair{0, 0};
    require(!inference.run(zero, 40, &schedule).at("family_available").get<bool>(),
            "zero studentizer was removed from family");
    auto sparse = input;
    for (auto &e : sparse)
        for (std::size_t i = 0; i < 100; ++i)
            e.days[i].reset();
    require(!inference.run(sparse, 40, &schedule).at("all_endpoints_adequate").get<bool>(),
            "inadequate calendar relabelled adequate");
    bool rejected = false;
    try {
        static_cast<void>(inference.run(input, 20, &schedule));
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    require(rejected, "wrong schedule accepted");
    return {{"cached_matches_streamed", true},
            {"location_equivariance", true},
            {"zero_family_retained", true},
            {"inadequate_calendar_retained", true},
            {"wrong_schedule_rejected", true},
            {"binomial_zero", binomial_diagnostic(0, 1000, .05, 1.959963984540054)},
            {"binomial_fifty", binomial_diagnostic(50, 1000, .05, 1.959963984540054)}};
}
} // namespace trading
