#include "artifact_io.hpp"
#include "historical_capability.hpp"
#include "historical_capability_report.hpp"
#include "json_input.hpp"
#include "utc_timestamp.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace std::chrono;
using Json = nlohmann::json;
using namespace trading;

class ConstructedProvider {
public:
    bool quote_forbidden{false}, authentication_failure{false}, gapped_bars{false};
    std::size_t quote_calls{0};
    HistoricalCapabilityFetchers fetchers() {
        return {
            [&](const CalendarRange& range) {
                if (authentication_failure) { return MarketDataResponse{401, "{}", {}}; }
                auto day = parse_session_date(range.first()) + (range.first().substr(5, 2) == "01" ? days{3} : days{30});
                const auto backwards = range.first().substr(5, 2) == "12";
                while (weekday{day} == Saturday || weekday{day} == Sunday) { day += backwards ? days{-1} : days{1}; }
                return MarketDataResponse{200, Json::array({{{"date", format_utc_seconds(day).substr(0, 10)},
                    {"open", "09:30"}, {"close", "16:00"}}}).dump(), {}};
            },
            [&](const HistoricalBarRequest& r, const std::optional<std::string>& token) {
                const auto begin = token ? static_cast<std::size_t>(std::stoul(*token)) : 0U;
                const auto end = std::min(begin + r.page_size(), r.expected_minutes());
                Json values = Json::array();
                for (auto i = begin; i < end; ++i) {
                    if (gapped_bars && i == 10) { continue; }
                    values.push_back({{"t", format_utc_seconds(r.start() + minutes{static_cast<long long>(i)})},
                        {"o", 100}, {"h", 101}, {"l", 99}, {"c", 100}, {"v", 10}});
                }
                return MarketDataResponse{200, Json{{"bars", {{r.symbol(), values}}},
                    {"next_page_token", end < r.expected_minutes() ? Json(std::to_string(end)) : Json()}}.dump(), {}};
            },
            [&](const CorporateActionRequest& r, const std::optional<std::string>& token) {
                Json groups = Json::object();
                if (r.symbol() != "GLD") {
                    groups["cash_dividends"] = Json::array({{{"id", r.symbol() + r.first()}, {"symbol", r.symbol()},
                        {"process_date", r.first().substr(0, 4) + "-03-21"}, {"ex_date", r.first().substr(0, 4) + "-03-18"},
                        {"record_date", r.first().substr(0, 4) + "-03-21"},
                        {"payable_date", r.first().substr(0, 4) + "-03-31"}, {"rate", 0.5}}});
                }
                if (token) { throw std::logic_error("unexpected action continuation"); }
                return MarketDataResponse{200, Json{{"corporate_actions", groups}, {"next_page_token", nullptr}}.dump(), {}};
            },
            [&](const HistoricalTickRequest& r) {
                if (r.kind() == HistoricalTickKind::quotes) {
                    ++quote_calls;
                    if (quote_forbidden) { return MarketDataResponse{403, "{}", {}}; }
                }
                const auto stamp = format_utc_seconds(r.start());
                const auto t = stamp.substr(0, 19) + ".123456789Z";
                const auto kind = r.kind() == HistoricalTickKind::quotes ? "quotes" : "trades";
                Json point = r.kind() == HistoricalTickKind::quotes ? Json{{"t", t}, {"bp", 100}, {"ap", 101}, {"bs", 2}, {"as", 3}}
                    : Json{{"t", t}, {"p", 100}, {"s", 2}};
                return MarketDataResponse{200, Json{{kind, {{r.symbol(), Json::array({point})}}},
                    {"next_page_token", "more_ticks"}}.dump(), {}};
            }};
    }
};

class CapabilityChecks {
public:
    void run() {
        directory_ = create_unique_directory(std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch/runs", "historical_capability_check");
        check("legacy calendar request unchanged", CalendarRange{"2022-01-03", "2022-01-31"}.path() ==
            "/v2/calendar?start=2022-01-03&end=2022-01-31&date_type=TRADING");
        check("legacy SPY request unchanged", CalendarSession{"2023-01-03", "09:30", "16:00"}.bar_request().path() ==
            "/v2/stocks/bars?symbols=SPY&timeframe=1Min&start=2023-01-03T14%3A30%3A00Z&end=2023-01-03T20%3A59%3A00Z&limit=100&feed=sip&adjustment=raw&sort=asc&currency=USD&asof=-");
        check("legacy corporate action path unchanged", CorporateActionRequest{"2021-01-01", "2021-12-31"}.path() ==
            "/v1/corporate-actions?symbols=SPY&start=2021-01-01&end=2021-12-31&limit=100&data_quality=complete&sort=asc");
        rejects("legacy action date guard retained", [] { CorporateActionRequest r{"2016-01-01", "2016-12-31"}; });
        rejects("calendar before 2016 rejected", [] { static_cast<void>(parse_session_date("2015-12-31")); });
        rejects("calendar after 2025 rejected", [] { static_cast<void>(parse_session_date("2026-01-01")); });
        rejects("weekend calendar rejected", [] { CalendarSession s{"2016-01-02", "09:30", "16:00"}; });
        rejects("invalid symbol rejected", [] { CorporateActionRequest r{"SPY&feed=iex", "2016-01-01", "2016-12-31"}; });
        const CalendarSession winter{"2016-03-11", "09:30", "16:00"}, summer{"2016-03-14", "09:30", "16:00"};
        check("2016 DST winter", format_utc_seconds(winter.open_utc()) == "2016-03-11T14:30:00Z");
        check("2016 DST summer", format_utc_seconds(summer.open_utc()) == "2016-03-14T13:30:00Z");
        check("early close length", CalendarSession{"2016-11-25", "09:30", "13:00"}.bar_request("TLT").expected_minutes() == 210);
        check("explicit symbol retained", winter.bar_request("QQQ").symbol() == "QQQ");
        check("legacy holiday reference intact", development_calendar(CalendarRange{"2022-06-20", "2022-06-20"}).empty());
        rejects("legacy development guard retained", [] { static_cast<void>(development_calendar(CalendarRange{"2016-01-04", "2016-01-04"})); });
        const HistoricalTickRequest request{HistoricalTickKind::quotes, "IWM", winter.open_utc(), winter.open_utc() + minutes{1}};
        check("pagination encoding shared", request.path("a+b/c=").ends_with("&page_token=a%2Bb%2Fc%3D"));
        rejects("tick size bound", [&] { HistoricalTickRequest r{HistoricalTickKind::quotes, "SPY", winter.open_utc(), winter.open_utc()+minutes{2}}; });
        HistoricalCapabilityAudit audit;
        auto bad = Json{{"quotes", {{"QQQ", Json::array()}}}, {"next_page_token", nullptr}};
        rejects("unexpected tick symbol", [&] { static_cast<void>(audit.ticks(request, bad.dump())); });
        bad = {{"quotes", {{"IWM", Json::array({{{"t", "2016-03-11T14:32:00Z"}, {"bp", 1}, {"ap", 2}, {"bs", 1}, {"as", 1}}})}}}, {"next_page_token", nullptr}};
        rejects("tick outside interval", [&] { static_cast<void>(audit.ticks(request, bad.dump())); });
        rejects("malformed tick token", [&] { static_cast<void>(audit.ticks(request, Json{{"quotes", Json::object()}, {"next_page_token", ""}}.dump())); });
        const auto in_window = format_utc_seconds(request.start());
        bad = {{"quotes", {{"IWM", Json::array({{{"t", in_window}, {"bp", 2}, {"ap", 1}, {"bs", 1}, {"as", 1}}})}}}, {"next_page_token", nullptr}};
        check("crossed quote is not an executable reference", audit.ticks(request, bad.dump()).at("usable_records").get<int>() == 0);
        bad["quotes"]["IWM"][0]["bs"] = -1;
        rejects("negative quote size rejected", [&] { static_cast<void>(audit.ticks(request, bad.dump())); });
        const auto split = Json{{"corporate_actions", {{"forward_splits", Json::array({{{"id", "split1"}, {"symbol", "IWM"},
            {"process_date", "2016-06-01"}, {"ex_date", "2016-06-01"}, {"old_rate", 1}, {"new_rate", 2}}})}}}, {"next_page_token", nullptr}};
        check("split fields validated", audit.actions(CorporateActionRequest{"IWM", "2016-01-01", "2016-12-31"}, split.dump()).at("complete_split_fields").get<int>() == 1);
        auto repeated = split;
        repeated["corporate_actions"]["forward_splits"].push_back(split["corporate_actions"]["forward_splits"][0]);
        rejects("duplicate action identities rejected", [&] { static_cast<void>(audit.actions(CorporateActionRequest{"IWM", "2016-01-01", "2016-12-31"}, repeated.dump())); });
        ConstructedProvider fixture;
        const auto happy = probe("complete", fixture, {2016, 2017});
        check("five symbols times two years", happy.at("rows").size() == 10);
        check("sample route complete", happy.at("status") == Json("sampled_price_route_ready"));
        check("provisional common start", happy.at("provisional_common_price_start").get<int>() == 2016);
        check("sampling is not full coverage", !happy.at("full_history_verified").get<bool>() && !happy.at("research_inputs_ready").get<bool>());
        check("synthetic origin and zero network", happy.at("evidence_origin") == Json("synthetic") && happy.at("network_requests").get<int>() == 0);
        check("empty GLD actions remain explicit", happy.at("rows").at(4).at("actions").at("status") == Json("accessible_empty"));
        check("tick page continuation is not discarded", happy.at("rows").at(0).at("quotes").at("more_pages").get<bool>());
        check("payment fields counted", happy.at("rows").at(0).at("actions").at("complete_payment_fields").get<int>() == 1);
        fixture.quote_forbidden = true;
        fixture.quote_calls = 0;
        const auto denied = probe("quotes_denied", fixture, {2016, 2017});
        check("403 diagnosed once per symbol", fixture.quote_calls == 5);
        check("quote denial does not remove bar sample", denied.at("status") == Json("sampled_price_route_ready"));
        fixture.authentication_failure = true;
        const auto unauthorized = probe("authentication_failed", fixture, {2016, 2016});
        check("401 stops all further requests", unauthorized.at("request_attempts").get<int>() == 1);
        check("401 produces retained report", unauthorized.at("status") == Json("input_access_review_required"));
        fixture.authentication_failure = false;
        fixture.quote_forbidden = false;
        fixture.gapped_bars = true;
        const auto gaps = probe("gaps", fixture, {2016, 2016});
        check("missing minutes remain gaps", gaps.at("rows").at(0).at("bar_samples").at(0).at("missing_minutes").get<int>() == 1);
        check("gapped panel has no common start", gaps.at("provisional_common_price_start").is_null());
        fixture.gapped_bars = false;
        const auto sealed = probe("sealed", fixture, {2025, 2025});
        check("later responses use sealed boundary", sealed.at("attempts").at(0).at("raw_file").get<std::string>().starts_with("sealed/"));
        const CorporateActionRequest actions{"QQQ", "2016-01-01", "2016-12-31"};
        auto page = parse_json_document(fixture.fetchers().actions(actions, {}).body);
        page["corporate_actions"]["cash_dividends"][0].erase("payable_date");
        check("missing payment fields remain unavailable", audit.actions(actions, page.dump()).at("complete_payment_fields").get<int>() == 0);
        page["corporate_actions"]["cash_dividends"][0]["symbol"] = "SPY";
        rejects("wrong action symbol", [&] { static_cast<void>(audit.actions(actions, page.dump())); });
        int attempts = 0, observations = 0;
        const ApiRequestPolicy transient{[](auto) {}, true};
        const auto response = transient.get([&] { return MarketDataResponse{++attempts < 3 ? 503U : 200U, "{}", {}}; },
            [&](const auto&) { ++observations; });
        check("bounded transient retry and observation", response.status == 200 && attempts == 3 && observations == 3);
        attempts = 0;
        const ApiRequestPolicy legacy{[](auto) {}};
        static_cast<void>(legacy.get([&] { ++attempts; return MarketDataResponse{503, "{}", {}}; }));
        check("legacy server policy unchanged", attempts == 1);
        attempts = 0;
        rejects("429 retry bound", [&] { static_cast<void>(transient.get([&] { ++attempts; return MarketDataResponse{429, "{}", {}}; })); });
        check("429 attempts bounded to three", attempts == 3);
        const auto storage = directory_ / "storage_failure";
        std::filesystem::create_directory(storage);
        auto fetchers = fixture.fetchers();
        const auto old_calendar = fetchers.calendar;
        fetchers.calendar = [&](const auto& range) {
            write_new_text_file(storage / "responses/0.json", "preexisting");
            return old_calendar(range);
        };
        rejects("response archival failure stops the probe", [&] {
            static_cast<void>(HistoricalCapabilityProbe{{2016, 2016}, CapabilityOrigin::synthetic, [](auto) {}}.run(storage, fetchers));
        });
        check("storage failure cannot publish a completed summary", !std::filesystem::exists(storage / "summary.json"));
        write_new_text_file(directory_ / "checks.json", Json{{"evidence_origin", "synthetic"},
            {"network_requests", 0}, {"checks", labels_}}.dump(2) + '\n');
        std::cout << "Historical capability | constructed contracts\nChecks passed: " << labels_.size()
                  << "\nNetwork requests sent: 0\nEvidence: " << directory_.string() << '\n';
    }
private:
    void check(std::string label, bool condition) {
        if (!condition) { throw std::runtime_error("capability check failed: " + label); }
        labels_.push_back(std::move(label));
    }
    template<class F> void rejects(std::string label, F operation) {
        bool rejected = false;
        try { operation(); } catch (const std::exception&) { rejected = true; }
        check(std::move(label), rejected);
    }
    Json probe(const std::string& name, ConstructedProvider& fixture, HistoricalCapabilityPlan plan) {
        const auto path = directory_ / name;
        std::filesystem::create_directory(path);
        const auto result = HistoricalCapabilityProbe{plan, CapabilityOrigin::synthetic, [](auto) {}}.run(path, fixture.fetchers());
        HistoricalCapabilityReport{}.write(path, result);
        return result;
    }
    std::filesystem::path directory_;
    std::vector<std::string> labels_;
};
} // namespace
int main() {
    try { CapabilityChecks{}.run(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
