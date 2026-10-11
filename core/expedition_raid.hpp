#pragma once
#include "nearby_match.hpp"
#include <cstddef>
#include <cstdint>

// Shared dungeon/boss fight on the existing nearby radio. The versus match
// record stays untouched. Both devices attack one foe; the key or sigil is
// spent only after that foe falls.
namespace digivice::nearby {
constexpr std::size_t kRaidBytes = 108;
constexpr std::uint32_t kMaxRaidRounds = 24;
enum class RaidStatus : std::uint8_t { Active, Cleared, Wiped };
struct Raid {
    Fighter players[2]{};
    std::uint32_t foeForm = 0, foeLevel = 0, foeHp = 0, foeMax = 0;
    std::uint32_t hp[2]{}, energy[2]{};
    std::uint32_t seed = 0, rng = 0, sequence = 0;
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
bool legalRaidChoice(const Raid&, unsigned actor, Choice);
bool resolveRaid(Raid&, std::uint32_t expectedSequence, Choice host, Choice guest);
bool encodeRaid(const Raid&, std::uint8_t* bytes, std::size_t capacity);
bool decodeRaid(const std::uint8_t* bytes, std::size_t length, Raid& out);
bool sameRaid(const Raid&, const Raid&);
} // namespace digivice::nearby
