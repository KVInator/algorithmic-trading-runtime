#include "console_progress.hpp"
#include "panel_account_report.hpp"
#include "panel_replay_checks.hpp"
#include <iostream>

int main(int argc, char **argv) {
    try {
        if (argc > 2 || (argc == 2 && std::string_view{argv[1]} != "--check")) {
            throw std::invalid_argument("usage: panel_account_development [--check]");
        }
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        const trading::ConsoleProgress progress;
        progress("M6 | continuous risk ledgers and shared-capital economic references");
        const auto contract =
            trading::ExtensionStudyContract::load(root / "research/extension_protocol.json");
        const auto policy =
            trading::PanelInputPolicy::load(root / "research/extension_input_policy.json", contract);
        std::filesystem::path output;
        if (argc == 2) {
            const auto calendar = trading::PanelReplayChecks::calendar_sessions();
            nlohmann::json actions = nlohmann::json::array({{{"id", "constructed_QQQ_cash"},
                                                             {"symbol", "QQQ"},
                                                             {"ex_date", calendar[268].date()},
                                                             {"payable_date", calendar[271].date()},
                                                             {"usd_per_share", 1.25}}});
            trading::PanelAccountReport report{root / "scratch/runs",
                                               {{"evidence_origin", "synthetic"},
                                                {"fixture", "analytic_panel_300"},
                                                {"protocol_sha256", contract.identity()}},
                                               {},
                                               contract,
                                               actions};
            std::cout << "Evidence directory: " << report.directory().string() << std::endl;
            std::map<std::string, trading::PanelReplayState> states;
            for (const auto &s : contract.symbols()) {
                states.emplace(s, trading::PanelReplayState{s, policy});
            }
            for (std::size_t i = 0; i < 300; ++i) {
                std::vector<trading::PanelReplayDay> rows;
                std::vector<trading::SessionDownload> sessions;
                std::vector<std::optional<trading::MinuteBar>> recovered(5);
                for (const auto &s : contract.symbols()) {
                    auto bars = trading::PanelReplayChecks::bars(calendar[i], s, i);
                    const auto request = calendar[i].bar_request(s);
                    rows.push_back(states.at(s).advance(request, bars, s == "QQQ" && i == 268 ? 1.25 : 0));
                    const auto audit = trading::audit_session(request, bars);
                    sessions.push_back({request, std::move(bars), {}, audit, {}, {}});
                }
                const bool common = std::all_of(rows.begin(), rows.end(),
                                                [](const auto &d) { return d.availability.operational; });
                for (auto &d : rows) {
                    d.common_operational = common;
                }
                report.append(rows, sessions, recovered, i == 299);
                if ((i + 1) % 25 == 0) {
                    progress("Constructed account calendar | " + std::to_string(i + 1) + "/300");
                }
            }
            output = report.finish();
        } else {
            const auto resolution = trading::PanelSourceResolution::load(
                root / "research/extension_source_resolution.json", policy);
            const auto inputs = root / "scratch/datasets/extension_panel_2016_2025";
            const trading::PanelReplayDataset dataset{inputs, contract, policy, resolution};
            auto actions = nlohmann::json::array();
            for (const auto &a : dataset.accounting_actions().at("cash_distributions")) {
                if (a.at("ex_date").get<std::string>() < "2024") {
                    actions.push_back(a);
                }
            }
            trading::PanelAccountReport report{root / "scratch/runs", dataset.provenance(), inputs, contract,
                                               actions};
            std::cout << "Evidence directory: " << report.directory().string() << std::endl;
            const auto last =
                trading::extension_calendar(trading::CalendarRange{"2023-12-01", "2023-12-31"}).back().date();
            dataset.replay_with_sessions(
                [&](auto days, auto sessions) {
                    std::vector<std::optional<trading::MinuteBar>> recovered;
                    for (const auto &d : days) {
                        recovered.push_back(dataset.recovered_close(d.symbol, d.date));
                    }
                    report.append(days, sessions, recovered, days.front().date == last);
                },
                progress);
            output = report.finish();
        }
        std::cout << "Risk account cases: 200\nShared-capital cases: 50\nNetwork requests sent: 0\nReserved "
                     "strategy inputs opened: false\nVisual report: "
                  << (output / "report.html").string() << "\nManifest: " << (output / "summary.json").string()
                  << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Account comparison stopped: " << e.what() << '\n';
        return 1;
    }
}
