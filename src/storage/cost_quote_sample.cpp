#include "cost_quote_sample.hpp"
#include "artifact_io.hpp"
#include "broker_value.hpp"
#include "content_digest.hpp"
#include "journal_storage.hpp"
#include "json_input.hpp"
#include "market_page_archive.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace trading {
namespace {
using Json = nlohmann::json;
std::optional<std::string> continuation(const Json &page) {
    const auto &token = page.at("next_page_token");
    if (token.is_null()) {
        return {};
    }
    if (!token.is_string() || token.get<std::string>().size() > 512) {
        throw std::invalid_argument("quote continuation token is invalid");
    }
    const auto value = token.get<std::string>();
    return value.empty() ? std::nullopt : std::optional<std::string>{value};
}
std::int64_t units(const Json &value, unsigned decimals) {
    if (!value.is_number() || value.is_boolean()) {
        throw std::invalid_argument("quote number has an invalid type");
    }
    const auto result = BrokerDecimal{value.dump()}.scaled_integer(decimals);
    if (result < 0) {
        throw std::invalid_argument("quote number cannot be negative");
    }
    return result;
}
Json classify(const Json &quote, std::chrono::sys_seconds end) {
    const auto bid = units(quote.at("bp"), 9), ask = units(quote.at("ap"), 9), bs = units(quote.at("bs"), 0),
               as = units(quote.at("as"), 0);
    if (bs > std::numeric_limits<std::int64_t>::max() / 100 ||
        as > std::numeric_limits<std::int64_t>::max() / 100) {
        throw std::invalid_argument("quote size exceeds share-unit range");
    }
    const auto source = BrokerTimestamp{quote.at("t").get<std::string>()}.value();
    std::string status = "eligible";
    if (bid <= 0 || ask <= 0) {
        status = "missing_side";
    } else if (ask < bid) {
        status = "crossed";
    } else if (ask == bid) {
        status = "locked";
    } else if (bs <= 0 || as <= 0) {
        status = "missing_size";
    } else if (!quote.at("bx").is_string() || quote.at("bx").get<std::string>().empty() ||
               !quote.at("ax").is_string() || quote.at("ax").get<std::string>().empty()) {
        status = "missing_exchange";
    } else {
        const auto &flags = quote.at("c");
        if (!flags.is_array() || flags.empty() || flags.size() > 2 ||
            !std::all_of(flags.begin(), flags.end(), [](const auto &f) {
                return f.is_string() && f.template get<std::string>() == "R";
            })) {
            status = "nonregular";
        }
    }
    Json result{
        {"status", status},
        {"source_timestamp", quote.at("t")},
        {"age_nanoseconds", std::chrono::duration_cast<std::chrono::nanoseconds>(end - source).count()},
        {"bid_units", bid},
        {"ask_units", ask},
        {"bid_shares", bs * 100},
        {"ask_shares", as * 100}};
    if (status == "eligible") {
        const auto spread =
            static_cast<double>(ask - bid) * 20000.0 / (static_cast<double>(ask) + static_cast<double>(bid));
        result["spread_bps"] = spread;
        result["half_spread_bps"] = spread / 2;
    }
    return result;
}
void retain_plan(const std::filesystem::path &path, const Json &plan, bool acquire) {
    if (!std::filesystem::exists(path)) {
        if (!acquire) {
            throw std::runtime_error("quote sample plan is missing");
        }
        write_new_text_file(path, plan.dump(2) + '\n');
    }
    if (parse_json_document(read_text_file(path)) != plan) {
        throw std::invalid_argument("quote sample plan or registered identity differs");
    }
}
} // namespace
CostQuoteSample::CostQuoteSample(std::filesystem::path directory, CostQuotePlan plan)
    : directory_(std::move(directory)), plan_(std::move(plan)) {}
Json CostQuoteSample::run(bool acquire, const Fetch &fetch, ApiRequestPolicy::Wait wait,
                          const std::function<void(const std::string &)> &progress) const {
    if (acquire) {
        std::filesystem::create_directories(directory_);
    }
    const auto lockfile = directory_ / "writer.lock";
    auto lock = std::filesystem::exists(lockfile) ? execution::open_journal_file(lockfile)
                                                  : execution::create_journal_file(lockfile);
    retain_plan(directory_ / "plan.json", plan_.document(), acquire);
    MarketPageArchive archive{directory_, acquire, std::move(wait)};
    Json windows = Json::array();
    std::map<std::string, std::size_t> counts;
    std::set<std::string> attempt_files;
    std::size_t retained = 0;
    bool denied = false;
    for (const auto &window : plan_.windows()) {
        Json row{{"id", window.id},
                 {"symbol", window.symbol},
                 {"date", window.date},
                 {"decision_minute", window.decision_minute},
                 {"delay_minutes", window.delay_minutes},
                 {"start", format_utc_seconds(window.request.start())},
                 {"end", format_utc_seconds(window.request.end())},
                 {"pages", Json::array()},
                 {"records", 0},
                 {"measurement", nullptr}};
        std::string status = "empty";
        std::optional<std::string> token;
        std::set<std::string> tokens;
        std::optional<Json> latest;
        std::optional<std::chrono::sys_time<std::chrono::nanoseconds>> previous;
        bool ambiguous = false;
        std::size_t records = 0;
        if (denied) {
            status = "not_attempted_after_denial";
        } else {
            for (std::size_t page = 0; page < 16; ++page) {
                const auto response = archive.get("responses/" + window.id + "/page_" + std::to_string(page),
                                                  "data.alpaca.markets", window.request.path(token),
                                                  fetch ? [&] { return fetch(window.request, token); }
                                                        : std::function<MarketDataResponse()>{});
                row["pages"].push_back(archive.last());
                for (const auto &receipt : archive.last().at("attempts")) {
                    const auto file = receipt.at("file").get<std::string>();
                    if (attempt_files.insert(file).second) {
                        ++retained;
                    }
                }
                if (response.status != 200) {
                    status = "http_" + std::to_string(response.status);
                    if (response.status == 401 || response.status == 403) {
                        denied = true;
                    }
                    break;
                }
                const auto doc = parse_json_document(response.body);
                const auto &grouped = doc.at("quotes");
                if (!grouped.is_object() ||
                    (!grouped.empty() && (grouped.size() != 1 || !grouped.contains(window.symbol) ||
                                          !grouped.at(window.symbol).is_array()))) {
                    throw std::invalid_argument("quote page symbol or shape differs");
                }
                if (!grouped.empty()) {
                    const auto &values = grouped.at(window.symbol);
                    if (values.size() > 100) {
                        throw std::invalid_argument("quote page exceeds requested limit");
                    }
                    for (const auto &quote : values) {
                        const auto at = BrokerTimestamp{quote.at("t").get<std::string>()}.value();
                        if (at < window.request.start() || at > window.request.end() ||
                            (previous && at < *previous)) {
                            throw std::invalid_argument(
                                "quote source clock violates requested interval or page order");
                        }
                        ++records;
                        if (!previous || at > *previous) {
                            latest = quote;
                            ambiguous = false;
                        } else if (latest && quote != *latest) {
                            ambiguous = true;
                        }
                        previous = at;
                    }
                }
                token = continuation(doc);
                if (!token) {
                    if (latest) {
                        if (ambiguous) {
                            status = "ambiguous_latest";
                        } else {
                            auto result = classify(*latest, window.request.end());
                            status = result.at("status").get<std::string>();
                            row["measurement"] = std::move(result);
                        }
                    }
                    break;
                }
                if (!tokens.insert(*token).second) {
                    throw std::invalid_argument("quote pagination token cycled");
                }
                if (page == 15) {
                    status = "incomplete_page_limit";
                }
            }
        }
        row["status"] = status;
        row["records"] = records;
        ++counts[status];
        windows.push_back(std::move(row));
        if (progress && (windows.size() % 25 == 0 || windows.size() == plan_.windows().size())) {
            progress("Historical quote windows | " + std::to_string(windows.size()) + '/' +
                     std::to_string(plan_.windows().size()) + " | new requests " +
                     std::to_string(archive.network_requests()));
        }
    }
    return {
        {"schema_version", 1},
        {"kind", "cost_quote_sample"},
        {"evidence_origin", plan_.document().at("evidence_origin")},
        {"policy_sha256", TRADING_COST_EXECUTION_PROTOCOL_SHA256},
        {"plan_sha256", sha256_file(directory_ / "plan.json")},
        {"plan", plan_.document()},
        {"dataset", directory_.generic_string()},
        {"windows", windows},
        {"status_counts", counts},
        {"retained_http_attempts", retained},
        {"request_callbacks_this_invocation", archive.network_requests()},
        {"network_requests_this_invocation",
         plan_.document().at("evidence_origin").get<std::string>() == "provider" ? archive.network_requests()
                                                                                 : 0},
        {"historical_strategy_scored", false},
        {"reserved_strategy_inputs_opened", false},
        {"source_sha256", TRADING_SOURCE_SHA256},
        {"compiler", TRADING_COMPILER_ID},
        {"build_configuration", TRADING_BUILD_CONFIGURATION},
        {"status", counts["eligible"] == windows.size() ? "quote_sample_complete"
                                                        : "quote_sample_completed_with_unestimated_windows"}};
}
} // namespace trading
