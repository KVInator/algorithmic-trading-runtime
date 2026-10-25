#include "runtime_benchmark.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "journal_frame.hpp"
#include "runtime_json.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <set>
#include <sstream>

namespace trading::execution {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
const RuntimeConfiguration legacy_config{"SPY", 10000000, {10, 4}};
const PaperExecutionConfiguration paper_config{"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "100000", {10, 4}};
const std::string at = "2023-01-03T14:31:00.000000000Z";
void require(bool value, std::string_view why) {
    if (!value)
        throw std::logic_error(std::string{why});
}
std::int64_t elapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}
Json resource_json(const ProcessResources &r) {
    return {{"resident_bytes", r.resident_bytes ? Json(*r.resident_bytes) : Json(nullptr)},
            {"peak_resident_bytes", r.peak_resident_bytes ? Json(*r.peak_resident_bytes) : Json(nullptr)}};
}
std::string uuid(std::size_t value) {
    std::ostringstream out;
    out << "00000000-0000-4000-8000-" << std::hex << std::setw(12) << std::setfill('0') << value;
    return out.str();
}
struct Workload {
    std::vector<RuntimeInput> typed;
    std::vector<Json> paper;
    std::vector<Outcome> expected;
    std::vector<Outcome> paper_expected;
    Json orders = Json::array();
    std::size_t round_trips;
    explicit Workload(std::size_t count) : round_trips{count} {
        typed.push_back(ConnectionInput{true});
        typed.push_back(BrokerSnapshot{legacy_config.initial_cash_units, 0, {}});
        expected = {Outcome::applied, Outcome::applied};
        paper = {{{"kind", "stream"},
                  {"received_at", at},
                  {"raw", R"({"stream":"authorization","data":{"status":"authorized"}})"}},
                 {{"kind", "stream"},
                  {"received_at", at},
                  {"raw", R"({"stream":"listening","data":{"streams":["trade_updates"]}})"}},
                 {{"kind", "reconcile"},
                  {"account", Json{{"id", paper_config.account_id},
                                   {"cash", "100000"},
                                   {"currency", "USD"},
                                   {"status", "ACTIVE"},
                                   {"account_blocked", false},
                                   {"trading_blocked", false}}
                                  .dump()},
                  {"positions", "[]"},
                  {"orders", "[]"}}};
        paper_expected = {Outcome::applied, Outcome::applied, Outcome::applied};
        for (std::size_t i = 0; i < count; ++i)
            for (const bool sell : {false, true}) {
                const auto number = 2 * i + (sell ? 2U : 1U);
                const auto id = "B" + std::to_string(number);
                const OrderRequest order{id, "SPY", sell ? Side::sell : Side::buy, 1, sell ? 10001 : 10000,
                                         0};
                const BrokerReport ack{"A" + id, id, ReportKind::accepted, "", 0, 0, 0, 0, ""};
                const BrokerReport fill{"F" + id, id, ReportKind::fill, "X" + id, 1, order.limit_units, 0,
                                        0,        ""};
                Json raw_order{{"id", uuid(number)},
                               {"client_order_id", id},
                               {"symbol", "SPY"},
                               {"asset_class", "us_equity"},
                               {"side", name(order.side)},
                               {"qty", "1"},
                               {"filled_qty", "0"},
                               {"filled_avg_price", nullptr},
                               {"limit_price", sell ? "100.01" : "100.00"},
                               {"type", "limit"},
                               {"order_class", "simple"},
                               {"time_in_force", "day"},
                               {"extended_hours", false},
                               {"status", "new"},
                               {"updated_at", at}};
                const auto message = [&](bool execution) {
                    auto provider_order = raw_order;
                    Json data{{"event", execution ? "fill" : "new"}, {"timestamp", at}};
                    if (execution) {
                        provider_order["status"] = "filled";
                        provider_order["filled_qty"] = "1";
                        provider_order["filled_avg_price"] = sell ? "100.01" : "100.00";
                        data["execution_id"] = uuid(number + 10000);
                        data["qty"] = "1";
                        data["price"] = sell ? "100.01" : "100.00";
                        data["position_qty"] = sell ? "0" : "1";
                    }
                    data["order"] = provider_order;
                    return Json{{"kind", "stream"},
                                {"received_at", at},
                                {"raw", Json{{"stream", "trade_updates"}, {"data", data}}.dump()}};
                };
                typed.insert(typed.end(), {SubmitInput{order}, ack, fill});
                expected.insert(expected.end(), 3, Outcome::applied);
                paper.insert(paper.end(), {input_json(SubmitInput{order}), message(false), message(true)});
                paper_expected.insert(paper_expected.end(), 3, Outcome::applied);
                if (!sell) {
                    typed.push_back(fill);
                    expected.push_back(Outcome::duplicate);
                    paper.push_back(message(true));
                    paper_expected.push_back(Outcome::duplicate);
                }
                orders.push_back({{"id", id},
                                  {"side", name(order.side)},
                                  {"shares", 1},
                                  {"filled", 1},
                                  {"status", "filled"}});
            }
    }
    Json json() const {
        Json inputs = Json::array(), outcomes = Json::array(), paper_outcomes = Json::array();
        for (const auto &i : typed)
            inputs.push_back(input_json(i));
        for (const auto o : expected)
            outcomes.push_back(name(o));
        for (const auto o : paper_expected)
            paper_outcomes.push_back(name(o));
        return {{"round_trips", round_trips},
                {"typed_inputs", inputs},
                {"paper_commands", paper},
                {"expected_outcomes", outcomes},
                {"paper_expected_outcomes", paper_outcomes},
                {"orders", orders}};
    }
};
class MemoryStorage final : public JournalStorage {
  public:
    explicit MemoryStorage(std::string bytes = {}) : bytes_{std::move(bytes)} {}
    std::string read_all() override { return bytes_; }
    void append_and_sync(std::string_view bytes) override { bytes_.append(bytes); }

  private:
    std::string bytes_;
};
class ObservedStorage final : public JournalStorage {
  public:
    explicit ObservedStorage(std::unique_ptr<JournalStorage> inner) : inner_{std::move(inner)} {}
    std::string read_all() override { return inner_->read_all(); }
    void append_and_sync(std::string_view bytes) override {
        const auto start = Clock::now();
        inner_->append_and_sync(bytes);
        writes.push_back(elapsed(start));
    }
    std::vector<std::int64_t> writes;

  private:
    std::unique_ptr<JournalStorage> inner_;
};
Outcome apply_paper(JournaledPaperRuntime &runtime, const Json &c) {
    const auto kind = c.at("kind").get<std::string>();
    if (kind == "submit")
        return runtime.submit(std::get<SubmitInput>(parse_runtime_input(c)).request).outcome;
    if (kind == "stream")
        return runtime.receive(c.at("raw").get<std::string>(), c.at("received_at").get<std::string>())
            .outcome;
    return runtime
        .reconcile(c.at("account").get<std::string>(), c.at("positions").get<std::string>(),
                   c.at("orders").get<std::string>())
        .outcome;
}
Json normalized(const OrderRuntime &core) {
    Json result = Json::array();
    for (const auto &o : core.orders())
        result.push_back({{"id", o.request.id},
                          {"side", name(o.request.side)},
                          {"shares", o.request.shares},
                          {"filled", o.filled_shares},
                          {"status", name(o.status)}});
    return result;
}
Json normalized(const Json &snapshot) {
    Json result = Json::array();
    for (const auto &o : snapshot.at("orders"))
        result.push_back({{"id", o.at("client_id")},
                          {"side", o.at("side")},
                          {"shares", o.at("shares")},
                          {"filled", o.at("filled_shares")},
                          {"status", o.at("status")}});
    return result;
}
std::string order_digest(Json value) {
    std::sort(value.begin(), value.end(), [](const auto &a, const auto &b) {
        return a.at("id").template get<std::string>() < b.at("id").template get<std::string>();
    });
    return sha256(value.dump());
}
void validate(const AccountSnapshot &a, const Json &orders, const Workload &w, bool paper, bool recovered) {
    const auto scale = paper ? 10000000LL : 1LL;
    require(a.cash_units ==
                    (legacy_config.initial_cash_units + static_cast<std::int64_t>(w.round_trips)) * scale &&
                a.position_shares == 0 && a.reserved_cash_units == 0 && a.reserved_sell_shares == 0 &&
                a.open_orders == 0,
            "benchmark final account differs from analytical round trips");
    require(a.connected != recovered && a.reconciliation_required == recovered,
            "benchmark routing/recovery gate differs");
    require(order_digest(orders) == order_digest(w.orders), "benchmark terminal order parity failed");
}
class BenchmarkTrial {
  public:
    BenchmarkTrial(std::filesystem::path directory, const Workload &workload)
        : directory_{std::move(directory)}, w_{workload} {
        std::filesystem::create_directories(directory_);
    }
    Json run(const std::string &mode) const {
        std::vector<std::int64_t> times, writes;
        times.reserve(w_.typed.size() + 1);
        const auto before = process_resources();
        AccountSnapshot final{};
        Json orders;
        std::string bytes;
        std::size_t records = 0;
        bool recovered = false;
        const bool paper = mode.starts_with("paper_");
        const bool recovery = mode.ends_with("recovery");
        if (mode == "core" || mode == "copy_candidate") {
            OrderRuntime core{legacy_config.symbol, legacy_config.initial_cash_units, legacy_config.limits};
            core.disconnect();
            for (std::size_t i = 0; i < w_.typed.size(); ++i) {
                Outcome outcome;
                const auto start = Clock::now();
                if (mode == "copy_candidate") {
                    auto candidate = core;
                    outcome = apply_input(candidate, w_.typed[i]);
                    core = std::move(candidate);
                } else
                    outcome = apply_input(core, w_.typed[i]);
                times.push_back(elapsed(start));
                require(outcome == w_.expected[i], "typed benchmark input outcome mismatch");
            }
            final = core.account();
            orders = normalized(core);
        } else if (!recovery) {
            const bool file = mode.ends_with("sync");
            auto storage =
                std::make_unique<ObservedStorage>(file ? create_journal_file(directory_ / (mode + ".journal"))
                                                       : std::make_unique<MemoryStorage>());
            auto *observed = storage.get();
            if (paper) {
                auto runtime = JournaledPaperRuntime::create(std::move(storage), paper_config);
                const auto skip = observed->writes.size();
                for (std::size_t i = 0; i < w_.paper.size(); ++i) {
                    const auto start = Clock::now();
                    const auto outcome = apply_paper(runtime, w_.paper[i]);
                    times.push_back(elapsed(start));
                    require(outcome == w_.paper_expected[i], "paper benchmark input outcome mismatch");
                }
                const auto state = runtime.snapshot();
                const auto &a = state.at("account");
                final = {a.at("cash_units").get<std::int64_t>(),
                         a.at("position_shares").get<std::int64_t>(),
                         a.at("reserved_cash_units").get<std::int64_t>(),
                         a.at("reserved_sell_shares").get<std::int64_t>(),
                         a.at("outstanding_buy_shares").get<std::int64_t>(),
                         a.at("open_orders").get<std::size_t>(),
                         a.at("connected").get<bool>(),
                         a.at("reconciliation_required").get<bool>()};
                orders = normalized(state);
                records = state.at("durable_records").get<std::size_t>();
                writes.assign(observed->writes.begin() + static_cast<std::ptrdiff_t>(skip),
                              observed->writes.end());
                bytes = observed->read_all();
            } else {
                auto runtime = JournaledRuntime::create(std::move(storage), legacy_config, 2);
                const auto skip = observed->writes.size();
                for (std::size_t i = 0; i < w_.typed.size(); ++i) {
                    const auto start = Clock::now();
                    const auto outcome = runtime.execute(w_.typed[i]);
                    times.push_back(elapsed(start));
                    require(outcome == w_.expected[i], "journal benchmark input outcome mismatch");
                }
                final = runtime.account();
                orders = Json::array();
                for (const auto &o : runtime.orders())
                    orders.push_back({{"id", o.request.id},
                                      {"side", name(o.request.side)},
                                      {"shares", o.request.shares},
                                      {"filled", o.filled_shares},
                                      {"status", name(o.status)}});
                records = runtime.inspection().input_records;
                writes.assign(observed->writes.begin() + static_cast<std::ptrdiff_t>(skip),
                              observed->writes.end());
                bytes = observed->read_all();
            }
            if (!file)
                write_new_text_file(directory_ / (mode + ".journal"), bytes);
        } else {
            const auto source = directory_ / (paper ? "paper_sync.journal" : "journal_sync.journal");
            bytes = read_text_file(source);
            const auto destination = directory_ / (mode + ".journal");
            {
                auto storage = create_journal_file(destination);
                storage->append_and_sync(bytes);
            }
            const auto start = Clock::now();
            if (paper) {
                auto runtime = JournaledPaperRuntime::recover(open_journal_file(destination), paper_config);
                times.push_back(elapsed(start));
                const auto state = runtime.snapshot();
                const auto &a = state.at("account");
                final = {a.at("cash_units").get<std::int64_t>(),
                         a.at("position_shares").get<std::int64_t>(),
                         a.at("reserved_cash_units").get<std::int64_t>(),
                         a.at("reserved_sell_shares").get<std::int64_t>(),
                         a.at("outstanding_buy_shares").get<std::int64_t>(),
                         a.at("open_orders").get<std::size_t>(),
                         a.at("connected").get<bool>(),
                         a.at("reconciliation_required").get<bool>()};
                orders = normalized(state);
                records = state.at("durable_records").get<std::size_t>();
            } else {
                auto runtime = JournaledRuntime::recover(open_journal_file(destination), legacy_config);
                times.push_back(elapsed(start));
                final = runtime.account();
                records = runtime.inspection().input_records;
                orders = Json::array();
                for (const auto &o : runtime.orders())
                    orders.push_back({{"id", o.request.id},
                                      {"side", name(o.request.side)},
                                      {"shares", o.request.shares},
                                      {"filled", o.filled_shares},
                                      {"status", name(o.status)}});
            }
            recovered = true;
            bytes = read_text_file(destination);
        }
        const auto after = process_resources();
        validate(final, orders, w_, paper, recovered);
        const auto ns = std::accumulate(times.begin(), times.end(), std::int64_t{0});
        require(ns > 0, "benchmark clock did not resolve this workload");
        std::size_t largest_frame = 0, previous = 0;
        for (std::size_t pos = 0; pos < bytes.size(); ++pos)
            if (bytes[pos] == '\n') {
                largest_frame = std::max(largest_frame, pos + 1 - previous);
                previous = pos + 1;
            }
        return {{"mode", mode},
                {"round_trips", w_.round_trips},
                {"schema", paper                                          ? 3
                           : (mode == "core" || mode == "copy_candidate") ? 0
                                                                          : 2},
                {"sample_unit", recovery ? "recovery_operation" : "input"},
                {"latencies_ns", times},
                {"append_sync_ns", writes},
                {"latency", LatencyDistribution{}.summarize(times)},
                {"append_latency", writes.empty() ? Json(nullptr) : LatencyDistribution{}.summarize(writes)},
                {"elapsed_ns", ns},
                {"operations_per_second", 1e9 * static_cast<double>(times.size()) / static_cast<double>(ns)},
                {"resources_before", resource_json(before)},
                {"resources_after", resource_json(after)},
                {"journal_bytes", bytes.size()},
                {"journal_records", records},
                {"largest_frame_bytes", largest_frame},
                {"recovered_routing_closed", recovered},
                {"cash_cents", final.cash_units / (paper ? 10000000LL : 1LL)},
                {"position_shares", final.position_shares},
                {"terminal_orders_sha256", order_digest(orders)},
                {"journal_sha256", bytes.empty() ? Json(nullptr) : Json(sha256(bytes))}};
    }

  private:
    std::filesystem::path directory_;
    const Workload &w_;
};
} // namespace
Json LatencyDistribution::summarize(const std::vector<std::int64_t> &values) const {
    if (values.empty() || std::any_of(values.begin(), values.end(), [](auto n) { return n < 0; }))
        throw std::invalid_argument("latency distribution requires nonnegative samples");
    auto sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const auto percentile = [&sorted](std::size_t numerator) {
        return sorted.at((numerator * sorted.size() + 99) / 100 - 1);
    };
    return {{"count", values.size()},   {"min_ns", sorted.front()}, {"p50_ns", percentile(50)},
            {"p95_ns", percentile(95)}, {"p99_ns", percentile(99)}, {"max_ns", sorted.back()}};
}
RuntimeBenchmarkResult RuntimeBenchmark::run(const std::filesystem::path &directory,
                                             const RuntimeBenchmarkConfiguration &configuration,
                                             const Progress &progress) const {
    if (configuration.repetitions == 0 || configuration.repetitions > 10 ||
        configuration.round_trips.empty() || configuration.round_trips.size() > 4)
        throw std::invalid_argument("benchmark requires 1-10 repetitions and 1-4 sizes");
    std::set<std::size_t> sizes;
    for (auto count : configuration.round_trips)
        if (count == 0 || count > 128 || !sizes.insert(count).second)
            throw std::invalid_argument("benchmark sizes must be unique in 1-128 round trips");
    std::filesystem::create_directories(directory);
    RuntimeBenchmarkResult result;
    result.environment = benchmark_environment();
    std::vector<std::int64_t> probe;
    for (int i = 0; i < 1000; ++i) {
        const auto start = Clock::now();
        probe.push_back(elapsed(start));
    }
    result.clock_probe = LatencyDistribution{}.summarize(probe);
    const std::vector<std::string> modes{"core",         "copy_candidate",   "journal_memory",
                                         "journal_sync", "journal_recovery", "paper_memory",
                                         "paper_sync",   "paper_recovery"};
    const Workload warmup{2};
    for (const auto &mode : modes)
        static_cast<void>(BenchmarkTrial{directory / "warmup", warmup}.run(mode));
    const auto total = sizes.size() * configuration.repetitions * modes.size();
    std::size_t complete = 0;
    if (progress)
        progress(0, total);
    for (auto count : configuration.round_trips) {
        const Workload workload{count};
        result.workloads.push_back(workload.json());
        for (std::size_t trial = 0; trial < configuration.repetitions; ++trial) {
            const auto name = "n" + std::to_string(count) + "_trial" + std::to_string(trial);
            // Rotate the two schema blocks; recovery follows its source file in each block.
            auto order = modes;
            if (trial % 2)
                std::rotate(order.begin() + 2, order.begin() + 5, order.end());
            if (trial % 2)
                std::swap(order[0], order[1]);
            for (const auto &mode : order) {
                auto row = BenchmarkTrial{directory / name, workload}.run(mode);
                row["trial"] = trial;
                row["directory"] = name;
                result.rows.push_back(std::move(row));
                if (progress)
                    progress(++complete, total);
            }
        }
    }
    return result;
}
} // namespace trading::execution
