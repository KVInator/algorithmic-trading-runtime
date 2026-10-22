#include "session_download.hpp"
#include "session_storage.hpp"
#include "session_gap_probe.hpp"
#include "content_digest.hpp"
#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using Json = nlohmann::json;
using trading::HistoricalBarRequest;
using trading::MarketDataResponse;
using trading::parse_utc_seconds;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string{message});
    }
}

template <typename Error, typename Operation>
void expect_failure(Operation operation, std::string_view message) {
    try {
        operation();
    } catch (const Error&) {
        return;
    }
    throw std::runtime_error(std::string{message});
}

HistoricalBarRequest session(std::size_t page_size = 100) {
    return {"SPY", parse_utc_seconds("2023-01-03T14:30:00Z"),
            parse_utc_seconds("2023-01-03T21:00:00Z"), page_size};
}

// Synthetic OHLCV fixtures exercise transport-independent ingestion only.
Json record(int minute) {
    const auto time = session().start() + std::chrono::minutes{minute};
    const double open = 100.0 + static_cast<double>(minute) * 0.01;
    return {{"t", trading::format_utc_seconds(time)}, {"o", open}, {"h", open + 1.0},
            {"l", open - 1.0}, {"c", open + 0.5}, {"v", 1000.0 + 0.25 * minute}};
}

MarketDataResponse page(Json records, Json token = nullptr) {
    return {200, Json{{"bars", {{"SPY", std::move(records)}}},
                      {"next_page_token", std::move(token)}}.dump()};
}

trading::SessionDownload download_pages(const std::vector<MarketDataResponse>& pages,
                                       HistoricalBarRequest request = session(),
                                       std::size_t maximum_pages = 16) {
    std::size_t index = 0;
    return trading::SessionDownloader{std::move(request), maximum_pages}.download(
        [&](const auto&, const auto&) { return pages.at(index++); });
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    require(input.good(), "saved file must be readable");
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void test_request_contracts() {
    const auto request = session();
    require(request.expected_minutes() == 390, "regular-session fixture must contain 390 slots");
    const auto path = request.path("A+/=&?%");
    require(path == "/v2/stocks/bars?symbols=SPY&timeframe=1Min"
                    "&start=2023-01-03T14%3A30%3A00Z&end=2023-01-03T20%3A59%3A00Z"
                    "&limit=100&feed=sip&adjustment=raw&sort=asc&currency=USD&asof=-"
                    "&page_token=A%2B%2F%3D%26%3F%25",
            "exclusive session close and opaque token must be serialized correctly");
    for (const std::string symbol : {"", "SPY,QQQ", "SPY&feed=iex", "spy", "../SPY"}) {
        expect_failure<std::invalid_argument>([&] {
            static_cast<void>(HistoricalBarRequest{symbol, request.start(), request.end_exclusive()});
        }, "invalid or multiple symbols must be rejected");
    }
    for (const std::size_t size : {std::size_t{0}, std::size_t{101}}) {
        expect_failure<std::invalid_argument>([&] { static_cast<void>(session(size)); },
                                              "page bounds must be enforced");
    }
    for (const auto end : {request.start(), request.start() - std::chrono::minutes{1},
                           request.start() + std::chrono::hours{25},
                           request.end_exclusive() + std::chrono::seconds{1}}) {
        expect_failure<std::invalid_argument>([&] {
            static_cast<void>(HistoricalBarRequest{"SPY", request.start(), end});
        }, "invalid session intervals must be rejected");
    }
    expect_failure<std::invalid_argument>([&] { static_cast<void>(request.path("")); },
                                          "empty tokens must fail");
    expect_failure<std::invalid_argument>([&] { static_cast<void>(request.path(std::string(4097, 'x'))); },
                                          "oversized tokens must fail");
}

trading::SessionDownload test_complete_download() {
    std::size_t index = 0;
    const auto result = trading::SessionDownloader{session()}.download(
        [&](const HistoricalBarRequest& request, const std::optional<std::string>& token) {
            require(request.expected_minutes() == 390, "each page must retain the original interval");
            if (index == 0) {
                require(!token, "first page must have no token");
            } else {
                require(token == "NEXT+/=" + std::to_string(index), "next token must be passed unchanged");
            }
            auto rows = Json::array();
            const int first = static_cast<int>(index) * 100;
            const int last = index == 3 ? 390 : first + 100;
            for (int minute = first; minute < last; ++minute) {
                rows.push_back(record(minute));
            }
            ++index;
            return page(std::move(rows), index == 4 ? Json(nullptr) : Json("NEXT+/=" + std::to_string(index)));
        });
    require(index == 4 && result.pages.size() == 4, "four pages must be fetched, ending at a null token");
    require(result.bars.size() == 390 && result.audit.complete(), "every expected minute must be present");
    require(result.bars.front().start() == parse_utc_seconds("2023-01-03T14:30:00Z") &&
                result.bars.back().start() == parse_utc_seconds("2023-01-03T20:59:00Z"),
            "the 21:00 closing boundary must remain excluded");
    require(result.audit.total_volume_shares == 390000.0 + 0.25 * 389.0 * 390.0 / 2.0,
            "total volume must match an independently summed arithmetic sequence");
    return result;
}

void test_missing_minutes() {
    const HistoricalBarRequest short_window{
        "SPY", session().start(), session().start() + std::chrono::minutes{5}, 2};
    const auto result = download_pages({page(Json::array({record(1), record(3)}))}, short_window);
    require(!result.audit.complete() && result.bars.size() == 2, "gaps must not create fabricated rows");
    require(result.audit.missing_minutes == std::vector<std::chrono::sys_seconds>{
                short_window.start(), short_window.start() + std::chrono::minutes{2},
                short_window.start() + std::chrono::minutes{4}},
            "leading, internal and trailing missing minutes must be reported exactly");
    const auto empty = download_pages({page(Json::array(), "continue"), page(Json::array())});
    require(empty.pages.size() == 2 && empty.bars.empty() && empty.audit.missing_minutes.size() == 390,
            "empty intermediate pages must follow their token; empty sessions must remain incomplete");
    const auto continued = download_pages({page(Json::array(), "continue"),
                                            page(Json::array({record(0)}))});
    require(continued.bars.size() == 1, "an empty page must not terminate pagination prematurely");
}

void test_download_failures() {
    const auto first = page(Json::array({record(1)}), "next");
    for (const int minute : {1, 0, 390, -1}) {
        expect_failure<std::invalid_argument>([&] {
            static_cast<void>(download_pages({first, page(Json::array({record(minute)}))}));
        }, "cross-page duplicates, descending bars and session-boundary violations must fail");
    }
    expect_failure<std::invalid_argument>([&] {
        static_cast<void>(download_pages({first, page(Json::array(), "next")}));
    }, "a repeated token must fail instead of looping");
    expect_failure<std::invalid_argument>([&] {
        static_cast<void>(download_pages({page(Json::array({record(0), record(1)}))}, session(1)));
    }, "a response must respect the requested page size");
    expect_failure<std::runtime_error>([&] {
        static_cast<void>(download_pages({first}, session(), 1));
    }, "a nonterminal page must fail when the page bound is reached");
    for (const std::uint32_t status : {401U, 403U, 429U, 500U}) {
        expect_failure<std::runtime_error>([&] {
            static_cast<void>(download_pages({first, {status, "API error"}}));
        }, "HTTP errors must abort pagination");
    }
    expect_failure<std::invalid_argument>([&] {
        static_cast<void>(download_pages({{200, "not JSON"}}));
    }, "malformed response data must fail");
    expect_failure<std::runtime_error>([&] {
        static_cast<void>(download_pages({{200, std::string(65537, 'x')}}));
    }, "page bodies must remain bounded even for injected transports");
    expect_failure<std::invalid_argument>([&] {
        static_cast<void>(trading::SessionDownloader{session(), 0});
    }, "zero-page bounds must fail");
    expect_failure<std::invalid_argument>([&] {
        static_cast<void>(trading::SessionDownloader{session()}.download({}));
    }, "missing page fetchers must fail");
    expect_failure<std::runtime_error>([&] {
        static_cast<void>(trading::SessionDownloader{session()}.download(
            [](const auto&, const auto&) -> MarketDataResponse {
                throw std::runtime_error("synthetic connection failure");
            }));
    }, "transport failures must propagate without fallback data");
    const std::vector<trading::MinuteBar> excessive_volume{
        {"SPY", session().start(), {1, 1, 1, 1}, std::numeric_limits<double>::max()},
        {"SPY", session().start() + std::chrono::minutes{1}, {1, 1, 1, 1},
         std::numeric_limits<double>::max()}};
    expect_failure<std::overflow_error>([&] {
        static_cast<void>(trading::audit_session(session(), excessive_volume));
    }, "overflow in aggregate volume must fail");
}

void test_storage(const trading::SessionDownload& result, const std::filesystem::path& root) {
    const auto first = trading::save_session_download(result, root);
    const auto second = trading::save_session_download(result, root);
    require(first != second, "repeated saves must create distinct directories without overwriting");
    const auto metadata = Json::parse(read_file(first / "metadata.json"));
    require(metadata["pagination_complete"] == true && metadata["audit"]["minute_grid_complete"] == true &&
                metadata["audit"]["observed_bars"] == 390 && metadata["pages"].size() == 4,
            "saved metadata must identify pagination and grid coverage separately");
    require(metadata["end_exclusive_utc"] == "2023-01-03T21:00:00Z" &&
                metadata["provider_end_inclusive_utc"] == "2023-01-03T20:59:00Z",
            "metadata must retain both end-time conventions");
    for (std::size_t index = 0; index < result.pages.size(); ++index) {
        const std::string name = metadata["pages"][index]["file"];
        require(read_file(first / name) == result.pages[index].raw_json &&
                    read_file(second / name) == result.pages[index].raw_json,
                "raw response pages must be retained byte for byte");
    }
    const auto csv = read_file(first / "bars.csv");
    std::istringstream input{csv};
    std::string line;
    std::getline(input, line);
    require(line == "symbol,timestamp_utc,open,high,low,close,volume_shares", "CSV schema must be explicit");
    std::size_t row = 0;
    while (std::getline(input, line)) {
        std::istringstream fields{line};
        std::vector<std::string> values;
        std::string value;
        while (std::getline(fields, value, ',')) { values.push_back(value); }
        const auto& bar = result.bars.at(row++);
        require(values.size() == 7 && values[0] == bar.symbol() &&
                    parse_utc_seconds(values[1]) == bar.start() &&
                    std::stod(values[2]) == bar.prices().open &&
                    std::stod(values[3]) == bar.prices().high &&
                    std::stod(values[4]) == bar.prices().low &&
                    std::stod(values[5]) == bar.prices().close &&
                    std::stod(values[6]) == bar.volume(),
                "CSV numbers and timestamps must round-trip without precision loss");
    }
    require(row == 390 && !std::filesystem::exists(first / "metadata.json.tmp"),
            "the final marker must follow a complete write");
    const auto incomplete = download_pages({page(Json::array({record(2)}))});
    const auto incomplete_path = trading::save_session_download(incomplete, root);
    const auto incomplete_metadata = Json::parse(read_file(incomplete_path / "metadata.json"));
    require(incomplete_metadata["pagination_complete"] == true &&
                incomplete_metadata["audit"]["minute_grid_complete"] == false &&
                incomplete_metadata["audit"]["missing_minutes_utc"].size() == 389,
            "finished pagination must not conceal missing minute coverage");
    expect_failure<std::filesystem::filesystem_error>([&] {
        static_cast<void>(trading::save_session_download(result, first / "bars.csv"));
    }, "a file cannot serve as an output directory");
    require(read_file(first / "bars.csv") == csv, "failed writes must preserve existing results");
    expect_failure<std::invalid_argument>([&] {
        static_cast<void>(trading::save_session_download(result, {}));
    }, "empty output roots must fail");
}

void test_gap_probe(const std::filesystem::path& root) {
    const HistoricalBarRequest request{"SPY", session().start(), session().start() + std::chrono::minutes{10}};
    auto original = Json::array();
    for (int minute = 0; minute < 10; ++minute) {
        if (minute < 2 || minute > 5) { original.push_back(record(minute)); }
    }
    const auto source = trading::save_session_download(download_pages({page(original)}, request), root / "sources");
    const auto source_csv = read_file(source / "bars.csv");
    const auto source_metadata = read_file(source / "metadata.json");
    const auto loaded = trading::load_session_download(source);
    require(loaded.audit.missing_minutes.size() == 4 && loaded.bars.size() == 6,
            "self-describing load must use full saved-session verification");
    const int expected_begin[]{1, 2, 3, 4, 5};
    const int expected_end[]{7, 3, 4, 5, 6};
    std::size_t calls = 0;
    const auto unresolved = trading::SessionGapProbe{source}.run(root / "reports",
        [&](const auto& query, const auto& token) {
            require(!token && calls < 5 && query.start() == request.start() + std::chrono::minutes{expected_begin[calls]} &&
                query.end_exclusive() == request.start() + std::chrono::minutes{expected_end[calls]},
                "gap probe must query the context window and each absent minute with exact bounds");
            ++calls;
            auto rows = Json::array();
            for (const auto& bar : original) {
                const auto time = parse_utc_seconds(bar.at("t").template get<std::string>());
                if (time >= query.start() && time < query.end_exclusive()) { rows.push_back(bar); }
            }
            // The provider may represent an empty multi-symbol response as {}.
            return rows.empty() ? MarketDataResponse{200, R"({"bars":{},"next_page_token":null})"} : page(rows);
        });
    require(calls == 5 && unresolved.probe_windows == 5 && unresolved.missing_minutes == 4 &&
            unresolved.candidate_minutes_found == 0 && unresolved.conflicting_minutes == 0,
            "persistent empty results must remain explicit without candidate or filled bars");
    const auto summary = Json::parse(read_file(unresolved.directory / "summary.json"));
    require(summary.at("dataset_modified") == false && summary.at("probes").size() == 5 &&
            summary.at("probes").at(0).at("matching_reference_bars") == 2 &&
            summary.at("source_record_sha256") == trading::sha256_file(unresolved.directory / "source.json"),
            "probe report must retain reference checks and source identity");
    for (const auto& entry : summary.at("probes")) {
        const auto saved = unresolved.directory / entry.at("directory").get<std::string>();
        const auto check = trading::load_session_download(saved);
        require(check.bars.size() == entry.at("observed_bars").get<std::size_t>(),
                "all empty and nonempty probe responses must be independently reconstructible");
    }

    const auto changed = trading::SessionGapProbe{source}.run(root / "reports",
        [&](const auto& query, const auto&) {
            auto rows = Json::array();
            for (int minute = 0; minute < 10; ++minute) {
                auto bar = record(minute);
                const auto time = request.start() + std::chrono::minutes{minute};
                if (time >= query.start() && time < query.end_exclusive()) {
                    if (minute == 1 || (minute == 2 && query.expected_minutes() == 1)) {
                        bar["c"] = bar.at("o").template get<double>() + 0.75;
                    }
                    rows.push_back(bar);
                }
            }
            return page(rows);
        });
    require(changed.candidate_minutes_found == 4 && changed.conflicting_minutes == 2 &&
            changed.directory != unresolved.directory,
            "candidate bars, changed reference values and contradictory probes must remain separate evidence");
    require(read_file(source / "bars.csv") == source_csv && read_file(source / "metadata.json") == source_metadata,
            "diagnosis must never modify the original saved download");

    calls = 0;
    const auto interrupted_root = root / "interrupted-probes";
    expect_failure<std::runtime_error>([&] {
        static_cast<void>(trading::SessionGapProbe{source}.run(interrupted_root, [&](const auto&, const auto&) {
            ++calls;
            return calls == 1 ? page(Json::array({record(1), record(6)})) : MarketDataResponse{401, "unauthorized"};
        }));
    }, "HTTP failures must stop diagnosis without publishing a complete report");
    const auto interrupted = std::filesystem::directory_iterator{interrupted_root}->path();
    require(calls == 2 && std::filesystem::is_regular_file(interrupted / "source.json") &&
            !std::filesystem::exists(interrupted / "summary.json") &&
            std::distance(std::filesystem::directory_iterator{interrupted / "probes"},
                          std::filesystem::directory_iterator{}) == 1,
            "interrupted diagnosis must retain the source record and completed raw probe only");

    auto edges = Json::array();
    for (int minute = 1; minute < 9; ++minute) { edges.push_back(record(minute)); }
    const auto edge_source = trading::save_session_download(download_pages({page(edges)}, request), root / "sources");
    const int edge_begin[]{0, 0, 8, 9};
    const int edge_end[]{2, 1, 10, 10};
    calls = 0;
    static_cast<void>(trading::SessionGapProbe{edge_source}.run(root / "reports", [&](const auto& query, const auto&) {
        require(calls < 4 && query.start() == request.start() + std::chrono::minutes{edge_begin[calls]} &&
            query.end_exclusive() == request.start() + std::chrono::minutes{edge_end[calls]},
            "leading and trailing context probes must stay within the source request");
        ++calls;
        return page(Json::array());
    }));
    require(calls == 4, "separate edge gaps require separate context and minute probes");

    const HistoricalBarRequest excessive{"SPY", request.start(), request.start() + std::chrono::minutes{18}};
    const auto excessive_source = trading::save_session_download(download_pages({page(Json::array())}, excessive), root / "sources");
    calls = 0;
    const auto no_network = [&](const auto&, const auto&) { ++calls; return page(Json::array()); };
    expect_failure<std::invalid_argument>([&] {
        static_cast<void>(trading::SessionGapProbe{excessive_source}.run(root / "reports", no_network));
    }, "excessive diagnostic windows must fail before network requests");
    const auto complete_source = trading::save_session_download(download_pages({page(Json::array({record(0)}))},
        {"SPY", request.start(), request.start() + std::chrono::minutes{1}, 1}), root / "sources");
    expect_failure<std::invalid_argument>([&] {
        static_cast<void>(trading::SessionGapProbe{complete_source}.run(root / "reports", no_network));
    }, "complete downloads must not trigger gap requests");
    { std::ofstream file{source / "page_001.json", std::ios::binary | std::ios::trunc}; file << "{}"; }
    expect_failure<std::invalid_argument>([&] {
        static_cast<void>(trading::SessionGapProbe{source}.run(root / "reports", no_network));
    }, "corrupt raw evidence must fail before network requests");
    require(calls == 0, "invalid probe sources must not make network requests");
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            throw std::invalid_argument("session tests require an explicit artifact directory");
        }
        test_request_contracts();
        const auto result = test_complete_download();
        test_missing_minutes();
        test_download_failures();
        test_storage(result, argv[1]);
        test_gap_probe(argv[1]);
        std::cout << "Session download contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Session download contract failure: " << error.what() << '\n';
        return 1;
    }
}
