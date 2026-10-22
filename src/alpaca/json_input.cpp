#include "json_input.hpp"

#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace trading {

nlohmann::json parse_json_document(std::string_view payload) {
    using Json = nlohmann::json;
    std::vector<std::unordered_set<std::string>> object_keys;
    auto callback = [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 64) { throw std::invalid_argument("JSON document exceeds the supported nesting depth"); }
        if (event == Json::parse_event_t::object_start) {
            object_keys.emplace_back();
        } else if (event == Json::parse_event_t::object_end) {
            object_keys.pop_back();
        } else if (event == Json::parse_event_t::key) {
            if (!object_keys.back().insert(value.get<std::string>()).second) {
                throw std::invalid_argument("document contains a duplicate JSON object key");
            }
        }
        return true;
    };
    try {
        return Json::parse(payload.begin(), payload.end(), callback);
    } catch (const Json::parse_error& error) {
        throw std::invalid_argument("invalid JSON document at byte " + std::to_string(error.byte));
    } catch (const Json::out_of_range&) {
        throw std::invalid_argument("JSON document contains an unrepresentable numeric value");
    }
}

} // namespace trading
