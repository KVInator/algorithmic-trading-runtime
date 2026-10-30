#pragma once
#include "panel_account_study.hpp"
#include "stability_study.hpp"
#include <deque>

namespace trading {
struct RiskMatchSizing {
    std::string first, last;
    std::size_t slots{}, valid{};
    std::optional<double> candidate_volatility, control_volatility, target, candidate_scale,
        control_scale;
    [[nodiscard]] nlohmann::json json() const;
};
class RiskMatchWindow {
  public:
    [[nodiscard]] RiskMatchSizing prior(const std::string &date) const;
    void commit(const std::string &date, std::optional<StabilityPair> returns);

  private:
    std::deque<std::pair<std::string, std::optional<StabilityPair>>> history_;
};
class IntradayAccountLedger {
  public:
    [[nodiscard]] nlohmann::json append(const PanelReplayDay &day, const SessionDownload &market,
                                        const PanelPolicyReplay *policy, bool ready,
                                        std::optional<double> scale);

  private:
    LongCashAccount account_{100000};
    bool started_{}, held_{};
    std::string stopped_, previous_date_;
    double previous_equity_{100000}, last_price_{100};
};
[[nodiscard]] StabilityObservation ledger_observation(const nlohmann::json &record, bool risk);
} // namespace trading
