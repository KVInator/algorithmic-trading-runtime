#include "stability_dataset.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include "panel_replay_dataset.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace trading {
namespace {
using Json = nlohmann::json;
void require(bool value, const char *reason) {
    if (!value) {
        throw std::invalid_argument(reason);
    }
}
constexpr std::uintmax_t file_bound = 8ULL * 1024 * 1024 * 1024;
constexpr std::size_t record_bound = 16U * 1024 * 1024;
const std::vector<std::string> policies{"momentum",
                                        "momentum_always_long",
                                        "continuation_price",
                                        "continuation_volume",
                                        "reversion_candidate",
                                        "reversion_sign_only",
                                        "reversion_drift",
                                        "reversion_always_long"};
const std::vector<std::string> risk_names{"intraday_risk", "daily_only", "static_risk_budget",
                                          "passive_equity"};
const std::vector<std::string> shared_names{"candidate", "momentum_control", "continuation_control",
                                            "reversion_sign_control", "reversion_drift_control"};
struct Source {
    std::filesystem::path directory;
    Json summary;
    std::string hash;
    std::map<std::string, Json> artifacts;
    explicit Source(std::filesystem::path path) : directory(std::move(path)) {
        const auto raw = read_text_file(directory / "summary.json", 32U * 1024 * 1024);
        hash = sha256(raw);
        summary = parse_json_document(raw);
        for (const auto &item : summary.at("artifacts")) {
            const auto name = item.at("path").get<std::string>();
            require(artifacts.emplace(name, item).second, "duplicate M6 artifact");
            static_cast<void>(contained_path(directory, name));
        }
        require(!summary.at("reserved_strategy_inputs_opened").get<bool>() &&
                    summary.at("network_requests").get<int>() == 0,
                "M6 source scope differs");
        require(sha256_file(directory / "report.html", 32U * 1024 * 1024) ==
                    summary.at("report_sha256").get<std::string>(),
                "M6 report identity changed");
    }
    void records(const std::string &name, const std::function<void(const Json &)> &consume) const {
        const auto &artifact = artifacts.at(name);
        const auto path = contained_path(directory, name);
        require(std::filesystem::file_size(path) == artifact.at("bytes").get<std::uintmax_t>() &&
                    std::filesystem::file_size(path) <= file_bound,
                "M6 file size differs or exceeds bound");
        std::ifstream stream{path, std::ios::binary};
        require(stream.is_open(), "M6 stream unavailable");
        Sha256 hash_state;
        std::array<char, 65536> buffer{};
        std::string pending;
        while (stream) {
            stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = static_cast<std::size_t>(stream.gcount());
            hash_state.update(std::string_view{buffer.data(), count});
            std::size_t begin = 0;
            for (std::size_t i = 0; i < count; ++i) {
                if (buffer[i] != '\n') {
                    continue;
                }
                require(pending.size() + i - begin <= record_bound, "M6 record exceeds bound");
                pending.append(buffer.data() + begin, i - begin);
                require(!pending.empty(), "empty M6 record");
                consume(parse_json_document(pending));
                pending.clear();
                begin = i + 1;
            }
            require(pending.size() + count - begin <= record_bound, "M6 record exceeds bound");
            pending.append(buffer.data() + begin, count - begin);
        }
        require(stream.eof() && pending.empty(), "M6 stream is truncated or unreadable");
        require(hash_state.finish() == artifact.at("sha256").get<std::string>(),
                "M6 stream hash changed");
    }
    Json identity() const {
        return {{"directory", directory.generic_string()},
                {"summary_sha256", hash},
                {"artifacts", summary.at("artifacts")}};
    }
};
std::optional<double> number(const Json &value) {
    if (value.is_null()) {
        return {};
    }
    const auto result = value.get<double>();
    require(std::isfinite(result), "nonfinite saved observation");
    return result;
}
} // namespace
StabilityDataset::StabilityDataset(std::filesystem::path cost_directory,
                                   std::filesystem::path account_directory,
                                   ExtensionStudyContract contract)
    : costs_(std::move(cost_directory)), accounts_(std::move(account_directory)),
      contract_(std::move(contract)) {}
std::map<std::string, StabilityObservation> StabilityDataset::intraday_observations(const Json &record) {
    const auto &primary = record.at("primary");
    const auto date = primary.at("date").get<std::string>();
    require(date >= "2016-01-01" && date < "2024-01-01", "M7 intraday input is outside development");
    const auto sessions = extension_calendar(CalendarRange{date, date});
    require(sessions.size() == 1, "M7 intraday input is not an exchange session");
    std::map<std::string, StabilityObservation> observations;
    for (const auto &policy : policies) {
        observations.emplace(policy, StabilityObservation{});
    }
    if (!sessions.front().regular()) {
        require(primary.at("policies").empty() && record.at("paths").empty() &&
                    !primary.at("common_operational").get<bool>() &&
                    !primary.at("availability").at("operational").get<bool>(),
                "M6 shortened session contains intraday paths or an operational outcome");
        return observations;
    }
    std::set<std::string> seen_policies;
    for (const auto &path : record.at("paths")) {
        if (path.at("delay_minutes").get<int>() != 1) {
            continue;
        }
        const auto policy = path.at("policy").get<std::string>();
        require(std::find(policies.begin(), policies.end(), policy) != policies.end() &&
                    seen_policies.insert(policy).second,
                "M6 primary policy matrix differs");
        const bool valid = path.at("outcome_available").get<bool>();
        require(valid ==
                    (primary.at("common_operational").get<bool>() && path.at("resolved").get<bool>()),
                "M6 availability differs");
        auto &observation = observations.at(policy);
        std::size_t found = 0;
        for (const auto &cost : path.at("costs")) {
            if (cost.at("cost_bps").get<double>() != 1) {
                continue;
            }
            ++found;
            observation.net = number(cost.at("net"));
            observation.gross = number(cost.at("gross"));
            observation.turnover = number(cost.at("traded_notional_per_initial_equity"));
            observation.cost = number(cost.at("cost_per_initial_equity"));
        }
        require(found == 1 && observation.net.has_value() == valid,
                "M6 primary charge or outcome differs");
        if (valid) {
            observation.holding_minutes = path.at("minutes_held").get<double>();
            observation.exposure = *observation.holding_minutes / 390;
            observation.trades = static_cast<double>(path.at("completed_trades").size());
        }
    }
    require(seen_policies.size() == 8, "M6 primary policies incomplete");
    return observations;
}
StabilityInputs StabilityDataset::load(const StabilityStudy::Progress &progress) const {
    const Source costs{costs_}, accounts{accounts_};
    require(costs.summary.at("kind").get<std::string>() == "cost_delay_comparison",
            "unexpected cost/delay source kind");
    require(accounts.summary.at("kind").get<std::string>() == "panel_account_study",
            "unexpected account source kind");
    require(costs.summary.at("policy_sha256").get<std::string>() ==
                    TRADING_COST_EXECUTION_PROTOCOL_SHA256 &&
                accounts.summary.at("policy_sha256").get<std::string>() ==
                    TRADING_COST_EXECUTION_PROTOCOL_SHA256 &&
                costs.summary.at("primary_reference_parity_passed").get<bool>(),
            "M6 policy or reference parity differs");
    require(costs.artifacts.size() == 5 && accounts.artifacts.size() == 7,
            "M6 artifact inventory differs");
    for (const auto *source : {&costs, &accounts}) {
        require(source->summary.at("provenance").at("protocol_sha256").get<std::string>() ==
                    contract_.identity(),
                "M6 extension protocol differs");
    }
    const auto origin = accounts.summary.at("provenance").at("evidence_origin").get<std::string>();
    require(origin == costs.summary.at("provenance").at("evidence_origin").get<std::string>() &&
                (origin == "provider" || origin == "synthetic"),
            "M6 source origins differ");
    if (origin == "provider") {
        for (const auto *key :
             {"study_manifest_sha256", "acquisition_manifest_sha256", "eligibility_masks_sha256"}) {
            require(costs.summary.at("provenance").at(key) == accounts.summary.at("provenance").at(key),
                    "M6 registered sources differ");
        }
        require(accounts.summary.at("provenance").at("study_manifest_sha256").get<std::string>() ==
                    registered_panel_study_sha256,
                "unregistered panel source");
    } else {
        require(
            accounts.summary.at("provenance").at("fixture").get<std::string>() == "analytic_panel_300" &&
                costs.summary.at("provenance").at("fixture").get<std::string>() == "analytic_panel_300",
            "unknown synthetic fixture");
    }
    StabilityInputs output;
    output.synthetic = origin == "synthetic";
    output.dates = accounts.summary.at("development_dates").get<std::vector<std::string>>();
    require(!output.dates.empty() && std::is_sorted(output.dates.begin(), output.dates.end()) &&
                std::adjacent_find(output.dates.begin(), output.dates.end()) == output.dates.end(),
            "development calendar differs");
    std::map<std::string, std::size_t> dates;
    for (std::size_t i = 0; i < output.dates.size(); ++i) {
        contract_.require_development_access(output.dates[i]);
        require(output.dates[i] > "2016-12-31", "initialization was scored");
        dates.emplace(output.dates[i], i);
    }
    std::map<std::string, StabilitySeries> series;
    const auto add = [&](const std::string &layer, const std::string &symbol,
                         const std::string &policy) -> StabilitySeries & {
        const auto id = layer + "/" + symbol + "/" + policy;
        return series
            .try_emplace(id, StabilitySeries{id, layer, symbol, policy,
                                             std::vector<StabilityObservation>(output.dates.size())})
            .first->second;
    };
    std::map<std::string, std::map<std::string, std::string>> primary_hashes;
    std::vector<std::string> calendar;
    std::map<std::string, std::vector<std::optional<double>>> market_returns;
    for (const auto &symbol : contract_.symbols()) {
        for (const auto &policy : policies) {
            static_cast<void>(add("intraday", symbol, policy));
        }
        std::vector<std::string> seen_dates;
        auto &market = market_returns[symbol];
        costs.records(symbol + ".jsonl", [&](const Json &row) {
            const auto &primary = row.at("primary");
            const auto date = primary.at("date").get<std::string>();
            contract_.require_development_access(date);
            require(primary.at("symbol").get<std::string>() == symbol, "M6 instrument differs");
            require(seen_dates.empty() || date > seen_dates.back(), "M6 source dates unordered");
            seen_dates.push_back(date);
            primary_hashes[symbol].emplace(date, sha256(primary.dump()));
            market.push_back(primary.at("measurement").is_null()
                                 ? std::nullopt
                                 : number(primary.at("measurement").at("log_return")));
            if (date <= "2016-12-31") {
                return;
            }
            const auto index = dates.at(date);
            for (auto &[policy, observation] : intraday_observations(row)) {
                series.at("intraday/" + symbol + "/" + policy).days[index] = std::move(observation);
            }
        });
        if (calendar.empty()) {
            calendar = seen_dates;
        } else {
            require(calendar == seen_dates, "M6 five-instrument calendars differ");
        }
        std::size_t count = 0;
        accounts.records(symbol + ".jsonl", [&](const Json &primary) {
            require(count < calendar.size() && primary.at("date").get<std::string>() == calendar[count],
                    "M6 account primary calendar differs");
            require(sha256(primary.dump()) == primary_hashes.at(symbol).at(calendar[count]),
                    "M6 causal primary records differ");
            ++count;
        });
        require(count == calendar.size(), "M6 account primary stream truncated");
        if (progress) {
            progress("Authenticated saved M6 inputs | " + symbol);
        }
    }
    require(calendar.size() == accounts.summary.at("calendar_dates").get<std::size_t>(),
            "M6 calendar count differs");
    std::vector<CalendarSession> registered_calendar;
    for (int year = 2016; year <= 2023; ++year) {
        for (unsigned month = 1; month <= 12; ++month) {
            const auto end = std::chrono::year_month_day{std::chrono::year{year} /
                                                         std::chrono::month{month} / std::chrono::last};
            const auto id = std::to_string(year) + (month < 10 ? "-0" : "-") + std::to_string(month);
            const auto days = extension_calendar(
                CalendarRange{id + "-01", id + '-' + std::to_string(static_cast<unsigned>(end.day()))});
            registered_calendar.insert(registered_calendar.end(), days.begin(), days.end());
        }
    }
    const auto expected_count = output.synthetic ? std::size_t{300} : registered_calendar.size();
    require(calendar.size() == expected_count, "M6 source calendar has an unregistered length");
    for (std::size_t i = 0; i < calendar.size(); ++i) {
        require(calendar[i] == registered_calendar[i].date(),
                "M6 source calendar differs from exchange sessions");
    }
    std::vector<std::string> scored;
    for (const auto &date : calendar) {
        if (date > "2016-12-31") {
            scored.push_back(date);
        }
    }
    require(scored == output.dates, "development calendar is not the source calendar suffix");
    std::map<std::string, std::vector<std::optional<double>>> losses;
    for (const auto &symbol : contract_.symbols()) {
        losses[symbol].resize(output.dates.size());
        for (const auto &policy : risk_names) {
            static_cast<void>(add("risk", symbol, policy));
        }
    }
    for (const auto &policy : shared_names) {
        static_cast<void>(add("shared", "all", policy));
    }
    std::size_t account_dates = 0;
    std::set<std::pair<std::string, std::string>> forecast_keys;
    accounts.records("account_days.jsonl", [&](const Json &row) {
        require(account_dates < calendar.size() &&
                    row.at("date").get<std::string>() == calendar[account_dates],
                "M6 account day calendar differs");
        ++account_dates;
        const auto date = row.at("date").get<std::string>();
        for (const auto &forecast : row.at("forecasts")) {
            if (forecast.at("delay").get<int>() == 1) {
                const auto d = forecast.at("date").get<std::string>(),
                           symbol = forecast.at("symbol").get<std::string>();
                require(dates.contains(d) && forecast_keys.emplace(symbol, d).second,
                        "M6 forecast identity differs");
                if (forecast.at("next_date").is_null()) {
                    require(date == output.dates.back() && d == date &&
                                forecast.at("control_minus_candidate_qlike").is_null(),
                            "M6 terminal forecast horizon differs");
                } else {
                    require(account_dates >= 2 && d == calendar[account_dates - 2] &&
                                forecast.at("next_date").get<std::string>() == date,
                            "M6 forecast is not assigned to its prior decision date");
                }
                losses.at(symbol)[dates.at(d)] = number(forecast.at("control_minus_candidate_qlike"));
            }
        }
        if (date <= "2016-12-31") {
            require(row.at("risk").empty() && row.at("shared").empty(),
                    "initialization contains economic accounts");
            return;
        }
        const auto index = dates.at(date);
        std::set<std::string> seen;
        for (const auto &account : row.at("risk")) {
            if (account.at("delay").get<int>() != 1 || account.at("cost_bps").get<double>() != 1) {
                continue;
            }
            const auto id = "risk/" + account.at("symbol").get<std::string>() + "/" +
                            account.at("account").get<std::string>();
            require(seen.insert(id).second, "duplicate primary risk account");
            auto &observation = series.at(id).days[index];
            observation.net = number(account.at("net_return"));
            observation.cost = 0;
            observation.turnover = 0;
            for (const auto &event : account.at("events")) {
                *observation.cost += event.at("cost").get<double>() / 100000;
                *observation.turnover += std::abs(event.at("notional").get<double>()) / 100000;
            }
            if (observation.net) {
                const auto &value = account.at("value");
                observation.exposure = value.at("shares").get<double>() *
                                       account.at("last_price").get<double>() /
                                       value.at("liquid").get<double>();
            }
        }
        require(seen.size() == 20, "M6 risk primary matrix differs");
        seen.clear();
        for (const auto &account : row.at("shared")) {
            if (account.at("delay").get<int>() != 1 || account.at("cost_bps").get<double>() != 1) {
                continue;
            }
            const auto id = "shared/all/" + account.at("variant").get<std::string>();
            require(seen.insert(id).second, "duplicate shared primary account");
            auto &observation = series.at(id).days[index];
            observation.net = number(account.at("net_return"));
            observation.turnover = account.at("traded_mid_notional_units").get<double>() / 1e14;
            observation.cost = account.at("crossing_cost_units").get<double>() / 1e14;
            if (observation.net) {
                observation.exposure = account.at("maximum_gross_fraction").get<double>();
            }
        }
        require(seen.size() == 5, "M6 shared primary matrix differs");
        if (progress && account_dates % 250 == 0) {
            progress("Authenticated account ledger dates | " + std::to_string(account_dates));
        }
    });
    require(account_dates == calendar.size(), "M6 account calendar truncated");
    require(forecast_keys.size() == output.dates.size() * 5, "M6 primary forecasts incomplete");
    const auto plan_file = accounts.artifacts.at("plan.json");
    const auto raw_plan = read_text_file(accounts_ / "plan.json", 16U * 1024 * 1024);
    const auto account_plan = parse_json_document(raw_plan);
    require(raw_plan.size() == plan_file.at("bytes").get<std::size_t>() &&
                sha256(raw_plan) == plan_file.at("sha256").get<std::string>() &&
                account_plan == accounts.summary.at("plan") &&
                sha256(account_plan.dump()) == accounts.summary.at("plan_sha256").get<std::string>() &&
                account_plan.at("protocol_sha256").get<std::string>() == contract_.identity(),
            "M6 plan changed");
    output.market = {
        {"dates", calendar}, {"log_returns", Json::object()}, {"conditions", Json::object()}};
    for (const auto &[symbol, values] : market_returns) {
        auto rows = Json::array();
        for (auto value : values) {
            rows.push_back(value ? Json(*value) : Json(nullptr));
        }
        output.market["log_returns"][symbol] = rows;
    }
    std::vector<std::optional<double>> pooled(calendar.size());
    for (std::size_t i = 0; i < calendar.size(); ++i) {
        bool complete = true;
        double sum = 0;
        for (const auto &symbol : contract_.symbols()) {
            if (market_returns[symbol][i]) {
                sum += *market_returns[symbol][i] / 5;
            } else {
                complete = false;
            }
        }
        if (complete) {
            pooled[i] = sum;
        }
    }
    market_returns["equal_weight"] = pooled;
    for (const auto &[symbol, values] : market_returns) {
        PriorMarketCondition state;
        auto rows = Json::array();
        for (std::size_t i = 0; i < calendar.size(); ++i) {
            const auto condition = state.advance(calendar[i], values[i]);
            rows.push_back(condition);
            if (dates.contains(calendar[i])) {
                for (auto &[id, s] : series) {
                    static_cast<void>(id);
                    if (s.symbol == symbol || (s.layer == "shared" && symbol == "equal_weight")) {
                        s.days[dates.at(calendar[i])].condition =
                            condition.at("label").get<std::string>();
                    }
                }
            }
        }
        output.market["conditions"][symbol] = rows;
    }
    for (const auto &layer : std::vector<std::string>{"intraday", "risk"}) {
        for (const auto &policy : layer == "intraday" ? policies : risk_names) {
            auto &target = add(layer, "equal_weight", policy);
            for (std::size_t i = 0; i < output.dates.size(); ++i) {
                bool complete = true;
                StabilityObservation result;
                result.net = 0;
                result.exposure = 0;
                result.cost = 0;
                result.turnover = 0;
                if (layer == "intraday") {
                    result.gross = 0;
                    result.holding_minutes = 0;
                    result.trades = 0;
                }
                for (const auto &symbol : contract_.symbols()) {
                    const auto &source = series.at(layer + "/" + symbol + "/" + policy).days[i];
                    if (!source.net) {
                        complete = false;
                    }
                    const auto add_value = [](auto &to, const auto &from) {
                        if (to && from) {
                            *to += *from / 5;
                        } else {
                            to.reset();
                        }
                    };
                    add_value(result.net, source.net);
                    add_value(result.gross, source.gross);
                    add_value(result.exposure, source.exposure);
                    add_value(result.cost, source.cost);
                    add_value(result.turnover, source.turnover);
                    add_value(result.holding_minutes, source.holding_minutes);
                    add_value(result.trades, source.trades);
                }
                if (!complete) {
                    result.net.reset();
                    result.gross.reset();
                    result.exposure.reset();
                    result.holding_minutes.reset();
                    result.trades.reset();
                }
                result.condition = output.market.at("conditions")
                                       .at("equal_weight")
                                       .at(calendar.size() - output.dates.size() + i)
                                       .at("label")
                                       .get<std::string>();
                target.days[i] = result;
            }
        }
    }
    const std::array<std::pair<const char *, const char *>, 5> pairs{
        {{"momentum", "momentum_always_long"},
         {"continuation_volume", "continuation_price"},
         {"reversion_candidate", "reversion_sign_only"},
         {"reversion_candidate", "reversion_drift"},
         {"intraday_risk", "daily_only"}}};
    for (std::size_t j = 0; j < 6; ++j) {
        const auto &registered = contract_.document().at("primary_endpoints").at(j);
        StabilityEndpoint endpoint{registered.at("id").get<std::string>(),
                                   j == 4,
                                   registered.at("material_increment").get<double>(),
                                   {}};
        for (std::size_t i = 0; i < output.dates.size(); ++i) {
            if (j < 5) {
                const auto layer = j == 4 ? "risk" : "intraday";
                const auto &a =
                    series.at(std::string{layer} + "/equal_weight/" + pairs[j].first).days[i].net;
                const auto &b =
                    series.at(std::string{layer} + "/equal_weight/" + pairs[j].second).days[i].net;
                endpoint.days.push_back(a && b ? std::optional<StabilityPair>{{*a, *b}} : std::nullopt);
            } else {
                bool complete = true;
                double sum = 0;
                for (const auto &symbol : contract_.symbols()) {
                    if (losses[symbol][i]) {
                        sum += *losses[symbol][i] / 5;
                    } else {
                        complete = false;
                    }
                }
                endpoint.days.push_back(complete ? std::optional<StabilityPair>{{sum, 0}}
                                                 : std::nullopt);
            }
        }
        output.endpoints.push_back(std::move(endpoint));
    }
    for (auto &[id, s] : series) {
        static_cast<void>(id);
        output.series.push_back(std::move(s));
    }
    output.sources = {{"cost_delay", costs.identity()},
                      {"accounts", accounts.identity()},
                      {"evidence_origin", origin},
                      {"saved_m6_records_authenticated", true},
                      {"raw_market_archive_reauthenticated", false}};
    return output;
}
} // namespace trading
