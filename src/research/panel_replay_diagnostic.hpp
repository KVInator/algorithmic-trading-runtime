#pragma once
#include "panel_replay_report.hpp"
#include <cstddef>
#include <functional>
#include <map>
#include <span>

namespace trading {
// One analytic observation source serves the causal replay and M6 account checks.
// Python reconstructs the same declared fixture independently.
class PanelReplayFixture {
  public:
    static constexpr std::size_t date_count = 300;
    using Consumer =
        std::function<void(std::vector<PanelReplayDay>, std::vector<SessionDownload>, bool)>;
    using States = std::map<std::string, PanelReplayState>;
    explicit PanelReplayFixture(std::size_t sessions = date_count);
    [[nodiscard]] std::span<const CalendarSession> calendar() const noexcept;
    [[nodiscard]] static std::vector<MinuteBar>
    bars(const CalendarSession &day, const std::string &symbol, std::size_t index, bool faults = true);
    [[nodiscard]] States replay(const PanelInputPolicy &policy, const std::vector<std::string> &symbols,
                                const Consumer &consume) const;

  private:
    const std::vector<CalendarSession> calendar_;
    const std::size_t sessions_;
};

class PanelReplayDiagnostic {
  public:
    using Check = std::function<void(const std::string &, bool)>;
    using Reject = std::function<void(const std::string &, const std::function<void()> &)>;
    [[nodiscard]] std::filesystem::path run(const std::filesystem::path &output_root,
                                            const PanelInputPolicy &policy, const Check &check,
                                            const Reject &reject) const;
};
} // namespace trading
