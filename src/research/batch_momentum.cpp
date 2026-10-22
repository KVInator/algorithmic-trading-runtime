#include "batch_momentum.hpp"
#include "utc_timestamp.hpp"


#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace trading {

std::vector<MomentumCaseSummary> BatchMomentumReplay::run(std::span<const SessionDownload> sessions) const {
    if (sessions.empty() || sessions.size() > 512) {
        throw std::invalid_argument("batch replay requires 1 to 512 sessions");
    }
    std::string previous;
    for (const auto& session : sessions) {
        const auto date = format_utc_seconds(session.request.start()).substr(0, 10);
        if (date <= previous || date == "2023-01-03") {
            throw std::invalid_argument("batch dates must be unique, ascending and exclude the diagnostic fixture");
        }
        previous = date;
    }
    std::vector<MomentumCaseSummary> cases;
    for (const auto delay : {std::chrono::minutes{1}, std::chrono::minutes{2}}) {
        for (const double cost : {0.0, 0.5, 1.0, 2.0, 5.0}) {
            MomentumCaseSummary result{delay, cost, {}, 0, 0.0, 0.0, 0.0, 0.0, {}, {}};
            for (const auto& session : sessions) {
                auto replay = DelayedBarReplay{delay, cost}.run(IntradayMomentumStrategy{session.request}, session.bars);
                result.days.push_back({format_utc_seconds(session.request.start()).substr(0, 10), std::move(replay)});
            }
            const auto statistics = describe_returns(research_returns(result.days));
            result.active_days = statistics.active_days;
            result.strategy_mean_daily = statistics.strategy_net.mean;
            result.always_long_mean_daily = statistics.always_long_net.mean;
            result.strategy_compounded = statistics.strategy_net.compounded;
            result.always_long_compounded = statistics.always_long_net.compounded;
            result.strategy_mean_net_active = statistics.mean_net_active;
            result.signal_contrast_gross = statistics.signal_contrast_gross;
            cases.push_back(std::move(result));
        }
    }
    return cases;
}

std::vector<DailyResearchReturn> research_returns(std::span<const DailyMomentumReturn> days) {
    std::vector<DailyResearchReturn> result;
    result.reserve(days.size());
    for (const auto& day : days) {
        result.push_back({day.date, day.replay.decision.enter_long,
            day.replay.always_long_control.returns.gross_return, day.replay.always_long_control.returns.net_return});
    }
    return result;
}

std::string format_momentum_daily_csv(std::span<const MomentumCaseSummary> cases) {
    std::ostringstream csv;
    csv.imbue(std::locale::classic());
    csv << std::setprecision(std::numeric_limits<double>::max_digits10)
        << "date,delay_minutes,cost_bps_per_side,signal_return,active,strategy_gross_return,"
           "strategy_net_return,always_long_gross_return,always_long_net_return\n";
    for (const auto& result : cases) {
        for (const auto& day : result.days) {
            csv << day.date << ',' << result.delay.count() << ',' << result.cost_bps_per_side << ','
                << day.replay.decision.signal_return << ',' << (day.replay.decision.enter_long ? 1 : 0) << ','
                << day.replay.strategy_gross_return() << ',' << day.replay.strategy_net_return() << ','
                << day.replay.always_long_control.returns.gross_return << ','
                << day.replay.always_long_control.returns.net_return << '\n';
        }
    }
    return csv.str();
}

} // namespace trading
