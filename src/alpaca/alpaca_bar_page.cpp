#include "alpaca_bar_page.hpp"
#include "json_input.hpp"

#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <utility>

namespace trading {
namespace {

using Json = nlohmann::json;

double number_field(const Json& record, const char* name) {
    const auto field = record.find(name);
    if (field == record.end() || !field->is_number()) {
        throw std::invalid_argument(std::string{name} + " must be a numeric field");
    }
    return field->get<double>();
}

} // namespace

AlpacaBarPage decode_alpaca_bar_page(std::string_view payload,
                                    std::string_view expected_symbol) {
    if (expected_symbol.empty()) {
        throw std::invalid_argument("expected symbol must be non-empty");
    }
    const auto document = parse_json_document(payload);
    if (!document.is_object() || !document.contains("bars") ||
        !document.at("bars").is_object() || !document.contains("next_page_token")) {
        throw std::invalid_argument("response requires bars and next_page_token fields");
    }

    AlpacaBarPage page;
    const auto& token = document.at("next_page_token");
    if (token.is_string() && !token.get_ref<const std::string&>().empty()) {
        page.next_page_token = token.get<std::string>();
    } else if (!token.is_null()) {
        throw std::invalid_argument("next_page_token must be null or a non-empty string");
    }

    const auto& by_symbol = document.at("bars");
    if (by_symbol.empty()) {
        return page;
    }
    const std::string symbol{expected_symbol};
    if (by_symbol.size() != 1 || !by_symbol.contains(symbol) ||
        !by_symbol.at(symbol).is_array()) {
        throw std::invalid_argument("bars must contain only the requested symbol and an array of records");
    }

    const auto& records = by_symbol.at(symbol);
    page.bars.reserve(records.size());
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto& record = records.at(index);
        try {
            if (!record.is_object() || !record.contains("t") || !record.at("t").is_string()) {
                throw std::invalid_argument("bar requires a UTC timestamp string in t");
            }
            const auto start = parse_utc_seconds(record.at("t").get_ref<const std::string&>());
            MinuteBar bar{symbol, start,
                          {number_field(record, "o"), number_field(record, "h"),
                           number_field(record, "l"), number_field(record, "c")},
                          number_field(record, "v")};
            if (!page.bars.empty() && bar.start() <= page.bars.back().start()) {
                throw std::invalid_argument("timestamps must be strictly increasing without duplicates");
            }
            page.bars.push_back(std::move(bar));
        } catch (const std::invalid_argument& error) {
            throw std::invalid_argument("bar[" + std::to_string(index) + "]: " + error.what());
        }
    }
    return page;
}

} // namespace trading
