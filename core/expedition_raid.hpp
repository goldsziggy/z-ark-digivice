#pragma once
#include "nearby_match.hpp"
#include <cstddef>
#include <cstdint>

// Shared dungeon/boss fight on the existing nearby radio. The versus match
// record stays untouched. Both devices attack one foe; the key or sigil is
// spent only after the final tier falls. Dungeon runs carry HP/energy between
// three tiers; standalone bosses retain a single fight.
namespace digivice::nearby {
constexpr std::size_t kRaidBytes = 120;
constexpr std::uint32_t kMaxRaidRounds = 24;
enum class RaidStatus : std::uint8_t { Active, Cleared, Wiped };
struct Raid {
    Fighter players[2]{};
    std::uint32_t foeForm = 0, foeLevel = 0, foeHp = 0, foeMax = 0;
    std::uint32_t hp[2]{}, energy[2]{};
    std::uint32_t seed = 0, rng = 0, sequence = 0;
    std::uint32_t tierRound = 0; // Round budget resets; sequence never does.
    std::uint8_t tier = 1, lastTier = 0;
    // Preserve the struck foe through feedback when a kill opens the next tier.
    std::uint32_t lastFoeForm = 0, lastFoeLevel = 0;
    std::uint8_t scenario = 0;
    RaidStatus status = RaidStatus::Active;
    Choice lastAttack = Choice::None, lastDefense = Choice::None;
    std::uint32_t lastFoeDamage = 0, lastPlayerDamage = 0;
    std::uint8_t foeTarget = 0;
    bool lastCritical = false;
};
// Seed packs a production foe: form in 0-15, level in 16-23, scenario in 24-26.
std::uint32_t raidSeed(std::uint32_t formId, std::uint32_t level, std::uint32_t scenario);
bool decodeRaidSeed(std::uint32_t seed, std::uint32_t scenario, std::uint32_t& formId, std::uint32_t& level);
bool validRaid(const Raid&);
bool beginRaid(const Fighter& host, const Fighter& guest, std::uint32_t seed, Raid& out);
// Roles skip knocked-out partners. A sole survivor supplies the strike and
// receives the reply without a guard. kNoRaidActor means no living role exists.
constexpr unsigned kNoRaidActor = 2;
unsigned raidAttacker(const Raid&);
unsigned raidDefender(const Raid&);
// Feedback needs the role from before the last exchange changed HP. None guard
// marks a turn that began with one survivor; otherwise prior parity identifies it.
unsigned raidLastAttacker(const Raid&);
// No rest move exists; the actual living attacker must afford a basic attack.
bool raidEnergyExhausted(const Raid&);
bool legalRaidChoice(const Raid&, unsigned actor, Choice);
// KO slots must contain None, supplied internally by the protocol. None is
// never a legal user choice; living actors still supply their own strike/guard.
bool resolveRaid(Raid&, std::uint32_t expectedSequence, Choice host, Choice guest);
// Prefer the stronger visible basic strike; equal-damage ties and the three
// defensive guards retain their seeded draws. Friendly duel Auto is unchanged.
// Host pacing and peer verification remain the protocol's responsibility.
bool resolveAutoRaid(Raid&, std::uint32_t expectedSequence);
bool encodeRaid(const Raid&, std::uint8_t* bytes, std::size_t capacity);
bool decodeRaid(const std::uint8_t* bytes, std::size_t length, Raid& out);
bool sameRaid(const Raid&, const Raid&);
} // namespace digivice::nearby
