#pragma once
#include "game.hpp"
namespace digivice::controls {
// Ephemeral review of existing native Rest actions. Caller saves one complete
// candidate before replacing RAM/acknowledging. No timer, new heal rule or XP.
class RecoveryChoice {
public:
    bool propose(const State& state);
    bool confirm(const State& state, State& candidate);
    void cancel() { count_=0; }
    std::uint32_t count() const { return count_; }
private:
    std::uint32_t sequence_=0, member_=0, form_=0, count_=0;
};
}
