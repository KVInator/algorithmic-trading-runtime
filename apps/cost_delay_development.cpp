#include "artifact_io.hpp"
#include "console_progress.hpp"
#include "cost_delay_report.hpp"
#include "panel_replay_checks.hpp"
#include "panel_replay_dataset.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
class CostDelayChecks {
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
                              const trading::CostDelayStudy &study,
                              const trading::ConsoleProgress &progress) {
        using namespace std::chrono;
        using namespace trading;
        const auto dates = PanelReplayChecks::calendar_sessions();
        const auto &day = dates[300];
        const auto request = day.bar_request("QQQ");
        auto data = PanelReplayChecks::bars(day, "QQQ", 300, false);
        for (auto &bar : data) {
            bar = MinuteBar{"QQQ", bar.start(), {100, 111, 89, 100}, 5000};
        }
        const auto price = [&](std::size_t i, double value) {
            data[i] = MinuteBar{
                "QQQ", data[i].start(), {value, std::max(value, 111.0), std::min(value, 89.0), 100}, 5000};
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
        const auto a = study.replay(frozen, request, data, 1), b = study.replay(frozen, request, data, 2);
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

        std::map<std::string, PanelReplayState> states;
        for (const auto *symbol : {"SPY", "QQQ", "IWM", "TLT", "GLD"}) {
            states.emplace(symbol, PanelReplayState{symbol, policy});
        }
        CostDelayReport report{root / "scratch/runs",
                               {{"evidence_origin", "synthetic"},
                                {"fixture", "analytic_panel_300"},
                                {"protocol_sha256", TRADING_EXTENSION_PROTOCOL_SHA256}},
                               {},
                               study};
        std::cout << "Evidence directory: " << report.directory().string() << '\n';
        for (std::size_t i = 0; i < 300; ++i) {
            std::vector<PanelReplayDay> rows;
            std::vector<SessionDownload> sessions;
            for (auto &[symbol, state] : states) {
                const auto request_i = dates[i].bar_request(symbol);
                auto bars = PanelReplayChecks::bars(dates[i], symbol, i);
                rows.push_back(state.advance(request_i, bars, symbol == "QQQ" && i == 268 ? 1.25 : 0));
                const auto audit = audit_session(request_i, bars);
                sessions.push_back({request_i, std::move(bars), {}, audit, {}, {}});
            }
            const bool common = std::all_of(rows.begin(), rows.end(),
                                            [](const auto &row) { return row.availability.operational; });
            for (auto &row : rows) {
                row.common_operational = common;
            }
            report.append(rows, sessions);
            if ((i + 1) % 50 == 0) {
                progress("Constructed causal cost/delay replay | " + std::to_string(i + 1) + "/300 dates");
            }
        }
        std::cout << "Constructed numerical and failure checks passed: " << checks_.size() << '\n';
        return report.finish(checks_);
    }

  private:
    Json checks_ = Json::array();
};
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc > 2 || (argc == 2 && std::string_view{argv[1]} != "--check")) {
            throw std::invalid_argument("usage: cost_delay_development [--check]");
        }
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        const trading::ConsoleProgress progress;
        progress("M6 | frozen-decision historical cost and delay comparison");
        const auto contract =
            trading::ExtensionStudyContract::load(root / "research/extension_protocol.json");
        const auto policy =
            trading::PanelInputPolicy::load(root / "research/extension_input_policy.json", contract);
        const trading::CostDelayStudy study{contract};
        std::filesystem::path output;
        if (argc == 2) {
            output = CostDelayChecks{}.run(root, policy, study, progress);
        } else {
            const auto resolution = trading::PanelSourceResolution::load(
                root / "research/extension_source_resolution.json", policy);
            const auto inputs = root / "scratch/datasets/extension_panel_2016_2025";
            const trading::PanelReplayDataset dataset{inputs, contract, policy, resolution};
            trading::CostDelayReport report{root / "scratch/runs", dataset.provenance(), inputs, study};
            std::cout << "Evidence directory: " << report.directory().string() << '\n';
            dataset.replay_with_sessions([&](auto days, auto sessions) { report.append(days, sessions); },
                                         progress);
            progress("Writing cost curves, paired comparisons and reproduction evidence");
            output = report.finish();
        }
        std::cout << "Policy/instrument/cost/delay series: 400\nPaired comparisons: 240\nPrimary reference "
                     "parity: passed\nNetwork requests sent: 0"
                  << "\nReserved strategy inputs opened: false\nVisual report: "
                  << (output / "report.html").string() << "\nManifest: " << (output / "summary.json").string()
                  << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Cost/delay comparison stopped: " << error.what() << '\n';
        return 1;
    }
}
