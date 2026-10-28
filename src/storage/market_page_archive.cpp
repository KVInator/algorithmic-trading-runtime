#include "market_page_archive.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "json_input.hpp"
#include "response_bytes.hpp"
#include "utc_timestamp.hpp"
#include <utility>
namespace trading {
namespace {
using Json = nlohmann::json;
std::string now() {
    return format_utc_seconds(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
}
Json reference(const std::filesystem::path &root, const std::string &file) {
    return {{"file", file}, {"sha256", sha256_file(contained_path(root, file))}};
}
} // namespace
MarketPageArchive::MarketPageArchive(std::filesystem::path root, bool acquire, ApiRequestPolicy::Wait wait)
    : root_{std::move(root)}, acquire_{acquire}, policy_{std::move(wait), true} {}
MarketDataResponse MarketPageArchive::get(const std::string &key, const std::string &host,
                                          const std::string &path,
                                          const std::function<MarketDataResponse()> &fetch) {
    std::size_t attempts = 0;
    std::optional<MarketDataResponse> latest;
    Json receipts = Json::array();
    while (std::filesystem::exists(contained_path(root_, filename(key, attempts)))) {
        if (attempts == 9) {
            throw std::invalid_argument("request contains more than nine archived attempts");
        }
        if (latest && !retryable(latest->status)) {
            throw std::invalid_argument("request contains attempts after a terminal response");
        }
        const auto file = filename(key, attempts++);
        const auto doc = parse_json_document(read_text_file(contained_path(root_, file), 192U * 1024U));
        const auto body = decode_response_bytes(doc.at("body_hex").get<std::string>());
        static_cast<void>(parse_utc_seconds(doc.at("retrieved_utc").get<std::string>()));
        if (doc.at("schema_version") != 1 || doc.at("host") != host || doc.at("request_path") != path ||
            body.size() > maximum_market_data_page_bytes || doc.at("body_sha256") != sha256(body)) {
            throw std::invalid_argument("panel response provenance/hash mismatch");
        }
        latest =
            MarketDataResponse{doc.at("http_status").get<std::uint32_t>(), body,
                               doc.at("retry_after").is_null() ? std::optional<std::string>{}
                                                               : doc.at("retry_after").get<std::string>()};
        last_ = reference(root_, file);
        receipts.push_back(last_);
        last_["attempts"] = receipts;
    }
    for (auto index = attempts + 1; index <= 9; ++index) {
        if (std::filesystem::exists(contained_path(root_, filename(key, index)))) {
            throw std::invalid_argument("panel request has a noncontiguous attempt history");
        }
    }
    if (latest && (latest->status == 200 || !retryable(latest->status) || attempts == 9)) {
        return *latest;
    }
    if (!acquire_ || !fetch) {
        throw std::runtime_error("panel request is not complete: " + key);
    }
    auto result = policy_.get(
        [&] {
            if (attempts >= 9) {
                throw std::runtime_error("panel request exhausted its retained nine-attempt budget");
            }
            ++network_requests_;
            return fetch();
        },
        [&](const MarketDataResponse &response) {
            if (response.body.size() > maximum_market_data_page_bytes) {
                throw std::invalid_argument("panel response exceeds its 64-KiB bound");
            }
            const auto file = filename(key, attempts++);
            const Json doc{
                {"schema_version", 1},
                {"host", host},
                {"request_path", path},
                {"http_status", response.status},
                {"retry_after", response.retry_after ? Json(*response.retry_after) : Json(nullptr)},
                {"retrieved_utc", now()},
                {"body_sha256", sha256(response.body)},
                {"body_hex", encode_response_bytes(response.body)}};
            write_new_text_file(contained_path(root_, file), doc.dump(2) + '\n');
            last_ = reference(root_, file);
            receipts.push_back(last_);
            last_["attempts"] = receipts;
        });
    return result;
}

const Json &MarketPageArchive::last() const noexcept { return last_; }
std::size_t MarketPageArchive::network_requests() const noexcept { return network_requests_; }
bool MarketPageArchive::retryable(unsigned status) {
    return status == 429 || status == 500 || status == 502 || status == 503 || status == 504;
}
std::string MarketPageArchive::filename(const std::string &key, std::size_t index) {
    return key + "/attempt_" + std::to_string(index) + ".json";
}
} // namespace trading
