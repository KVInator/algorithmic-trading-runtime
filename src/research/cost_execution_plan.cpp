#include "cost_execution_plan.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include "panel_replay_dataset.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace trading {
using Json = nlohmann::json;
CostQuotePlan::CostQuotePlan(std::vector<std::string> dates, bool synthetic, Json evidence) {
    if (dates.empty() || dates.size() > 7 || !std::is_sorted(dates.begin(), dates.end()) ||
        std::set<std::string>(dates.begin(), dates.end()).size() != dates.size()) {
        throw std::invalid_argument("quote plan requires ordered unique sample dates");
    }
    const std::vector<std::string> symbols{"SPY", "QQQ", "IWM", "TLT", "GLD"};
    Json records = Json::array();
    for (const auto &date : dates) {
        if (date < "2017-01-01" || date >= "2024-01-01") {
            throw std::invalid_argument("quote plan cannot open initialization or reserved years");
        }
        const auto sessions = extension_calendar({date, date});
        if (sessions.size() != 1 || !sessions.front().regular()) {
            throw std::invalid_argument("quote sample requires a regular exchange session");
        }
        for (const auto &symbol : symbols) {
            for (int minute = 30; minute <= 385; minute += minute == 360 ? 25 : 30) {
                for (int delay : {1, 2}) {
                    const auto instant = sessions.front().open_utc() + std::chrono::minutes{minute + delay};
                    const auto id =
                        symbol + '/' + date + "/m" + std::to_string(minute) + "d" + std::to_string(delay);
                    windows_.push_back({id, symbol, date, minute, delay,
                                        HistoricalTickRequest{HistoricalTickKind::quotes, symbol,
                                                              instant - std::chrono::seconds{1}, instant}});
                    records.push_back({{"id", id},
                                       {"symbol", symbol},
                                       {"date", date},
                                       {"decision_minute", minute},
                                       {"delay_minutes", delay},
                                       {"start", format_utc_seconds(instant - std::chrono::seconds{1})},
                                       {"end", format_utc_seconds(instant)},
                                       {"request_path", windows_.back().request.path()}});
                }
            }
        }
    }
    document_ = {{"schema_version", 1},
                 {"kind", "cost_quote_plan"},
                 {"evidence_origin", synthetic ? "synthetic" : "provider"},
                 {"policy_sha256", TRADING_COST_EXECUTION_PROTOCOL_SHA256},
                 {"sample_dates", dates},
                 {"symbols", symbols},
                 {"sample_selection", "first_common_operational_date_per_year"},
                 {"input_evidence", std::move(evidence)},
                 {"maximum_pages_per_window", 16},
                 {"round_lot_shares", 100},
                 {"windows", records}};
}
CostQuotePlan CostQuotePlan::from_panel(const std::filesystem::path &directory) {
    const auto study_file = directory / "study_manifest.json";
    if (sha256_file(study_file) != registered_panel_study_sha256) {
        throw std::invalid_argument("quote plan requires the accepted M2 study identity");
    }
    const auto study = parse_json_document(read_text_file(study_file));
    const auto masks_file = directory / "eligibility_masks.json";
    const auto digest = sha256_file(masks_file, 16U * 1024U * 1024U);
    if (digest != study.at("eligibility_masks_sha256").get<std::string>() ||
        !study.at("research_inputs_ready").get<bool>()) {
        throw std::invalid_argument("quote sample input-mask identity differs");
    }
    const auto masks = parse_json_document(read_text_file(masks_file, 16U * 1024U * 1024U));
    if (masks.at("protocol_sha256").get<std::string>() != TRADING_EXTENSION_PROTOCOL_SHA256) {
        throw std::invalid_argument("quote sample mask protocol differs");
    }
    std::map<std::string, std::map<std::string, bool>> grouped;
    for (const auto &row : masks.at("rows")) {
        const auto date = row.at("date").get<std::string>();
        if (date < "2017-01-01" || date >= "2024-01-01") {
            continue;
        }
        if (!grouped[date]
                 .emplace(row.at("symbol").get<std::string>(),
                          row.at("input_availability").at("operational").get<bool>())
                 .second) {
            throw std::invalid_argument("duplicate quote-selection mask");
        }
    }
    std::vector<std::string> dates;
    for (int year = 2017; year <= 2023; ++year) {
        const auto prefix = std::to_string(year);
        bool found = false;
        for (const auto &[date, symbols] : grouped) {
            if (!date.starts_with(prefix)) {
                continue;
            }
            const auto calendar = extension_calendar({date, date});
            if (symbols.size() == 5 && calendar.size() == 1 && calendar.front().regular() &&
                std::all_of(symbols.begin(), symbols.end(), [](const auto &item) { return item.second; })) {
                dates.push_back(date);
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::invalid_argument("no common operational quote sample in " + prefix);
        }
    }
    return CostQuotePlan{dates,
                         false,
                         {{"study_manifest_sha256", std::string{registered_panel_study_sha256}},
                          {"eligibility_masks_sha256", digest}}};
}
CostQuotePlan CostQuotePlan::constructed(std::vector<std::string> dates) {
    return CostQuotePlan{std::move(dates), true, Json::object()};
}
const Json &CostQuotePlan::document() const noexcept { return document_; }
const std::vector<CostQuoteWindow> &CostQuotePlan::windows() const noexcept { return windows_; }
std::vector<ExecutionStressWorkload> cost_execution_workloads() {
    std::vector<ExecutionStressWorkload> out;
    for (const auto *symbol : {"SPY", "QQQ", "IWM", "TLT", "GLD"}) {
        for (const auto *side : {"buy", "sell"}) {
            ExecutionStressWorkload base{"base", symbol, side};
            std::vector<ExecutionStressWorkload> variants{base};
            for (const auto *path : {"adverse", "reverting"}) {
                auto w = base;
                w.id = path;
                w.path = path;
                variants.push_back(w);
            }
            for (int value : {1, 100}) {
                auto w = base;
                w.id = "quantity_" + std::to_string(value);
                w.requested_shares = value;
                variants.push_back(w);
            }
            for (int value : {0, 1}) {
                auto w = base;
                w.id = "displayed_" + std::to_string(value);
                w.displayed_shares = value;
                variants.push_back(w);
            }
            for (int value : {0, 10}) {
                auto w = base;
                w.id = "spread_" + std::to_string(value);
                w.spread_cents = value;
                variants.push_back(w);
            }
            for (int value : {1, 3}) {
                auto w = base;
                w.id = "outbound_" + std::to_string(value);
                w.outbound_seconds = value;
                variants.push_back(w);
            }
            for (int value : {1, 3}) {
                auto w = base;
                w.id = "receipt_" + std::to_string(value);
                w.receipt_seconds = value;
                variants.push_back(w);
            }
            for (int value : {0, 3}) {
                auto w = base;
                w.id = "cancel_" + std::to_string(value);
                w.cancel_processing_seconds = value;
                variants.push_back(w);
            }
            for (int value : {1, 3}) {
                auto w = base;
                w.id = "age_" + std::to_string(value);
                w.source_age_seconds = value;
                variants.push_back(w);
            }
            for (auto &w : variants) {
                w.id = std::string{symbol} + '_' + side + '_' + w.id;
                out.push_back(std::move(w));
            }
        }
    }
    return out;
}
} // namespace trading
