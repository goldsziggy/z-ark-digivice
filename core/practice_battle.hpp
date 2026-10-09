#pragma once
#include <cstddef>
#include <cstdint>
#include "battle_trace.hpp"

// Independent practice rules. No dependency on, or writes to, the companion game.
namespace digivice::practice {
constexpr std::size_t kLegacySnapshotSize = 100;
constexpr std::size_t kV2SnapshotSize = 112;
constexpr std::size_t kSnapshotSize = 120;
constexpr std::size_t kJsonCapacity = 2048;
constexpr std::uint32_t kMaxExchanges = 40;
constexpr std::uint32_t kLegacyMaxExchanges = 30;
enum class Phase : std::uint8_t { Attack, Defend, Finished };
enum class Status : std::uint8_t { Active, Won, Lost, Draw, Retreated };
enum class Move : std::uint8_t { None, Physical, Heavy, Magic, Brace, Counter, Ward };
enum class Error : std::uint8_t { None, InvalidState, InvalidAction, WrongPhase, InvalidValue, CardUsed, Finished };
struct State {
    // Retained per snapshot: saved2..6 duels finish under their original rules.
    std::uint32_t rulesVersion = 7;
    std::uint32_t playerFormId = 0, enemyFormId = 0;
    std::uint32_t sequence = 0;
    std::uint32_t rngState = 1; // Private: never expose to the browser.
    std::uint32_t playerLevel = 1;
    std::uint32_t playerSpecies = 1;
    std::uint32_t enemySpecies = 2;
    std::uint32_t enemyLevel = 1;
    std::uint32_t playerHp = 100;
    std::uint32_t enemyHp = 100;
    std::uint32_t exchanges = 0;
    Phase phase = Phase::Attack;
    Status status = Status::Active;
    bool cardUsed = false;
    std::uint32_t attackBoost = 0;
    std::uint32_t shield = 0;
    Move enemyChoice = Move::None; // Private committed move, selected before input.
    Move excludedChoice = Move::None; // Public hint omits this impossible alternative.
    Move previousEnemyMove = Move::None;
    Move lastEnemyMove = Move::None;
    Move lastPlayerChoice = Move::None;
    Move lastOpponentChoice = Move::None;
    std::uint32_t lastPlayerDamage = 0;
    std::uint32_t lastEnemyDamage = 0;
    bool lastReflected = false;
    Phase lastPhase = Phase::Attack;
};
struct Snapshot { std::uint8_t bytes[kSnapshotSize]{}; };
State newBattle(std::uint32_t seed, std::uint32_t playerSpecies, std::uint32_t playerLevel,
                std::uint32_t enemySpecies, std::uint32_t enemyLevel);
State newBattleWithForms(std::uint32_t seed, std::uint32_t playerSpecies, std::uint32_t playerLevel,
                std::uint32_t playerFormId, std::uint32_t enemySpecies, std::uint32_t enemyLevel,
                std::uint32_t enemyFormId);
std::uint32_t selectProductionRivalForm(std::uint32_t seed, std::uint32_t playerFormId, std::uint32_t rivalLevel);
std::uint32_t selectRivalForm(std::uint32_t seed, std::uint32_t playerFormId, std::uint32_t rivalLevel);
std::uint32_t maxHp(const State& state, bool enemy = false);
std::uint32_t maxExchanges(const State& state);
// Public-profile-only resolver input for practice and audit policies. Zero means
// unsupported rules/HP or a floor outside the combat resolver's4..32 bound.
std::uint32_t minimumRawDamage(std::uint32_t rulesVersion, std::uint32_t intendedDefenderMaxHp);
std::size_t snapshotSize(const State& state);
// Header-only length hint; full decode still verifies version, CRC and bounds.
std::size_t snapshotSize(const std::uint8_t* bytes, std::size_t available);
bool isValid(const State& state);
// Atomic errors: neither state nor hidden commitment changes on rejected input.
Error apply(State& state, const char* action, std::uint32_t value = 0);
// Whole-battle Auto from an untouched start only. Transactional on failure;
// uses the same resolver and a separate seeded policy stream with no access to
// committed enemy moves/hints. Rules7 scores public profiles/HP against all three
// possible opponent moves; saved2..6 retain their phase-only random policy.
// At most40 exchanges (30 for saved rules2–4); no cards or pet rewards.
// Auto's terminal snapshot retains its start's format; the service persists mode and
// initial snapshot alongside it so it can regenerate this trace after restart.
Error runAuto(State& state, autobattle::Trace& trace);
const char* errorText(Error error);
const char* moveName(Move move);
std::size_t writePublicJson(const State& state, char* output, std::size_t capacity);
bool encodeSnapshot(const State& state, Snapshot& snapshot);
bool decodeSnapshot(const std::uint8_t* bytes, std::size_t length, State& state);
// Explicit rules upgrade: preserve commitment/history; scale living HP upward
// to the next integer at the same fraction. Legacy rival becomes Flicker Lv. 1.
// The trusted external profile must match the level encoded by the old save.
bool migrateV1(const std::uint8_t* bytes, std::size_t length, std::uint32_t playerSpecies,
               std::uint32_t playerLevel, State& state);
} // namespace digivice::practice
