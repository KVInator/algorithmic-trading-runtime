#include "risk_preflight.hpp"
#include "alpaca_corporate_action_request.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "malformed preflight evidence must be rejected");
}
trading::SessionDownload download(const trading::CalendarSession &session) {
    const auto request = session.bar_request();
    std::vector<trading::MinuteBar> bars;
    for (auto time = request.start(); time < request.end_exclusive(); time += std::chrono::minutes{1}) {
        bars.emplace_back("SPY", time, trading::Ohlc{100, 100, 100, 100}, 1000);
    }
    return {
        request, bars, {}, trading::audit_session(request, bars), request.start(), request.end_exclusive()};
}
void test_coverage() {
    const std::vector<trading::CalendarSession> calendar{
        {"2023-06-02", "09:30", "16:00"}, {"2023-06-05", "09:30", "16:00"}, {"2023-07-03", "09:30", "13:00"}};
    auto regular = download(calendar[0]);
    auto gap = download(calendar[1]);
    gap.bars.erase(gap.bars.begin() + 22, gap.bars.begin() + 26);
    const auto rows = trading::audit_risk_session_coverage(calendar, std::vector{regular, gap});
    require(rows.size() == 3 && rows[0].observed_minutes == 390 && rows[0].rv5_endpoints_complete,
            "complete session retains all required endpoints");
    require(rows[1].observed_minutes == 386 && rows[1].account_references_complete &&
                !rows[1].rv5_endpoints_complete,
            "early interior gap blocks RV5 without erasing valid account references");
    require(rows[1].missing_rv5_bar_starts == std::vector<std::string>{"2023-06-05T13:54:00Z"},
            "09:55 endpoint uses the close of the 09:54 minute bar");
    require(rows[2].expected_minutes == 210 && rows[2].observed_minutes == 0 &&
                !rows[2].account_references_complete,
            "absent shortened session remains absent rather than 390 padded minutes");
    const auto shortened =
        trading::audit_risk_session_coverage(calendar, std::vector{regular, gap, download(calendar[2])});
    require(shortened[2].rv5_endpoints_complete && shortened[2].account_references_complete,
            "complete shortened session has its own correct close reference");
    auto interior = regular;
    interior.bars.erase(interior.bars.begin() + 22);
    const auto sparse = trading::audit_risk_session_coverage(std::vector{calendar[0]}, std::vector{interior});
    require(sparse[0].observed_minutes == 389 && sparse[0].rv5_endpoints_complete,
            "endpoint coverage is distinguished from full-grid completeness");
    rejects([&] {
        static_cast<void>(trading::audit_risk_session_coverage(calendar, std::vector{regular, regular}));
    });
    rejects([&] {
        static_cast<void>(
            trading::audit_risk_session_coverage(std::vector{calendar[1], calendar[0]}, std::vector{gap}));
    });
    auto unordered = regular;
    std::swap(unordered.bars[0], unordered.bars[1]);
    rejects(
        [&] { static_cast<void>(trading::audit_risk_session_coverage(calendar, std::vector{unordered})); });
    auto wrong = download(trading::CalendarSession{"2023-06-06", "09:30", "16:00"});
    rejects([&] { static_cast<void>(trading::audit_risk_session_coverage(calendar, std::vector{wrong})); });
}
void test_action_probes() {
    const auto response = trading::inspect_corporate_action_probe(
        2022, {200, R"({"corporate_actions":{},"next_page_token":"next"})"});
    require(response.json_object && response.more_pages,
            "truncated response cannot be mistaken for a complete page sequence");
    const auto denied = trading::inspect_corporate_action_probe(
        2021, {403, R"({"message":"subscription does not permit access"})"});
    require(denied.http_status == 403 && denied.json_object,
            "access rejection remains evidence, not an empty action ledger");
    const auto malformed = trading::inspect_corporate_action_probe(2023, {200, "not JSON"});
    require(!malformed.json_object, "a malformed success response does not establish a JSON data contract");
    require(trading::CorporateActionProbeRequest{2021}.path().find("end=2021-12-31") != std::string::npos,
            "probe interval is explicit and inclusive");
    rejects([] { static_cast<void>(trading::CorporateActionProbeRequest{2024}); });
}
} // namespace

int main() {
    try {
        test_coverage();
        test_action_probes();
        std::cout << "Risk preflight contracts passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Risk preflight contract failure: " << error.what() << '\n';
        return 1;
    }
}
