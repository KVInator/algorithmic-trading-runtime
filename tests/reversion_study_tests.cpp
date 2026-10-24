#include "reversion_study_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "market_calendar.hpp"
#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {
using namespace trading;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string{message});
    }
}

void near(double a, double b) {
    require(std::abs(a - b) < 1e-11 * std::max({1.0, std::abs(a), std::abs(b)}),
            "independent arithmetic mismatch");
}

template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "invalid state must fail");
}

std::vector<SessionDownload> fixture() {
    std::vector<CalendarSession> calendar;
    for (const auto& range :
         {CalendarRange{"2022-01-01", "2022-01-31"}, CalendarRange{"2022-02-01", "2022-02-28"},
          CalendarRange{"2022-03-01", "2022-03-31"}}) {
        const auto part = development_calendar(range);
        calendar.insert(calendar.end(), part.begin(), part.end());
    }
    std::vector<SessionDownload> result;
    for (std::size_t day = 0; day < 45; ++day) {
        const auto request = calendar[day].bar_request();
        std::array<double, 25> residuals{};
        for (std::size_t k = 1; k < 25; ++k) {
            residuals[k] =
                .0002 + .8 * residuals[k - 1] + .00005 * std::sin(static_cast<double>(k + day) * 1.73);
        }
        if (day >= 20) {
            residuals.back() = day % 3 == 0 ? .004 : -.004;
        }
        std::vector<MinuteBar> bars;
        for (std::size_t m = 0; m < 390; ++m) {
            double close = 100;
            if (m >= 29 && m <= 149 && (m - 29) % 5 == 0) {
                close = 100 * std::exp(residuals[(m - 29) / 5]);
            }
            double open = 100;
            if (m == 181) {
                open = day % 2 == 0 ? 101 : 99.5;
            }
            if (m == 182) {
                open = day % 2 == 0 ? 100.7 : 99.4;
            }
            bars.emplace_back("SPY", request.start() + std::chrono::minutes{m},
                              Ohlc{open, std::max(open, close), std::min(open, close), close}, 100);
        }
        const auto audit = audit_session(request, bars);
        result.push_back({request, std::move(bars), {}, audit, request.start(), request.end_exclusive()});
    }
    return result;
}

void history_contracts(std::vector<SessionDownload> sessions) {
    ReversionHistory history{};
    rejects([&] { static_cast<void>(history.prior(sessions[20].request)); });
    rejects([&] {
        static_cast<void>(history.complete(sessions[0].request, sessions[0].bars,
                                           sessions[0].request.start() + std::chrono::minutes{150}));
    });
    for (std::size_t i = 0; i < 20; ++i) {
        static_cast<void>(
            history.complete(sessions[i].request, sessions[i].bars, sessions[i].request.end_exclusive()));
    }
    auto prior = history.prior(sessions[20].request);
    require(prior.oldest == sessions[0].request.start() && prior.latest == sessions[19].request.start() &&
                prior.fit.pairs == 480,
            "history has exactly twenty earlier sessions and no boundary pair");
    rejects([&] { static_cast<void>(history.prior(sessions[19].request)); });
    rejects([&] {
        static_cast<void>(
            history.complete(sessions[19].request, sessions[19].bars, sessions[19].request.end_exclusive()));
    });
    const ReversionStrategy strategy{};
    const auto prefix = std::span{sessions[20].bars}.first(150);
    const auto decision = strategy.decide(sessions[20].request, prefix, history);
    require(decision.actions[0].enter && decision.actions[1].enter && decision.actions[2].enter,
            "constructed noon case activates model, sign and positive prior drift");
    const auto x = decision.current->values.back();
    const auto& f = decision.prior.fit;
    const double independent = (*f.equilibrium - x) * std::pow(*f.slope, .2) * (1 - std::pow(*f.slope, 6));
    near(*decision.expected_move, independent);
    rejects([&] { static_cast<void>(strategy.decide(sessions[20].request, sessions[20].bars, history)); });
    auto changed = sessions[20].bars;
    for (std::size_t m = 150; m < 390; ++m) {
        changed[m] = MinuteBar{"SPY", changed[m].start(), Ohlc{900, 900, 900, 900}, 100};
    }
    const auto unchanged = strategy.decide(sessions[20].request, std::span{changed}.first(150), history);
    require(unchanged.expected_move == decision.expected_move &&
                unchanged.prior.fit.slope == decision.prior.fit.slope,
            "future prices cannot change noon policy or prior fit");
    for (std::size_t m = 0; m < 30; ++m) {
        const auto& b = sessions[20].bars[m];
        sessions[20].bars[m] = MinuteBar{"SPY", b.start(), b.prices(), 0};
    }
    const auto missing =
        strategy.decide(sessions[20].request, std::span{sessions[20].bars}.first(150), history);
    require(!missing.actions[0].enter && !missing.actions[1].enter && missing.actions[2].enter &&
                missing.actions[3].enter,
            "current anchor failure affects only dependent policies");
    static_cast<void>(
        history.complete(sessions[20].request, sessions[20].bars, sessions[20].request.end_exclusive()));
    const auto blocked =
        strategy.decide(sessions[21].request, std::span{sessions[21].bars}.first(150), history);
    require(!blocked.prior.complete && !blocked.actions[0].enter && !blocked.actions[2].enter &&
                blocked.prior.oldest == sessions[1].request.start(),
            "unavailable history slot is retained rather than replaced");
    for (std::size_t i = 21; i <= 39; ++i) {
        static_cast<void>(
            history.complete(sessions[i].request, sessions[i].bars, sessions[i].request.end_exclusive()));
    }
    require(!history.prior(sessions[40].request).complete,
            "bad anchor remains in exactly twenty subsequent history windows");
    static_cast<void>(
        history.complete(sessions[40].request, sessions[40].bars, sessions[40].request.end_exclusive()));
    require(history.prior(sessions[41].request).complete,
            "history recovers only after the failed slot rolls out");
}

void gate_contracts(ReversionStudyResult result, const ReversionStudy& study) {
    auto& s = result.statistics[2];
    s.overall.returns.active_days = 100;
    s.overall.gross.mean = .001;
    s.overall.returns.strategy_net.mean = .0005;
    s.years.emplace("2023", s.overall);
    s.years.at("2022").returns.strategy_net.mean = .0005;
    result.statistics[3].overall.returns.strategy_net.mean = .0002;
    result.statistics[7].overall.returns.strategy_net.mean = .0002;
    s.overall.concentration.largest_five_share_of_positive_returns = .5;
    s.overall.concentration.arithmetic_net_sum = 1;
    s.overall.concentration.largest_five_net_sum = .4;
    for (const auto m : {80U, 82U}) {
        result.metrics[m].mean = .0001;
    }
    for (const auto m : {81U, 83U}) {
        result.metrics[m].mean = 0;
    }
    for (auto& u : result.uncertainty) {
        for (const auto m : {2U, 80U, 82U}) {
            u.metrics[m].range = PercentileInterval{.00001, .001};
        }
    }
    study.assess(result);
    require(result.status == "passes_development_screens",
            "all registered economic gates pass independently of forecast gates");
    result.uncertainty[0].metrics[80].range->lower = 0;
    study.assess(result);
    require(result.status == "failed_registered_screens", "zero lower endpoint fails strict positivity");
    s.overall.gross.mean = 0;
    study.assess(result);
    require(result.status == "no_gross_edge", "nonpositive gross takes registered precedence");
    s.overall.gross.mean = .001;
    s.overall.returns.strategy_net.mean = 0;
    study.assess(result);
    require(result.status == "cost_dominated", "nonpositive net after positive gross is cost dominated");
}

void outcome_contracts(ReversionStudyResult result, const ReversionStudy& study) {
    // Identical common-date strategy and control paths must give identically zero
    // paired differences and ranges; outcome tests need no statistical guessing.
    for (auto& day : result.days) {
        for (auto& a : day.decision.actions) {
            a = {true, "constructed_long"};
        }
        for (std::size_t i = 0; i < 40; ++i) {
            const auto& c = reversion_cases()[i];
            const auto d = static_cast<std::size_t>(c.delay - 1);
            const auto r = evaluate_long_prices(day.entry_prices[d], day.exit_prices[d], c.cost);
            day.accounts[i] = {r.gross_return, r.net_return};
        }
        day.squared_loss_contrast.reset();
        day.absolute_loss_contrast.reset();
    }
    study.analyze(result);
    for (const auto& u : result.uncertainty) {
        for (std::size_t m = 80; m < 86; ++m) {
            require(u.metrics[m].range && u.metrics[m].range->lower == 0 && u.metrics[m].range->upper == 0,
                    "paired equal paths remain exactly zero");
        }
        require(u.metrics[86].undefined_replicates == u.replicates && !u.metrics[86].range,
                "undefined forecast draws invalidate entire range");
    }
    require(!std::all_of(result.economic_gates.begin(), result.economic_gates.end(),
                         [](const auto& g) { return g.passed; }),
            "zero increments and insufficient activity cannot advance");
    // A common mask present at one date produces valid point loss but some
    // undefined block draws. Those draws cannot be silently dropped.
    result.days.front().squared_loss_contrast = .000001;
    result.days.front().absolute_loss_contrast = .001;
    study.analyze(result);
    require(result.metrics[86].available == 1 && result.uncertainty[0].metrics[86].undefined_replicates > 0 &&
                !result.uncertainty[0].metrics[86].range,
            "partial-mask resampling retains full-date failures");
    for (auto& day : result.days) {
        for (auto& a : day.decision.actions) {
            a = {false, "constructed_cash"};
        }
        for (auto& r : day.accounts) {
            r = {0, 0};
        }
    }
    study.analyze(result);
    require(result.status == "no_gross_edge" &&
                !result.statistics[2].overall.returns.strategy_net.annualized_daily_sharpe,
            "all cash is complete, flat and undefined Sharpe");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::invalid_argument("study contracts require output root");
        }
        const auto sessions = fixture();
        history_contracts(sessions);
        const ReversionStudy study{{20, 40}};
        auto result = study.replay(sessions);
        require(result.days.size() == 25 && result.observations.size() == 45,
                "warm-up is not scored as cash");
        for (std::size_t d = 0; d < 25; ++d) {
            const auto& day = result.days[d];
            require(day.decision.prior.oldest == sessions[d].request.start() &&
                        day.decision.prior.latest == sessions[d + 19].request.start(),
                    "prior window rolls without current session");
            for (std::size_t i = 0; i < 40; ++i) {
                const auto& c = reversion_cases()[i];
                const auto j = static_cast<std::size_t>(c.delay - 1);
                const bool active = day.decision.actions[static_cast<std::size_t>(c.policy)].enter;
                const double ratio = day.exit_prices[j] / day.entry_prices[j];
                near(day.accounts[i].gross, active ? ratio - 1 : 0);
                near(day.accounts[i].net,
                     active ? ratio * (1 - c.cost / 10000) / (1 + c.cost / 10000) - 1 : 0);
            }
            for (const auto& u : day.parameter_ranges) {
                require(std::accumulate(u.status_counts.begin(), u.status_counts.end(), std::size_t{0}) == 20,
                        "all refits counted");
            }
        }
        study.analyze(result);
        gate_contracts(result, study);
        outcome_contracts(result, study);
        auto gap = sessions;
        gap[23].bars.erase(gap[23].bars.begin() + 155);
        rejects([&] { static_cast<void>(study.replay(gap)); });
        auto reversed = sessions;
        std::reverse(reversed.begin(), reversed.end());
        rejects([&] { static_cast<void>(study.replay(reversed)); });
        ReversionStudy::verify_registration(TRADING_PROJECT_ROOT);
        const FrozenDevelopmentDataset invalid_snapshot{};
        rejects([&] { static_cast<void>(study.run_frozen(invalid_snapshot, TRADING_PROJECT_ROOT)); });
        rejects(
            [&] { static_cast<void>(ReversionStudy{}.run_frozen(invalid_snapshot, TRADING_PROJECT_ROOT)); });
        const auto dir = create_unique_directory(argv[1], "reversion-study");
        ReversionStudyReport{result}.write(dir);
        const auto summary = nlohmann::json::parse(read_text_file(dir / "summary.json"));
        require(summary.at("cases").size() == 40 &&
                    summary.at("study_status").get<std::string>() == "constructed_fixture",
                "fixture report cannot masquerade as historical evidence");
        for (const auto& a : summary.at("artifacts")) {
            require(sha256_file(dir / a.at("path").get<std::string>()) == a.at("sha256").get<std::string>(),
                    "artifact hash mismatch");
        }
        const auto copy = create_unique_directory(argv[1], "registration-tamper");
        std::filesystem::create_directory(copy / "research");
        std::filesystem::copy_file(std::filesystem::path{TRADING_PROJECT_ROOT} /
                                       "research/intraday_reversion_protocol.md",
                                   copy / "research/intraday_reversion_protocol.md");
        write_new_text_file(copy / "research/intraday_reversion_protocol.json", "{}\n");
        rejects([&] { ReversionStudy::verify_registration(copy); });
        std::cout
            << "Reversion chronology, policies, accounts, masks, gates and registration contracts passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Reversion study contract failure: " << e.what() << '\n';
        return 1;
    }
}
