// Frozen rules-v1 engine for trusted legacy history replay. CLI only; never linked into firmware.
#pragma once

#include <cstddef>
#include <cstdint>

// This core has no heap allocation, clock, network, filesystem, or hardware dependency.
namespace digivice_v1 {

constexpr std::uint32_t kSchemaVersion = 2;
constexpr std::uint32_t kRulesVersion = 1;
constexpr std::uint32_t kDevelopmentSeed = 12345;
constexpr std::size_t kSnapshotSize = 100;
constexpr std::size_t kLegacySnapshotSize = 96;
constexpr std::size_t kJsonCapacity = 1024;
constexpr std::uint32_t kMaxReplayEvents = 10000;

enum class Phase : std::uint8_t { Home, Encounter };
enum class Action : std::uint8_t { Feed, Play, Rest, Walk, Card, Attack, Capture };
enum class Message : std::uint8_t {
    Welcome, Fed, Played, Rested, Walked, Encounter, AttackCard, ShieldCard,
    Attacked, Won, Captured, CaptureMissed, Retreated, Evolved
};
enum class Error : std::uint8_t {
    None, InvalidState, InvalidAction, InvalidValue, WrongPhase, LowEnergy,
    CardAlreadyUsed, WildTooStrong, CaptureLimit, CounterOverflow
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
};

struct Snapshot { std::uint8_t bytes[kSnapshotSize]{}; };
enum class SnapshotStatus : std::uint8_t {
    Ok, Migrated, InvalidLength, BadMagic, UnsupportedVersion,
    UnsupportedRules, BadChecksum, InvalidState
};

State newGame(std::uint32_t seed = kDevelopmentSeed);
bool isValid(const State& state);
// Every error leaves state byte-for-byte unchanged. Successful actions advance sequence.
Error apply(State& state, Action action, std::uint32_t value = 0);
const char* errorText(Error error);
const char* messageText(Message message);
const char* creatureName(const State& state);
bool parseAction(const char* name, Action& action);
// Returns bytes written, excluding NUL; zero indicates an invalid state or small buffer.
std::size_t writeJson(const State& state, char* output, std::size_t capacity);
// Canonical little-endian encoding with CRC32. It never persists struct padding.
bool encodeSnapshot(const State& state, Snapshot& snapshot);
// Decoder is transactional. V1 snapshots migrate by initializing the new shield field.
SnapshotStatus decodeSnapshot(const std::uint8_t* bytes, std::size_t length, State& state);
const char* snapshotStatusText(SnapshotStatus status);

} // namespace digivice_v1
