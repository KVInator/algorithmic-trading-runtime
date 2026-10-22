#include "api_request_policy.hpp"
#include "artifact_io.hpp"
#include "batch_momentum.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include "session_storage.hpp"
#include "session_batch.hpp"
#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

using Json = nlohmann::json;
using trading::parse_utc_seconds;

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

template <typename Operation>
void rejects(Operation operation, std::string_view message) {
    try { operation(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error(std::string{message});
}

void near(double actual, double expected, std::string_view message) {
    require(std::abs(actual - expected) < 3e-15, message);
}

void overwrite(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file << text;
    file.close();
}

void test_calendar() {
    for (const auto& [date, opening] : std::vector<std::pair<std::string, std::string>>{
        {"2022-03-11", "14:30"}, {"2022-03-14", "13:30"},
        {"2022-11-04", "13:30"}, {"2022-11-07", "14:30"},
        {"2023-03-10", "14:30"}, {"2023-03-13", "13:30"},
        {"2024-02-29", "14:30"}, {"2024-03-08", "14:30"},
        {"2024-03-11", "13:30"}, {"2024-11-01", "13:30"},
        {"2024-11-04", "14:30"}, {"2025-03-07", "14:30"},
        {"2025-03-10", "13:30"}, {"2025-11-03", "14:30"}}) {
        const trading::CalendarSession session{date, "09:30", "16:00:00"};
        require(session.regular() && session.open_utc() == parse_utc_seconds(date + "T" + opening + ":00Z") &&
            session.close_utc() - session.open_utc() == std::chrono::minutes{390}, "calendar UTC clock mismatch");
    }
    const trading::CalendarSession short_day{"2024-11-29", "09:30", "13:00"};
    require(!short_day.regular() && short_day.bar_request().expected_minutes() == 210,
            "short calendar sessions must remain explicitly distinguishable");
    const trading::CalendarRange range{"2022-03-01", "2022-03-31"};
    require(range.path() == "/v2/calendar?start=2022-03-01&end=2022-03-31&date_type=TRADING", "calendar request path mismatch");
    for (const auto* date : {"2021-12-31", "2026-01-02", "2023-02-29", "2022-03-12"}) {
        rejects([&] { static_cast<void>(trading::CalendarSession{date, "09:30", "16:00"}); }, "unsupported date accepted");
    }
    rejects([] { static_cast<void>(trading::CalendarRange{"2022-03-01", "2022-04-01"}); }, "oversized range accepted");
    rejects([] { static_cast<void>(trading::CalendarSession{"2022-03-11", "09:30:01", "16:00"}); }, "sub-minute clock accepted");
    rejects([] { static_cast<void>(trading::CalendarSession{"2022-03-11", "08:30", "16:00"}); }, "extended session accepted");
    const Json record{{"date", "2022-03-11"}, {"open", "09:30"}, {"close", "16:00"}};
    rejects([&] { static_cast<void>(trading::decode_market_calendar(Json::array({record, record}).dump(), range)); }, "duplicate dates accepted");
    rejects([&] { static_cast<void>(trading::decode_market_calendar("[]", range)); }, "empty calendar accepted");
    rejects([&] { static_cast<void>(trading::decode_market_calendar("[{\"date\":\"2022-03-11\",\"open\":930,\"close\":\"16:00\"}]", range)); }, "numeric clock accepted");
    rejects([] { static_cast<void>(trading::parse_json_document("{\"a\":1,\"a\":2}")); }, "duplicate JSON key accepted");
    rejects([] { static_cast<void>(trading::parse_json_document(std::string(70, '[') + "0" + std::string(70, ']'))); },
            "excessive JSON nesting accepted");
}

void test_policy_and_hashes() {
    require(trading::sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty SHA-256 mismatch");
    require(trading::sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc SHA-256 mismatch");
    std::vector<std::chrono::milliseconds> waits;
    const trading::ApiRequestPolicy policy{[&](auto duration) { waits.push_back(duration); }};
    unsigned int calls = 0;
    const auto response = policy.get([&] {
        ++calls;
        return calls == 1 ? trading::MarketDataResponse{429, "rate limit", "3"} : trading::MarketDataResponse{200, "ok"};
    });
    require(response.status == 200 && calls == 2 && waits == std::vector<std::chrono::milliseconds>{
        std::chrono::milliseconds{750}, std::chrono::milliseconds{3000}, std::chrono::milliseconds{750}}, "pacing/retry contract mismatch");
    calls = 0;
    rejects([&] { static_cast<void>(policy.get([&] { ++calls; return trading::MarketDataResponse{429, "limited"}; })); }, "unbounded retry accepted");
    require(calls == 3, "HTTP 429 must stop at three attempts");
    for (const auto* header : {"31", "Wed, 21 Oct 2026 00:00:00 GMT", "-1", ""}) {
        calls = 0;
        rejects([&] { static_cast<void>(policy.get([&] { ++calls; return trading::MarketDataResponse{429, "limited", header}; })); }, "unsupported retry delay accepted");
        require(calls == 1, "unsupported Retry-After must stop before another request");
    }
    calls = 0;
    require(policy.get([&] { ++calls; return trading::MarketDataResponse{403, "forbidden"}; }).status == 403 && calls == 1,
            "non-rate-limit failures must not be retried");
}

std::string calendar_body() {
    return R"([{"date":"2022-03-11","open":"09:30","close":"16:00"},
                {"date":"2022-03-14","open":"09:30","close":"16:00"},
                {"date":"2022-03-15","open":"09:30","close":"13:00"}])";
}

trading::MarketDataResponse synthetic_page(const trading::HistoricalBarRequest& request,
                                          const std::optional<std::string>& token, bool omit = false) {
    const auto begin = token ? std::stoi(*token) : 0;
    const auto end = std::min(begin + 100, 390);
    Json bars = Json::array();
    const auto positive = trading::format_utc_seconds(request.start()).starts_with("2022-03-11");
    for (int i = begin; i < end; ++i) {
        if (omit && i == 17) { continue; }
        double open = 100;
        if (i == 386) { open = positive ? 102 : 95; }
        if (i == 387) { open = positive ? 103 : 96; }
        const auto close = i == 359 ? (positive ? 101.0 : 99.0) : 100.0;
        bars.push_back({{"t", trading::format_utc_seconds(request.start() + std::chrono::minutes{i})},
                       {"o", open}, {"h", std::max(open, close) + 1}, {"l", std::min(open, close) - 1},
                       {"c", close}, {"v", 1000}});
    }
    return {200, Json{{"bars", {{"SPY", bars}}},
                      {"next_page_token", end < 390 ? Json(std::to_string(end)) : Json(nullptr)}}.dump()};
}

void test_batch(const std::filesystem::path& output) {
    const auto root = output / "resumable";
    const trading::SessionBatch batch{{"2022-03-11", "2022-03-15"}, 3, root};
    int calendar_calls = 0;
    const auto fetch_calendar = [&](const trading::CalendarRange&) {
        ++calendar_calls; return trading::MarketDataResponse{200, calendar_body()};
    };
    int first_day_calls = 0;
    rejects([&] { static_cast<void>(batch.acquire(fetch_calendar, [&](const auto& request, const auto& token) {
        if (trading::format_utc_seconds(request.start()).starts_with("2022-03-14")) {
            throw std::runtime_error("injected network interruption");
        }
        ++first_day_calls;
        return synthetic_page(request, token);
    })); }, "interrupted batch was published");
    require(calendar_calls == 1 && first_day_calls == 4 &&
            std::filesystem::exists(root / "checkpoints/2022-03-11.json") &&
            !std::filesystem::exists(root / "manifest.json"), "completed first day was not checkpointed");
    // A crash between the session commit marker and its checkpoint must also
    // recover offline, without downloading the completed session again.
    const auto orphan_root = output / "orphan";
    std::filesystem::copy(root, orphan_root, std::filesystem::copy_options::recursive);
    std::filesystem::remove(orphan_root / "checkpoints/2022-03-11.json");
    auto fetch_bars = [&](const auto& request, const auto& token) {
        if (trading::format_utc_seconds(request.start()).starts_with("2022-03-11")) { ++first_day_calls; }
        return synthetic_page(request, token);
    };
    const auto resumed = batch.acquire(fetch_calendar, fetch_bars);
    static_cast<void>(trading::SessionBatch{{"2022-03-11", "2022-03-15"}, 3, orphan_root}.acquire(fetch_calendar, fetch_bars));
    require(calendar_calls == 1 && first_day_calls == 4 && resumed.sessions.size() == 2 &&
            resumed.excluded_dates == std::vector<std::string>{"2022-03-15"}, "resume reused the wrong dates or fetched completed data");
    const auto offline = batch.acquire({}, {});
    require(offline.manifest_sha256 == resumed.manifest_sha256, "completed batch must verify without fetch callbacks");
    const auto cases = trading::BatchMomentumReplay{}.run(offline.sessions);
    require(cases.size() == 10 && cases[2].active_days == 1, "fixed matrix or cash-day count mismatch");
    // Day one: 100 -> 102. Day two: cash; control 100 -> 95.
    const auto active_net = 101.9898 / 100.01 - 1.0;
    const auto inactive_control = 94.9905 / 100.01 - 1.0;
    near(cases[2].strategy_mean_daily, active_net / 2, "cash days must remain in daily means");
    near(*cases[2].strategy_mean_net_active, active_net, "active-trade mean denominator mismatch");
    near(cases[2].always_long_mean_daily, (active_net + inactive_control) / 2, "control must include cash dates");
    near(cases[2].strategy_compounded, active_net, "cash days must preserve strategy wealth");
    near(*cases[2].signal_contrast_gross, 0.07, "positive/nonpositive gross contrast mismatch");
    const auto csv = trading::format_momentum_daily_csv(cases);
    require(csv.starts_with("date,delay_minutes,cost_bps_per_side,signal_return,active,") &&
            std::count(csv.begin(), csv.end(), '\n') == 21,
            "daily research CSV must retain all dates for every declared case");
    const auto cash_cases = trading::BatchMomentumReplay{}.run(std::span<const trading::SessionDownload>{offline.sessions}.subspan(1));
    require(!cash_cases[2].strategy_mean_net_active && !cash_cases[2].signal_contrast_gross,
            "absent signal groups must remain undefined");

    const auto checkpoint = trading::parse_json_document(trading::read_text_file(root / "checkpoints/2022-03-11.json"));
    const auto session = trading::contained_path(root, checkpoint.at("session_directory").get<std::string>());
    for (const auto* filename : {"bars.csv", "metadata.json", "page_001.json"}) {
        const auto path = session / filename;
        const auto original = trading::read_text_file(path);
        overwrite(path, original + " ");
        rejects([&] { static_cast<void>(batch.load()); }, "modified session artifact passed verification");
        overwrite(path, original);
    }
    const auto csv_path = session / "bars.csv";
    const auto csv_original = trading::read_text_file(csv_path);
    auto changed = csv_original;
    const auto price = changed.find(",100,", changed.find('\n'));
    require(price != std::string::npos, "fixture price not found");
    changed.replace(price, 5, ",100.5,");
    overwrite(csv_path, changed);
    rejects([&] { static_cast<void>(trading::load_session_download(session, offline.sessions[0].request)); },
            "plausible CSV prices disagreeing with raw pages passed reconstruction");
    overwrite(csv_path, csv_original);
    for (const auto* filename : {"calendar_snapshot.json", "checkpoints/2022-03-11.json", "manifest.json"}) {
        const auto path = root / filename;
        const auto original = trading::read_text_file(path);
        overwrite(path, "{}");
        rejects([&] { static_cast<void>(batch.load()); }, "modified dataset provenance passed verification");
        overwrite(path, original);
    }
    rejects([&] { static_cast<void>(trading::contained_path(root, "../outside")); }, "relative path traversal accepted");
    rejects([&] { static_cast<void>(trading::contained_path(root, "..\\outside")); }, "Windows path traversal accepted");
    rejects([&] { static_cast<void>(trading::contained_path(root, std::filesystem::absolute(root).generic_string())); }, "absolute reference accepted");
    require(batch.load().sessions.size() == 2, "restored dataset no longer verifies");
}

void test_missing_grid(const std::filesystem::path& output) {
    const auto root = output / "missing-grid";
    const trading::SessionBatch batch{{"2022-03-11", "2022-03-11"}, 1, root};
    const auto calendar = [](const trading::CalendarRange&) {
        return trading::MarketDataResponse{200, R"([{"date":"2022-03-11","open":"09:30","close":"16:00"}])"};
    };
    rejects([&] { static_cast<void>(batch.acquire(calendar, [](const auto& request, const auto& token) {
        return synthetic_page(request, token, true);
    })); }, "missing minute grid was published");
    require(!std::filesystem::exists(root / "manifest.json"), "missing grid must block the manifest");
    const auto complete = batch.acquire({}, [](const auto& request, const auto& token) {
        return synthetic_page(request, token);
    });
    require(complete.sessions.size() == 1 && complete.sessions[0].bars.size() == 390, "incomplete session did not recover");
    const auto cases = trading::BatchMomentumReplay{}.run(complete.sessions);
    require(!cases[2].signal_contrast_gross, "all-positive signals must have undefined contrast");
}

std::filesystem::path test_full_month(const std::filesystem::path& output) {
    Json calendar = Json::array();
    std::size_t winter = 0;
    std::size_t summer = 0;
    for (int day = 1; day <= 31; ++day) {
        const auto date = "2022-03-" + (day < 10 ? std::string{"0"} : std::string{}) + std::to_string(day);
        const auto weekday = std::chrono::weekday{trading::parse_session_date(date)};
        if (weekday == std::chrono::Saturday || weekday == std::chrono::Sunday) { continue; }
        calendar.push_back({{"date", date}, {"open", "09:30"}, {"close", "16:00"}});
    }
    const auto project = output / "synthetic-cli-project";
    const auto root = project / "scratch/datasets/SPY_1Min_SIP_2022-03";
    const trading::SessionBatch batch{{"2022-03-01", "2022-03-31"}, 23, root};
    const auto frozen = batch.acquire([&](const auto&) { return trading::MarketDataResponse{200, calendar.dump()}; },
        [](const auto& request, const auto& token) { return synthetic_page(request, token); });
    for (const auto& session : frozen.sessions) {
        const auto clock = trading::format_utc_seconds(session.request.start()).substr(11, 5);
        winter += clock == "14:30" ? 1U : 0U;
        summer += clock == "13:30" ? 1U : 0U;
    }
    require(winter == 9 && summer == 14 && frozen.sessions.size() * 390 == 8970,
            "March count or DST partition mismatch");
    require(trading::BatchMomentumReplay{}.run(frozen.sessions).front().days.size() == 23,
            "month replay lost a date");
    const auto bad_root = output / "bad-calendar-count";
    calendar.erase(calendar.begin());
    rejects([&] { static_cast<void>(trading::SessionBatch{{"2022-03-01", "2022-03-31"}, 23, bad_root}.acquire(
        [&](const auto&) { return trading::MarketDataResponse{200, calendar.dump()}; }, {})); },
        "truncated month calendar accepted");
    require(!std::filesystem::exists(bad_root / "calendar_snapshot.json"), "invalid calendar was committed");
    return project;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) { throw std::invalid_argument("batch tests require an explicit artifact root"); }
        const auto output = trading::create_unique_directory(argv[1], "contracts");
        test_calendar();
        test_policy_and_hashes();
        test_batch(output);
        test_missing_grid(output);
        std::cout << "Synthetic CLI fixture: " << test_full_month(output).string() << '\n';
        std::cout << "Batch pipeline contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Batch pipeline test failed: " << error.what() << '\n';
        return 1;
    }
}
