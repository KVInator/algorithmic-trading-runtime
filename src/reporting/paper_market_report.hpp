#pragma once
#include "paper_market_data.hpp"

namespace trading {
class PaperMarketReport {
public:
    explicit PaperMarketReport(const PaperMarketResult& result) : result_{result} {}

    void write(const std::filesystem::path& directory) const;

private:
    const PaperMarketResult& result_;
};
} // namespace trading
