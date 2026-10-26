#include "historical_capability.hpp"
#include "artifact_io.hpp"
#include "broker_value.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include "market_data_query.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {
using Json = nlohmann::json;

std::optional<std::string> continuation(const Json& document) {
    const auto& value = document.at("next_page_token");
    if (value.is_null()) { return {}; }
    if (!value.is_string()) { throw std::invalid_argument("invalid continuation type"); }
    auto result = value.get<std::string>();
    static_cast<void>(page_token_query(result));
    return result;
}
double number(const Json& row, const char* key) {
    const auto& value = row.at(key);
    if (!value.is_number()) { throw std::invalid_argument("numeric observation field required"); }
    const auto result = value.get<double>();
    if (!std::isfinite(result) || result < 0) { throw std::invalid_argument("invalid observation value"); }
    return result;
}
std::string action_date(const Json& row, const char* key) {
    const auto value = row.at(key).get<std::string>();
    if (value.size() != 10) { throw std::invalid_argument("action date requires YYYY-MM-DD"); }
    static_cast<void>(parse_utc_seconds(value + "T00:00:00Z"));
    return value;
}
std::string status(unsigned code) {
    if (code == 401) { return "authentication_failed"; }
    if (code == 403) { return "forbidden"; }
    if (code == 404) { return "endpoint_unavailable"; }
    if (code == 429 || code >= 500) { return "transient_failure"; }
    return code == 0 ? "transport_or_retry_failure" : "http_" + std::to_string(code);
}

class EvidenceWriteError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// One bounded acquisition owner archives every HTTP attempt before interpretation.
class CapabilityRun {
public:
    CapabilityRun(std::filesystem::path directory, const ApiRequestPolicy& policy)
        : directory_{std::move(directory)}, policy_{policy} {}
    MarketDataResponse get(std::string key, std::string path, int year,
                          const std::function<MarketDataResponse()>& fetch) {
        if (authentication_failed_) { return {401, {}, {}}; }
        if (blocked_.contains(key)) { return {blocked_.at(key), {}, {}}; }
        unsigned observed = 0;
        try {
            auto response = policy_.get(fetch, [&](const MarketDataResponse& item) {
                observed = item.status;
                retain(key, path, year, item);
            });
            if (response.status == 401) { authentication_failed_ = true; }
            if (response.status == 400 || response.status == 403 || response.status == 404) {
                blocked_.emplace(std::move(key), response.status);
            }
            return response;
        } catch (const EvidenceWriteError&) {
            throw;
        } catch (const std::exception&) {
            if (observed == 0) {
                retain(key, path, year, {0, {}, {}});
            }
            // A failed endpoint is diagnosed once per symbol in this run.
            blocked_[std::move(key)] = observed;
            return {observed, {}, {}};
        }
    }
    const Json& records() const noexcept { return records_; }
private:
    void retain(const std::string& key, const std::string& path, int year, const MarketDataResponse& response) {
        if (records_.size() >= 5000 || response.body.size() > maximum_market_data_page_bytes ||
            response.body.size() > 64U * 1024U * 1024U - bytes_) {
            throw EvidenceWriteError("capability evidence exceeds its request/page/64-MiB bound");
        }
        const auto file = std::string{year >= 2024 ? "sealed/" : "responses/"} +
                          std::to_string(records_.size()) + ".json";
        const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        try {
            write_new_text_file(directory_ / file, response.body);
            const Json record{{"key", key}, {"year", year}, {"request_path", path},
                {"host", key == "calendar" ? "paper-api.alpaca.markets" : "data.alpaca.markets"},
                {"http_status", response.status}, {"retry_after", response.retry_after ? Json(*response.retry_after) : Json()},
                {"raw_file", file}, {"sha256", sha256(response.body)}, {"bytes", response.body.size()},
                {"retrieved_utc", format_utc_seconds(now)}};
            // The append-only index also survives an interrupted probe.
            write_new_text_file(directory_ / "index" / (std::to_string(records_.size()) + ".json"),
                                record.dump(2) + '\n');
            records_.push_back(record);
        } catch (const std::exception&) {
            throw EvidenceWriteError("cannot preserve capability response; acquisition stopped");
        }
        bytes_ += response.body.size();
    }
    std::filesystem::path directory_;
    const ApiRequestPolicy& policy_;
    Json records_ = Json::array();
    std::size_t bytes_{0};
    bool authentication_failed_{false};
    std::map<std::string, unsigned> blocked_;
};
} // namespace

Json HistoricalCapabilityAudit::ticks(const HistoricalTickRequest& request, std::string_view body) const {
    const auto doc = parse_json_document(body);
    const std::string kind = request.kind() == HistoricalTickKind::quotes ? "quotes" : "trades";
    const auto& grouped = doc.at(kind);
    const auto token = continuation(doc);
    if (!grouped.is_object() || (!grouped.empty() &&
        (grouped.size() != 1 || !grouped.contains(request.symbol()) || !grouped.at(request.symbol()).is_array()))) {
        throw std::invalid_argument("tick response contains an unexpected symbol or shape");
    }
    std::size_t count = 0, usable = 0;
    std::optional<std::chrono::sys_time<std::chrono::nanoseconds>> previous;
    if (!grouped.empty()) {
        for (const auto& row : grouped.at(request.symbol())) {
            if (++count > 100) { throw std::invalid_argument("tick page exceeds requested limit"); }
            const auto t = BrokerTimestamp{row.at("t").get<std::string>()}.value();
            if (t < request.start() || t > request.end() || (previous && t < *previous)) {
                throw std::invalid_argument("tick timestamp violates requested interval/order");
            }
            previous = t;
            if (request.kind() == HistoricalTickKind::quotes) {
                const auto bp = number(row, "bp"), ap = number(row, "ap");
                const auto bs = number(row, "bs"), as = number(row, "as");
                usable += bp > 0 && ap >= bp && bs > 0 && as > 0 ? 1U : 0U;
            } else {
                const auto price = number(row, "p"), size = number(row, "s");
                usable += price > 0 && size > 0 ? 1U : 0U;
            }
        }
    }
    return {{"status", count == 0 ? "accessible_empty" : "accessible_sample"},
        {"records", count}, {"usable_records", usable}, {"more_pages", token.has_value()},
        {"pagination_exhausted", !token.has_value()}, {"full_window_verified", false}};
}

Json HistoricalCapabilityAudit::actions(const CorporateActionRequest& request, std::string_view body) const {
    const auto doc = parse_json_document(body);
    const auto& groups = doc.at("corporate_actions");
    const auto token = continuation(doc);
    if (!groups.is_object()) { throw std::invalid_argument("action groups must be an object"); }
    Json ids = Json::array(), types = Json::object();
    std::set<std::string> unique;
    std::size_t count = 0, dividends = 0, payment_fields = 0, splits = 0, split_fields = 0;
    for (const auto& [kind, rows] : groups.items()) {
        if (!rows.is_array()) { throw std::invalid_argument("action group must be an array"); }
        types[kind] = rows.size();
        for (const auto& row : rows) {
            if (++count > 100) { throw std::invalid_argument("action page exceeds requested limit"); }
            const auto id = row.at("id").get<std::string>();
            const auto process = action_date(row, "process_date");
            if (id.empty() || !unique.insert(id).second || row.at("symbol").get<std::string>() != request.symbol() ||
                process < request.first() || process > request.last()) {
                throw std::invalid_argument("action identity/symbol/process date mismatch");
            }
            ids.push_back(id);
            if (kind == "cash_dividends") {
                ++dividends;
                try {
                    const auto ex = action_date(row, "ex_date"), pay = action_date(row, "payable_date");
                    const auto record = action_date(row, "record_date");
                    payment_fields += number(row, "rate") > 0 && pay >= ex && record >= ex ? 1U : 0U;
                } catch (const std::exception&) { /* Retain incomplete provider fields as a coverage failure. */ }
            } else if (kind == "forward_splits" || kind == "reverse_splits" || kind == "unit_splits") {
                ++splits;
                try {
                    static_cast<void>(action_date(row, "ex_date"));
                    split_fields += number(row, "old_rate") > 0 && number(row, "new_rate") > 0 ? 1U : 0U;
                } catch (const std::exception&) { /* Empty/missing ratios cannot establish a usable split. */ }
            }
        }
    }
    return {{"records", count}, {"ids", ids}, {"types", types}, {"cash_dividends", dividends},
        {"complete_payment_fields", payment_fields}, {"splits", splits}, {"complete_split_fields", split_fields},
        {"next_page_token", token ? Json(*token) : Json()}};
}

HistoricalCapabilityProbe::HistoricalCapabilityProbe(HistoricalCapabilityPlan plan, CapabilityOrigin origin,
    ApiRequestPolicy::Wait wait) : plan_{plan}, origin_{origin}, policy_{std::move(wait), true} {
    if (plan.first_year < 2016 || plan.last_year > 2025 || plan.first_year > plan.last_year ||
        (origin != CapabilityOrigin::provider && origin != CapabilityOrigin::synthetic)) {
        throw std::invalid_argument("capability plan must lie within 2016-2025");
    }
}

Json HistoricalCapabilityProbe::run(const std::filesystem::path& directory,
    const HistoricalCapabilityFetchers& f, const std::function<void(const std::string&)>& progress) const {
    if (!f.calendar || !f.bars || !f.actions || !f.ticks || !std::filesystem::is_empty(directory)) {
        throw std::invalid_argument("capability run requires four fetchers and a new empty directory");
    }
    CapabilityRun run{directory, policy_};
    HistoricalCapabilityAudit audit;
    Json rows = Json::array(), calendar_rows = Json::array();
    for (int year = plan_.first_year; year <= plan_.last_year; ++year) {
        const auto y = std::to_string(year);
        if (progress) { progress("Historical capability | calendar " + y); }
        std::vector<CalendarSession> selected;
        for (const std::string month : {"01", "12"}) {
            const CalendarRange range{y + '-' + month + "-01", y + '-' + month + "-31"};
            Json item{{"year", year}, {"month", month}, {"status", "unavailable"}};
            const auto response = run.get("calendar", range.path(), year, [&] { return f.calendar(range); });
            item["http_status"] = response.status;
            if (response.status == 200) {
                try {
                    const auto sessions = decode_market_calendar(response.body, range);
                    std::vector<CalendarSession> regular;
                    std::copy_if(sessions.begin(), sessions.end(), std::back_inserter(regular),
                                 [](const auto& session) { return session.regular(); });
                    if (regular.empty()) { throw std::invalid_argument("no full session in sampled month"); }
                    selected.push_back(month == "01" ? regular.front() : regular.back());
                    item["status"] = "sampled_calendar_valid";
                    item["sessions"] = sessions.size();
                    item["selected_date"] = selected.back().date();
                } catch (const std::exception&) { item["status"] = "invalid_calendar_evidence"; }
            } else { item["status"] = status(response.status); }
            calendar_rows.push_back(item);
        }
        for (const auto symbol_view : plan_.symbols) {
            const std::string symbol{symbol_view};
            if (progress) { progress("Historical capability | " + symbol + ' ' + y); }
            Json row{{"year", year}, {"symbol", symbol}, {"bar_samples", Json::array()}};
            for (const auto& session : selected) {
                Json bar{{"date", session.date()}, {"status", "unavailable"}};
                const auto request = session.bar_request(symbol);
                unsigned http = 0;
                try {
                    const auto data = SessionDownloader{request}.download([&](const auto& r, const auto& token) {
                        auto response = run.get("bars/" + symbol, r.path(token), year,
                                                [&] { return f.bars(r, token); });
                        http = response.status;
                        return response;
                    });
                    bar["status"] = data.audit.complete() ? "complete_sample" : "gapped_sample";
                    bar["expected_minutes"] = data.audit.expected_minutes;
                    bar["observed_minutes"] = data.audit.observed_bars;
                    bar["pages"] = data.pages.size();
                    bar["missing_minutes"] = data.audit.missing_minutes.size();
                } catch (const EvidenceWriteError&) { throw; }
                catch (const std::exception&) { bar["status"] = http == 200 ? "invalid_bar_evidence" : status(http); }
                bar["http_status"] = http;
                row["bar_samples"].push_back(bar);
            }
            row["sampled_bars_complete"] = selected.size() == 2 &&
                std::all_of(row["bar_samples"].begin(), row["bar_samples"].end(),
                            [](const Json& v) { return v.at("status") == Json("complete_sample"); });
            for (const auto kind : {HistoricalTickKind::quotes, HistoricalTickKind::trades}) {
                const std::string label = kind == HistoricalTickKind::quotes ? "quotes" : "trades";
                Json tick{{"status", "calendar_unavailable"}};
                if (!selected.empty()) {
                    const auto start = selected.front().open_utc() + std::chrono::minutes{150};
                    const HistoricalTickRequest request{kind, symbol, start, start + std::chrono::minutes{1}};
                    const auto response = run.get(label + '/' + symbol, request.path(), year,
                                                  [&] { return f.ticks(request); });
                    tick["http_status"] = response.status;
                    if (response.status == 200) {
                        try { tick.update(audit.ticks(request, response.body)); }
                        catch (const std::exception&) { tick["status"] = "invalid_tick_evidence"; }
                    } else { tick["status"] = status(response.status); }
                }
                row[label] = tick;
            }
            Json actions{{"status", "unavailable"}, {"records", 0}, {"cash_dividends", 0},
                {"complete_payment_fields", 0}, {"splits", 0}, {"complete_split_fields", 0},
                {"types", Json::object()}, {"issuer_reconciled", false}, {"ex_date_coverage_verified", false}};
            const CorporateActionRequest request{symbol, y + "-01-01", y + "-12-31"};
            std::optional<std::string> token;
            std::set<std::string> tokens, ids;
            unsigned http = 0;
            try {
                for (std::size_t page = 0; ; ++page) {
                    if (page >= 16) { throw std::invalid_argument("action page bound exceeded"); }
                    const auto response = run.get("actions/" + symbol, request.path(token), year,
                                                  [&] { return f.actions(request, token); });
                    http = response.status;
                    if (http != 200) { throw std::runtime_error("action HTTP failure"); }
                    const auto a = audit.actions(request, response.body);
                    for (const auto& id : a.at("ids")) {
                        if (!ids.insert(id.get<std::string>()).second) { throw std::invalid_argument("action repeated across pages"); }
                    }
                    for (const char* key : {"records", "cash_dividends", "complete_payment_fields", "splits", "complete_split_fields"}) {
                        actions[key] = actions.at(key).get<std::size_t>() + a.at(key).get<std::size_t>();
                    }
                    for (const auto& [type, count] : a.at("types").items()) {
                        actions["types"][type] = actions["types"].value(type, std::size_t{0}) + count.get<std::size_t>();
                    }
                    token = continuation(a);
                    if (!token) { break; }
                    if (!tokens.insert(*token).second) { throw std::invalid_argument("repeated action token"); }
                }
                actions["status"] = actions.at("records").get<std::size_t>() == 0 ? "accessible_empty" : "accessible_records";
                if (actions.at("cash_dividends") != actions.at("complete_payment_fields") ||
                    actions.at("splits") != actions.at("complete_split_fields")) {
                    actions["status"] = "accessible_incomplete_fields";
                }
                actions["pagination_exhausted"] = true;
            } catch (const EvidenceWriteError&) { throw; }
            catch (const std::exception&) {
                actions["status"] = http == 200 ? "invalid_action_evidence" : status(http);
                actions["pagination_exhausted"] = false;
            }
            actions["http_status"] = http;
            row["actions"] = actions;
            rows.push_back(row);
        }
    }
    Json common_start;
    for (int first = plan_.first_year; first <= plan_.last_year; ++first) {
        const bool complete = std::all_of(rows.begin(), rows.end(), [&](const Json& r) {
            return r.at("year").get<int>() < first || r.at("sampled_bars_complete").get<bool>();
        });
        if (complete) { common_start = first; break; }
    }
    const bool all_bars = std::all_of(rows.begin(), rows.end(), [](const Json& r) {
        return r.at("sampled_bars_complete").get<bool>();
    });
    return {{"schema_version", 1}, {"kind", "historical_capability"},
        {"evidence_origin", origin_ == CapabilityOrigin::provider ? "provider" : "synthetic"},
        {"status", all_bars ? "sampled_price_route_ready" : "input_access_review_required"},
        {"first_year", plan_.first_year}, {"last_year", plan_.last_year},
        {"symbols", plan_.symbols}, {"rows", rows}, {"calendar_samples", calendar_rows},
        {"provisional_common_price_start", common_start}, {"full_history_verified", false},
        {"corporate_actions_reconciled", false}, {"research_inputs_ready", false}, {"strategy_scored", false},
        {"source_sha256", TRADING_SOURCE_SHA256}, {"compiler", TRADING_COMPILER_ID},
        {"build_configuration", TRADING_BUILD_CONFIGURATION}, {"attempts", run.records()},
        {"request_attempts", run.records().size()},
        {"network_requests", origin_ == CapabilityOrigin::provider ? run.records().size() : 0U}};
}
} // namespace trading
