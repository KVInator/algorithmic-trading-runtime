#include "historical_panel.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "historical_capability.hpp"
#include "json_input.hpp"
#include "journal_storage.hpp"
#include "market_data_query.hpp"
#include "response_bytes.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {
using Json = nlohmann::json;

std::string now() {
    return format_utc_seconds(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
}
void retain(const std::filesystem::path& file, const Json& record, bool acquire) {
    if (!std::filesystem::exists(file)) {
        if (!acquire) { throw std::runtime_error("panel evidence is incomplete: " + file.generic_string()); }
        write_new_text_file(file, record.dump(2) + '\n');
    }
    if (parse_json_document(read_text_file(file, 2U * 1024U * 1024U)) != record) {
        throw std::invalid_argument("panel checkpoint content or identity changed: " + file.generic_string());
    }
}
Json reference(const std::filesystem::path& root, const std::string& file) {
    return {{"file", file}, {"sha256", sha256_file(contained_path(root, file))}};
}
std::string prefix(std::string_view date) { return date >= "2024" ? "sealed/" : "responses/"; }

class PanelPageArchive {
public:
    PanelPageArchive(std::filesystem::path root, bool acquire, ApiRequestPolicy::Wait wait)
        : root_{std::move(root)}, acquire_{acquire}, policy_{std::move(wait), true} {}

    MarketDataResponse get(const std::string& key, const std::string& host, const std::string& path,
                          const std::function<MarketDataResponse()>& fetch) {
        std::size_t attempts = 0;
        std::optional<MarketDataResponse> latest;
        Json receipts = Json::array();
        while (std::filesystem::exists(contained_path(root_, filename(key, attempts)))) {
            if (attempts == 9) { throw std::invalid_argument("request contains more than nine archived attempts"); }
            if (latest && !retryable(latest->status)) { throw std::invalid_argument("request contains attempts after a terminal response"); }
            const auto file = filename(key, attempts++);
            const auto doc = parse_json_document(read_text_file(contained_path(root_, file), 192U * 1024U));
            const auto body = decode_response_bytes(doc.at("body_hex").get<std::string>());
            static_cast<void>(parse_utc_seconds(doc.at("retrieved_utc").get<std::string>()));
            if (doc.at("schema_version") != 1 || doc.at("host") != host || doc.at("request_path") != path ||
                body.size() > maximum_market_data_page_bytes || doc.at("body_sha256") != sha256(body)) {
                throw std::invalid_argument("panel response provenance/hash mismatch");
            }
            latest = MarketDataResponse{doc.at("http_status").get<std::uint32_t>(), body,
                doc.at("retry_after").is_null() ? std::optional<std::string>{} : doc.at("retry_after").get<std::string>()};
            last_ = reference(root_, file);
            receipts.push_back(last_);
            last_["attempts"] = receipts;
        }
        for (auto index = attempts + 1; index <= 9; ++index) {
            if (std::filesystem::exists(contained_path(root_, filename(key, index)))) {
                throw std::invalid_argument("panel request has a noncontiguous attempt history");
            }
        }
        if (latest && (latest->status == 200 || !retryable(latest->status) || attempts == 9)) { return *latest; }
        if (!acquire_ || !fetch) { throw std::runtime_error("panel request is not complete: " + key); }
        auto result = policy_.get([&] {
            if (attempts >= 9) { throw std::runtime_error("panel request exhausted its retained nine-attempt budget"); }
            ++network_requests_;
            return fetch();
        }, [&](const MarketDataResponse& response) {
            if (response.body.size() > maximum_market_data_page_bytes) {
                throw std::invalid_argument("panel response exceeds its 64-KiB bound");
            }
            const auto file = filename(key, attempts++);
            const Json doc{{"schema_version", 1}, {"host", host}, {"request_path", path},
                {"http_status", response.status}, {"retry_after", response.retry_after ? Json(*response.retry_after) : Json(nullptr)},
                {"retrieved_utc", now()}, {"body_sha256", sha256(response.body)}, {"body_hex", encode_response_bytes(response.body)}};
            write_new_text_file(contained_path(root_, file), doc.dump(2) + '\n');
            last_ = reference(root_, file);
            receipts.push_back(last_);
            last_["attempts"] = receipts;
        });
        return result;
    }
    [[nodiscard]] const Json& last() const noexcept { return last_; }
    [[nodiscard]] std::size_t network_requests() const noexcept { return network_requests_; }
private:
    static bool retryable(unsigned status) { return status == 429 || status == 500 || status == 502 || status == 503 || status == 504; }
    static std::string filename(const std::string& key, std::size_t index) {
        return key + "/attempt_" + std::to_string(index) + ".json";
    }
    std::filesystem::path root_;
    bool acquire_;
    ApiRequestPolicy policy_;
    Json last_;
    std::size_t network_requests_{};
};

void require_http(const MarketDataResponse& response) {
    if (response.status != 200) {
        throw std::runtime_error("panel acquisition stopped on retained HTTP " + std::to_string(response.status) +
            "; completed pages are preserved; diagnose access before starting another vintage");
    }
}
std::vector<CalendarRange> full_ranges() {
    std::vector<CalendarRange> ranges;
    for (int y = 2016; y <= 2025; ++y) {
        for (unsigned month = 1; month <= 12; ++month) {
            const auto id = std::to_string(y) + (month < 10 ? "-0" : "-") + std::to_string(month);
            const auto end = std::chrono::year_month_day{std::chrono::year{y}/std::chrono::month{month}/std::chrono::last};
            ranges.emplace_back(id + "-01", id + '-' + std::to_string(static_cast<unsigned>(end.day())));
        }
    }
    return ranges;
}
std::vector<CalendarSession> calendar(const CalendarRange& range,
    PanelPageArchive& archive, const PanelFetchers& fetchers, Json& receipt) {
    const auto response = archive.get(prefix(range.first()) + "calendar/" + range.first().substr(0, 7),
        "paper-api.alpaca.markets", range.path(), [&] { return fetchers.calendar(range); });
    require_http(response);
    receipt = archive.last();
    const auto sessions = decode_market_calendar(response.body, range);
    const auto expected = extension_calendar(range);
    if (sessions.size() != expected.size()) { throw std::invalid_argument("panel calendar session count differs from the independent NYSE reference"); }
    for (std::size_t i = 0; i < sessions.size(); ++i) {
        if (sessions[i].date() != expected[i].date() || sessions[i].open_utc() != expected[i].open_utc() ||
            sessions[i].close_utc() != expected[i].close_utc()) {
            throw std::invalid_argument("panel calendar clocks/dates differ from the independent NYSE reference");
        }
    }
    return sessions;
}
std::pair<SessionDownload, Json> session(const CalendarSession& day, const std::string& symbol,
    PanelPageArchive& archive, const PanelFetchers& fetchers) {
    Json pages = Json::array();
    const auto request = day.bar_request(symbol);
    auto download = SessionDownloader{request}.download([&](const auto& req, const auto& token) {
        const auto response = archive.get(prefix(day.date()) + "bars/" + symbol + '/' + day.date() +
            "/page_" + std::to_string(pages.size()), "data.alpaca.markets", req.path(token),
            [&] { return fetchers.bars(req, token); });
        require_http(response);
        pages.push_back(archive.last());
        return response;
    });
    return {std::move(download), std::move(pages)};
}
Json session_record(const CalendarSession& day, const SessionDownload& download, const Json& pages,
                    const ExtensionStudyContract& contract) {
    Json missing = Json::array();
    for (const auto at : download.audit.missing_minutes) { missing.push_back(format_utc_seconds(at)); }
    const auto& bars = download.bars;
    const bool opening = !bars.empty() && bars.front().start() == day.open_utc();
    const bool closing = !bars.empty() && bars.back().start() == day.close_utc() - std::chrono::minutes{1};
    return {{"schema_version", 1}, {"symbol", download.request.symbol()}, {"date", day.date()},
        {"protocol_sha256", contract.identity()}, {"open_utc", format_utc_seconds(day.open_utc())},
        {"close_utc", format_utc_seconds(day.close_utc())}, {"regular", day.regular()},
        {"expected_minutes", download.audit.expected_minutes}, {"observed_minutes", download.audit.observed_bars},
        {"missing_minutes_utc", missing}, {"open_available", opening}, {"close_available", closing},
        {"complete_grid", download.audit.complete()}, {"reserved", contract.reserved(day.date())},
        {"intraday_eligible", day.regular() && download.audit.complete() && !contract.diagnostic_excluded(download.request.symbol(), day.date())},
        {"diagnostic_excluded", contract.diagnostic_excluded(download.request.symbol(), day.date())}, {"pages", pages}};
}
std::string checkpoint(std::string_view symbol, std::string_view date) {
    return "checkpoints/" + std::string{date.substr(0, 7)} + '/' + std::string{symbol} + '/' + std::string{date} + ".json";
}
Json action_interval(const CorporateActionRequest& request, PanelPageArchive& archive, const PanelFetchers& fetchers) {
    Json pages = Json::array();
    Json result{{"symbol", request.symbol()}, {"first_process_date", request.first()}, {"last_process_date", request.last()},
        {"records", 0}, {"cash_dividends", 0}, {"complete_payment_fields", 0}, {"splits", 0}, {"complete_split_fields", 0}};
    std::set<std::string> tokens, ids;
    std::optional<std::string> token;
    do {
        if (pages.size() >= 32) { throw std::invalid_argument("panel action interval exceeded 32 pages"); }
        const auto response = archive.get(prefix(request.first()) + "actions/" + request.symbol() + '/' + request.first().substr(0, 4) +
            "/page_" + std::to_string(pages.size()), "data.alpaca.markets", request.path(token),
            [&] { return fetchers.actions(request, token); });
        require_http(response);
        const auto counts = HistoricalCapabilityAudit{}.actions(request, response.body);
        const auto body = parse_json_document(response.body);
        for (const auto& [kind, values] : body.at("corporate_actions").items()) {
            for (const auto& row : values) {
                if (!ids.insert(row.at("id").get<std::string>()).second) {
                    throw std::invalid_argument("duplicate action identity across panel pages");
                }
            }
            if (kind != "cash_dividends" && kind != "forward_splits" && kind != "reverse_splits" && kind != "unit_splits" && !values.empty()) {
                result["unsupported_types"].push_back(kind);
            }
        }
        for (const auto* key : {"records", "cash_dividends", "complete_payment_fields", "splits", "complete_split_fields"}) {
            result[key] = result.at(key).get<std::size_t>() + counts.at(key).get<std::size_t>();
        }
        pages.push_back(archive.last());
        token = body.at("next_page_token").is_null() ? std::optional<std::string>{} : body.at("next_page_token").get<std::string>();
        if (token && !tokens.insert(*token).second) { throw std::invalid_argument("panel action pagination token repeated"); }
    } while (token);
    result["pages"] = pages;
    result["issuer_reconciled"] = false;
    return result;
}
} // namespace

HistoricalPanelDataset::HistoricalPanelDataset(std::filesystem::path directory, ExtensionStudyContract contract,
    PanelOrigin origin, std::vector<CalendarRange> constructed_ranges)
    : directory_{std::move(directory)}, contract_{std::move(contract)}, origin_{origin},
      ranges_{constructed_ranges.empty() ? full_ranges() : std::move(constructed_ranges)} {
    if (directory_.empty() || (origin_ == PanelOrigin::provider && ranges_.size() != 120)) {
        throw std::invalid_argument("provider panel requires the registered 120-month plan");
    }
    if (origin_ == PanelOrigin::provider) {
        const auto expected = full_ranges();
        if (!std::equal(ranges_.begin(), ranges_.end(), expected.begin(), [](const auto& a, const auto& b) {
            return a.first() == b.first() && a.last() == b.last(); })) {
            throw std::invalid_argument("provider panel requires complete registered calendar months");
        }
    }
    std::set<std::string> months;
    std::string previous;
    for (const auto& range : ranges_) {
        if (!months.insert(range.first().substr(0, 7)).second || range.first() <= previous) {
            throw std::invalid_argument("panel ranges must be chronological and unique by month");
        }
        previous = range.last();
    }
}
PanelClosingRecovery HistoricalPanelDataset::recover_closing_marks(const PanelInputPolicy& policy,
    const Json& audited_panel, const BarPageFetcher& fetch, ApiRequestPolicy::Wait wait, bool acquire) const {
    if (!wait || (acquire && !fetch)) { throw std::invalid_argument("closing recovery requires pacing and an acquisition fetcher"); }
    const auto acquisition_sha = sha256_file(directory_ / "acquisition_manifest.json");
    const std::string origin = origin_ == PanelOrigin::provider ? "provider" : "synthetic";
    if (audited_panel.at("acquisition_manifest_sha256").get<std::string>() != acquisition_sha ||
        audited_panel.at("protocol_sha256").get<std::string>() != contract_.identity() || audited_panel.at("evidence_origin").get<std::string>() != origin) {
        throw std::invalid_argument("closing recovery requires the authenticated original acquisition");
    }
    const auto recovery_root = directory_ / "resolutions/closing_marks";
    if (acquire) { std::filesystem::create_directories(recovery_root); }
    std::unique_ptr<execution::JournalStorage> owner;
    if (acquire) {
        const auto lock = directory_ / "acquisition.lock";
        owner = std::filesystem::exists(lock) ? execution::open_journal_file(lock) : execution::create_journal_file(lock);
    }
    PanelPageArchive archive{recovery_root, acquire, std::move(wait)};
    Json records = Json::array();
    std::vector<MinuteBar> recovered;
    for (const auto& item : policy.closing_recovery_plan()) {
        const auto symbol = item.at("symbol").get<std::string>(), date = item.at("date").get<std::string>();
        contract_.require_development_access(date);
        const auto found = std::find_if(audited_panel.at("coverage").begin(), audited_panel.at("coverage").end(), [&](const auto& row) {
            return row.at("symbol").template get<std::string>() == symbol && row.at("date").template get<std::string>() == date;
        });
        if (found == audited_panel.at("coverage").end()) {
            if (origin_ == PanelOrigin::synthetic) { continue; }
            throw std::invalid_argument("registered closing recovery date is absent from the acquisition");
        }
        if (found->at("close_available").get<bool>()) { continue; }
        const CalendarSession day{date, "09:30", found->at("regular").get<bool>() ? "16:00" : "13:00"};
        const HistoricalBarRequest request{symbol, day.close_utc() - std::chrono::minutes{1}, day.close_utc()};
        Json pages = Json::array();
        const auto download = SessionDownloader{request, 1}.download([&](const auto& req, const auto& token) {
            const auto response = archive.get("responses/" + symbol + '/' + date, "data.alpaca.markets", req.path(token),
                [&] { return fetch(req, token); });
            pages.push_back(archive.last());
            return response;
        });
        const bool available = download.audit.complete();
        records.push_back({{"symbol", symbol}, {"date", date}, {"start_utc", format_utc_seconds(request.start())},
            {"end_exclusive_utc", format_utc_seconds(request.end_exclusive())}, {"recovered", available}, {"pages", pages}});
        if (available) { recovered.push_back(download.bars.front()); }
    }
    const Json manifest{{"schema_version", 1}, {"kind", "closing_mark_recovery"}, {"evidence_origin", origin},
        {"acquisition_manifest_sha256", acquisition_sha}, {"protocol_sha256", contract_.identity()},
        {"input_policy_sha256", policy.identity()}, {"records", records}};
    retain(recovery_root / "manifest.json", manifest, acquire);
    return {{{"manifest_sha256", sha256_file(recovery_root / "manifest.json")}, {"required", records.size()},
        {"recovered", recovered.size()}, {"network_requests", archive.network_requests()}, {"complete", records.size() == recovered.size()}}, std::move(recovered)};
}

nlohmann::json HistoricalPanelDataset::assess_input_policy(const PanelInputPolicy& policy,
    std::string_view expected_acquisition_sha256) const {
    if (sha256_file(directory_ / "acquisition_manifest.json") != expected_acquisition_sha256) {
        throw std::invalid_argument("availability assessment acquisition identity changed");
    }
    const auto manifest = parse_json_document(read_text_file(directory_ / "acquisition_manifest.json"));
    const std::string origin = origin_ == PanelOrigin::provider ? "provider" : "synthetic";
    if (manifest.at("protocol_sha256").get<std::string>() != contract_.identity() || manifest.at("evidence_origin").get<std::string>() != origin) {
        throw std::invalid_argument("availability assessment origin or protocol mismatch");
    }
    const auto load = [&](const Json& ref) {
        const auto path = contained_path(directory_, ref.at("file").get<std::string>());
        if (sha256_file(path) != ref.at("sha256").get<std::string>()) { throw std::invalid_argument("availability checkpoint identity changed"); }
        return parse_json_document(read_text_file(path));
    };
    struct Year { std::size_t regular{}, operational{}, strict{}, halt_dates{}; };
    struct Features { std::size_t regular{}, momentum{}, reversion{}, continuation{}; };
    std::map<std::string, Year> years;
    std::map<std::pair<std::string, std::string>, Features> features;
    Json availability_rows = Json::array();
    const auto symbols = contract_.symbols();
    for (const auto& month_ref : manifest.at("months")) {
        const auto month = load(month_ref);
        const auto& rows = month.at("checkpoints");
        if (rows.size() % symbols.size() != 0) { throw std::invalid_argument("availability checkpoint group is incomplete"); }
        for (std::size_t first = 0; first < rows.size(); first += symbols.size()) {
            bool common = true, strict = true, regular = false, halt = false;
            std::string day;
            for (std::size_t offset = 0; offset < symbols.size(); ++offset) {
                const auto record = load(rows.at(first + offset));
                const auto symbol = record.at("symbol").get<std::string>();
                const auto date = record.at("date").get<std::string>();
                if (symbol != symbols.at(offset) || (offset != 0 && day != date)) { throw std::invalid_argument("availability checkpoint ordering mismatch"); }
                day = date;
                regular = record.at("regular").get<bool>();
                const CalendarSession session{date, "09:30", regular ? "16:00" : "13:00"};
                std::vector<std::chrono::sys_seconds> missing;
                for (const auto& at : record.at("missing_minutes_utc")) { missing.push_back(parse_utc_seconds(at.get<std::string>())); }
                const auto available = policy.evaluate(session, symbol, missing);
                availability_rows.push_back({{"symbol", symbol}, {"date", date}, {"operational", available.operational},
                    {"momentum_features", available.momentum_features}, {"reversion_features", available.reversion_features},
                    {"continuation_prefixes", available.continuation_prefixes}});
                common = common && available.operational;
                strict = strict && record.at("intraday_eligible").get<bool>();
                halt = available.known_halt_date;
                auto& count = features[{symbol, date.substr(0, 4)}];
                if (regular && !available.diagnostic_excluded) {
                    ++count.regular;
                    count.momentum += available.momentum_features ? 1U : 0U;
                    count.reversion += available.reversion_features ? 1U : 0U;
                    count.continuation += static_cast<std::size_t>(std::count(available.continuation_prefixes.begin(), available.continuation_prefixes.end(), true));
                }
            }
            auto& year = years[day.substr(0, 4)];
            year.regular += regular ? 1U : 0U;
            year.operational += common ? 1U : 0U;
            year.strict += strict ? 1U : 0U;
            year.halt_dates += regular && halt ? 1U : 0U;
        }
    }
    Json annual = Json::array(), feature_counts = Json::array();
    std::size_t qualified = 0;
    const auto& rules = contract_.document().at("eligibility");
    for (const auto& [year, count] : years) {
        const double fraction = count.regular == 0 ? 0 : static_cast<double>(count.operational) / static_cast<double>(count.regular);
        const bool development = year > "2016" && year < "2024";
        const bool passes = development && count.operational >= rules.at("minimum_common_development_dates_per_qualified_year").get<std::size_t>() &&
            fraction >= rules.at("minimum_common_development_calendar_coverage").get<double>();
        qualified += passes ? 1U : 0U;
        annual.push_back({{"year", year}, {"regular_dates", count.regular}, {"common_operational_dates", count.operational},
            {"parent_complete_grid_dates", count.strict}, {"operational_fraction", fraction}, {"qualifies_development_year", passes},
            {"known_halt_context_dates", count.halt_dates}});
    }
    for (const auto& [key, count] : features) {
        feature_counts.push_back({{"symbol", key.first}, {"year", key.second}, {"regular_non_diagnostic_dates", count.regular},
            {"momentum_feature_dates", count.momentum}, {"reversion_feature_dates", count.reversion},
            {"continuation_available_prefixes", count.continuation}, {"continuation_possible_prefixes", count.regular * 12}});
    }
    return {{"policy_sha256", policy.identity()}, {"parent_protocol_sha256", contract_.identity()},
        {"execution_minute_slots", policy.execution_slots()}, {"year_coverage", annual}, {"feature_coverage", feature_counts}, {"rows", availability_rows},
        {"qualified_operational_development_years", qualified}, {"minimum_development_years", rules.at("minimum_scored_years")},
        {"operational_year_gate_passed", qualified >= rules.at("minimum_scored_years").get<std::size_t>()},
        {"research_inputs_ready", false}, {"model_fit_availability_evaluated", false}, {"replay_adoption_pending", true}};
}

nlohmann::json HistoricalPanelDataset::collect(const PanelFetchers& fetchers, ApiRequestPolicy::Wait wait, const Progress& progress) const {
    if (!fetchers.calendar || !fetchers.bars || !fetchers.actions || !wait) {
        throw std::invalid_argument("panel collection requires calendar, bar, action and pacing callbacks");
    }
    std::filesystem::create_directories(directory_);
    const auto lock = contained_path(directory_, "acquisition.lock");
    // Reuse the existing platform owner lock. OS process exit releases it;
    // the empty file remains and never enters a data/evidence identity.
    [[maybe_unused]] const auto lease = std::filesystem::exists(lock) ? execution::open_journal_file(lock)
        : execution::create_journal_file(lock);
    return inspect(true, fetchers, std::move(wait), progress);
}
nlohmann::json HistoricalPanelDataset::audit(const Progress& progress) const {
    return inspect(false, {}, [](auto) {}, progress);
}
nlohmann::json HistoricalPanelDataset::inspect(bool acquire, const PanelFetchers& fetchers,
    ApiRequestPolicy::Wait wait, const Progress& progress) const {
    Json plan = Json::array();
    for (const auto& range : ranges_) { plan.push_back({{"first", range.first()}, {"last", range.last()}}); }
    const Json identity{{"schema_version", 1}, {"kind", "historical_panel_contract"}, {"protocol_sha256", contract_.identity()},
        {"evidence_origin", origin_ == PanelOrigin::provider ? "provider" : "synthetic"}, {"symbols", contract_.symbols()}, {"ranges", plan}};
    retain(contained_path(directory_, "dataset_contract.json"), identity, acquire);
    PanelPageArchive archive{directory_, acquire, std::move(wait)};
    Json months = Json::array(), actions = Json::array(), action_coverage = Json::array(), coverage = Json::array();
    std::set<int> years;
    for (const auto& range : ranges_) {
        const auto month = range.first().substr(0, 7);
        years.insert(std::stoi(month.substr(0, 4)));
        if (progress) { progress("Panel | " + month + " | authenticating calendar and five symbols"); }
        Json calendar_receipt;
        const auto days = calendar(range, archive, fetchers, calendar_receipt);
        Json checkpoints = Json::array();
        for (const auto& day : days) {
            bool common = true;
            for (const auto& symbol : contract_.symbols()) {
                auto [download, pages] = session(day, symbol, archive, fetchers);
                const auto record = session_record(day, download, pages, contract_);
                const auto name = checkpoint(symbol, day.date());
                retain(contained_path(directory_, name), record, acquire);
                checkpoints.push_back(reference(directory_, name));
                common = common && record.at("intraday_eligible").get<bool>();
                coverage.push_back({{"symbol", symbol}, {"date", day.date()}, {"regular", day.regular()},
                    {"expected_minutes", record.at("expected_minutes")}, {"observed_minutes", record.at("observed_minutes")},
                    {"missing_minutes", download.audit.missing_minutes.size()}, {"open_available", record.at("open_available")},
                    {"close_available", record.at("close_available")}, {"intraday_eligible", record.at("intraday_eligible")},
                    {"reserved", record.at("reserved")}, {"common_intraday_eligible", false}});
            }
            for (std::size_t i = coverage.size() - contract_.symbols().size(); i < coverage.size(); ++i) {
                coverage.at(i)["common_intraday_eligible"] = common;
            }
            if (progress) { progress("Panel | " + day.date() + " | five symbol-days retained"); }
        }
        const Json record{{"schema_version", 1}, {"month", month}, {"calendar", calendar_receipt}, {"checkpoints", checkpoints}};
        const auto name = "months/" + month + ".json";
        retain(contained_path(directory_, name), record, acquire);
        months.push_back(reference(directory_, name));
    }
    for (const auto& symbol : contract_.symbols()) {
        for (const int year : years) {
            if (progress) { progress("Panel | " + symbol + " | corporate actions " + std::to_string(year)); }
            const CorporateActionRequest request{symbol, std::to_string(year) + "-01-01", std::to_string(year) + "-12-31"};
            const auto record = action_interval(request, archive, fetchers);
            const auto name = "actions/" + symbol + '/' + std::to_string(year) + ".json";
            retain(contained_path(directory_, name), record, acquire);
            actions.push_back(reference(directory_, name));
            auto review = record;
            review.erase("pages");
            action_coverage.push_back(std::move(review));
        }
        if (years.contains(2025)) {
            const CorporateActionRequest request{symbol, "2026-01-01", "2026-03-31"};
            const auto record = action_interval(request, archive, fetchers);
            const auto name = "actions/" + symbol + "/2026_tail.json";
            retain(contained_path(directory_, name), record, acquire);
            actions.push_back(reference(directory_, name));
            auto review = record;
            review.erase("pages");
            action_coverage.push_back(std::move(review));
        }
    }
    const Json manifest{{"schema_version", 1}, {"kind", "historical_panel_acquisition"}, {"protocol_sha256", contract_.identity()},
        {"evidence_origin", identity.at("evidence_origin")}, {"dataset_contract", reference(directory_, "dataset_contract.json")},
        {"months", months}, {"actions", actions}, {"full_price_plan_authenticated", origin_ == PanelOrigin::provider && months.size() == 120},
        {"issuer_reconciled", false}, {"research_inputs_ready", false}, {"strategy_scored", false}};
    retain(contained_path(directory_, "acquisition_manifest.json"), manifest, acquire);
    return {{"schema_version", 1}, {"kind", "historical_panel_review"}, {"evidence_origin", identity.at("evidence_origin")},
        {"protocol_sha256", contract_.identity()}, {"acquisition_manifest_sha256", sha256_file(directory_ / "acquisition_manifest.json")},
        {"source_sha256", TRADING_SOURCE_SHA256}, {"compiler", TRADING_COMPILER_ID}, {"build_configuration", TRADING_BUILD_CONFIGURATION},
        {"network_requests", archive.network_requests()}, {"coverage", coverage}, {"months", months.size()},
        {"action_coverage", action_coverage},
        {"issuer_reconciled", false}, {"research_inputs_ready", false}, {"strategy_scored", false},
        {"status", "prices_acquired_action_reconciliation_required"}};
}
SessionDownload HistoricalPanelDataset::load_development_session(std::string_view symbol, std::string_view date,
    std::string_view expected_acquisition_sha256) const {
    contract_.require_development_access(date);
    const auto symbols = contract_.symbols();
    if (std::find(symbols.begin(), symbols.end(), symbol) == symbols.end()) { throw std::invalid_argument("symbol is outside the registered panel"); }
    const auto manifest_file = contained_path(directory_, "acquisition_manifest.json");
    if (sha256_file(manifest_file) != expected_acquisition_sha256) { throw std::invalid_argument("panel acquisition identity changed"); }
    const auto manifest = parse_json_document(read_text_file(manifest_file));
    const auto month_name = "months/" + std::string{date.substr(0, 7)} + ".json";
    const auto month = std::find_if(manifest.at("months").begin(), manifest.at("months").end(),
        [&](const auto& item) { return item.at("file").template get<std::string>() == month_name; });
    if (month == manifest.at("months").end() || sha256_file(contained_path(directory_, month_name)) != month->at("sha256").get<std::string>()) {
        throw std::invalid_argument("panel month identity changed");
    }
    const auto month_record = parse_json_document(read_text_file(contained_path(directory_, month_name)));
    const auto name = checkpoint(symbol, date);
    const auto found = std::find_if(month_record.at("checkpoints").begin(), month_record.at("checkpoints").end(),
        [&](const auto& item) { return item.at("file").template get<std::string>() == name; });
    if (found == month_record.at("checkpoints").end() || sha256_file(contained_path(directory_, name)) != found->at("sha256").get<std::string>()) {
        throw std::invalid_argument("panel session identity changed");
    }
    const auto record = parse_json_document(read_text_file(contained_path(directory_, name)));
    const auto opening = parse_utc_seconds(record.at("open_utc").get<std::string>());
    const auto closing = parse_utc_seconds(record.at("close_utc").get<std::string>());
    const HistoricalBarRequest request{std::string{symbol}, opening, closing};
    std::size_t index = 0;
    auto result = SessionDownloader{request}.download([&](const auto& req, const auto& token) {
        const auto& ref = record.at("pages").at(index++);
        const auto file = contained_path(directory_, ref.at("file").template get<std::string>());
        if (sha256_file(file) != ref.at("sha256").get<std::string>()) { throw std::invalid_argument("development page identity changed"); }
        for (const auto& attempt : ref.at("attempts")) {
            const auto attempt_file = contained_path(directory_, attempt.at("file").get<std::string>());
            if (sha256_file(attempt_file) != attempt.at("sha256").get<std::string>()) {
                throw std::invalid_argument("development retry evidence changed");
            }
        }
        const auto doc = parse_json_document(read_text_file(file, 192U * 1024U));
        const auto body = decode_response_bytes(doc.at("body_hex").template get<std::string>());
        if (doc.at("host") != "data.alpaca.markets" || doc.at("request_path") != req.path(token) ||
            doc.at("body_sha256") != sha256(body) || doc.at("http_status") != 200) {
            throw std::invalid_argument("development page provenance changed");
        }
        return MarketDataResponse{200, body};
    });
    const CalendarSession day{std::string{date}, "09:30", record.at("regular").get<bool>() ? "16:00" : "13:00"};
    if (index != record.at("pages").size() || record != session_record(day, result, record.at("pages"), contract_)) {
        throw std::invalid_argument("development checkpoint mismatch");
    }
    return result;
}
} // namespace trading
