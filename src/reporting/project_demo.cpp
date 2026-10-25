#include "project_demo.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "paper_broker_diagnostic.hpp"
#include "paper_transport_report.hpp"
#include "strategy_execution_report.hpp"
#include <algorithm>
#include <vector>

namespace trading {
ProjectDemoResult ProjectDemo::run(const std::filesystem::path &output_root) const {
    const auto directory = create_unique_directory(output_root, "project_demo");
    const auto strategies = directory / "strategies";
    const auto broker = directory / "broker";
    std::filesystem::create_directories(strategies);
    std::filesystem::create_directories(broker);
    const auto strategy_result = execution::StrategyExecutionDiagnostic{strategies}.run();
    execution::StrategyExecutionReport{strategy_result}.write(strategies);
    const auto broker_result = PaperBrokerDiagnostic{broker}.run();
    PaperTransportReport{broker_result}.write(broker);
    write_new_text_file(directory / "report.html",
                        R"html(<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Strategy and execution replay</title>
<style>body{font:17px system-ui;max-width:950px;margin:3rem auto;padding:0 1rem;color:#172f43;background:#f3f6fa}
article{padding:1.5rem;margin:1rem 0;background:white;border-top:4px solid #147d92}a{color:#126c85}li{margin:.6rem 0}</style>
<h1>Strategy decisions, orders and recovery</h1><p>Offline C++ demonstration with constructed prices, quotes,
broker messages and event times. No network connection or historical dataset is required.</p>
<article><h2>Strategy-to-order replay</h2><p>Inspect momentum, time-of-day continuation and opening-anchor reversion.
Completed information determines targets; delayed fills change cash, reservations and positions.</p>
<a href="strategies/report.html">Open strategy replay and account parity</a></article>
<article><h2>Broker lifecycle and durable recovery</h2><p>Inspect partial fills, cancellation races, duplicate
delivery, uncertain submission and restart. Missing executions and contradictory snapshots close routing.</p>
<a href="broker/report.html">Open broker lifecycle and recovery states</a></article>
<p>Expand saved events to inspect authoritative client and independent simulated-broker state. These are
synthetic protocol scenarios, separate from the registered historical strategy findings.</p>
<p><a href="summary.json">Artifact manifest</a></p></html>)html");
    nlohmann::json artifacts = nlohmann::json::array();
    std::vector<std::filesystem::path> files;
    for (const auto &file : std::filesystem::recursive_directory_iterator(directory))
        if (file.is_regular_file())
            files.push_back(file.path());
    std::sort(files.begin(), files.end());
    for (const auto &file : files)
        artifacts.push_back({{"path", std::filesystem::relative(file, directory).generic_string()},
                             {"sha256", sha256_file(file)}});
    const auto checks = strategy_result.checks.size() + broker_result.checks.size();
    const auto cases = strategy_result.cases.size() + broker_result.cases.size();
    write_new_text_file(directory / "summary.json", nlohmann::json{{"schema_version", 1},
                                                                   {"evidence_origin", "synthetic"},
                                                                   {"network_requests", 0},
                                                                   {"checks", checks},
                                                                   {"cases", cases},
                                                                   {"artifacts", artifacts}}
                                                            .dump(2) +
                                                        '\n');
    return {directory, checks, cases};
}
} // namespace trading
