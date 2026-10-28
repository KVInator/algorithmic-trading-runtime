#pragma once
#include "api_request_policy.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>
namespace trading {
// Shared immutable page/attempt retention for historical bars, actions and quotes.
class MarketPageArchive {
  public:
    MarketPageArchive(std::filesystem::path root, bool acquire, ApiRequestPolicy::Wait wait);
    [[nodiscard]] MarketDataResponse get(const std::string &key, const std::string &host,
                                         const std::string &path,
                                         const std::function<MarketDataResponse()> &fetch);
    [[nodiscard]] const nlohmann::json &last() const noexcept;
    [[nodiscard]] std::size_t network_requests() const noexcept;

  private:
    static bool retryable(unsigned status);
    static std::string filename(const std::string &key, std::size_t index);
    std::filesystem::path root_;
    bool acquire_;
    ApiRequestPolicy policy_;
    nlohmann::json last_;
    std::size_t network_requests_{};
};
} // namespace trading
