#pragma once
#include <cstddef>
#include <cstdint>

namespace digivice::autobattle {
constexpr std::size_t kMaxTraceSteps = 48;
// Host CLI serialization only. Device resolution can omit the optional trace.
constexpr std::size_t kTraceJsonCapacity = 16384;
enum class Kind : std::uint8_t { Wild, Practice };
enum class Outcome : std::uint8_t { None, Won, Captured, Retreated, Lost, Draw };
enum class Move : std::uint8_t { None, Physical, Heavy, Magic, Brace, Counter, Ward, Capture };
struct Step {
    bool defending = false;
    Move action = Move::None, opponentAction = Move::None;
    std::uint32_t playerHpBefore = 0, playerHpAfter = 0;
    std::uint32_t enemyHpBefore = 0, enemyHpAfter = 0;
    bool reflected = false, captured = false;
    Move guard = Move::None;
    std::uint32_t captureChance = 0;
    std::uint8_t captureAttempt = 0, captureResult = 0; // CaptureResult numeric values; no RNG.
};
struct Trace {
    std::uint32_t combatRulesVersion = 4;
    std::uint32_t playerFormId = 0, enemyFormId = 0;
    bool includeFormIds = false;
    std::uint32_t playerOffenseBonus = 0, playerProtectionBonus = 0;
    std::uint32_t enemyOffenseBonus = 0, enemyProtectionBonus = 0;
    Kind kind = Kind::Wild;
    Outcome outcome = Outcome::None;
    std::uint32_t startSequence = 0, endSequence = 0;
    std::uint32_t playerSpecies = 0, playerLevel = 0, enemySpecies = 0, enemyLevel = 0;
    std::size_t count = 0;
    Step steps[kMaxTraceSteps]{};
};
const char* moveName(Move move);
const char* outcomeName(Outcome outcome);
// No RNG/commitment/seed is serialized. Zero return clears a nonempty output.
std::size_t writeJson(const Trace& trace, char* output, std::size_t capacity);
// Separate deterministic policy stream: callers never feed opponent intent.
std::uint32_t nextRandom(std::uint32_t& state);
} // namespace digivice::autobattle
