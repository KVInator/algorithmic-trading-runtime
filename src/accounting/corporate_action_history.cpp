#include "corporate_action_history.hpp"
#include "json_input.hpp"
#include "utc_timestamp.hpp"

#include <cmath>
#include <map>
#include <stdexcept>

namespace trading {
namespace {
std::string date(const nlohmann::json& row, const char* key) {
    const auto result = row.at(key).get<std::string>();
    if (result.size() != 10) { throw std::invalid_argument("action date requires YYYY-MM-DD"); }
    static_cast<void>(parse_utc_seconds(result + "T00:00:00Z"));
    return result;
}
bool in_horizon(const std::string& ex) { return ex >= "2021-01-01" && ex <= "2023-12-31"; }
} // namespace

std::optional<std::string> CorporateActionHistory::append_page(std::string_view payload,
    const std::string& first, const std::string& last) {
    const auto doc = parse_json_document(payload);
    if (!doc.is_object() || !doc.contains("corporate_actions") || !doc.at("corporate_actions").is_object() ||
        !doc.contains("next_page_token")) { throw std::invalid_argument("malformed corporate-action page"); }
    std::optional<std::string> token;
    if (!doc.at("next_page_token").is_null()) {
        token = doc.at("next_page_token").get<std::string>();
        if (token->empty() || token->size() > 4096) { throw std::invalid_argument("invalid action continuation token"); }
    }
    // Validate a page before committing any of its state.
    auto ids = ids_;
    auto distributions = distributions_;
    auto unsupported = unsupported_;
    std::size_t count = 0;
    for (const auto& [kind, records] : doc.at("corporate_actions").items()) {
        if (!records.is_array()) { throw std::invalid_argument("action groups must be arrays"); }
        for (const auto& row : records) {
            if (++count > 100) { throw std::invalid_argument("action page exceeds the requested record limit"); }
            const auto id = row.at("id").get<std::string>();
            const auto process = date(row, "process_date");
            if (id.empty() || !ids.insert(id).second || row.at("symbol") != "SPY" || process < first || process > last) {
                throw std::invalid_argument("action identity, symbol or process interval mismatch");
            }
            if (kind != "cash_dividends") { unsupported.push_back(kind + ": " + id); continue; }
            if (!row.at("rate").is_number()) { throw std::invalid_argument("cash dividend rate must be numeric"); }
            const auto rate = row.at("rate").get<double>();
            CashDistribution item{id, date(row, "ex_date"), date(row, "record_date"),
                date(row, "payable_date"), process, rate};
            if (!std::isfinite(rate) || rate <= 0 || item.record_date < item.ex_date || item.payable_date < item.ex_date) {
                throw std::invalid_argument("cash dividend has invalid dates or USD/share amount");
            }
            distributions.push_back(std::move(item));
        }
    }
    ids_ = std::move(ids);
    distributions_ = std::move(distributions);
    unsupported_ = std::move(unsupported);
    return token;
}
const std::vector<CashDistribution>& CorporateActionHistory::distributions() const noexcept { return distributions_; }
const std::vector<std::string>& CorporateActionHistory::unsupported_types() const noexcept { return unsupported_; }

std::vector<std::string> CorporateActionHistory::reconcile(std::span<const CashDistribution> reference) const {
    if (reference.empty()) { throw std::invalid_argument("independent dividend reference is empty"); }
    std::map<std::string, const CashDistribution*> expected;
    for (const auto& row : reference) {
        static_cast<void>(parse_utc_seconds(row.ex_date + "T00:00:00Z"));
        static_cast<void>(parse_utc_seconds(row.payable_date + "T00:00:00Z"));
        if (!in_horizon(row.ex_date) || !std::isfinite(row.usd_per_share) || row.usd_per_share <= 0 ||
            row.payable_date < row.ex_date || !expected.emplace(row.ex_date, &row).second) {
            throw std::invalid_argument("independent dividend reference is invalid or ambiguous");
        }
    }
    std::vector<std::string> errors;
    std::set<std::string> observed;
    for (const auto& row : distributions_) {
        if (!in_horizon(row.ex_date)) { continue; }
        if (!observed.insert(row.ex_date).second) { errors.push_back("Multiple cash distributions on " + row.ex_date); }
        const auto it = expected.find(row.ex_date);
        if (it == expected.end()) { errors.push_back("Provider-only cash distribution on " + row.ex_date); }
        else if (std::abs(row.usd_per_share - it->second->usd_per_share) > 1e-6 ||
            row.payable_date != it->second->payable_date || row.record_date != it->second->record_date) {
            errors.push_back("Amount, record-date or payable-date mismatch on " + row.ex_date);
        }
    }
    for (const auto& [ex, row] : expected) {
        static_cast<void>(row);
        if (!observed.contains(ex)) { errors.push_back("Missing provider distribution on " + ex); }
    }
    return errors;
}

} // namespace trading
