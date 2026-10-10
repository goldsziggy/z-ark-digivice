// Frozen rules 16 / schema 23 before rules 17 care mistakes, care-gated routes and injury.
#pragma once

#include <cstddef>
#include <cstdint>
#include "battle_trace.hpp"
#include "combat.hpp"
#include "encounters.hpp"

// This core has no heap allocation, clock, network, filesystem, or hardware dependency.
namespace digivice::legacy_v16 {

constexpr std::uint32_t kSchemaVersion = 23;
constexpr std::uint32_t kRulesVersion = 16;
constexpr std::uint32_t kDevelopmentSeed = 12345;
constexpr std::size_t kSnapshotSize = 3216;
constexpr std::size_t kV22SnapshotSize = 2964;
constexpr std::size_t kV21SnapshotSize = 2952;
constexpr std::size_t kV20SnapshotSize = 664;
constexpr std::size_t kV19SnapshotSize = 660;
constexpr std::size_t kV18SnapshotSize = 656;
constexpr std::size_t kV17SnapshotSize = 652; // V16 and V17 share this layout.
constexpr std::size_t kV15SnapshotSize = 636;
constexpr std::size_t kV14SnapshotSize = 600;
constexpr std::size_t kV13SnapshotSize = 576;
constexpr std::size_t kV7SnapshotSize = 500;
constexpr std::size_t kV6SnapshotSize = 428;
constexpr std::size_t kV5SnapshotSize = 412;
constexpr std::size_t kPreviousSnapshotSize = 404; // Formats 3 and 4.
constexpr std::size_t kLegacySnapshotSize = 96;
constexpr std::size_t kV2SnapshotSize = 100;
// Measured60-member catalog sweep:38,430 bytes including combat/care details.
constexpr std::size_t kJsonCapacity = 65536;
constexpr std::size_t kJournalCapacity = 512;
constexpr std::size_t kJournalWords = kJournalCapacity / 32;
constexpr std::uint32_t kMaxLevel = 50;
constexpr std::uint32_t kMaxXp = 49000;
constexpr std::size_t kCollectionCapacity = 60;
constexpr std::size_t kPartyCapacity = 3;
constexpr std::size_t kLegacyCollectionCapacity = 8; // Snapshot schemas1..20.
constexpr std::uint32_t kMaxReplayEvents = 10000;

enum class Phase : std::uint8_t { Home, Encounter, Egg };
enum class BattleMode : std::uint8_t { Tactical, Auto };
enum class AutoCapture : std::uint8_t { None, Awaiting };
enum class EncounterRate : std::uint8_t { Off, Relaxed, Normal, Frequent };
enum class Species : std::uint16_t {
    None, Mote, Flicker, Rill, Cinder, Impmon, Agumon, Gabumon, Patamon,
    Tentomon, Palmon, Gomamon, Renamon
};
enum class Action : std::uint8_t { Feed, Play, Rest, Walk, Card, Attack, Capture, Select, Heavy, Magic, Hatch, Mode, Auto, Evolve, Release, Flick, Explore, EncounterRate, EncounterSeed, StarterOfferSeed, AccrueSteps, PresentEncounter, ResolveTestEncounter, AutoFight, AutoResume, WorldSeed, RingCapture, PartyAdd, PartyRemove, Toilet, Retreat, CareMinute, EvolveMember };
enum class Message : std::uint8_t {
    Welcome, Fed, Played, Rested, Walked, Encounter, AttackCard, ShieldCard,
    Attacked, Won, Captured, CaptureMissed, Retreated, Evolved, Selected, EggReady, Hatched, Trained, Released, CaptureEnded, EncounterCleared, PartyAdded, PartyRemoved, Toileted
};
enum class Error : std::uint8_t {
    None, InvalidState, InvalidAction, InvalidValue, WrongPhase, LowEnergy,
    CardAlreadyUsed, WildTooStrong, CaptureLimit, CounterOverflow, CollectionFull, UnknownMember, AlreadyHatched,
    WrongMode, AutoLimit, EvolutionUnavailable, ActiveMemberRelease, PartyFull, PartyMemberExists, NotPartyMember, ActiveMemberParty
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
    // care 0-100, toilet 0-100, missed, and four 0-7 cooldowns. Zero on old saves.
    std::uint32_t careState = 0;
};
inline std::uint32_t carePoints(const CreatureMember& member) { return member.careState & 0x7fu; }
inline std::uint32_t toiletNeed(const CreatureMember& member) { return (member.careState >> 7) & 0x7fu; }
inline bool careWasMissed(const CreatureMember& member) { return ((member.careState >> 14) & 1u) != 0; }

enum class CaptureResult : std::uint8_t { None, Miss, Escaped, Captured };
struct CaptureRecord {
    std::uint32_t sequence=0,targetFormId=0,chance=0;
    std::uint8_t attempt=0,targetLevel=0;
    CaptureResult result=CaptureResult::None;
};
// A single already-selected walking foe. Zero fields mean no pending encounter.
struct PendingEncounter { std::uint32_t formId=0,level=0,rules=0; };
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
    // Explore preserves historical Home-only replay. AccrueSteps also counts in
    // menus/battles; one frozen pending foe prevents an encounter backlog.
    std::uint32_t explorationSteps = 0, walkingEncounters = 0;
    std::uint32_t encounterRng = 0, encounterTarget = 0, encounterProgress = 0;
    EncounterRate encounterRate = EncounterRate::Normal;
    CaptureRecord lastCapture{};
    std::uint32_t starterOfferSeed=0,starterOffers[3]{};
    PendingEncounter pendingEncounter{};
    // Latest gameplay action revision; walking checkpoints do not dismiss its result.
    std::uint32_t foregroundSequence=0;
    // Local acquisitions by durable Nearby exchange; never battle captures/rewards.
    std::uint32_t receivedTrades=0;
    // AutoFight stops here until an actual Flick/RingCapture or explicit AutoResume.
    AutoCapture autoCapture=AutoCapture::None;
    // Independent future encounter roster seed. Zero retains historical replay.
    std::uint32_t worldSeed=0;
    // Up to3 owned, non-active XP companions in selection order; unused slots0.
    std::uint32_t partyMemberIds[kPartyCapacity]{};
    // Awake minutes submitted by the device. Gaps and repeats do not reward or punish.
    std::uint32_t careMinute = 0;
    // 1 when the player's latest attack in this result was a critical hit.
    std::uint32_t lastCritical = 0;
    // Rules 16: a miss hides capture until the next attack resolves.
    std::uint32_t captureDeferred = 0;
};

struct Snapshot { std::uint8_t bytes[kSnapshotSize]{}; };
enum class SnapshotStatus : std::uint8_t {
    Ok, Migrated, InvalidLength, BadMagic, UnsupportedVersion,
    UnsupportedRules, BadChecksum, InvalidState
};

// Historical original-roster factory for explicit legacy/test replay only.
// Production enrollment must use newDevice.
State newGame(std::uint32_t seed = kDevelopmentSeed);
// Explicit opt-in only for a newly enrolled identity or genuinely missing save.
// Existing devices, including zero-event saves, must continue using newGame.
State newDevice(std::uint32_t seed = kDevelopmentSeed);
std::uint32_t xpForLevel(std::uint32_t level);
std::uint32_t levelForXp(std::uint32_t xp);
const CreatureMember* findMember(const State& state, std::uint32_t id);
const CreatureMember* activeMember(const State& state);
bool hasObtained(const State& state, std::uint32_t formId);
bool isPartyMember(const State&,std::uint32_t id);
std::size_t partyCount(const State&);
// Display order only: active, XP companions in selection order, remaining newest
// acquisition first. The canonical collection remains in ascending stable ID order.
const CreatureMember* collectionMemberAtDisplayIndex(const State&,std::size_t index);
std::size_t displayIndexForMember(const State&,std::uint32_t id); // collectionCount if absent.
// Ownership/partner changes compact selections, dropping lost or active IDs.
void reconcileParty(State&);
combat::CareBonus memberCare(const CreatureMember& member);
combat::Profile memberBattleProfile(const State&,const CreatureMember&);
const char* captureResultName(CaptureResult);
// IDs1..8 are fixed,9..11 are this saved identity's three one-time offers.
std::uint32_t starterForm(const State&,std::uint32_t slot);
bool validStarterOfferForm(std::uint32_t formId);
combat::Defense wildGuard(const State& state);
// Native capture odds; zero outside a legal attempt. Auto may use the same helper.
std::uint32_t captureChance(const State& state);
// Timing-input v1: phaseMs is 0..2399, graded against this state's actual foe.
// One factor on captureChance: red10%, orange50%, green100%; floor with minimum
// one only for an eligible positive base. Invalid phase/ineligible state returns0.
// No RNG, mutation, or additional rarity/level/care multiplier.
std::uint32_t ringCaptureChance(const State& state, std::uint32_t phaseMs);
// Capture-input v1 retains its existing encoding; rules12 updates odds/results.
// Quantized client observations are not proof of a real gesture.
constexpr std::uint32_t kFlickInputVersion = 1;
constexpr std::uint32_t kFlickMaxValue = 321u * 256u - 1u;
struct FlickTrajectory { std::int32_t landingX = 0, landingY = 0; bool hit = false; };
// value = (dx + 160) * 256 + reach, dx=-160..160, reach=0..255.
// 412x412 reference stage: launch(206,300), target(206,120), hit radius48.
// Invalid input leaves result unchanged; successful decoding does not mutate a save.
bool decodeFlick(std::uint32_t value, FlickTrajectory& result);
// Does not advance any RNG; zero worldSeed preserves the saved legacy seed.
std::uint32_t worldSelectionSeed(const State& state);
// Pure bounded pool selection; only explicit partner stage unlocks higher tiers.
std::uint32_t selectWildForm(std::uint32_t encounter, std::uint32_t seed,
                             std::uint32_t partnerFormId, std::uint32_t rivalLevel);
// Partner level is the center. The wild level is that center, one below, or one
// above, then clamped to 1..kMaxLevel. Same encounter, seed, and center always
// agree, including peers that share those inputs. Does not advance any RNG.
std::uint32_t wildEncounterLevel(std::uint32_t encounter, std::uint32_t seed,
                                 std::uint32_t center);
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
// Explicit checkpointed upgrade; decode itself never changes a saved encounter.
bool needsTestEncounterResolution(const State& state);
// Every error leaves state byte-for-byte unchanged. Successful actions advance sequence.
// Explore(1..1000) accepts only eligible Home steps, never a queued grant.
// EncounterRate(0..3) changes effort per step without rerolling a pending gap.
// AccrueSteps(1..1000) accepts walking in any post-starter phase. It never
// changes the active match or message; at most one frozen encounter waits.
// PresentEncounter(0) consumes that slot only in Home. The caller controls when
// Home is quiet enough for presentation. Off preserves an already earned foe.
// EncounterSeed(nonzero u32) is a private setup event, legal only once after hatch
// before walking progress. Native firmware supplies entropy and checkpoints it before use;
// replay tools may omit it and use the deterministic seed fallback.
// WorldSeed(nonzero u32) initializes future roster selection once after hatch.
// It preserves current/pending encounters, capture/pacing RNG and foregroundSequence.
// PartyAdd/PartyRemove(memberId) modify only Home companion selection.
// Selected extras each receive full base wild victory/capture XP and the same
// bond, once. Care, practice, and nearby duels do not grant companion XP.
// Toilet/CareMinute/EvolveMember/Retreat are rules 16. EvolveMember packs
// (memberId<<16)|formId and can digivolve a benched Digimon at Home.
// RingCapture(phaseMs0..2399) is additive: existing Capture/Flick replay stays
// unchanged. Every legal timing grade spends one attempt and one capture draw.
Error apply(State& state, Action action, std::uint32_t value = 0);
// Equivalent to Action::Auto. A trace is optional and valid only on success;
// firmware can persist just State's summary. One whole fight advances sequence once.
Error applyAuto(State& state, autobattle::Trace* trace = nullptr);
// Current UI flow: attack-only chunk ending at a capture opportunity or terminal
// result. Outcome::None means the full last exchange finished, then Awaiting was
// checkpointed. No capture RNG/attempt is spent without a subsequent Flick/RingCapture.
Error applyAutoFight(State& state, autobattle::Trace* trace = nullptr);
// Explicit Skip/Resume Fight: finish the remainder without any capture attempt.
// One durable event, so no separate declined flag or prompt retry is needed.
Error applyAutoResume(State& state, autobattle::Trace* trace = nullptr);
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
// V20 appends worldSeed; all older snapshots retain historical selection with zero.
// V19 appends AutoCapture; all older snapshots migrate it to None.
// V18 appends receivedTrades; all older snapshots migrate it to zero.
// V17 is the production-only roster epoch, preserving the V16 byte layout.
// V16 appends one pending encounter; V15 migration preserves every old field.
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

} // namespace digivice::legacy_v16
