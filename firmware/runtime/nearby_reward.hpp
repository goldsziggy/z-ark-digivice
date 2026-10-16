#pragma once
#include "nearby_protocol.hpp"
#include "expeditions.hpp"
#include "game.hpp"

namespace digivice::nearby {
enum class RewardResult { Unavailable, RetryCheckpoint, Committed, Rejected };

// The runtime and host tests share this exact checkpoint boundary. No gameplay
// state or session receipt changes until durable persistence succeeds. A failed
// checkpoint is safe to retry; a committed session cannot consume another key.
// Sessions are ephemeral: closing/rebooting never reconstructs a terminal offer.
template <class Checkpoint>
RewardResult checkpointRaidReward(const View& view, State& state,
                                  std::uint64_t& claimedSession, Checkpoint checkpoint) {
    if (state.phase != Phase::Home || !claimableRaid(view, claimedSession)) return RewardResult::Unavailable;
    State candidate = state;
    const auto value = expeditions::claimExpeditionValue(view.raid.scenario, view.raid.foeForm, view.raid.foeLevel);
    if (apply(candidate, Action::ClaimExpedition, value) != Error::None) {
        claimedSession = view.session;
        return RewardResult::Rejected;
    }
    if (!checkpoint(candidate)) return RewardResult::RetryCheckpoint;
    state = candidate;
    claimedSession = view.session;
    return RewardResult::Committed;
}
} // namespace digivice::nearby
