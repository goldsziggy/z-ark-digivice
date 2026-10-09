#pragma once
#include <cstdint>

namespace digivice::controls {
// UI choice only; never a combat rule or persisted game mode. The caller maps
// a confirmed choice into a shared-core action and saves before acknowledging.
enum class BattleChoice : std::uint8_t { Tactical, Auto };
class BattleModeChoice {
public:
    bool propose(BattleChoice choice, std::uint32_t sequence);
    bool confirm(std::uint32_t sequence, BattleChoice& result);
    void cancel() { pending_ = false; }
    bool pending() const { return pending_; }
    BattleChoice selected() const { return choice_; }
private:
    std::uint32_t sequence_ = 0;
    BattleChoice choice_ = BattleChoice::Tactical;
    bool pending_ = false;
};
} // namespace digivice::controls
