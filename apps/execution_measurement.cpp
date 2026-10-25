#include "execution_measurement_report.hpp"
#include "artifact_io.hpp"
#include <iostream>

int main() {
    try {
        std::cout << "Execution measurement | matched synthetic limit and cancellation policies starting"
                  << std::endl;
        const auto directory = trading::create_unique_directory(
            std::filesystem::path{TRADING_PROJECT_ROOT} / "scratch/runs", "execution_measurement");
        const auto result = trading::execution::ExecutionMeasurementStudy{}.run(
            directory, trading::execution::ExecutionMeasurementStudy::workloads(),
            trading::execution::ExecutionMeasurementStudy::policies(),
            [](std::size_t completed, std::size_t total) {
                if (completed % 16 == 0 || completed == total)
                    std::cout << "Replaying account cases: " << completed << '/' << total << std::endl;
            });
        std::cout << "Writing figures, metrics and artifact manifest" << std::endl;
        trading::ExecutionMeasurementReport{result}.write(directory);
        std::cout
            << "Execution measurement | matched synthetic limit and cancellation policies\nAccount cases: "
            << result.cases.size() << "\nPaired comparisons: " << result.comparisons.size()
            << "\nNetwork requests sent: 0\nVisual report: " << (directory / "report.html").string()
            << "\nManifest: " << (directory / "summary.json").string() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Execution measurement failed: " << e.what() << '\n';
        return 1;
    }
}
