#include "continuation_diagnostics.hpp"
#include "market_calendar.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <cmath>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace trading {
namespace {

constexpr std::array<std::size_t, 2> primary{2, 12};

void equal(double actual, double expected, std::string_view message) {
    if (!std::isfinite(actual) || !std::isfinite(expected) || std::abs(actual - expected) > 2e-11) {
        throw std::invalid_argument(std::string{message});
    }
}

double gross_log(const ScheduledRoundTrip& trade) {
    return std::log(trade.exit_reference_price / trade.entry_reference_price);
}

void validate_trades(const ContinuationCaseDay& day) {
    double gross = 0, net = 0;
    std::optional<std::chrono::sys_seconds> previous_exit;
    for (const auto& trade : day.trades) {
        if (trade.entry_reference_time != trade.entry_decision + std::chrono::minutes{1} ||
            trade.exit_reference_time != trade.exit_decision + std::chrono::minutes{1} ||
            trade.entry_decision >= trade.exit_decision ||
            (previous_exit && trade.entry_reference_time < *previous_exit)) {
            throw std::invalid_argument("diagnostics require ordered one-minute primary trade references");
        }
        const auto returns = evaluate_long_prices(trade.entry_reference_price, trade.exit_reference_price, 1);
        equal(trade.returns.gross_return, returns.gross_return, "trade gross return differs from reference prices");
        equal(trade.returns.net_return, returns.net_return, "trade net return differs from primary cost");
        gross += std::log1p(returns.gross_return);
        net += std::log1p(returns.net_return);
        previous_exit = trade.exit_reference_time;
    }
    equal(std::log1p(day.gross_return), gross, "day gross return differs from compounded trades");
    equal(std::log1p(day.net_return), net, "day net return differs from compounded trades");
}

PairedDiagnosticPeriod period(const ContinuationStudyResult& study, std::string label,
                             const std::vector<std::size_t>& indices,
                             const std::set<std::string>& cash_dates = {}) {
    PairedDiagnosticPeriod result{};
    result.label = std::move(label);
    result.sessions = indices.size();
    result.cash_dates.assign(cash_dates.begin(), cash_dates.end());
    if (indices.empty()) { throw std::invalid_argument("diagnostic period has no sessions"); }
    for (std::size_t variant = 0; variant < 2; ++variant) {
        std::vector<double> gross, net;
        for (const auto index : indices) {
            const auto& day = study.days.at(index);
            const auto& value = day.cases[primary[variant]];
            const bool retained = !cash_dates.contains(day.date);
            gross.push_back(retained ? value.gross_return : 0);
            net.push_back(retained ? value.net_return : 0);
            if (retained && !value.trades.empty()) { ++result.active_days[variant]; }
        }
        result.gross[variant] = describe_return_series(gross);
        result.net[variant] = describe_return_series(net);
    }
    result.paired_net_mean = result.net[1].mean - result.net[0].mean;
    result.paired_gross_mean = result.gross[1].mean - result.gross[0].mean;
    return result;
}

std::size_t minute_index(const SessionDownload& session, std::chrono::sys_seconds at) {
    const auto seconds = (at - session.request.start()).count();
    if (seconds < 0 || seconds % 60 != 0 || seconds / 60 >= static_cast<long long>(session.bars.size())) {
        throw std::invalid_argument("diagnostic reference is outside the complete session grid");
    }
    return static_cast<std::size_t>(seconds / 60);
}

void match_exposure(const ContinuationDay& day, ContinuationDiagnosticsResult& result) {
    const auto& price = day.cases[primary[0]];
    const auto& volume = day.cases[primary[1]];
    validate_trades(price);
    validate_trades(volume);
    const double cost_log_drag = std::log1p(0.0001) - std::log1p(-0.0001);
    std::size_t next_volume = 0;
    double day_gross_increment = 0, day_net_increment = 0;
    for (const auto& trade : price.trades) {
        RemovedExposure row{day.date, ExposureMatch::skipped, trade, {}, gross_log(trade), cost_log_drag, 0};
        if (next_volume < volume.trades.size()) {
            const auto& candidate = volume.trades[next_volume];
            if (candidate.entry_reference_time < trade.entry_reference_time) {
                throw std::invalid_argument("volume exposure begins outside price exposure");
            }
            if (candidate.entry_reference_time < trade.exit_reference_time) {
                if (candidate.exit_reference_time != trade.exit_reference_time || candidate.exit_decision != trade.exit_decision) {
                    throw std::invalid_argument("matched volume exposure must share the price exit");
                }
                equal(candidate.exit_reference_price, trade.exit_reference_price, "matched exit prices differ");
                row.volume_trade = candidate;
                row.match = candidate.entry_reference_time == trade.entry_reference_time ?
                    ExposureMatch::shared : ExposureMatch::delayed_entry;
                if (row.match == ExposureMatch::shared) {
                    equal(candidate.entry_reference_price, trade.entry_reference_price, "shared entry prices differ");
                }
                row.removed_gross_log_return = std::log(candidate.entry_reference_price / trade.entry_reference_price);
                row.saved_cost_log_drag = 0;
                ++next_volume;
            }
        }
        row.net_log_increment = -row.removed_gross_log_return + row.saved_cost_log_drag;
        day_gross_increment -= row.removed_gross_log_return;
        day_net_increment += row.net_log_increment;
        result.saved_cost_log_drag += row.saved_cost_log_drag;
        auto& attribution = result.attribution.at(static_cast<std::size_t>(row.match));
        ++attribution.price_trades;
        attribution.removed_positive_segments += row.removed_gross_log_return > 0 ? 1U : 0U;
        attribution.removed_negative_segments += row.removed_gross_log_return < 0 ? 1U : 0U;
        attribution.gross_log_increment -= row.removed_gross_log_return;
        attribution.saved_cost_log_drag += row.saved_cost_log_drag;
        attribution.net_log_increment += row.net_log_increment;
        result.exposure.push_back(std::move(row));
    }
    if (next_volume != volume.trades.size()) { throw std::invalid_argument("unmatched volume trade"); }
    equal(day_gross_increment, std::log1p(volume.gross_return) - std::log1p(price.gross_return),
        "gross log attribution does not reconcile");
    equal(day_net_increment, std::log1p(volume.net_return) - std::log1p(price.net_return),
        "net log attribution does not reconcile");
    result.gross_log_increment += day_gross_increment;
    result.net_log_increment += day_net_increment;
    result.mean_daily_saved_cost_drag += (price.gross_return - price.net_return) -
        (volume.gross_return - volume.net_return);
}

} // namespace

ScheduledReleaseCalendar::ScheduledReleaseCalendar(std::string_view csv) {
    std::istringstream input{std::string{csv}};
    input.imbue(std::locale::classic());
    std::string line;
    const auto next = [&]() {
        if (!std::getline(input, line)) { return false; }
        if (!line.empty() && line.back() == '\r') { line.pop_back(); }
        return true;
    };
    if (!next() || line != "date,event,time_new_york,source_url") {
        throw std::invalid_argument("unrecognized scheduled-release calendar header");
    }
    std::set<std::pair<std::string, unsigned>> seen;
    std::string previous;
    while (next()) {
        std::array<std::string, 4> cells;
        std::istringstream row{line};
        for (auto& cell : cells) {
            if (!std::getline(row, cell, ',') || cell.empty()) { throw std::invalid_argument("incomplete release calendar row"); }
        }
        if (row.peek() != std::char_traits<char>::eof() || line.back() == ',') {
            throw std::invalid_argument("extra release calendar column");
        }
        static_cast<void>(parse_session_date(cells[0]));
        const auto year = cells[0].substr(0, 4);
        if ((year != "2022" && year != "2023") || cells[0] < previous) {
            throw std::invalid_argument("release calendar must be ordered within 2022-2023");
        }
        ScheduledRelease kind;
        if (cells[1] == "cpi") { kind = ScheduledRelease::cpi; }
        else if (cells[1] == "employment") { kind = ScheduledRelease::employment; }
        else if (cells[1] == "fomc") { kind = ScheduledRelease::fomc; }
        else { throw std::invalid_argument("unknown scheduled release"); }
        std::string expected_source = "https://www.bls.gov/schedule/" + year + "/home.htm";
        if (kind == ScheduledRelease::fomc) {
            auto compact_date = cells[0];
            std::erase(compact_date, '-');
            expected_source = "https://www.federalreserve.gov/newsevents/pressreleases/monetary" + compact_date + "a.htm";
        }
        if (cells[2] != (kind == ScheduledRelease::fomc ? "14:00" : "08:30") || cells[3] != expected_source ||
            !seen.emplace(cells[0], static_cast<unsigned>(kind)).second) {
            throw std::invalid_argument("invalid release time, source or duplicate record");
        }
        records_.push_back({cells[0], kind, cells[2], cells[3]});
        previous = cells[0];
    }
    if (records_.empty()) { throw std::invalid_argument("empty scheduled-release calendar"); }
}

unsigned ScheduledReleaseCalendar::flags(std::string_view date) const {
    unsigned result = 0;
    for (const auto& record : records_) {
        if (record.date == date) { result |= static_cast<unsigned>(record.kind); }
    }
    return result;
}

const std::vector<ScheduledReleaseRecord>& ScheduledReleaseCalendar::records() const noexcept { return records_; }

std::string scheduled_release_label(unsigned flags) {
    if (flags > 7) { throw std::invalid_argument("unknown release flags"); }
    if (!flags) { return "No selected release"; }
    std::string result;
    for (const auto& [bit, label] : {std::pair{1U, "CPI"}, {2U, "Employment"}, {4U, "FOMC"}}) {
        if (flags & bit) { result += (result.empty() ? "" : " + ") + std::string{label}; }
    }
    return result;
}

std::string_view exposure_match_name(ExposureMatch value) {
    switch (value) {
    case ExposureMatch::shared: return "shared";
    case ExposureMatch::delayed_entry: return "delayed_entry";
    case ExposureMatch::skipped: return "skipped";
    }
    throw std::invalid_argument("unknown exposure match");
}

ContinuationDiagnosticsResult ContinuationDiagnostics::analyze(
    const ContinuationStudyResult& study, std::span<const SessionDownload> eligible_sessions,
    const ScheduledReleaseCalendar& calendar) const {
    if (study.days.empty()) { throw std::invalid_argument("diagnostics require scored development days"); }
    std::map<std::string, const SessionDownload*> sessions;
    for (const auto& session : eligible_sessions) {
        if (session.bars.size() != 390 || !audit_session(session.request, session.bars).complete() ||
            !sessions.emplace(format_utc_seconds(session.request.start()).substr(0, 10), &session).second) {
            throw std::invalid_argument("diagnostics require unique complete eligible sessions");
        }
    }
    ContinuationDiagnosticsResult result{};
    std::vector<std::size_t> all;
    std::map<unsigned, std::vector<std::size_t>> releases;
    std::set<std::string> months;
    for (std::size_t i = 0; i < study.days.size(); ++i) {
        const auto& day = study.days[i];
        if (day.date < "2022-01-01" || day.date > "2023-12-31" ||
            (i && study.days[i - 1].date >= day.date) || !sessions.contains(day.date)) {
            throw std::invalid_argument("scored days must be ordered and match saved sessions");
        }
        const auto& session = *sessions.at(day.date);
        const unsigned release_flags = calendar.flags(day.date);
        releases[release_flags].push_back(i);
        months.insert(day.date.substr(0, 7));
        all.push_back(i);
        match_exposure(day, result);
        double squared_returns = 0;
        double previous_close = session.bars.front().prices().open;
        std::size_t completed = 0;
        for (std::size_t clock = 0; clock < day.opportunities.size(); ++clock) {
            const auto& opportunity = day.opportunities[clock];
            const auto& f = opportunity.features;
            const std::size_t elapsed = 30 * (clock + 1);
            if (f.at != session.request.start() + std::chrono::minutes{static_cast<std::chrono::minutes::rep>(elapsed)}) {
                throw std::invalid_argument("opportunity clock differs from scored session");
            }
            while (completed < elapsed) {
                const double close = session.bars[completed].prices().close;
                const double change = std::log(close / previous_close);
                squared_returns += change * change;
                previous_close = close;
                ++completed;
            }
            equal(f.move, previous_close / session.bars.front().prices().open - 1, "feature move differs from completed prefix");
            if (!std::isfinite(f.typical_absolute_move) || f.typical_absolute_move < 0 ||
                !std::isfinite(f.relative_volume) || f.relative_volume < 0) {
                throw std::invalid_argument("invalid diagnostic feature scale or volume");
            }
            OpportunityDiagnostic row{day.date, elapsed, release_flags, f.move > f.typical_absolute_move,
                f.relative_volume > 1, f.move, {}, std::sqrt(squared_returns), opportunity.forward[0].returns.gross_return};
            if (!std::isfinite(row.prefix_realized_volatility) || !std::isfinite(row.forward_gross_return) || row.forward_gross_return <= -1) {
                throw std::invalid_argument("diagnostic risk and forward returns must be finite");
            }
            if (f.typical_absolute_move > 0) {
                row.normalized_move = f.move / f.typical_absolute_move;
                if (!std::isfinite(*row.normalized_move)) { throw std::overflow_error("normalized move exceeds the finite range"); }
            }
            if (row.price_condition) {
                auto& group = result.covariates[clock][row.high_volume ? 0 : 1];
                ++group.count;
                group.move_sum += row.move;
                group.realized_volatility_sum += row.prefix_realized_volatility;
                group.forward_gross_sum += row.forward_gross_return;
                if (row.normalized_move) { ++group.normalized_count; group.normalized_move_sum += *row.normalized_move; }
            }
            result.opportunities.push_back(std::move(row));
        }
        for (std::size_t variant = 0; variant < 2; ++variant) {
            for (const auto& trade : day.cases[primary[variant]].trades) {
                const auto decision_index = minute_index(session, trade.entry_decision);
                const auto entry_index = minute_index(session, trade.entry_reference_time);
                if (!decision_index) { throw std::invalid_argument("entry has no completed decision prefix"); }
                const auto& bar = session.bars[entry_index];
                equal(trade.entry_reference_price, bar.prices().open, "trade reference differs from saved entry bar");
                equal(trade.exit_reference_price, session.bars[minute_index(session, trade.exit_reference_time)].prices().open,
                    "trade reference differs from saved exit bar");
                result.entries.push_back({day.date, variant == 0 ? ContinuationVariant::price_only : ContinuationVariant::volume_conditioned,
                    trade.entry_decision, trade.entry_reference_time,
                    bar.prices().open / session.bars[decision_index - 1].prices().close - 1,
                    (bar.prices().high - bar.prices().low) / bar.prices().open, bar.volume()});
                const auto& entry = result.entries.back();
                auto& summary = result.entry_summaries[variant];
                ++summary.count;
                summary.mean_signed_gap += entry.entry_gap;
                summary.mean_absolute_gap += std::abs(entry.entry_gap);
                summary.mean_entry_minute_range += entry.entry_minute_range;
                summary.mean_entry_minute_volume += entry.entry_minute_volume;
            }
        }
    }
    result.mean_daily_saved_cost_drag /= static_cast<double>(study.days.size());
    for (auto& summary : result.entry_summaries) {
        if (!summary.count) { continue; }
        const double count = static_cast<double>(summary.count);
        summary.mean_signed_gap /= count;
        summary.mean_absolute_gap /= count;
        summary.mean_entry_minute_range /= count;
        summary.mean_entry_minute_volume /= count;
    }
    equal(result.net_log_increment, result.gross_log_increment + result.saved_cost_log_drag, "total log attribution does not reconcile");
    for (const auto& [flags, indices] : releases) { result.release_groups.push_back(period(study, scheduled_release_label(flags), indices)); }
    std::vector<WinningContribution> winners;
    for (const auto& day : study.days) {
        if (day.cases[12].net_return > 0) { winners.push_back({day.date, day.cases[12].net_return}); }
    }
    std::sort(winners.begin(), winners.end(), [](const auto& a, const auto& b) {
        return a.net_return != b.net_return ? a.net_return > b.net_return : a.date < b.date;
    });
    for (const std::size_t count : {1U, 3U, 5U, 10U}) {
        if (count > winners.size()) { continue; }
        std::set<std::string> cash_dates;
        for (std::size_t i = 0; i < count; ++i) { cash_dates.insert(winners[i].date); }
        result.common_winner_removal.push_back(period(study, "Cash on volume's largest " + std::to_string(count) + " winners", all, cash_dates));
    }
    if (months.size() > 1) {
        for (const auto& month : months) {
            std::vector<std::size_t> indices;
            for (const auto index : all) {
                if (study.days[index].date.substr(0, 7) != month) { indices.push_back(index); }
            }
            result.leave_one_month_out.push_back(period(study, month, indices));
        }
    }
    return result;
}

} // namespace trading
