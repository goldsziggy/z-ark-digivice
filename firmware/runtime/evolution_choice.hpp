#pragma once
#include "game.hpp"

namespace digivice::controls {
// A preview is an ephemeral choice, never a saved event. The caller checkpoints
// the returned candidate before replacing authoritative RAM or acknowledging it.
class EvolutionChoice {
public:
    bool propose(const State& state, std::uint32_t targetForm);
    bool confirm(const State& state, State& candidate);
    void cancel() { target_ = 0; }
    std::uint32_t target() const { return target_; }
    Error error() const { return error_; }
private:
    std::uint32_t target_ = 0, sequence_ = 0, member_ = 0, form_ = 0;
    Error error_ = Error::None;
};
} // namespace digivice::controls
