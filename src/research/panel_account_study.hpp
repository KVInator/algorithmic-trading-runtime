#pragma once
#include "panel_replay_dataset.hpp"
#include <memory>

namespace trading {
// Continuous fractional risk ledgers and whole-share shared ledgers retain
// separate financial conventions. Both consume the same frozen M3 decisions.
class PanelAccountStudy {
  public:
    PanelAccountStudy(const ExtensionStudyContract &contract, nlohmann::json actions);
    ~PanelAccountStudy();
    [[nodiscard]] nlohmann::json plan() const;
    [[nodiscard]] nlohmann::json append(std::span<const PanelReplayDay> days,
                                        std::span<const SessionDownload> sessions,
                                        std::span<const std::optional<MinuteBar>> recovered, bool terminal);
    [[nodiscard]] nlohmann::json finish();

  private:
    class State;
    std::unique_ptr<State> state_;
};
} // namespace trading
