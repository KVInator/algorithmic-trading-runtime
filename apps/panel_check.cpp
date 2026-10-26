#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "extension_contract.hpp"
#include "historical_panel.hpp"
#include "historical_panel_report.hpp"
#include "panel_input_finalizer.hpp"
#include "response_bytes.hpp"
#include "journal_storage.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

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
                    if (request.symbol() == "GLD" && day == "2023-01-03" && i == 10) { continue; }
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
                }
                return trading::MarketDataResponse{200, Json{{"corporate_actions", {{"cash_dividends", rows}}}, {"next_page_token", nullptr}}.dump()};
            }};
    }
    std::size_t calls{}, bar_calls{}, interrupt_at{};
    bool deny{}, transient{}, always_transient{}, retried{}, bad_calendar{};
};

class PanelChecks {
public:
    void run() {
        const std::filesystem::path root{TRADING_PROJECT_ROOT};
        const auto contract = trading::ExtensionStudyContract::load(root / "research/extension_protocol.json");
        directory_ = trading::create_unique_directory(root / "scratch/runs", "panel_check");
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
        trading::write_new_text_file(issuer / "incomplete.json", incomplete.dump(2) + '\n');
        check("cash history does not establish split completeness", !action_reference.reconcile(directory_ / "complete", issuer / "incomplete.json", contract,
            trading::PanelOrigin::synthetic).at("issuer_reconciled").get<bool>());
        auto bad_source = reference;
        bad_source["sources"][0]["sha256"] = std::string(64, '0');
        trading::write_new_text_file(issuer / "bad_source.json", bad_source.dump(2) + '\n');
        reject("altered issuer source rejected", [&] { static_cast<void>(action_reference.reconcile(directory_ / "complete", issuer / "bad_source.json", contract, trading::PanelOrigin::synthetic)); });
        trading::FrozenDevelopmentDataset legacy{};
        legacy.manifest_sha256 = "constructed_legacy_fixture";
        legacy.sessions.push_back(dataset.load_development_session("SPY", "2023-01-03", acquisition_sha));
        const trading::PanelInputFinalizer finalizer{directory_ / "complete", contract, trading::PanelOrigin::synthetic};
        const auto registered = finalizer.finalize(summary, action_result, legacy);
        check("study manifest published only after prerequisites", registered.at("research_inputs_ready").get<bool>() && !registered.at("strategy_scored").get<bool>());
        check("reserved prices remain closed after registration", !registered.at("reserved_strategy_inputs_opened").get<bool>());
        check("registration is idempotent", finalizer.finalize(summary, action_result, legacy) == registered);
        reject("unreconciled actions cannot register inputs", [&] { static_cast<void>(finalizer.finalize(summary, Json{{"issuer_reconciled", false}}, legacy)); });

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
