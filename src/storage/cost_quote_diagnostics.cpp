#include "cost_quote_diagnostics.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {
using Json = nlohmann::json;
Json field(const Json &quote, const char *name) {
    if (!quote.contains(name)) {
        return {{"present", false}};
    }
    const auto &value = quote.at(name);
    const auto encoded = value.dump();
    if (encoded.size() > 512) {
        return {{"present", true},
                {"representation", "sha256_only_over_512_bytes"},
                {"json_type", value.type_name()},
                {"value_sha256", sha256(encoded)}};
    }
    return {{"present", true}, {"value", value}};
}
Json observe(const CostQuoteWindow &window, const Json &row, const std::vector<Json> &records) {
    const auto status = row.at("status").get<std::string>();
    const bool complete = !status.starts_with("http_") && status != "incomplete_page_limit" &&
                          status != "not_attempted_after_denial";
    Json variants = Json::array();
    std::map<std::string, std::size_t> counts;
    std::map<std::string, Json> descriptors;
    std::set<std::string> changed;
    bool identical = true, price_same = true, size_same = true, supported = true;
    std::optional<Json> first_prices, first_sizes;
    for (const auto &quote : records) {
        const Json metadata{{"conditions", field(quote, "c")}, {"tape", field(quote, "z")}};
        const auto key = metadata.dump();
        ++counts[key];
        descriptors[key] = metadata;
        if (quote != records.front()) {
            identical = false;
        }
        std::set<std::string> names;
        for (const auto &[name, value] : quote.items()) {
            static_cast<void>(value);
            names.insert(name);
        }
        for (const auto &[name, value] : records.front().items()) {
            static_cast<void>(value);
            names.insert(name);
        }
        for (const auto &name : names) {
            if (quote.contains(name) != records.front().contains(name) ||
                (quote.contains(name) && quote.at(name) != records.front().at(name))) {
                changed.insert(name);
            }
        }
        try {
            const auto assessed = classify_cost_quote(quote, window.request.end());
            const Json prices = Json::array({assessed.at("bid_units"), assessed.at("ask_units")});
            const Json sizes = Json::array({assessed.at("bid_shares"), assessed.at("ask_shares")});
            if (!first_prices) {
                first_prices = prices;
                first_sizes = sizes;
            } else {
                price_same = price_same && prices == *first_prices;
                size_same = size_same && sizes == *first_sizes;
            }
        } catch (const Json::exception &) {
            supported = false;
        } catch (const std::invalid_argument &) {
            supported = false;
        }
    }
    for (const auto &[key, count] : counts) {
        auto value = descriptors.at(key);
        value["records"] = count;
        variants.push_back(std::move(value));
    }
    std::string conflict = !complete         ? "incomplete"
                           : records.empty() ? "no_quote"
                           : identical       ? (records.size() == 1 ? "single_quote" : "identical_duplicates")
                           : !supported      ? "unsupported_fields"
                           : !price_same     ? "price_conflict"
                           : !size_same      ? "size_conflict"
                                             : "metadata_only";
    return {
        {"id", window.id},
        {"symbol", window.symbol},
        {"date", window.date},
        {"primary_status", status},
        {"complete", complete},
        {"latest_records", records.size()},
        {"latest_relation", conflict},
        {"price_consistent", complete && !records.empty() && supported ? Json(price_same) : Json(nullptr)},
        {"size_consistent", complete && !records.empty() && supported ? Json(size_same) : Json(nullptr)},
        {"changed_fields", changed},
        {"metadata_variants", variants},
        {"passes_if_only_condition_test_removed", status == "eligible" || status == "nonregular"}};
}
} // namespace
CostQuoteDiagnostics::CostQuoteDiagnostics(std::filesystem::path panel_directory)
    : panel_directory_{std::move(panel_directory)} {}
Json CostQuoteDiagnostics::inspect(const std::filesystem::path &saved_summary,
                                   const std::function<void(const std::string &)> &progress) const {
    const auto raw = read_text_file(saved_summary, 16U * 1024U * 1024U);
    const auto original = parse_json_document(raw);
    if (original.at("kind").get<std::string>() != "cost_quote_sample" ||
        original.at("policy_sha256").get<std::string>() != TRADING_COST_EXECUTION_PROTOCOL_SHA256) {
        throw std::invalid_argument("quote diagnostics require the registered saved sample");
    }
    const auto origin = original.at("evidence_origin").get<std::string>();
    if (origin != "provider" && origin != "synthetic") {
        throw std::invalid_argument("quote diagnostic origin is unsupported");
    }
    const auto plan = origin == "provider"
                          ? CostQuotePlan::from_panel(panel_directory_)
                          : CostQuotePlan::constructed(
                                original.at("plan").at("sample_dates").get<std::vector<std::string>>());
    if (!std::filesystem::exists(std::filesystem::path{original.at("dataset").get<std::string>()} /
                                 "writer.lock")) {
        throw std::invalid_argument(
            "cached quote writer lock is missing; inspection does not create archive files");
    }
    Json observations = Json::array();
    const auto replay = CostQuoteSample{original.at("dataset").get<std::string>(), plan}.run(
        false, {}, [](auto) {}, progress,
        [&](const auto &window, const auto &row, const auto &records) {
            observations.push_back(observe(window, row, records));
        });
    for (const auto *key :
         {"kind", "evidence_origin", "policy_sha256", "plan_sha256", "plan", "windows", "status_counts",
          "retained_http_attempts", "historical_strategy_scored", "reserved_strategy_inputs_opened"}) {
        if (replay.at(key) != original.at(key)) {
            throw std::invalid_argument(std::string{"saved quote sample differs from read-only replay: "} +
                                        key);
        }
    }
    if (replay.at("request_callbacks_this_invocation").get<std::size_t>() != 0 ||
        sha256_file(saved_summary, 16U * 1024U * 1024U) != sha256(raw)) {
        throw std::invalid_argument("quote diagnostic changed its source or requested data");
    }
    std::map<std::string, std::size_t> conflicts, frequency_counts, frequency_windows;
    std::map<std::string, Json> frequency_descriptors;
    std::size_t without_conditions = 0;
    for (const auto &row : observations) {
        if (row.at("passes_if_only_condition_test_removed").get<bool>()) {
            ++without_conditions;
        }
        if (row.at("primary_status").get<std::string>() == "ambiguous_latest") {
            ++conflicts[row.at("latest_relation").get<std::string>()];
        }
        if (!row.at("complete").get<bool>()) {
            continue;
        }
        for (const auto &variant : row.at("metadata_variants")) {
            const Json descriptor{{"year", row.at("date").get<std::string>().substr(0, 4)},
                                  {"symbol", row.at("symbol")},
                                  {"conditions", variant.at("conditions")},
                                  {"tape", variant.at("tape")}};
            const auto key = descriptor.dump();
            frequency_counts[key] += variant.at("records").get<std::size_t>();
            ++frequency_windows[key];
            frequency_descriptors[key] = descriptor;
        }
    }
    Json frequencies = Json::array();
    for (const auto &[key, count] : frequency_counts) {
        auto row = frequency_descriptors.at(key);
        row["records"] = count;
        row["windows"] = frequency_windows.at(key);
        frequencies.push_back(std::move(row));
    }
    return {{"schema_version", 1},
            {"kind", "cost_quote_diagnostics"},
            {"diagnostic_rules_version", 1},
            {"source_summary", std::filesystem::absolute(saved_summary).generic_string()},
            {"source_summary_sha256", sha256(raw)},
            {"producer_source_sha256", original.at("source_sha256")},
            {"policy_sha256", original.at("policy_sha256")},
            {"plan_sha256", original.at("plan_sha256")},
            {"evidence_origin", origin},
            {"windows", observations},
            {"primary_status_counts", original.at("status_counts")},
            {"primary_replay_identical", true},
            {"ambiguous_latest_relations", conflicts},
            {"metadata_frequencies", frequencies},
            {"condition_test_removal_only_count", without_conditions},
            {"revised_eligibility_adopted", false},
            {"retained_http_attempts", original.at("retained_http_attempts")},
            {"network_requests", 0},
            {"historical_strategy_scored", false},
            {"reserved_strategy_inputs_opened", false},
            {"source_sha256", TRADING_SOURCE_SHA256},
            {"compiler", TRADING_COMPILER_ID},
            {"build_configuration", TRADING_BUILD_CONFIGURATION}};
}
} // namespace trading
