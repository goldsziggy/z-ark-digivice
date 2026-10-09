// Frozen rules11/schema14 before Care, capture and starter offers.
#pragma once

#include <cstddef>
#include <cstdint>
#include "battle_trace.hpp"
#include "combat.hpp"
#include "encounters.hpp"

// This core has no heap allocation, clock, network, filesystem, or hardware dependency.
namespace digivice::legacy_v11 {

constexpr std::uint32_t kSchemaVersion = 14;
constexpr std::uint32_t kRulesVersion = 11;
constexpr std::uint32_t kDevelopmentSeed = 12345;
constexpr std::size_t kSnapshotSize = 600;
constexpr std::size_t kV13SnapshotSize = 576;
constexpr std::size_t kV7SnapshotSize = 500;
constexpr std::size_t kV6SnapshotSize = 428;
constexpr std::size_t kV5SnapshotSize = 412;
constexpr std::size_t kPreviousSnapshotSize = 404; // Formats 3 and 4.
constexpr std::size_t kLegacySnapshotSize = 96;
constexpr std::size_t kV2SnapshotSize = 100;
constexpr std::size_t kJsonCapacity = 8192;
constexpr std::size_t kJournalCapacity = 512;
constexpr std::size_t kJournalWords = kJournalCapacity / 32;
constexpr std::uint32_t kMaxLevel = 20;
constexpr std::uint32_t kMaxXp = 7600;
constexpr std::size_t kCollectionCapacity = 8;
constexpr std::uint32_t kMaxReplayEvents = 10000;

enum class Phase : std::uint8_t { Home, Encounter, Egg };
enum class BattleMode : std::uint8_t { Tactical, Auto };
enum class EncounterRate : std::uint8_t { Off, Relaxed, Normal, Frequent };
enum class Species : std::uint16_t {
    None, Mote, Flicker, Rill, Cinder, Impmon, Agumon, Gabumon, Patamon,
    Tentomon, Palmon, Gomamon, Renamon
};
enum class Action : std::uint8_t { Feed, Play, Rest, Walk, Card, Attack, Capture, Select, Heavy, Magic, Hatch, Mode, Auto, Evolve, Release, Flick, Explore, EncounterRate, EncounterSeed };
enum class Message : std::uint8_t {
    Welcome, Fed, Played, Rested, Walked, Encounter, AttackCard, ShieldCard,
    Attacked, Won, Captured, CaptureMissed, Retreated, Evolved, Selected, EggReady, Hatched, Trained, Released
};
enum class Error : std::uint8_t {
    None, InvalidState, InvalidAction, InvalidValue, WrongPhase, LowEnergy,
    CardAlreadyUsed, WildTooStrong, CaptureLimit, CounterOverflow, CollectionFull, UnknownMember, AlreadyHatched,
    WrongMode, AutoLimit, EvolutionUnavailable, ActiveMemberRelease
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
    std::uint32_t xp = 0;
    std::uint32_t formId = 0;
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
    std::uint32_t wildLevel = 0, wildTurn = 0;
    std::uint32_t wildFormId = 0, wildRules = 0;
    std::uint32_t nextMemberId = 2;
    std::uint32_t journal[kJournalWords]{};
    // Legacy steps/stepCredit above belong solely to replayable Walk events.
    // Physical lifetime steps live in the device pedometer, including menus/battles.
    // Explore counts only eligible Home steps; excess at a crossing is discarded.
    std::uint32_t explorationSteps = 0, walkingEncounters = 0;
    std::uint32_t encounterRng = 0, encounterTarget = 0, encounterProgress = 0;
    EncounterRate encounterRate = EncounterRate::Normal;
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
std::uint32_t xpForLevel(std::uint32_t level);
std::uint32_t levelForXp(std::uint32_t xp);
const CreatureMember* findMember(const State& state, std::uint32_t id);
const CreatureMember* activeMember(const State& state);
bool hasObtained(const State& state, std::uint32_t formId);
combat::Defense wildGuard(const State& state);
// Native capture odds; zero outside a legal attempt. Auto may use the same helper.
std::uint32_t captureChance(const State& state);
// Additive capture-input v1. No change to historical Capture(0), state layout,
// or RNG policy. Quantized client observations are not proof of a real gesture.
constexpr std::uint32_t kFlickInputVersion = 1;
constexpr std::uint32_t kFlickMaxValue = 321u * 256u - 1u;
struct FlickTrajectory { std::int32_t landingX = 0, landingY = 0; bool hit = false; };
// value = (dx + 160) * 256 + reach, dx=-160..160, reach=0..255.
// 412x412 reference stage: launch(206,300), target(206,120), hit radius48.
// Invalid input leaves result unchanged; successful decoding does not mutate a save.
bool decodeFlick(std::uint32_t value, FlickTrajectory& result);
// Pure bounded pool selection; only explicit partner stage unlocks higher tiers.
std::uint32_t selectWildForm(std::uint32_t encounter, std::uint32_t seed,
                             std::uint32_t partnerFormId, std::uint32_t rivalLevel);
// Number of ordinary Rest events needed for full HP/energy, or zero when
// full, unavailable, invalid, or unable to complete before sequence exhaustion.
// Bounded40, no mutation; current profile maximum requires at most12.
std::uint32_t recoveryRestCount(const State& state);
const char* encounterRateName(EncounterRate rate);
// Min/max complete gap at one unchanged rate; first gap is deliberately shorter.
// Off produces 0/0. A setting change preserves progress and does not reroll.
void encounterStepRange(EncounterRate rate, bool first, std::uint32_t& minimum, std::uint32_t& maximum);
// Saved remaining eligible steps; zero before initialization or while unavailable.
std::uint32_t encounterStepsRemaining(const State& state);
bool isValid(const State& state);
// Every error leaves state byte-for-byte unchanged. Successful actions advance sequence.
// Explore(1..1000) accepts only eligible Home steps, never a queued grant.
// EncounterRate(0..3) changes effort per step without rerolling a pending gap.
// EncounterSeed(nonzero u32) is a private setup event, legal only once in Home
// before Explore. Native firmware supplies entropy and checkpoints it before use;
// replay tools may omit it and use the deterministic seed fallback.
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
// V14 appends walking settings/pacing; V13 migration preserves every old field and
// starts Normal with no physical history. Existing active encounters keep their rules.
// V8 migration preserves all gameplay values, lineage identities and journal bits.
// Decoder is transactional. V1/V2 preserve aggregate captures as legacyCaptures.
// V1/V2/V3 scale existing HP proportions to the current derived combat maxima.
// V1–V4 become onboarding-complete; V4 preserves all existing gameplay fields.
// V1–V5 default to Tactical with no invented prior Auto result.
// V1–V6 convert training tiers 1/2/3 into levels 1/5/10 with XP credit
// 0/400/1800, preserving their displayed form, care and health proportion.
SnapshotStatus decodeSnapshot(const std::uint8_t* bytes, std::size_t length, State& state);
const char* snapshotStatusText(SnapshotStatus status);

} // namespace digivice
