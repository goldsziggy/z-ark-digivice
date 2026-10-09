// Frozen rules-3 game. Host replay only; do not update gameplay rules here.
#pragma once

#include <cstddef>
#include <cstdint>
#include "battle_trace.hpp"

// This core has no heap allocation, clock, network, filesystem, or hardware dependency.
namespace digivice::legacy_v3 {

constexpr std::uint32_t kSchemaVersion = 6;
constexpr std::uint32_t kRulesVersion = 3;
constexpr std::uint32_t kDevelopmentSeed = 12345;
constexpr std::size_t kSnapshotSize = 428;
constexpr std::size_t kV5SnapshotSize = 412;
constexpr std::size_t kPreviousSnapshotSize = 404; // Formats 3 and 4.
constexpr std::size_t kLegacySnapshotSize = 96;
constexpr std::size_t kV2SnapshotSize = 100;
constexpr std::size_t kJsonCapacity = 4096;
constexpr std::size_t kCollectionCapacity = 8;
constexpr std::uint32_t kMaxReplayEvents = 10000;

enum class Phase : std::uint8_t { Home, Encounter, Egg };
enum class BattleMode : std::uint8_t { Tactical, Auto };
enum class Species : std::uint8_t {
    None, Mote, Flicker, Rill, Cinder, Impmon, Agumon, Gabumon, Patamon,
    Tentomon, Palmon, Gomamon, Renamon
};
enum class Action : std::uint8_t { Feed, Play, Rest, Walk, Card, Attack, Capture, Select, Heavy, Magic, Hatch, Mode, Auto };
enum class Message : std::uint8_t {
    Welcome, Fed, Played, Rested, Walked, Encounter, AttackCard, ShieldCard,
    Attacked, Won, Captured, CaptureMissed, Retreated, Evolved, Selected, EggReady, Hatched, Trained
};
enum class Error : std::uint8_t {
    None, InvalidState, InvalidAction, InvalidValue, WrongPhase, LowEnergy,
    CardAlreadyUsed, WildTooStrong, CaptureLimit, CounterOverflow, CollectionFull, UnknownMember, AlreadyHatched,
    WrongMode, AutoLimit
};

struct CreatureMember {
    std::uint32_t id = 0;
    Species species = Species::None;
    std::uint32_t hp = 0;
    std::uint32_t energy = 0;
    std::uint32_t fullness = 0;
    std::uint32_t mood = 0;
    std::uint32_t bond = 0;
    std::uint32_t level = 0;
    std::uint32_t capturedAtSequence = 0;
};

struct State {
    std::uint32_t sequence = 0;
    std::uint32_t seed = kDevelopmentSeed;
    std::uint32_t rngState = kDevelopmentSeed;
    std::uint32_t steps = 0;
    std::uint32_t stepCredit = 0;
    std::uint32_t hp = 100;
    std::uint32_t energy = 80;
    std::uint32_t fullness = 70;
    std::uint32_t mood = 80;
    std::uint32_t bond = 0;
    std::uint32_t level = 1;
    std::uint32_t captures = 0;
    std::uint32_t encounters = 0;
    std::uint32_t wildHp = 0;
    std::uint32_t wildMaxHp = 0;
    std::uint32_t captureAttempts = 0;
    Phase phase = Phase::Home;
    bool cardUsed = false;
    std::uint32_t attackBoost = 0;
    std::uint32_t shield = 0;
    Message message = Message::Welcome;
    std::uint32_t legacyCaptures = 0;
    std::uint32_t activeCreatureId = 1;
    std::uint32_t collectionCount = 1;
    Species wildSpecies = Species::None;
    CreatureMember collection[kCollectionCapacity]{};
    bool onboardingComplete = true;
    std::uint32_t starterId = 0; // 0 identifies an existing original-roster save.
    BattleMode battleMode = BattleMode::Tactical;
    autobattle::Outcome lastAutoOutcome = autobattle::Outcome::None;
    std::uint32_t lastAutoTurns = 0, lastAutoSequence = 0;
};

struct Snapshot { std::uint8_t bytes[kSnapshotSize]{}; };
enum class SnapshotStatus : std::uint8_t {
    Ok, Migrated, InvalidLength, BadMagic, UnsupportedVersion,
    UnsupportedRules, BadChecksum, InvalidState
};

State newGame(std::uint32_t seed = kDevelopmentSeed);
// Explicit opt-in only for a newly enrolled identity or genuinely missing save.
// Existing devices, including zero-event saves, must continue using newGame.
State newDevice(std::uint32_t seed = kDevelopmentSeed);
bool isValid(const State& state);
// Every error leaves state byte-for-byte unchanged. Successful actions advance sequence.
Error apply(State& state, Action action, std::uint32_t value = 0);
// Equivalent to Action::Auto. A trace is optional and valid only on success;
// firmware can persist just State's summary. One whole fight advances sequence once.
Error applyAuto(State& state, autobattle::Trace* trace = nullptr);
const char* errorText(Error error);
const char* messageText(Message message);
const char* creatureName(const State& state);
const char* speciesId(Species species);
const char* memberName(const CreatureMember& member);
const char* wildName(const State& state);
bool parseAction(const char* name, Action& action);
// Returns bytes written, excluding NUL; zero indicates an invalid state or small buffer.
std::size_t writeJson(const State& state, char* output, std::size_t capacity);
// Canonical little-endian encoding with CRC32. It never persists struct padding.
bool encodeSnapshot(const State& state, Snapshot& snapshot);
// Decoder is transactional. V1/V2 preserve aggregate captures as legacyCaptures.
// V1/V2/V3 scale existing HP proportions to the current derived combat maxima.
// V1–V4 become onboarding-complete; V4 preserves all existing gameplay fields.
// V1–V5 default to Tactical with no invented prior Auto result.
SnapshotStatus decodeSnapshot(const std::uint8_t* bytes, std::size_t length, State& state);
const char* snapshotStatusText(SnapshotStatus status);

} // namespace digivice::legacy_v3
