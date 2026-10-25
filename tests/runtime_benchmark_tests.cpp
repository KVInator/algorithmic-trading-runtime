#include "runtime_benchmark_report.hpp"
#include "artifact_io.hpp"
#include <iostream>

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::invalid_argument("benchmark contracts require output root");
        const auto q = trading::execution::LatencyDistribution{}.summarize({10, 20, 30, 40, 50});
        if (q.at("p50_ns").get<int>() != 30 || q.at("p95_ns").get<int>() != 50 ||
            q.at("p99_ns").get<int>() != 50)
            throw std::logic_error("nearest-rank quantile oracle failed");
        bool rejected = false;
        try {
            static_cast<void>(trading::execution::LatencyDistribution{}.summarize({-1}));
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        if (!rejected)
            throw std::logic_error("negative duration accepted");
        const auto root = trading::create_unique_directory(argv[1], "runtime-benchmark");
        const auto result = trading::execution::RuntimeBenchmark{}.run(root, {{2}, 1});
        if (result.rows.size() != 8)
            throw std::logic_error("all eight benchmark paths required");
        for (const auto &row : result.rows) {
            if (row.at("cash_cents").get<std::int64_t>() != 10000002 ||
                row.at("position_shares").get<int>() != 0)
                throw std::logic_error("benchmark economics differ");
            if (row.at("mode").get<std::string>().ends_with("recovery") &&
                !row.at("recovered_routing_closed").get<bool>())
                throw std::logic_error("recovery must close routing");
        }
        trading::RuntimeBenchmarkReport{result}.write(root);
        std::cout << "Benchmark quantiles, eight-mode economics, recovery gates and resource export "
                     "contracts passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Benchmark contracts failed: " << e.what() << '\n';
        return 1;
    }
}
