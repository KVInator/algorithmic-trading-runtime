#include "runtime_benchmark_report.hpp"
#include "artifact_io.hpp"
#include <iostream>

int main() {
    try {
        std::cout << "Runtime benchmark | processing, synchronization and recovery starting" << std::endl;
        const auto directory = trading::create_unique_directory(
            std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch/runs", "runtime_benchmark");
        const auto result =
            trading::execution::RuntimeBenchmark{}.run(directory, {}, [](auto done, auto total) {
                if (done % 8 == 0 || done == total)
                    std::cout << "Benchmark trials: " << done << '/' << total << std::endl;
            });
        std::cout << "Writing benchmark figures and reproduction evidence" << std::endl;
        trading::RuntimeBenchmarkReport{result}.write(directory);
        std::cout << "Benchmark rows: " << result.rows.size()
                  << "\nBuild configuration: " << TRADING_BUILD_CONFIGURATION
                  << "\nAll account and order parity checks passed\nVisual report: "
                  << (directory / "report.html").string()
                  << "\nManifest: " << (directory / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Runtime benchmark failed: " << e.what() << '\n';
        return 1;
    }
}
