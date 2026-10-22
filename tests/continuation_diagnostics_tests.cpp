#include "continuation_study_report.hpp"
#include "artifact_io.hpp"
#include "market_calendar.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace std::chrono;

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

void near(double actual, double expected, std::string_view message) {
    require(std::isfinite(actual) && std::abs(actual - expected) < 2e-11, message);
}

template<class Function>
void rejects(Function operation, std::string_view message) {
    try { operation(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error(std::string{message});
}

std::string calendar_text() {
    return trading::read_text_file(std::filesystem::path{TRADING_PROJECT_ROOT} / "research/scheduled_releases_2022_2023.csv");
}

void test_calendar() {
    const trading::ScheduledReleaseCalendar calendar{calendar_text()};
    require(calendar.records().size() == 64, "complete 64-release calendar");
    std::map<std::string, std::array<std::size_t, 3>> counts;
    for (const auto& row : calendar.records()) {
        const auto index = row.kind == trading::ScheduledRelease::cpi ? 0U : row.kind == trading::ScheduledRelease::employment ? 1U : 2U;
        ++counts[row.date.substr(0, 4)][index];
    }
    for (const auto& year : {"2022", "2023"}) {
        require(counts.at(year) == std::array<std::size_t, 3>{12, 12, 8}, "annual calendar coverage");
    }
    require(calendar.flags("2022-10-13") == 1 && calendar.flags("2022-11-30") == 0,
        "CPI label is distinct from an unselected speech date");
    require(calendar.flags("2023-04-07") == 2, "closed-market employment release is retained");
    require(calendar.flags("2023-09-13") == 1 && calendar.flags("2023-09-12") == 0, "September CPI date is the 13th");
    const std::string header = "date,event,time_new_york,source_url\n";
    const std::string cpi = "2022-02-10,cpi,08:30,https://www.bls.gov/schedule/2022/home.htm\n";
    const std::string employment = "2022-02-10,employment,08:30,https://www.bls.gov/schedule/2022/home.htm\n";
    require(trading::ScheduledReleaseCalendar{header + cpi + employment}.flags("2022-02-10") == 3,
        "overlapping types form a combined flag, not duplicate sample rows");
    require(trading::scheduled_release_label(3) == "CPI + Employment", "combined label");
    rejects([&] { static_cast<void>(trading::ScheduledReleaseCalendar{header + cpi + cpi}); }, "duplicate event rejected");
    rejects([&] { static_cast<void>(trading::ScheduledReleaseCalendar{header + "2022-02-30,cpi,08:30,https://www.bls.gov/schedule/2022/home.htm\n"}); }, "invalid date rejected");
    auto wrong_time = cpi;
    wrong_time.replace(wrong_time.find("08:30"), 5, "14:00");
    rejects([&] { static_cast<void>(trading::ScheduledReleaseCalendar{header + wrong_time}); }, "wrong release time rejected");
}

struct Fixture {
    trading::ContinuationStudyResult study{};
    std::vector<trading::SessionDownload> sessions;
};

Fixture fixture(bool two_months = false) {
    auto dates = trading::development_calendar({"2022-02-01", "2022-02-28"});
    if (two_months) {
        const auto march = trading::development_calendar({"2022-03-01", "2022-03-31"});
        for (std::size_t i = 0; i < 6; ++i) { dates[i + 6] = march[i]; }
    }
    Fixture result;
    for (std::size_t day = 0; day < 12; ++day) {
        const auto request = dates[day].bar_request();
        std::vector<trading::MinuteBar> bars;
        for (int minute = 0; minute < 390; ++minute) {
            double open = 100;
            if (minute == 61 || minute == 121) { open = 110; }
            if (minute == 151) { open = 120; }
            if (minute == 211) { open = 90; }
            if (minute == 271) { open = 105; }
            bars.emplace_back("SPY", request.start() + minutes{minute},
                trading::Ohlc{open, std::max(open, 101.0), std::min(open, 101.0), 101}, 1000);
        }
        trading::ContinuationDay record{};
        record.date = dates[day].date();
        const auto trade = [&](int entry, int exit) {
            const double entry_price = bars[static_cast<std::size_t>(entry)].prices().open;
            const double exit_price = bars[static_cast<std::size_t>(exit)].prices().open;
            return trading::ScheduledRoundTrip{request.start() + minutes{entry - 1}, request.start() + minutes{entry},
                request.start() + minutes{exit - 1}, request.start() + minutes{exit}, entry_price, exit_price,
                trading::evaluate_long_prices(entry_price, exit_price, 1)};
        };
        record.cases[2].trades = {trade(31, 61), trade(91, 151), trade(181, 211), trade(241, 271)};
        record.cases[12].trades = {trade(31, 61), trade(121, 151)};
        for (const auto index : {2U, 12U}) {
            auto& value = record.cases[index];
            double gross_wealth = 1, net_wealth = 1;
            for (const auto& leg : value.trades) {
                gross_wealth *= 1 + leg.returns.gross_return;
                net_wealth *= 1 + leg.returns.net_return;
            }
            value.gross_return = gross_wealth - 1;
            value.net_return = net_wealth - 1;
        }
        for (std::size_t clock = 0; clock < record.opportunities.size(); ++clock) {
            auto& opportunity = record.opportunities[clock];
            opportunity.features.at = request.start() + minutes{static_cast<minutes::rep>(30 * (clock + 1))};
            opportunity.features.move = 101.0 / 100 - 1;
            opportunity.features.typical_absolute_move = 0.005;
            opportunity.features.relative_volume = day % 2 ? 0.5 : 2;
        }
        result.study.days.push_back(record);
        auto audit = trading::audit_session(request, bars);
        result.sessions.push_back({request, std::move(bars), {}, audit, {}, {}});
    }
    return result;
}

void test_attribution_and_stresses() {
    auto input = fixture();
    const trading::ScheduledReleaseCalendar calendar{calendar_text()};
    const auto analyze = [&] { return trading::ContinuationDiagnostics{}.analyze(input.study, input.sessions, calendar); };
    const auto d = analyze();
    require(d.exposure.size() == 48 && d.entries.size() == 72, "one exposure row per price leg and one timing row per actual entry");
    require(d.attribution[0].price_trades == 12 && d.attribution[1].price_trades == 12 && d.attribution[2].price_trades == 24,
        "shared, delayed and skipped legs classified independently");
    near(d.gross_log_increment, 12 * std::log(1.2 / 1.2474), "gross attribution has an independent analytical wealth ratio");
    const double cost_drag = std::log(1.0001 / 0.9999);
    near(d.saved_cost_log_drag, 24 * cost_drag, "only skipped whole legs save a round trip");
    near(d.net_log_increment, 12 * std::log(1.2 / 1.2474) + 24 * cost_drag, "net log identity includes reinvestment");
    near(d.exposure[1].removed_gross_log_return, std::log(1.1), "delayed entry removes only its early segment");
    require(d.attribution[2].removed_positive_segments == 12 && d.attribution[2].removed_negative_segments == 12,
        "removed winners are retained alongside avoided losers");
    const auto& first = input.study.days.front();
    near(d.mean_daily_saved_cost_drag, (first.cases[2].gross_return - first.cases[2].net_return) -
        (first.cases[12].gross_return - first.cases[12].net_return), "arithmetic cost decomposition differs from log attribution");
    require(d.common_winner_removal.size() == 4 && d.common_winner_removal[2].cash_dates.size() == 5,
        "fixed 1/3/5/10 common-date stresses");
    require(d.common_winner_removal[2].cash_dates.front() == "2022-02-01", "tied winners use ascending date");
    near(d.common_winner_removal[2].net[1].mean, first.cases[12].net_return * 7 / 12, "cash dates keep the full sample denominator");
    near(d.common_winner_removal[2].net[0].mean, first.cases[2].net_return * 7 / 12, "both variants use identical cash dates");
    require(d.leave_one_month_out.empty(), "single-month fixture cannot create an empty remaining sample");
    require(d.opportunities.size() == 144 && d.covariates[0][0].count == 6 && d.covariates[0][1].count == 6,
        "clock covariates preserve paired day groups");
    near(d.opportunities.front().prefix_realized_volatility, std::log(1.01), "constant closes imply a single open-to-first-close log change");
    near(*d.opportunities.front().normalized_move, 2, "same-clock historical scale is the normalization denominator");
    near(d.entry_summaries[0].mean_signed_gap, 100.0 / 101 - 1, "entry gaps use the completed decision close");
    require(trading::format_removed_exposure_csv(d).find("delayed_entry") != std::string::npos, "matched exposure is exported");
    const auto csv = trading::format_opportunity_diagnostics_csv(d);
    require(std::count(csv.begin(), csv.end(), '\n') == 145, "every opportunity exported once, without duplicated delays");
    auto before = d.opportunities.front().prefix_realized_volatility;
    input.sessions.front().bars.back() = trading::MinuteBar{"SPY", input.sessions.front().bars.back().start(), {100, 201, 99, 200}, 1000};
    near(analyze().opportunities.front().prefix_realized_volatility, before, "future bars cannot alter prefix risk");
    input = fixture();
    input.study.days[0].cases[12].trades[1].exit_reference_time += minutes{1};
    input.study.days[0].cases[12].trades[1].exit_decision += minutes{1};
    rejects([&] { static_cast<void>(analyze()); }, "volume exit outside the matching price leg rejected");
    input = fixture();
    input.study.days[0].cases[2].net_return += 0.01;
    rejects([&] { static_cast<void>(analyze()); }, "daily returns must reconcile to actual trade paths");
    input = fixture(true);
    const auto split = analyze();
    require(split.leave_one_month_out.size() == 2 && split.leave_one_month_out[0].sessions == 6,
        "month omission removes dates rather than replacing them with cash");
    near(split.leave_one_month_out[0].net[1].mean, input.study.days[0].cases[12].net_return,
        "remaining-sample denominator is six, not twelve");
    input = fixture();
    input.study.days[0].opportunities[0].features.typical_absolute_move = 0;
    const auto zero_scale = analyze();
    require(!zero_scale.opportunities[0].normalized_move && zero_scale.covariates[0][0].normalized_count == 5,
        "zero scales remain undefined without dropping the price/risk observation");
}

} // namespace

int main() {
    try {
        test_calendar();
        test_attribution_and_stresses();
        std::cout << "Continuation diagnostic contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Continuation diagnostic contracts failed: " << error.what() << '\n';
        return 1;
    }
}
