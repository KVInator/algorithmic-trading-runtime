#include "artifact_io.hpp"
#include "console_progress.hpp"
#include "content_digest.hpp"
#include "cost_quote_diagnostics.hpp"
#include "json_input.hpp"
#include "quote_reference_study.hpp"
#include "utc_timestamp.hpp"
#include <iostream>

int main(int argc, char **argv) {
    try {
        if (argc > 2) {
            throw std::invalid_argument("usage: quote_reference_development [--check|saved_quote_summary]");
        }
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        const trading::ConsoleProgress progress;
        progress("M6 | cached historical quote crossing and fixed cost references");
        const auto output = trading::create_unique_directory(root / "scratch/runs", "quote_reference");
        nlohmann::json sample, authentication;
        if (argc == 2 && std::string_view{argv[1]} == "--check") {
            const auto plan = trading::CostQuotePlan::constructed();
            sample = trading::CostQuoteSample{output / "archive", plan}.run(
                true,
                [](const trading::HistoricalTickRequest &r, const std::optional<std::string> &token) {
                    if (token) {
                        throw std::invalid_argument("constructed quote fixture has only one page");
                    }
                    const auto minute = std::chrono::duration_cast<std::chrono::minutes>(
                                            r.end() - trading::parse_utc_seconds("2022-01-04T14:30:00Z"))
                                            .count();
                    nlohmann::json q{{"t", trading::format_utc_seconds(r.end())},
                                     {"bp", 100},
                                     {"ap", static_cast<double>(10002 + (minute % 3) * 2) / 100},
                                     {"bs", 2},
                                     {"as", 3},
                                     {"bx", "P"},
                                     {"ax", "P"},
                                     {"c", nlohmann::json::array({"R"})},
                                     {"z", "B"}};
                    if (r.symbol() == "GLD") {
                        q["c"] = nlohmann::json::array({"?"});
                    }
                    nlohmann::json rows = nlohmann::json::array();
                    if (r.symbol() != "TLT") {
                        rows.push_back(q);
                    }
                    return trading::MarketDataResponse{
                        200,
                        nlohmann::json{{"quotes", {{r.symbol(), rows}}}, {"next_page_token", nullptr}}.dump(),
                        {}};
                },
                [](auto) {});
            authentication = {{"mode", "constructed_archive"},
                              {"sample_file", (output / "sample.json").string()}};
        } else {
            const std::filesystem::path saved =
                argc == 2 ? std::filesystem::path{argv[1]} : root / "scratch/runs/cost_quotes_0/summary.json";
            const auto diagnostic =
                trading::CostQuoteDiagnostics{root / "scratch/datasets/extension_panel_2016_2025"}.inspect(
                    saved, progress);
            static_cast<void>(diagnostic);
            sample = trading::parse_json_document(trading::read_text_file(saved, 16U * 1024U * 1024U));
            authentication = {{"mode", "raw_archive_read_only_replay"},
                              {"sample_file", saved.string()},
                              {"sample_file_sha256", trading::sha256_file(saved, 16U * 1024U * 1024U)}};
        }
        trading::QuoteReferenceStudy{}.write(output, sample, authentication);
        std::cout << "Compared quote windows: " << sample.at("windows").size()
                  << "\nNetwork requests sent: 0\nVisual report: " << (output / "report.html").string()
                  << "\nManifest: " << (output / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Quote reference comparison stopped: " << e.what() << '\n';
        return 1;
    }
}
