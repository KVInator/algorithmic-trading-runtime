#include "risk_input_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
template<class F> void rejects(F f) {
    bool failed = false;
    try { f(); } catch (const std::exception&) { failed = true; }
    require(failed, "invalid risk evidence must be rejected");
}
Json dividend(std::string id, std::string ex, std::string pay, std::string process, double rate = 1) {
    return {{"id", id}, {"symbol", "SPY"}, {"ex_date", ex}, {"record_date", ex},
        {"payable_date", pay}, {"process_date", process}, {"rate", rate}};
}
std::string page(Json rows, Json token = nullptr) {
    return Json{{"corporate_actions", {{"cash_dividends", rows}}}, {"next_page_token", token}}.dump();
}
void test_plan_and_calendar() {
    const auto plan = trading::risk_price_input_plan();
    require(plan.size() == 256 && plan.front().date == "2020-12-31", "calibration includes a leading close and the three shortened sessions");
    require(std::count_if(plan.begin(), plan.end(), [](const auto& p) { return p.role == "warmup_full"; }) == 20,
        "twenty earlier full sessions initialize the risk history");
    const auto total = std::accumulate(plan.begin(), plan.end(), std::size_t{0}, [](auto sum, const auto& p) { return sum + p.request.expected_minutes(); });
    require(total == 8663, "bounded price plan requests only necessary calibration references");
    for (const auto& p : plan) { require(p.date < "2024-01-01", "acquisition cannot read later-period prices"); }
    const auto warm = std::find_if(plan.begin(), plan.end(), [](const auto& p) { return p.role == "warmup_full"; });
    require(warm->date == "2021-12-03", "the leading twenty exchange dates are selected before acquisition");
    const auto shortened = std::find_if(plan.begin(), plan.end(), [](const auto& p) { return p.date == "2021-11-26"; });
    require(shortened->request.start() == trading::parse_utc_seconds("2021-11-26T17:59:00Z"), "shortened calibration date uses its actual last minute");
    require(trading::risk_calibration_calendar({"2021-06-01", "2021-06-30"}).size() == 22,
        "NYSE Juneteenth closure was not introduced in 2021");
    require(trading::risk_calibration_calendar({"2021-12-31", "2021-12-31"}).size() == 1,
        "year-end Friday remains an exchange session");
    rejects([] { static_cast<void>(trading::risk_calibration_calendar({"2022-01-01", "2022-01-31"})); });
}
void test_actions() {
    trading::CorporateActionHistory history;
    auto tail = dividend("tail", "2023-12-15", "2024-01-31", "2024-01-31", 1.906073);
    tail["record_date"] = "2023-12-18";
    require(!history.append_page(page(Json::array({tail})), "2024-01-01", "2024-03-31"), "year-end entitlement is loaded from its later process interval");
    const trading::CashDistribution reference{"issuer", "2023-12-15", "2023-12-18", "2024-01-31", "", 1.906073};
    require(history.reconcile(std::vector{reference}).empty(), "independent ex-date, payable date and amount agree");
    auto mismatch = reference; mismatch.usd_per_share += .01;
    require(history.reconcile(std::vector{mismatch}).size() == 1, "amount mismatches cannot certify an action ledger");
    trading::CorporateActionHistory missing;
    require(missing.reconcile(std::vector{reference}).size() == 1, "empty process history does not remove a terminal entitlement");
    rejects([&] { static_cast<void>(history.append_page(page(Json::array({tail})), "2024-01-01", "2024-03-31")); });
    require(history.distributions().size() == 1, "invalid duplicate page leaves the history unchanged");
    rejects([&] { static_cast<void>(missing.append_page(page(Json::array({tail})), "2023-01-01", "2023-12-31")); });
    auto bad = tail; bad["rate"] = -1;
    rejects([&] { static_cast<void>(missing.append_page(page(Json::array({bad})), "2024-01-01", "2024-03-31")); });
    require(missing.distributions().empty(), "invalid action page has no partial state");
    auto split = tail; split["id"] = "split";
    const auto unsupported = Json{{"corporate_actions", {{"forward_splits", Json::array({split})}}}, {"next_page_token", nullptr}};
    static_cast<void>(missing.append_page(unsupported.dump(), "2024-01-01", "2024-03-31"));
    require(missing.unsupported_types().size() == 1, "unsupported actions remain explicit blockers");
    const trading::CorporateActionRequest request{"2024-01-01", "2024-03-31"};
    require(request.path("a+/=").ends_with("page_token=a%2B%2F%3D"), "action tokens are query-encoded");
    rejects([] { static_cast<void>(trading::CorporateActionRequest{"2024-04-01", "2024-12-31"}); });
    rejects([&] { static_cast<void>(missing.append_page("{}", "2021-01-01", "2021-12-31")); });
}

trading::MarketDataResponse calendar(const trading::CalendarRange& range) {
    auto rows = Json::array();
    for (const auto& s : trading::risk_calibration_calendar(range)) {
        rows.push_back({{"date", s.date()}, {"open", "09:30"}, {"close", s.regular() ? "16:00" : "13:00"}});
    }
    return {200, rows.dump()};
}
trading::MarketDataResponse bars(const trading::HistoricalBarRequest& request, const std::optional<std::string>& token) {
    const auto first = token ? static_cast<std::size_t>(std::stoul(*token)) : 0;
    const auto last = std::min(first + request.page_size(), request.expected_minutes());
    auto rows = Json::array();
    for (auto i = first; i < last; ++i) {
        if (i == 4 && trading::format_utc_seconds(request.start()).starts_with("2021-12-03")) { continue; }
        const auto time = request.start() + std::chrono::minutes{static_cast<std::chrono::minutes::rep>(i)};
        rows.push_back({{"t", trading::format_utc_seconds(time)}, {"o", 100}, {"h", 100}, {"l", 100}, {"c", 100}, {"v", 1000}});
    }
    Json body{{"bars", {{"SPY", rows}}}, {"next_page_token", last < request.expected_minutes() ? Json(std::to_string(last)) : Json(nullptr)}};
    return {200, body.dump()};
}
trading::MarketDataResponse actions(const trading::CorporateActionRequest& request, const std::optional<std::string>& token) {
    const auto ref = Json::parse(trading::read_text_file(std::filesystem::path{TRADING_PROJECT_ROOT} / "research/spy_distribution_reference.json"));
    auto rows = Json::array();
    for (const auto& item : ref.at("cash_distributions")) {
        const auto pay = item.at("payable_date").get<std::string>();
        if (request.path().find("start=" + pay.substr(0,4)) == std::string::npos) { continue; }
        auto row = dividend("constructed-" + item.at("ex_date").get<std::string>(), item.at("ex_date").get<std::string>(), pay, pay,
            item.at("usd_per_share").get<double>());
        row["record_date"] = item.at("record_date");
        rows.push_back(row);
    }
    if (request.path().find("start=2021") != std::string::npos) {
        if (!token) { return {200, page(Json::array({rows.at(0)}), "next")}; }
        rows.erase(rows.begin());
    }
    return {200, page(rows)};
}
void overwrite(const std::filesystem::path& file, const std::string& content) {
    std::ofstream out{file, std::ios::binary | std::ios::trunc}; out.exceptions(std::ios::failbit | std::ios::badbit); out << content;
}
void test_collection(const std::filesystem::path& root) {
    const trading::RiskInputDataset dataset{root / "inputs", std::filesystem::path{TRADING_PROJECT_ROOT} / "research/spy_distribution_reference.json"};
    rejects([&] { static_cast<void>(dataset.collect(calendar, [&](const auto& r, const auto& t) {
        if (trading::format_utc_seconds(r.start()).starts_with("2021-01-05")) { throw std::runtime_error("constructed disconnect"); }
        return bars(r,t);
    }, actions)); });
    std::size_t requests = 0;
    const auto result = dataset.collect(calendar, [&](const auto& r, const auto& t) { ++requests; return bars(r,t); }, actions);
    require(requests == 320 && result.prices.size() == 256, "interrupted acquisition resumes without repeating completed price windows");
    require(result.actions.distributions().size() == 12 && result.cash_distributions_reconciled, "all action pages and the process-date tail are retained");
    const auto gap = std::find_if(result.prices.begin(), result.prices.end(), [](const auto& row) { return row.input.date == "2021-12-03"; });
    require(!result.required_evidence_complete && gap->observed_minutes == 389 && std::any_of(result.blockers.begin(), result.blockers.end(), [](const auto& text) {
        return text.find("Incomplete warmup_full on 2021-12-03") != std::string::npos;
    }), "a calibration gap remains explicit and cannot be treated as a zero return");
    const auto again = dataset.collect([](const auto&) -> trading::MarketDataResponse { throw std::runtime_error("unexpected calendar request"); },
        [](const auto&, const auto&) -> trading::MarketDataResponse { throw std::runtime_error("unexpected price request"); },
        [](const auto&, const auto&) -> trading::MarketDataResponse { throw std::runtime_error("unexpected action request"); });
    require(again.evidence_hashes == result.evidence_hashes, "rerun is read-only for retained acquisition evidence");
    const auto offline = dataset.load();
    require(offline.evidence_hashes == result.evidence_hashes, "offline verifier reconstructs the exact acquisition inventory");
    const auto supplements = dataset.supplementary_sessions();
    require(supplements.size() == 3 && supplements[0].bars.size() == 210, "shortened supplements retain full actual sessions");
    trading::RiskPreflightResult preflight;
    for (const auto& session : supplements) {
        const auto date = trading::format_utc_seconds(session.request.start()).substr(0,10);
        auto rows = trading::audit_risk_session_coverage(std::vector{trading::CalendarSession{date,"09:30","13:00"}}, std::vector{session});
        preflight.coverage.push_back(rows[0]);
    }
    const auto report = trading::save_risk_input_report(result, preflight, root / "reports");
    const auto summary = Json::parse(trading::read_text_file(report / "summary.json"));
    require(summary.at("strategy_scored") == false && summary.at("counts").at("account_reference_sessions") == 3,
        "report publishes coverage without a historical strategy score");
    for (const auto& a : summary.at("artifacts")) {
        require(trading::sha256_file(report / a.at("file").get<std::string>()) == a.at("sha256").get<std::string>(), "report artifacts retain valid digests");
    }
    const auto action_file = root / "inputs/actions/2021/page_002.json";
    auto doc = Json::parse(trading::read_text_file(action_file));
    const auto saved = doc;
    doc["body"] = page(Json::array(), "next"); doc["body_sha256"] = trading::sha256(doc.at("body").get<std::string>());
    overwrite(action_file, doc.dump());
    rejects([&] { static_cast<void>(dataset.load()); });
    overwrite(action_file, saved.dump(2) + '\n');
    const auto price_file = result.prices.front().directory / "bars.csv";
    overwrite(price_file, trading::read_text_file(price_file) + '\n');
    rejects([&] { static_cast<void>(dataset.load()); });

    const trading::RiskInputDataset denied{root / "denied", std::filesystem::path{TRADING_PROJECT_ROOT} / "research/spy_distribution_reference.json"};
    const auto failure = denied.collect(calendar, bars, [](const auto& r, const auto& token) {
        return r.path().find("start=2022") != std::string::npos ? trading::MarketDataResponse{403, "subscription denied"} : actions(r,token);
    });
    require(!failure.cash_distributions_reconciled && std::any_of(failure.blockers.begin(), failure.blockers.end(), [](const auto& s) {
        return s.find("HTTP 403") != std::string::npos;
    }), "HTTP denial remains evidence and blocks reconciliation");
    require(denied.load().evidence_hashes == failure.evidence_hashes, "rejected action responses remain reproducible on offline reload");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) { throw std::invalid_argument("risk input contracts require an output directory"); }
        test_plan_and_calendar(); test_actions();
        const auto run = trading::create_unique_directory(argv[1], "risk-input-contracts");
        test_collection(run);
        std::cout << "Risk input contracts passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "Risk input contract failure: " << error.what() << '\n'; return 1; }
}
