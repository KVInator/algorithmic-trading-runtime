#include "paper_market_data.hpp"
#include <stdexcept>

namespace trading {
PaperMarketResult PaperMarketDiagnostic::run() const {
    PaperMarketResult result{};
    constexpr std::string_view calendar =
        R"([{"date":"2026-10-26","open":"09:30","close":"16:00"},{"date":"2026-10-27","open":"09:30","close":"16:00"},{"date":"2026-10-28","open":"09:30","close":"16:00"},{"date":"2026-10-29","open":"09:30","close":"16:00"},{"date":"2026-10-30","open":"09:30","close":"16:00"},{"date":"2026-11-02","open":"09:30","close":"16:00"}])";
    constexpr std::string_view quote =
        R"({"symbol":"SPY","quote":{"bp":600.009999999999999999,"ap":600.010000000000000001,"bs":2,"as":3,"bx":"P","ax":"Q","t":"2026-10-26T14:30:00.000000001Z","c":["R"],"z":"B"}})";
    for (const auto name : {"regular_hours", "off_hours", "sip_forbidden", "rate_limited"}) {
        const bool closed = std::string_view{name} == "off_hours";
        const unsigned status = std::string_view{name} == "sip_forbidden"  ? 403U
                                : std::string_view{name} == "rate_limited" ? 429U
                                                                           : 200U;
        PaperCapture capture{directory_ / name / "capture"};
        const PaperReceipt at{closed ? "2026-10-26T21:00:00Z" : "2026-10-26T14:30:01Z", 1000};
        const auto read = [&](const PaperMarketRequest& request) -> MarketDataResponse {
            const auto [is_calendar, path] = validate_paper_market_request(request);
            static_cast<void>(path);
            if (is_calendar) {
                return {200, std::string{calendar}, {}};
            }
            return {status,
                    status == 200 ? std::string{quote} : R"({"message":"constructed access denial"})",
                    {}};
        };
        const auto observed = PaperMarketProbe{read, [at] { return at; }, capture}.run();
        result.request_attempts += observed.request_attempts;
        const bool access = status == 200;
        if (observed.access_verified != access || observed.request_attempts != 2) {
            throw std::runtime_error("constructed market access mismatch");
        }
        result.checks.emplace_back(std::string{name} + ": bounded read-only request/access outcome retained");
        if (access) {
            if (observed.cases.at(1).at("quote_usable").get<bool>() != !closed) {
                throw std::runtime_error("constructed quote gate mismatch");
            }
            result.checks.emplace_back(std::string{name} + ": access and quote usability distinguished");
        }
        result.cases.push_back({{"case", name},
                                {"access_verified", observed.access_verified},
                                {"request_attempts", observed.request_attempts},
                                {"detail", observed.detail},
                                {"observations", observed.cases}});
    }
    result.access_verified = true;
    result.detail = "Constructed quote/calendar checks completed; zero network requests, no enrollment and "
                    "no order events.";
    return result;
}
} // namespace trading
