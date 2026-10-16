#pragma once
#include "expedition_raid.hpp"

namespace digivice::nearby {
// Fixed work/stack budget: 24 candidates, two planning streams, both orders.
constexpr std::uint32_t kCoopFoeCandidateLimit = 24;
constexpr std::uint32_t kCoopFoeRoundLimit = kCoopFoeCandidateLimit * 2 * 2 * 3 * kMaxRaidRounds;
// These common planning streams are outside the valid packed raid-seed range.
// The actual battle RNG is never previewed by selection.
inline constexpr std::uint32_t kCoopPlanningSeeds[]{0x6d2b79f5u,0xa341316cu};
struct CoopFoeDiagnostics {
    std::uint32_t candidates = 0, rounds = 0;
    std::uint32_t clearTrials = 0, minSurvivors = 0, minHpPercent = 0;
    std::uint32_t minRounds = 0, maxRounds = 0;
};
// Preview only: no State, RNG, saved HP/energy, session or member identity is
// involved. Both natural full-HP/100-energy raid entrants are scored through
// the complete scenario by the real Auto resolver in both host orders with
// independent planning RNG. The future packed-seed battle is not simulated.
// Output arguments are unchanged on invalid input. Diagnostics describe the
// four planning trajectories, not a statistical success guarantee.
bool previewCoopExpeditionFoe(const Fighter&, const Fighter&, std::uint32_t scenario,
    std::uint32_t& formId, std::uint32_t& level, CoopFoeDiagnostics* diagnostics = nullptr);
} // namespace digivice::nearby
