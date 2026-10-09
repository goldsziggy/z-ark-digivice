#include "battle_mode_choice.hpp"

namespace digivice::controls {
bool BattleModeChoice::propose(BattleChoice choice, std::uint32_t sequence) {
    pending_ = choice == BattleChoice::Tactical || choice == BattleChoice::Auto;
    if (pending_) { choice_ = choice; sequence_ = sequence; }
    return pending_;
}
bool BattleModeChoice::confirm(std::uint32_t sequence, BattleChoice& result) {
    const bool accepted = pending_ && sequence == sequence_;
    pending_ = false; // Stale, repeated and failed commits all require a new choice.
    if (accepted) result = choice_;
    return accepted;
}
} // namespace digivice::controls
