#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "cost_delay_study.hpp"
#include "extension_contract.hpp"
#include "historical_panel.hpp"
#include "historical_panel_report.hpp"
#include "panel_input_finalizer.hpp"
#include "panel_replay_dataset.hpp"
#include "panel_replay_diagnostic.hpp"
#include "response_bytes.hpp"
#include "journal_storage.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using Json = nlohmann::json;

class PanelFixture {
public:
    trading::PanelFetchers fetchers() {
        return {
            [&](const trading::CalendarRange& range) {
                ++calls;
                Json body = Json::array();
                for (const auto& day : trading::extension_calendar(range)) {
                    body.push_back({{"date", day.date()}, {"open", "09:30"}, {"close", day.regular() ? "16:00" : "13:00"}});
                }
                if (bad_calendar) { body.at(0)["close"] = "15:00"; }
                return trading::MarketDataResponse{200, body.dump()};
            },
            [&](const trading::HistoricalBarRequest& request, const std::optional<std::string>& token) {
                ++calls;
                ++bar_calls;
                if (interrupt_at != 0 && bar_calls == interrupt_at) { throw std::runtime_error("constructed interruption"); }
                if (deny) { return trading::MarketDataResponse{401, "{}"}; }
                if (always_transient) { return trading::MarketDataResponse{503, "{}"}; }
                if (transient && !retried) { retried = true; return trading::MarketDataResponse{503, "{}"}; }
                const auto offset = token ? static_cast<std::size_t>(std::stoul(*token)) : 0U;
                Json all = Json::array();
                for (std::size_t i = 0; i < request.expected_minutes(); ++i) {
                    const auto day = trading::format_utc_seconds(request.start()).substr(0, 10);
                    if (closing_gaps && request.expected_minutes() == 390 && day == "2019-08-12" &&
                        request.symbol() != "QQQ" && request.symbol() != "TLT" && i == 389) { continue; }
                    if (request.symbol() == "GLD" && day == "2023-01-03" && i == 10) { continue; }
                    if (diagnostic_faults && ((request.symbol() == "GLD" && day == "2023-01-03" && (i == 11 || i == 12 || i == 20)) ||
                        (request.symbol() == "SPY" && day == "2023-01-03" && i == 389) ||
                        (request.symbol() == "GLD" && day == "2025-01-02" && i == 15))) { continue; }
                    const auto at = request.start() + std::chrono::minutes{static_cast<int>(i)};
                    all.push_back({{"t", trading::format_utc_seconds(at)}, {"o", 100}, {"h", 101}, {"l", 99}, {"c", 100.5}, {"v", 1000}});
                }
                Json values = Json::array();
                const auto end = std::min(offset + 100, all.size());
                for (auto i = offset; i < end; ++i) { values.push_back(all.at(i)); }
                return trading::MarketDataResponse{200, Json{{"bars", {{request.symbol(), values}}},
                    {"next_page_token", end < all.size() ? Json(std::to_string(end)) : Json(nullptr)}}.dump()};
            },
            [&](const trading::CorporateActionRequest& request, const std::optional<std::string>&) {
                ++calls;
                Json rows = Json::array();
                if (request.symbol() != "GLD" && request.first() < "2026") {
                    const auto year = request.first().substr(0, 4);
                    rows.push_back({{"id", request.symbol() + year}, {"symbol", request.symbol()}, {"process_date", year + "-06-01"},
                        {"ex_date", year + "-06-01"}, {"record_date", year + "-06-02"}, {"payable_date", year + "-06-10"}, {"rate", 0.5}});
                    if (year == "2022") { rows.at(0).erase("record_date"); rows.at(0).erase("payable_date"); }
                    if (diagnostic_faults && request.symbol() == "QQQ" && year == "2023") {
                        auto duplicate = rows.at(0);
                        duplicate["id"] = "QQQ2023second";
                        duplicate["payable_date"] = "2023-06-11";
                        rows.push_back(std::move(duplicate));
                    }
                }
                return trading::MarketDataResponse{200, Json{{"corporate_actions", {{"cash_dividends", rows}}}, {"next_page_token", nullptr}}.dump()};
            }};
    }
    std::size_t calls{}, bar_calls{}, interrupt_at{};
    bool deny{}, transient{}, always_transient{}, retried{}, bad_calendar{}, diagnostic_faults{}, closing_gaps{};
};

class PanelChecks {
public:
    void run() {
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        const auto contract = trading::ExtensionStudyContract::load(root / "research/extension_protocol.json");
        const auto input_policy = trading::PanelInputPolicy::load(root / "research/extension_input_policy.json", contract);
        const auto resolution = trading::PanelSourceResolution::load(root / "research/extension_source_resolution.json", input_policy);
        directory_ = trading::create_unique_directory(root / "scratch/runs", "panel_check");
        const std::string larger_artifact(4U * 1024U * 1024U + 1, 'x');
        const auto larger_file = directory_ / "large_artifact.txt";
        trading::write_new_text_file(larger_file, larger_artifact);
        reject("ordinary file hashing retains the four-MiB bound", [&] { static_cast<void>(trading::sha256_file(larger_file)); });
        check("registered larger files use an explicit hashing bound", trading::sha256_file(larger_file, 16U * 1024U * 1024U) == trading::sha256(larger_artifact));
        reject("explicit hashing bound is enforced", [&] { static_cast<void>(trading::sha256_file(larger_file, larger_artifact.size() - 1)); });
        const trading::CalendarSession policy_day{"2023-01-04", "09:30", "16:00"};
        const auto missing_at = [&](std::initializer_list<int> slots) {
            std::vector<std::chrono::sys_seconds> result;
            for (const int slot : slots) { result.push_back(policy_day.open_utc() + std::chrono::minutes{slot}); }
            return result;
        };
        const auto full = input_policy.evaluate(policy_day, "SPY", {});
        check("complete inputs retain every feature and execution reference", full.operational && full.momentum_features && full.reversion_features &&
            std::all_of(full.continuation_prefixes.begin(), full.continuation_prefixes.end(), [](bool value) { return value; }));
        check("execution references derive from all registered delays and holding periods", input_policy.execution_slots().size() == 33 &&
            input_policy.execution_slots().front() == 0 && input_policy.execution_slots().back() == 387);
        const auto middle = input_policy.evaluate(policy_day, "SPY", missing_at({100}));
        check("irrelevant missing minute preserves endpoint and fixed-grid features", middle.operational && middle.momentum_features && middle.reversion_features);
        check("volume prefix failure does not backdate to earlier decisions", middle.continuation_prefixes[2] && !middle.continuation_prefixes[3]);
        check("one-minute reversion grid requires its actual additional observations", !input_policy.evaluate(policy_day, "SPY", missing_at({100}), 1).reversion_features);
        check("fifteen-minute reversion grid preserves fixed clock endpoints", input_policy.evaluate(policy_day, "SPY", missing_at({100}), 15).reversion_features);
        check("missing anchor minute cannot become zero volume", !input_policy.evaluate(policy_day, "SPY", missing_at({5})).reversion_features);
        const auto signal_gap = input_policy.evaluate(policy_day, "SPY", missing_at({359}));
        check("signal availability is separate from execution observability", signal_gap.operational && !signal_gap.momentum_features);
        const auto fill_gap = input_policy.evaluate(policy_day, "SPY", missing_at({151}));
        check("future missing execution reference cannot alter past reversion features", !fill_gap.operational && fill_gap.reversion_features);
        check("missing closing mark is not an imputed execution quote", input_policy.evaluate(policy_day, "SPY", missing_at({389})).operational);
        check("shortened session stays outside primary comparisons", !input_policy.evaluate({"2022-11-25", "09:30", "13:00"}, "SPY", {}).operational);
        check("diagnostic exclusion is symbol specific under amendment", !input_policy.evaluate({"2023-01-03", "09:30", "16:00"}, "SPY", {}).operational &&
            input_policy.evaluate({"2023-01-03", "09:30", "16:00"}, "QQQ", {}).operational);
        check("halt context does not itself fabricate a missing interval", input_policy.evaluate({"2020-03-09", "09:30", "16:00"}, "SPY", {}).known_halt_date);
        reject("duplicate missing slot rejected", [&] { static_cast<void>(input_policy.evaluate(policy_day, "SPY", missing_at({1, 1}))); });
        reject("out of order missing slots rejected", [&] { static_cast<void>(input_policy.evaluate(policy_day, "SPY", missing_at({10, 9}))); });
        reject("outside-session missing slot rejected", [&] { static_cast<void>(input_policy.evaluate(policy_day, "SPY", missing_at({390}))); });
        reject("unregistered observation step rejected", [&] { static_cast<void>(input_policy.evaluate(policy_day, "SPY", {}, 0)); });
        reject("unregistered policy symbol rejected", [&] { static_cast<void>(input_policy.evaluate(policy_day, "DIA", {})); });
        trading::write_new_text_file(directory_ / "altered_input_policy.json", trading::read_text_file(root / "research/extension_input_policy.json") + " ");
        reject("altered amendment rejected", [&] { static_cast<void>(trading::PanelInputPolicy::load(directory_ / "altered_input_policy.json", contract)); });
        check("five fixed symbols", contract.symbols().size() == 5);
        const std::string malformed_bytes{"\xff\0\n", 3};
        check("raw byte archival is lossless for non-UTF-8 bodies", trading::decode_response_bytes(trading::encode_response_bytes(malformed_bytes)) == malformed_bytes);
        reject("invalid raw byte encoding rejected", [&] { static_cast<void>(trading::decode_response_bytes("0g")); });
        check("six primary endpoints", contract.document().at("primary_endpoints").size() == 6);
        reject("2024 strategy access closed", [&] { contract.require_development_access("2024-01-02"); });
        reject("2025 strategy access closed", [&] { contract.require_development_access("2025-01-02"); });
        contract.require_development_access("2023-12-29");
        check("2023 development access retained", true);
        check("SPY diagnostic exclusion retained", contract.diagnostic_excluded("SPY", "2023-01-03") && !contract.diagnostic_excluded("QQQ", "2023-01-03"));
        constexpr std::array<std::size_t, 10> expected{252, 251, 251, 252, 253, 252, 251, 250, 252, 250};
        for (int year = 2016; year <= 2025; ++year) {
            std::size_t count = 0;
            for (unsigned month = 1; month <= 12; ++month) {
                const auto id = std::to_string(year) + (month < 10 ? "-0" : "-") + std::to_string(month);
                const auto end = std::chrono::year_month_day{std::chrono::year{year}/std::chrono::month{month}/std::chrono::last};
                const trading::CalendarRange range{id + "-01", id + '-' + std::to_string(static_cast<unsigned>(end.day()))};
                const auto days = trading::extension_calendar(range);
                count += days.size();
                if (year == 2022 || year == 2023) {
                    const auto legacy = trading::development_calendar(range);
                    check("legacy calendar parity " + id, days.size() == legacy.size() && std::equal(days.begin(), days.end(), legacy.begin(),
                        [](const auto& a, const auto& b) { return a.date() == b.date() && a.open_utc() == b.open_utc() && a.close_utc() == b.close_utc(); }));
                }
            }
            check("annual calendar count " + std::to_string(year), count == expected.at(static_cast<std::size_t>(year - 2016)));
        }
        check("Bush mourning closure", trading::extension_calendar({"2018-12-05", "2018-12-05"}).empty());
        check("Carter mourning closure", trading::extension_calendar({"2025-01-09", "2025-01-09"}).empty());
        check("Saturday New Year does not close Friday", trading::extension_calendar({"2021-12-31", "2021-12-31"}).size() == 1);
        const std::vector<trading::CalendarRange> ranges{{"2022-11-25", "2022-11-25"}, {"2023-01-03", "2023-01-03"}, {"2025-01-02", "2025-01-02"}};
        reject("partial provider plan rejected", [&] { const trading::HistoricalPanelDataset invalid{directory_ / "invalid", contract, trading::PanelOrigin::provider, ranges}; });
        const auto no_wait = [](std::chrono::milliseconds) {};
        const auto locked_directory = directory_ / "locked";
        std::filesystem::create_directories(locked_directory);
        auto held = trading::execution::create_journal_file(locked_directory / "acquisition.lock");
        PanelFixture competing;
        const trading::HistoricalPanelDataset locked{locked_directory, contract, trading::PanelOrigin::synthetic, ranges};
        reject("second acquisition writer is rejected", [&] { static_cast<void>(locked.collect(competing.fetchers(), no_wait)); });
        check("competing writer sends no request", competing.calls == 0);
        held.reset();
        const trading::HistoricalPanelDataset dataset{directory_ / "complete", contract, trading::PanelOrigin::synthetic, ranges};
        PanelFixture fixture;
        auto summary = dataset.collect(fixture.fetchers(), no_wait);
        const auto original_calls = fixture.calls;
        check("fifteen symbol-days", summary.at("coverage").size() == 15);
        check("short sessions retained for daily inputs", summary.at("coverage").at(0).at("expected_minutes").get<int>() == 210 &&
            !summary.at("coverage").at(0).at("intraday_eligible").get<bool>());
        check("missing minute retained", summary.at("coverage").at(9).at("missing_minutes").get<int>() == 1);
        check("common mask keeps gaps and diagnostic exclusion", !summary.at("coverage").at(5).at("common_intraday_eligible").get<bool>());
        check("reserved coverage is metadata only", summary.at("coverage").at(10).at("reserved").get<bool>() && !summary.at("strategy_scored").get<bool>());
        const auto resumed = dataset.collect(fixture.fetchers(), no_wait);
        check("resume sends no request", fixture.calls == original_calls && resumed.at("network_requests").get<int>() == 0);
        check("audit coverage identical", dataset.audit().at("coverage") == summary.at("coverage"));
        const auto acquisition_sha = summary.at("acquisition_manifest_sha256").get<std::string>();
        check("development reader returns authenticated bars", dataset.load_development_session("QQQ", "2023-01-03", acquisition_sha).bars.size() == 390);
        reject("price reader blocks reserved dates before lookup", [&] { static_cast<void>(dataset.load_development_session("SPY", "2025-01-02", acquisition_sha)); });
        reject("price reader blocks unregistered symbol", [&] { static_cast<void>(dataset.load_development_session("DIA", "2023-01-03", acquisition_sha)); });
        reject("reader requires caller supplied dataset identity", [&] { static_cast<void>(dataset.load_development_session("SPY", "2023-01-03", std::string(64, '0'))); });
        const auto report = trading::HistoricalPanelReport{}.write(summary, directory_ / "reports");
        check("price-free report saved", std::filesystem::exists(report / "coverage.csv"));

        const auto issuer = directory_ / "issuer";
        trading::write_new_text_file(issuer / "source.txt", "Constructed full-horizon distribution and split record; no market evidence.\n");
        Json reference{{"schema_version", 1}, {"evidence_origin", "synthetic"}, {"first_ex_date", "2016-01-01"}, {"last_ex_date", "2025-12-31"},
            {"sources", {{{"file", "source.txt"}, {"sha256", trading::sha256_file(issuer / "source.txt")},
                {"url", "https://example.invalid/fixture"}, {"retrieved_utc", "2026-10-26T00:00:00Z"}}}},
            {"coverage", Json::array()}, {"cash_distributions", Json::array()}, {"splits", Json::array()}};
        for (const auto& symbol : contract.symbols()) {
            reference["coverage"].push_back({{"symbol", symbol}, {"cash_complete", true}, {"splits_complete", true},
                {"source_files", {"source.txt"}}, {"coverage_locator", "constructed whole-horizon fixture"}});
            if (symbol == "GLD") { continue; }
            for (const auto year : {"2022", "2023", "2025"}) {
                reference["cash_distributions"].push_back({{"symbol", symbol}, {"ex_date", std::string{year} + "-06-01"},
                    {"record_date", std::string{year} + "-06-02"}, {"payable_date", std::string{year} + "-06-10"},
                    {"usd_per_share", 0.5}, {"source_file", "source.txt"}, {"source_locator", symbol + year}});
            }
        }
        reference["cash_distributions"].push_back({{"symbol", "SPY"}, {"ex_date", "2019-06-03"}, {"record_date", "2019-06-04"},
            {"payable_date", "2019-06-10"}, {"usd_per_share", 0.4}, {"source_file", "source.txt"}, {"source_locator", "issuer-only constructed record"}});
        trading::write_new_text_file(issuer / "reference.json", reference.dump(2) + '\n');
        const trading::PanelActionReference action_reference;
        const auto action_result = action_reference.reconcile(directory_ / "complete", issuer / "reference.json", contract, trading::PanelOrigin::synthetic);
        check("issuer dates complete absent provider fields", action_result.at("issuer_reconciled").get<bool>() && action_result.at("issuer_completed_date_fields").get<int>() == 8);
        check("issuer-only distribution retained explicitly", action_result.at("issuer_only_cash_records").get<int>() == 1);
        check("GLD empty history requires source coverage", reference.at("coverage").size() == 5);
        auto contradiction = reference;
        contradiction["cash_distributions"][0]["usd_per_share"] = 0.6;
        trading::write_new_text_file(issuer / "contradiction.json", contradiction.dump(2) + '\n');
        check("contradictory amount blocks reconciliation", !action_reference.reconcile(directory_ / "complete", issuer / "contradiction.json", contract,
            trading::PanelOrigin::synthetic).at("issuer_reconciled").get<bool>());
        auto incomplete = reference;
        incomplete["coverage"][0]["splits_complete"] = false;
        incomplete["coverage"][1]["splits_complete"] = false;
        incomplete["cash_distributions"][0]["usd_per_share"] = 0.6;
        trading::write_new_text_file(issuer / "incomplete.json", incomplete.dump(2) + '\n');
        const auto incomplete_actions = action_reference.reconcile(directory_ / "complete", issuer / "incomplete.json", contract, trading::PanelOrigin::synthetic);
        check("cash history does not establish split completeness", !incomplete_actions.at("issuer_reconciled").get<bool>());
        check("incomplete coverage reports all symbols and amount contradictions", incomplete_actions.at("blockers").size() == 3 &&
            incomplete_actions.at("source_coverage").size() == 5 && incomplete_actions.at("development_cash_discrepancies").size() == 1);
        auto bad_source = reference;
        bad_source["sources"][0]["sha256"] = std::string(64, '0');
        trading::write_new_text_file(issuer / "bad_source.json", bad_source.dump(2) + '\n');
        reject("altered issuer source rejected", [&] { static_cast<void>(action_reference.reconcile(directory_ / "complete", issuer / "bad_source.json", contract, trading::PanelOrigin::synthetic)); });
        trading::FrozenDevelopmentDataset legacy{};
        legacy.manifest_sha256 = "constructed_legacy_fixture";
        legacy.sessions.push_back(dataset.load_development_session("SPY", "2023-01-03", acquisition_sha));
        const trading::PanelInputFinalizer finalizer{directory_ / "complete", contract, trading::PanelOrigin::synthetic};
        const auto missing_reference = finalizer.assess(summary, action_reference.reconcile(directory_ / "complete",
            issuer / "absent.json", contract, trading::PanelOrigin::synthetic));
        check("absent issuer reference still reports price gaps", !missing_reference.at("input_gates_passed").get<bool>() &&
            missing_reference.at("development_gap_intervals").size() == 1);
        const auto readiness = finalizer.assess(summary, incomplete_actions, &input_policy);
        check("assessment does not turn incomplete actions into ready inputs", !readiness.at("input_gates_passed").get<bool>() && !readiness.at("research_inputs_ready").get<bool>());
        check("single missing minute has exact half-open interval", readiness.at("development_gap_intervals").size() == 1 &&
            readiness.at("development_gap_intervals").at(0).at("start_utc").get<std::string>() == "2023-01-03T14:40:00Z" &&
            readiness.at("development_gap_intervals").at(0).at("end_exclusive_utc").get<std::string>() == "2023-01-03T14:41:00Z");
        const auto readiness_report = trading::HistoricalPanelReport{}.write_readiness(readiness, directory_ / "reports");
        check("readiness report contains structured evidence and HTML", std::filesystem::exists(readiness_report / "summary.json") && std::filesystem::exists(readiness_report / "report.html"));
        auto wrong_identity = summary;
        wrong_identity["acquisition_manifest_sha256"] = std::string(64, '0');
        reject("assessment rejects a different acquisition identity", [&] { static_cast<void>(finalizer.assess(wrong_identity, action_result)); });
        const auto registered = finalizer.finalize(summary, action_result, legacy);
        check("study manifest published only after prerequisites", registered.at("research_inputs_ready").get<bool>() && !registered.at("strategy_scored").get<bool>());
        check("reserved prices remain closed after registration", !registered.at("reserved_strategy_inputs_opened").get<bool>());
        check("registration is idempotent", finalizer.finalize(summary, action_result, legacy) == registered);
        reject("unreconciled actions cannot register inputs", [&] { static_cast<void>(finalizer.finalize(summary, Json{{"issuer_reconciled", false}}, legacy)); });

        PanelFixture diagnostic_fixture;
        diagnostic_fixture.diagnostic_faults = true;
        const trading::HistoricalPanelDataset diagnostic_dataset{directory_ / "diagnosis", contract, trading::PanelOrigin::synthetic, ranges};
        const auto diagnostic_summary = diagnostic_dataset.collect(diagnostic_fixture.fetchers(), no_wait);
        auto diagnostic_reference = incomplete;
        diagnostic_reference["cash_distributions"][2]["usd_per_share"] = 0.7;
        trading::write_new_text_file(issuer / "diagnostic.json", diagnostic_reference.dump(2) + '\n');
        const auto diagnostic_actions = action_reference.reconcile(directory_ / "diagnosis", issuer / "diagnostic.json", contract, trading::PanelOrigin::synthetic);
        const trading::PanelInputFinalizer diagnostic_finalizer{directory_ / "diagnosis", contract, trading::PanelOrigin::synthetic};
        const auto diagnostic = diagnostic_finalizer.assess(diagnostic_summary, diagnostic_actions);
        check("daily endpoint gap is reported alongside action blockers", diagnostic.at("missing_daily_endpoints").size() == 1 && diagnostic.at("actions").at("blockers").size() > 3);
        check("contiguous missing minutes merge without bridging a valid minute", diagnostic.at("development_gap_intervals").size() == 3 &&
            diagnostic.at("development_gap_intervals").at(1).at("missing_minutes").get<int>() == 3 &&
            diagnostic.at("development_gap_intervals").at(2).at("missing_minutes").get<int>() == 1);
        check("reserved gap details stay omitted", diagnostic.at("reserved_gapped_symbol_days_details_omitted").get<int>() == 1 &&
            std::all_of(diagnostic.at("development_gap_intervals").begin(), diagnostic.at("development_gap_intervals").end(),
                [](const auto& gap) { return gap.at("date").template get<std::string>() < "2024"; }));
        check("duplicate action cash is retained as a discrepancy", diagnostic_actions.at("development_cash_discrepancies").size() == 2 &&
            diagnostic_actions.at("development_cash_discrepancies").at(0).at("provider_records").size() == 2);
        check("reserved action amounts stay omitted", std::all_of(diagnostic_actions.at("development_cash_discrepancies").begin(), diagnostic_actions.at("development_cash_discrepancies").end(),
            [](const auto& comparison) { return comparison.at("symbol_ex_date").template get<std::string>().substr(4) < "2024"; }));
        check("blocked reconciliation cannot publish canonical actions", !std::filesystem::exists(directory_ / "diagnosis/sealed/accounting_actions.json"));
        reject("daily endpoint gaps block registration with otherwise reconciled actions", [&] { static_cast<void>(diagnostic_finalizer.finalize(diagnostic_summary, action_result, legacy)); });
        check("assessment cannot publish the study manifest", !std::filesystem::exists(directory_ / "diagnosis/study_manifest.json"));
        const auto diagnostic_report = trading::HistoricalPanelReport{}.write_readiness(diagnostic, directory_ / "reports");

        PanelFixture closing_fixture;
        closing_fixture.closing_gaps = true;
        const trading::HistoricalPanelDataset closing_dataset{directory_ / "closing", contract, trading::PanelOrigin::synthetic,
            {{"2019-08-12", "2019-08-12"}}};
        const auto closing_summary = closing_dataset.collect(closing_fixture.fetchers(), no_wait);
        const auto closing_acquisition = closing_summary.at("acquisition_manifest_sha256").get<std::string>();
        const auto recovered = closing_dataset.recover_closing_marks(input_policy, closing_summary, closing_fixture.fetchers().bars, no_wait, true);
        check("only the three registered exact closing slots are requested", recovered.summary.at("network_requests").get<int>() == 3 && recovered.bars.size() == 3);
        check("recovery preserves the original acquisition identity", trading::sha256_file(directory_ / "closing/acquisition_manifest.json") == closing_acquisition);
        check("closing recovery resumes without network calls", closing_dataset.recover_closing_marks(input_policy, closing_summary,
            closing_fixture.fetchers().bars, no_wait, true).summary.at("network_requests").get<int>() == 0);
        check("closing recovery can be authenticated offline", closing_dataset.recover_closing_marks(input_policy, closing_summary, {}, no_wait, false).bars.size() == 3);
        const trading::PanelInputFinalizer closing_finalizer{directory_ / "closing", contract, trading::PanelOrigin::synthetic};
        auto closing_reference = reference;
        closing_reference["cash_distributions"] = Json::array();
        for (const auto& symbol : contract.symbols()) {
            if (symbol == "GLD") { continue; }
            closing_reference["cash_distributions"].push_back({{"symbol", symbol}, {"ex_date", "2019-06-01"}, {"record_date", "2019-06-02"},
                {"payable_date", "2019-06-10"}, {"usd_per_share", 0.5}, {"source_file", "source.txt"}, {"source_locator", "constructed 2019 issuer source"}});
        }
        trading::write_new_text_file(issuer / "closing.json", closing_reference.dump(2) + '\n');
        const auto closing_actions = action_reference.reconcile(directory_ / "closing", issuer / "closing.json", contract, trading::PanelOrigin::synthetic);
        const auto closing_readiness = closing_finalizer.assess(closing_summary, closing_actions, &input_policy);
        check("recovered marks do not erase original endpoint failures", closing_readiness.at("missing_daily_endpoints").size() == 3 && closing_readiness.at("remaining_daily_endpoints").empty());
        check("all amended input gates pass only after real fixture marks are present", closing_readiness.at("active_input_gates_passed").get<bool>());
        const auto closing_registered = closing_finalizer.finalize(closing_summary, closing_actions, {}, {}, {}, &input_policy);
        check("registration binds the amendment and recovery identities", closing_registered.at("input_policy_sha256").get<std::string>() == input_policy.identity() &&
            closing_registered.at("closing_recovery_manifest_sha256") == recovered.summary.at("manifest_sha256"));
        const auto recovered_masks = Json::parse(trading::read_text_file(directory_ / "closing/eligibility_masks.json"));
        check("daily readiness uses recovered marks while preserving original missing flags", !recovered_masks.at("rows").at(0).at("close_available").get<bool>() &&
            recovered_masks.at("rows").at(0).at("daily_close_available").get<bool>());
        const auto closing_report = trading::HistoricalPanelReport{}.write_readiness(closing_readiness, directory_ / "reports");
        const trading::HistoricalPanelDataset empty_recovery_dataset{directory_ / "closing_empty", contract, trading::PanelOrigin::synthetic,
            {{"2019-08-12", "2019-08-12"}}};
        const auto empty_summary = empty_recovery_dataset.collect(closing_fixture.fetchers(), no_wait);
        const trading::BarPageFetcher empty_fetch = [](const auto&, const auto&) {
            return trading::MarketDataResponse{200, Json{{"bars", Json::object()}, {"next_page_token", nullptr}}.dump()};
        };
        const auto unavailable = empty_recovery_dataset.recover_closing_marks(input_policy, empty_summary, empty_fetch, no_wait, true);
        check("empty recovery response cannot create a closing price", unavailable.bars.empty() && !unavailable.summary.at("complete").get<bool>());
        check("empty successful response is not repeatedly re-probed", empty_recovery_dataset.recover_closing_marks(input_policy, empty_summary,
            empty_fetch, no_wait, true).summary.at("network_requests").get<int>() == 0);
        const auto empty_readiness = trading::PanelInputFinalizer{directory_ / "closing_empty", contract, trading::PanelOrigin::synthetic}.assess(empty_summary,
            action_reference.reconcile(directory_ / "closing_empty", issuer / "absent.json", contract, trading::PanelOrigin::synthetic), &input_policy);
        check("empty recovery preserves every unresolved daily endpoint", empty_readiness.at("remaining_daily_endpoints").size() == 3 && !empty_readiness.at("active_coverage_gates_passed").get<bool>());
        static_cast<void>(trading::HistoricalPanelReport{}.write_readiness(empty_readiness, directory_ / "reports"));

        // Resolved source receipts are deliberately constructed, not market evidence.
        auto resolved_reference = closing_reference;
        resolved_reference["schema_version"] = 2;
        resolved_reference["source_resolution_sha256"] = resolution.identity();
        Json monthly = Json::array();
        for (int year = 2016; year <= 2025; ++year) {
            for (int month = 1; month <= 12; ++month) {
                const auto clock = std::to_string(year) + '-' + (month < 10 ? "0" : "") + std::to_string(month) + "-01T00:00:00Z";
                monthly.push_back(trading::parse_utc_seconds(clock).time_since_epoch().count());
            }
        }
        trading::write_new_text_file(issuer / "gld_statement.html", resolution.document().at("gld_issuer_statement").get<std::string>());
        resolved_reference["sources"].push_back({{"file", "gld_statement.html"}, {"sha256", trading::sha256_file(issuer / "gld_statement.html")},
            {"url", resolution.document().at("gld_issuer_url")}, {"retrieved_utc", "2026-10-28T00:00:00Z"}});
        for (auto& coverage_row : resolved_reference["coverage"]) {
            const auto symbol = coverage_row.at("symbol").get<std::string>();
            const auto file = symbol + "_events.json";
            const Json event_feed{{"chart", {{"error", nullptr}, {"result", {{{"meta", {{"symbol", symbol}, {"currency", "USD"},
                {"dataGranularity", "1mo"}}}, {"timestamp", monthly}, {"events", Json::object()}}}}}}};
            trading::write_new_text_file(issuer / file, event_feed.dump());
            auto url = resolution.document().at("secondary_event_url").get<std::string>();
            url.replace(url.find("{symbol}"), 8, symbol);
            resolved_reference["sources"].push_back({{"file", file}, {"sha256", trading::sha256_file(issuer / file)},
                {"url", url}, {"retrieved_utc", "2026-10-28T00:00:00Z"}, {"kind", "secondary_events"}, {"symbol", symbol}});
            coverage_row["source_files"].push_back(file);
            if (symbol == "GLD") { coverage_row["source_files"].push_back("gld_statement.html"); }
        }
        auto& qqq_cash = resolved_reference["cash_distributions"].at(1);
        check("constructed canonical source order is explicit", qqq_cash.at("symbol") == "QQQ");
        qqq_cash["usd_per_share"] = 0.55;
        qqq_cash["payable_date"] = "2019-06-11";
        trading::write_new_text_file(issuer / "resolved.json", resolved_reference.dump(2) + '\n');
        const auto resolved_actions = action_reference.reconcile(directory_ / "closing_empty", issuer / "resolved.json", contract,
            trading::PanelOrigin::synthetic, &resolution);
        check("registered issuer authority resolves nonempty provider amount and date conflicts", resolved_actions.at("issuer_reconciled").get<bool>() &&
            resolved_actions.at("resolved_provider_cash_conflicts").size() == 2);
        const auto canonical_resolved = Json::parse(trading::read_text_file(directory_ / "closing_empty/sealed/accounting_actions.json"));
        check("issuer amount is retained without averaging provider observations", canonical_resolved.at("cash_distributions").at(1).at("usd_per_share").get<double>() == 0.55);
        const trading::PanelInputFinalizer empty_finalizer{directory_ / "closing_empty", contract, trading::PanelOrigin::synthetic};
        auto resolved_readiness = empty_finalizer.assess(empty_summary, resolved_actions, &input_policy, &resolution);
        check("only explicitly registered unavailable closing marks permit registration", resolved_readiness.at("active_input_gates_passed").get<bool>() &&
            resolved_readiness.at("remaining_daily_endpoints").size() == 3 && resolved_readiness.at("unavailable_daily_marks_accepted").get<bool>());
        const auto resolved_registered = empty_finalizer.finalize(empty_summary, resolved_actions, {}, {}, {}, &input_policy, &resolution);
        check("source resolution and original missing observations are bound to the study", resolved_registered.at("source_resolution_sha256") == resolution.identity() &&
            resolved_registered.at("unavailable_daily_endpoints").size() == 3);
        const auto absent_masks = Json::parse(trading::read_text_file(directory_ / "closing_empty/eligibility_masks.json"));
        for (const auto& mask : absent_masks.at("rows")) {
            if (mask.at("symbol") == "QQQ" || mask.at("symbol") == "TLT") { continue; }
            check("unavailable closes remain NA and cannot form a daily observation", !mask.at("daily_close_available").get<bool>() &&
                mask.at("daily_close_source") == "unavailable" && !mask.at("daily_risk_inputs_ready").get<bool>());
        }
        const Json recovery_identity{{"manifest_sha256", "constructed"}, {"required", 3}};
        auto extra_gap = resolved_readiness.at("remaining_daily_endpoints");
        extra_gap.at(0)["date"] = "2019-08-13";
        check("new daily gaps are never implicitly waived", !resolution.permits_missing_closes(extra_gap, recovery_identity));
        extra_gap = resolved_readiness.at("remaining_daily_endpoints");
        extra_gap.at(0)["open_available"] = false;
        check("unavailable opens remain blocking", !resolution.permits_missing_closes(extra_gap, recovery_identity));
        check("missing source attempts cannot justify a waiver", !resolution.permits_missing_closes(resolved_readiness.at("remaining_daily_endpoints"), Json::object()));
        auto missing_secondary = resolved_reference;
        missing_secondary["sources"].erase(missing_secondary["sources"].size() - 1);
        trading::write_new_text_file(issuer / "missing_secondary.json", missing_secondary.dump());
        reject("missing secondary event feed cannot assert split coverage", [&] { static_cast<void>(action_reference.reconcile(directory_ / "closing_empty",
            issuer / "missing_secondary.json", contract, trading::PanelOrigin::synthetic, &resolution)); });
        const auto reject_secondary = [&](const std::string& label, const std::string& symbol, const Json& payload, const std::string& file) {
            trading::write_new_text_file(issuer / file, payload.dump());
            auto invalid = resolved_reference;
            for (auto& source : invalid["sources"]) {
                if (source.value("kind", std::string{}) == "secondary_events" && source.at("symbol") == symbol) {
                    source["file"] = file;
                    source["sha256"] = trading::sha256_file(issuer / file);
                }
            }
            for (auto& row : invalid["coverage"]) {
                if (row.at("symbol") != symbol) { continue; }
                for (auto& source : row["source_files"]) { if (source == symbol + "_events.json") { source = file; } }
            }
            const auto reference_file = issuer / (file + ".reference.json");
            trading::write_new_text_file(reference_file, invalid.dump());
            reject(label, [&] { static_cast<void>(action_reference.reconcile(directory_ / "closing_empty", reference_file,
                contract, trading::PanelOrigin::synthetic, &resolution)); });
        };
        const auto spy_feed = Json::parse(trading::read_text_file(issuer / "SPY_events.json"));
        auto invalid_feed = spy_feed;
        invalid_feed["chart"]["result"][0]["timestamp"].erase(0);
        reject_secondary("truncated event horizon cannot establish split absence", "SPY", invalid_feed, "truncated_events.json");
        invalid_feed = spy_feed;
        invalid_feed["chart"]["result"][0]["meta"]["symbol"] = "QQQ";
        reject_secondary("another instrument cannot establish split absence", "SPY", invalid_feed, "wrong_symbol_events.json");
        invalid_feed = spy_feed;
        invalid_feed["chart"]["result"][0]["events"]["splits"] = {{"example", {{"numerator", 2}, {"denominator", 1}}}};
        reject_secondary("nonempty split evidence cannot be discarded", "SPY", invalid_feed, "nonempty_splits.json");
        invalid_feed = Json::parse(trading::read_text_file(issuer / "GLD_events.json"));
        invalid_feed["chart"]["result"][0]["events"]["dividends"] = {{"example", {{"amount", 0.5}}}};
        reject_secondary("GLD cash contradictions remain blocking", "GLD", invalid_feed, "gld_cash_events.json");
        auto wrong_resolution = resolved_actions;
        wrong_resolution["source_resolution_sha256"] = std::string(64, '0');
        reject("registration rejects inconsistent source-resolution identities", [&] {
            static_cast<void>(empty_finalizer.finalize(empty_summary, wrong_resolution, {}, {}, {}, &input_policy, &resolution));
        });

        auto duplicate_reference = resolved_reference;
        duplicate_reference["cash_distributions"] = reference.at("cash_distributions");
        trading::write_new_text_file(issuer / "resolved_duplicates.json", duplicate_reference.dump());
        const auto duplicate_actions = action_reference.reconcile(directory_ / "diagnosis", issuer / "resolved_duplicates.json", contract,
            trading::PanelOrigin::synthetic, &resolution);
        check("duplicate announcements resolve to one issuer cash event", duplicate_actions.at("issuer_reconciled").get<bool>() &&
            duplicate_actions.at("resolved_provider_cash_conflicts").size() == 2);
        const auto duplicate_canonical = Json::parse(trading::read_text_file(directory_ / "diagnosis/sealed/accounting_actions.json"));
        std::size_t qqq_events = 0;
        for (const auto& row : duplicate_canonical.at("cash_distributions")) {
            if (row.at("symbol") == "QQQ" && row.at("ex_date") == "2023-06-01") {
                ++qqq_events;
                check("duplicate provider records do not double cash income", row.at("usd_per_share").get<double>() == 0.5);
            }
        }
        check("exactly one cash entitlement survives duplicate announcements", qqq_events == 1);
        auto provider_only = duplicate_reference;
        provider_only["cash_distributions"].erase(3);
        trading::write_new_text_file(issuer / "resolved_provider_only.json", provider_only.dump());
        check("provider-only cash remains blocking under issuer authority", !action_reference.reconcile(directory_ / "complete",
            issuer / "resolved_provider_only.json", contract, trading::PanelOrigin::synthetic, &resolution).at("issuer_reconciled").get<bool>());
        resolved_readiness["registration"] = {{"study_manifest_sha256", trading::sha256_file(directory_ / "closing_empty/study_manifest.json")},
            {"vintage_bridge_sha256", resolved_registered.at("vintage_bridge_sha256")}, {"research_inputs_ready", true},
            {"strategy_scored", false}, {"reserved_strategy_inputs_opened", false}};
        const auto resolved_report = trading::HistoricalPanelReport{}.write_readiness(resolved_readiness, directory_ / "reports");
        const trading::PanelReplayDataset replay_inputs{directory_ / "closing_empty", contract, input_policy, resolution,
            trading::PanelOrigin::synthetic, trading::sha256_file(directory_ / "closing_empty/study_manifest.json")};
        trading::PanelReplayReport replay_smoke{directory_ / "reports", replay_inputs.provenance(), {{"passed", true}, {"scope", "constructed_registered_input"}}, directory_ / "closing_empty"};
        reject("replay loader rejects a different study identity", [&] {
            static_cast<void>(trading::PanelReplayDataset{directory_ / "closing_empty", contract, input_policy, resolution,
                trading::PanelOrigin::synthetic, std::string(64, '0')});
        });
        std::map<std::string, Json> primary_records;
        replay_inputs.replay([&](auto day) {
            check("registered reader emits all five instruments", day.size() == 5);
            for (const auto& row : day) { primary_records.emplace(row.symbol, trading::panel_replay_record(row)); }
            replay_smoke.append(std::move(day));
        });
        bool identical_market_replay = true;
        std::size_t priced_paths = 0;
        replay_inputs.replay_with_sessions([&](auto days, auto sessions) {
            identical_market_replay = identical_market_replay && days.size() == 5 && sessions.size() == 5;
            for (const auto& row : days) {
                identical_market_replay = identical_market_replay && primary_records.at(row.symbol) == trading::panel_replay_record(row);
                const auto found = std::find_if(sessions.begin(), sessions.end(), [&](const auto& session) { return session.request.symbol() == row.symbol; });
                if (found == sessions.end()) { throw std::logic_error("market-aware replay omitted a source session"); }
                priced_paths += trading::CostDelayStudy{contract}.evaluate(row, *found).size();
            }
        });
        check("market-aware reader preserves all original primary records", identical_market_replay);
        check("authenticated source sessions price all five-symbol cost paths", priced_paths == 80);
        reject("market-aware reader rejects an empty consumer", [&] { replay_inputs.replay_with_sessions({}); });
        const auto replay_smoke_report = replay_smoke.finish();
        const auto replay_report = trading::PanelReplayDiagnostic{}.run(directory_ / "reports", input_policy,
            [&](const auto& label, bool value) { check(label, value); }, [&](const auto& label, auto action) { reject(label, action); });


        const std::vector<trading::CalendarRange> history_ranges{{"2023-01-01", "2023-01-31"}, {"2023-02-01", "2023-02-01"}};
        const trading::HistoricalPanelDataset history_dataset{directory_ / "history", contract, trading::PanelOrigin::synthetic, history_ranges};
        PanelFixture history_fixture;
        const auto history_summary = history_dataset.collect(history_fixture.fetchers(), no_wait);
        const auto history_actions = action_reference.reconcile(directory_ / "history", issuer / "reference.json", contract, trading::PanelOrigin::synthetic);
        const trading::FrozenDevelopmentDataset empty_legacy{};
        static_cast<void>(trading::PanelInputFinalizer{directory_ / "history", contract, trading::PanelOrigin::synthetic}.finalize(history_summary, history_actions, empty_legacy));
        const auto history_masks = Json::parse(trading::read_text_file(directory_ / "history/eligibility_masks.json"));
        for (const auto& row : history_masks.at("rows")) {
            if (row.at("date").get<std::string>() != "2023-02-01") { continue; }
            if (row.at("symbol").get<std::string>() == "SPY") {
                check("missing prior reversion slot keeps comparison date", row.at("reversion_inputs_ready").get<bool>() && !row.at("reversion_history_complete").get<bool>());
                check("continuation cannot replace required eligible history with the current day", !row.at("continuation_inputs_ready").get<bool>());
            } else if (row.at("symbol").get<std::string>() == "QQQ") {
                check("complete twenty-session history becomes available before next day", row.at("continuation_inputs_ready").get<bool>() && row.at("reversion_history_complete").get<bool>());
            }
        }

        PanelFixture interrupted;
        interrupted.interrupt_at = 3;
        const trading::HistoricalPanelDataset recovery{directory_ / "interrupted", contract, trading::PanelOrigin::synthetic, ranges};
        reject("interrupted acquisition stops", [&] { static_cast<void>(recovery.collect(interrupted.fetchers(), no_wait)); });
        check("interruption cannot publish manifest", !std::filesystem::exists(directory_ / "interrupted/acquisition_manifest.json"));
        interrupted.interrupt_at = 0;
        const auto repaired = recovery.collect(interrupted.fetchers(), no_wait);
        check("interrupted coverage reconstructs identically", repaired.at("coverage") == summary.at("coverage"));
        check("completed pages were reused after interruption", interrupted.calls == original_calls + 1);

        PanelFixture transient;
        transient.transient = true;
        const trading::HistoricalPanelDataset retry{directory_ / "retry", contract, trading::PanelOrigin::synthetic, ranges};
        static_cast<void>(retry.collect(transient.fetchers(), no_wait));
        check("transient attempt archived", std::filesystem::exists(directory_ / "retry/responses/bars/SPY/2022-11-25/page_0/attempt_1.json"));
        check("one transient retry", transient.calls == original_calls + 1);

        PanelFixture outage;
        outage.always_transient = true;
        const trading::HistoricalPanelDataset outage_dataset{directory_ / "outage", contract, trading::PanelOrigin::synthetic, ranges};
        reject("transient outage has three attempts per invocation", [&] { static_cast<void>(outage_dataset.collect(outage.fetchers(), no_wait)); });
        check("outage budget retained", outage.calls == 4);
        outage.always_transient = false;
        check("later invocation can resume after transient outage", outage_dataset.collect(outage.fetchers(), no_wait).at("coverage") == summary.at("coverage"));
        check("outage responses remain archived", std::filesystem::exists(directory_ / "outage/responses/bars/SPY/2022-11-25/page_0/attempt_3.json"));

        PanelFixture denied;
        denied.deny = true;
        const trading::HistoricalPanelDataset denial{directory_ / "denied", contract, trading::PanelOrigin::synthetic, ranges};
        reject("401 stops collection", [&] { static_cast<void>(denial.collect(denied.fetchers(), no_wait)); });
        check("401 has one calendar and one bar call", denied.calls == 2);
        reject("401 resume retains diagnosis", [&] { static_cast<void>(denial.collect(denied.fetchers(), no_wait)); });
        check("401 is not repeatedly probed", denied.calls == 2);

        PanelFixture bad_calendar;
        bad_calendar.bad_calendar = true;
        const trading::HistoricalPanelDataset calendar_failure{directory_ / "bad_calendar", contract, trading::PanelOrigin::synthetic, ranges};
        reject("calendar disagreement stops before prices", [&] { static_cast<void>(calendar_failure.collect(bad_calendar.fetchers(), no_wait)); });
        check("no prices on bad calendar", bad_calendar.bar_calls == 0);
        trading::write_new_text_file(directory_ / "altered_protocol.json", trading::read_text_file(root / "research/extension_protocol.json") + " ");
        reject("altered registered contract rejected", [&] { static_cast<void>(trading::ExtensionStudyContract::load(directory_ / "altered_protocol.json")); });
        const auto changed = directory_ / "retry/responses/bars/SPY/2022-11-25/page_0/attempt_0.json";
        auto bytes = trading::read_text_file(changed);
        // Tamper only the constructed retry fixture; preserve the successful bundle.
        std::filesystem::rename(changed, changed.string() + ".original");
        bytes[bytes.find("503")] = '4';
        trading::write_new_text_file(changed, bytes);
        reject("altered raw attempt rejected", [&] { static_cast<void>(retry.audit()); });
        const Json checks{{"evidence_origin", "synthetic"}, {"network_requests", 0}, {"checks", labels_},
            {"readiness_reports", {{"complete", std::filesystem::relative(readiness_report, directory_).generic_string()},
                {"diagnosis", std::filesystem::relative(diagnostic_report, directory_).generic_string()},
                {"closing", std::filesystem::relative(closing_report, directory_).generic_string()},
                {"closing_empty", std::filesystem::relative(resolved_report, directory_).generic_string()}}},
            {"additional_archives", {"closing", "closing_empty"}},
            {"replay_reports", {std::filesystem::relative(replay_report, directory_).generic_string(), std::filesystem::relative(replay_smoke_report, directory_).generic_string()}},
            {"source_sha256", TRADING_SOURCE_SHA256}, {"protocol_sha256", contract.identity()}};
        trading::write_new_text_file(directory_ / "checks.json", checks.dump(2) + '\n');
        std::cout << "Historical panel | constructed acquisition, masks and access controls\nChecks passed: " << labels_.size()
            << "\nNetwork requests sent: 0\nEvidence directory: " << directory_.string() << '\n';
    }
private:
    void check(const std::string& label, bool value) {
        if (!value) { throw std::runtime_error("panel check failed: " + label); }
        labels_.push_back(label);
    }
    template<class Function> void reject(const std::string& label, Function action) {
        bool rejected = false;
        try { action(); } catch (const std::exception&) { rejected = true; }
        check(label, rejected);
    }
    std::filesystem::path directory_;
    std::vector<std::string> labels_;
};
} // namespace

int main() {
    try { PanelChecks{}.run(); return 0; }
    catch (const std::exception& error) { std::cerr << "Panel checks stopped: " << error.what() << '\n'; return 1; }
}
