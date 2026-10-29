#include "panel_study_application.hpp"
#include "cost_delay_report.hpp"
#include "panel_account_report.hpp"
#include "panel_replay_diagnostic.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace trading {
namespace {
using Json = nlohmann::json;

class PanelStudyInputs {
  public:
    explicit PanelStudyInputs(const std::filesystem::path &root)
        : contract(ExtensionStudyContract::load(root / "research/extension_protocol.json")),
          policy(PanelInputPolicy::load(root / "research/extension_input_policy.json", contract)),
          root_(root) {}
    [[nodiscard]] PanelReplayDataset dataset() const {
        const auto resolution =
            PanelSourceResolution::load(root_ / "research/extension_source_resolution.json", policy);
        return {root_ / "scratch/datasets/extension_panel_2016_2025", contract, policy, resolution};
    }
    const ExtensionStudyContract contract;
    const PanelInputPolicy policy;

  private:
    std::filesystem::path root_;
};
class CostDelayDiagnostic {
  public:
    void require(std::string label, bool passed) {
        if (!passed) {
            throw std::runtime_error("cost/delay check failed: " + label);
        }
        checks_.push_back(std::move(label));
    }
    template <class F> void rejects(std::string label, F &&run) {
        bool rejected = false;
        try {
            run();
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        require(std::move(label), rejected);
    }
    std::filesystem::path run(const std::filesystem::path &root, const trading::PanelInputPolicy &policy,
                              const trading::CostDelayStudy &study, const PanelStudyProgress &progress) {
        using namespace std::chrono;
        using namespace trading;
        const PanelReplayFixture fixture;
        const auto dates = fixture.calendar();
        const auto &day = dates[300];
        const auto request = day.bar_request("QQQ");
        auto data = PanelReplayFixture::bars(day, "QQQ", 300, false);
        for (auto &bar : data) {
            bar = MinuteBar{"QQQ", bar.start(), {100, 111, 89, 100}, 5000};
        }
        const auto price = [&](std::size_t i, double value) {
            data[i] = MinuteBar{"QQQ",
                                data[i].start(),
                                {value, std::max(value, 111.0), std::min(value, 89.0), 100},
                                5000};
        };
        price(31, 100);
        price(61, 110);
        price(91, 90);
        price(121, 99);
        price(32, 105);
        price(62, 106);
        price(92, 91);
        price(122, 92);
        PanelPolicyReplay frozen{"constructed_two_trades", {}, {}, true, 0, 0};
        for (const auto &[minute, target] :
             std::vector<std::pair<int, bool>>{{30, true}, {60, false}, {90, true}, {120, false}}) {
            frozen.decisions.push_back({request.start() + minutes{minute}, target, "constructed", {}});
        }
        const auto a = study.replay(frozen, request, data, 1),
                   b = study.replay(frozen, request, data, 2);
        require("registered grid has five charges and two delays",
                study.costs().size() == 5 && study.delays() == std::vector<int>{1, 2});
        require("frozen decisions generate two round trips for either delay",
                a.trades.size() == 2 && b.trades.size() == 2);
        require("holding duration uses reference clocks", a.minutes_held == 60 && b.minutes_held == 60);
        for (std::size_t i = 0; i < a.outcomes.size(); ++i) {
            const auto &v = a.outcomes[i];
            const auto c = v.cost_bps / 10000;
            const auto expected = 1.1 * 1.1 * std::pow((1 - c) / (1 + c), 2) - 1;
            require("two-trade compounding agrees with closed-form self-financing wealth",
                    std::abs(v.net_return - expected) < 1e-12);
            require("fees charge actual notional on both sides",
                    std::abs(v.cost_paid - v.traded_notional * c) < 1e-12);
            require("gross return is invariant to the charge", std::abs(v.gross_return - .21) < 1e-12);
            if (i) {
                require("increasing cost cannot improve a fixed long path",
                        v.net_return < a.outcomes[i - 1].net_return);
            }
        }
        require("zero-cost gross and net agree",
                std::abs(a.outcomes[0].gross_return - a.outcomes[0].net_return) < 1e-12);
        require("two-minute references use actual alternate opens",
                std::abs(b.outcomes[0].net_return - (106.0 / 105 * 92 / 91 - 1)) < 1e-12);
        auto cash = frozen;
        for (auto &decision : cash.decisions) {
            decision.target_long = false;
        }
        const auto empty = study.replay(cash, request, data, 1);
        require("cash days remain observed zero returns under every charge",
                empty.trades.empty() &&
                    std::all_of(empty.outcomes.begin(), empty.outcomes.end(), [](const auto &v) {
                        return v.net_return == 0 && v.cost_paid == 0 && v.traded_notional == 0;
                    }));
        auto missing = data;
        missing.erase(missing.begin() + 32);
        require("missing alternate entry stops its path",
                !study.replay(frozen, request, missing, 2).resolved);
        require("missing alternate entry leaves the primary reference intact",
                study.replay(frozen, request, missing, 1).resolved);
        auto null_target = frozen;
        null_target.decisions[1].target_long.reset();
        require("unavailable primary intent never becomes a cash outcome",
                !study.replay(null_target, request, data, 1).resolved);
        rejects("unregistered delay is rejected",
                [&] { static_cast<void>(study.replay(frozen, request, data, 3)); });
        auto unordered = frozen;
        std::swap(unordered.decisions[0], unordered.decisions[1]);
        rejects("out-of-order decisions are rejected",
                [&] { static_cast<void>(study.replay(unordered, request, data, 1)); });
        auto unterminated = frozen;
        unterminated.decisions.pop_back();
        rejects("an open terminal position cannot become an intraday return",
                [&] { static_cast<void>(study.replay(unterminated, request, data, 1)); });
        rejects("reserved dates remain blocked", [&] {
            static_cast<void>(study.replay(
                frozen, CalendarSession{"2024-01-02", "09:30", "16:00"}.bar_request("QQQ"), {}, 1));
        });

        CostDelayReport report{root / "scratch/runs",
                               {{"evidence_origin", "synthetic"},
                                {"fixture", "analytic_panel_300"},
                                {"protocol_sha256", TRADING_EXTENSION_PROTOCOL_SHA256}},
                               {},
                               study};
        std::cout << "Evidence directory: " << report.directory().string() << '\n';
        std::size_t count = 0;
        static_cast<void>(fixture.replay(policy, {"GLD", "IWM", "QQQ", "SPY", "TLT"},
                                         [&](auto rows, auto sessions, bool) {
                                             report.append(rows, sessions);
                                             if (++count % 50 == 0 && progress) {
                                                 progress("Constructed causal cost/delay replay | " +
                                                          std::to_string(count) + "/300 dates");
                                             }
                                         }));
        std::cout << "Constructed numerical and failure checks passed: " << checks_.size() << '\n';
        return report.finish(checks_);
    }

  private:
    Json checks_ = Json::array();
};
} // namespace

std::filesystem::path CostDelayDevelopment::run(bool constructed,
                                                const PanelStudyProgress &progress) const {
    const PanelStudyInputs inputs{root_};
    const CostDelayStudy study{inputs.contract};
    if (constructed) {
        return CostDelayDiagnostic{}.run(root_, inputs.policy, study, progress);
    }
    const auto dataset = inputs.dataset();
    CostDelayReport report{root_ / "scratch/runs", dataset.provenance(),
                           root_ / "scratch/datasets/extension_panel_2016_2025", study};
    std::cout << "Evidence directory: " << report.directory().string() << std::endl;
    dataset.replay_with_sessions([&](auto days, auto sessions) { report.append(days, sessions); },
                                 progress);
    if (progress) {
        progress("Writing cost curves, paired comparisons and reproduction evidence");
    }
    return report.finish();
}

std::filesystem::path PanelAccountDevelopment::run(bool constructed,
                                                   const PanelStudyProgress &progress) const {
    const PanelStudyInputs inputs{root_};
    if (constructed) {
        const PanelReplayFixture fixture;
        const auto calendar = fixture.calendar();
        const Json actions = Json::array({{{"id", "constructed_QQQ_cash"},
                                           {"symbol", "QQQ"},
                                           {"ex_date", calendar[268].date()},
                                           {"payable_date", calendar[271].date()},
                                           {"usd_per_share", 1.25}}});
        PanelAccountReport report{root_ / "scratch/runs",
                                  {{"evidence_origin", "synthetic"},
                                   {"fixture", "analytic_panel_300"},
                                   {"protocol_sha256", inputs.contract.identity()}},
                                  {},
                                  inputs.contract,
                                  actions};
        std::cout << "Evidence directory: " << report.directory().string() << std::endl;
        std::size_t count = 0;
        static_cast<void>(fixture.replay(
            inputs.policy, inputs.contract.symbols(), [&](auto days, auto sessions, bool terminal) {
                const std::vector<std::optional<MinuteBar>> recovered(days.size());
                report.append(days, sessions, recovered, terminal);
                if (++count % 25 == 0 && progress) {
                    progress("Constructed account calendar | " + std::to_string(count) + "/300");
                }
            }));
        return report.finish();
    }
    const auto dataset = inputs.dataset();
    auto actions = Json::array();
    for (const auto &action : dataset.accounting_actions().at("cash_distributions")) {
        if (action.at("ex_date").get<std::string>() < "2024") {
            actions.push_back(action);
        }
    }
    PanelAccountReport report{root_ / "scratch/runs", dataset.provenance(),
                              root_ / "scratch/datasets/extension_panel_2016_2025", inputs.contract,
                              actions};
    std::cout << "Evidence directory: " << report.directory().string() << std::endl;
    const auto last = extension_calendar(CalendarRange{"2023-12-01", "2023-12-31"}).back().date();
    dataset.replay_with_sessions(
        [&](auto days, auto sessions) {
            std::vector<std::optional<MinuteBar>> recovered;
            for (const auto &day : days) {
                recovered.push_back(dataset.recovered_close(day.symbol, day.date));
            }
            report.append(days, sessions, recovered, days.front().date == last);
        },
        progress);
    return report.finish();
}
} // namespace trading
