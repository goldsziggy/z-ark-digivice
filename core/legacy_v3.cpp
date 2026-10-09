// Frozen rules-3 game. Host replay only; do not update gameplay rules here.
#include "legacy_v3.hpp"
#include "legacy_combat_v3.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>

namespace digivice::legacy_v3 {
namespace {
constexpr std::uint32_t kMax = std::numeric_limits<std::uint32_t>::max();
std::uint32_t cappedAdd(std::uint32_t current, std::uint32_t amount, std::uint32_t cap) {
    return amount >= cap - current ? cap : current + amount;
}
std::uint32_t levelFor(Species species, std::uint32_t bond) {
    return species == Species::Flicker ? 1 : bond >= 100 ? 3 : bond >= 40 ? 2 : 1;
}
std::uint32_t maxHp(Species species, std::uint32_t level) {
    return combat::profile(static_cast<std::uint32_t>(species), level).stats.maxHp;
}
std::uint32_t scaleHp(std::uint32_t hp, std::uint32_t oldMax, std::uint32_t newMax) {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(hp) * newMax + oldMax - 1) / oldMax);
}
CreatureMember freshMember(std::uint32_t id, Species species, std::uint32_t sequence) {
    return {id, species, maxHp(species, 1), 80, 70, 80, 0, 1, sequence};
}
void storeActive(State& state) {
    auto& member = state.collection[state.activeCreatureId - 1];
    member.hp = state.hp; member.energy = state.energy; member.fullness = state.fullness;
    member.mood = state.mood; member.bond = state.bond; member.level = state.level;
}
void loadActive(State& state) {
    const auto& member = state.collection[state.activeCreatureId - 1];
    state.hp = member.hp; state.energy = member.energy; state.fullness = member.fullness;
    state.mood = member.mood; state.bond = member.bond; state.level = member.level;
}
std::uint32_t random(State& state) {
    std::uint32_t x = state.rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return state.rngState = x;
}
void addBond(State& state, std::uint32_t amount) {
    state.bond = cappedAdd(state.bond, amount, 200);
    const auto level = levelFor(state.collection[state.activeCreatureId - 1].species, state.bond);
    if (level > state.level) {
        const auto species = state.collection[state.activeCreatureId - 1].species;
        state.hp = scaleHp(state.hp, maxHp(species, state.level), maxHp(species, level));
        state.message = combat::isStarterSpecies(static_cast<std::uint32_t>(species)) ? Message::Trained : Message::Evolved;
    }
    state.level = level;
}
void home(State& state) {
    state.phase = Phase::Home;
    state.wildHp = state.wildMaxHp = state.captureAttempts = 0;
    state.attackBoost = state.shield = 0;
    state.cardUsed = false;
    state.wildSpecies = Species::None;
}
void wildResponse(State& state) {
    const auto activeSpecies = state.collection[state.activeCreatureId - 1].species;
    const auto damage = combat::resolve(static_cast<std::uint32_t>(state.wildSpecies), 1,
        static_cast<std::uint32_t>(activeSpecies), state.level,
        combat::Move::Physical, combat::Defense::None).damage;
    const auto blocked = state.shield < damage ? state.shield : damage;
    state.shield -= blocked;
    const auto received = damage - blocked;
    if (received >= state.hp) {
        home(state);
        state.hp = (maxHp(activeSpecies, state.level) + 9) / 10;
        state.message = Message::Retreated;
    } else {
        state.hp -= received;
    }
}
void put32(std::uint8_t* bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<std::uint8_t>(value >> (i * 8));
}
std::uint32_t get32(const std::uint8_t* bytes) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(bytes[i]) << (i * 8);
    return value;
}
std::uint32_t crc32(const std::uint8_t* bytes, std::size_t length) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
} // namespace

State newGame(std::uint32_t seed) {
    State state;
    state.seed = seed;
    state.rngState = seed ? seed : 0x6d2b79f5u;
    state.collection[0] = freshMember(1, Species::Mote, 0);
    loadActive(state);
    return state;
}

State newDevice(std::uint32_t seed) {
    State state;
    state.seed = seed;
    state.rngState = seed ? seed : 0x6d2b79f5u;
    state.hp = state.energy = state.fullness = state.mood = state.level = 0;
    state.activeCreatureId = state.collectionCount = 0;
    state.phase = Phase::Egg;
    state.message = Message::EggReady;
    state.onboardingComplete = false;
    return state;
}

static bool emptyMember(const CreatureMember& m) {
    return !m.id && m.species == Species::None && !m.hp && !m.energy && !m.fullness &&
           !m.mood && !m.bond && !m.level && !m.capturedAtSequence;
}

static bool validForVersion(const State& state, bool legacy) {
    if (static_cast<unsigned>(state.battleMode) > 1 || static_cast<unsigned>(state.lastAutoOutcome) > 3) return false;
    if (state.lastAutoOutcome == autobattle::Outcome::None) {
        if (state.lastAutoTurns || state.lastAutoSequence) return false;
    } else if (!state.lastAutoTurns || state.lastAutoTurns > autobattle::kMaxTraceSteps ||
               !state.lastAutoSequence || state.lastAutoSequence > state.sequence ||
               (state.lastAutoOutcome == autobattle::Outcome::Captured && !state.captures)) return false;
    if (!state.onboardingComplete) {
        if (legacy || state.phase != Phase::Egg || state.starterId || state.sequence ||
            state.rngState != (state.seed ? state.seed : 0x6d2b79f5u) ||
            state.steps || state.stepCredit || state.hp || state.energy || state.fullness ||
            state.mood || state.bond || state.level || state.captures || state.encounters ||
            state.wildHp || state.wildMaxHp || state.captureAttempts || state.cardUsed ||
            state.attackBoost || state.shield || state.legacyCaptures || state.activeCreatureId ||
            state.collectionCount || state.wildSpecies != Species::None || state.message != Message::EggReady ||
            state.battleMode != BattleMode::Tactical || state.lastAutoOutcome != autobattle::Outcome::None)
            return false;
        for (const auto& m : state.collection) if (!emptyMember(m)) return false;
        return true;
    }
    if (state.starterId > combat::kStarterCount || (state.starterId && (!state.sequence || legacy || state.legacyCaptures)) ||
        state.message == Message::EggReady ||
        (state.message == Message::Hatched && !state.starterId)) return false;
    if (state.collectionCount < 1 || state.collectionCount > kCollectionCapacity ||
        state.activeCreatureId < 1 || state.activeCreatureId > state.collectionCount ||
        state.legacyCaptures > state.captures ||
        state.captures - state.legacyCaptures != state.collectionCount - 1) return false;
    for (std::size_t i = 0; i < kCollectionCapacity; ++i) {
        const auto& member = state.collection[i];
        if (i >= state.collectionCount) {
            if (!emptyMember(member)) return false;
            continue;
        }
        if (member.id != i + 1 || !combat::validProfile(static_cast<std::uint32_t>(member.species), member.level) ||
            member.bond > 200 || member.level != levelFor(member.species, member.bond) ||
            member.hp < 1 || member.hp > (legacy ? 100 : maxHp(member.species, member.level)) || member.energy > 100 || member.fullness > 100 ||
            member.mood > 100 ||
            member.capturedAtSequence > state.sequence) return false;
        if (i == 0) {
            const auto founder = state.starterId ? static_cast<Species>(combat::starterSpecies(state.starterId)) : Species::Mote;
            if (member.species != founder || member.capturedAtSequence != 0) return false;
        } else if (member.species < Species::Flicker || member.species > Species::Cinder ||
                   member.capturedAtSequence <= state.collection[i - 1].capturedAtSequence) return false;
    }
    const auto& active = state.collection[state.activeCreatureId - 1];
    if (state.hp != active.hp || state.energy != active.energy || state.fullness != active.fullness ||
        state.mood != active.mood || state.bond != active.bond || state.level != active.level) return false;
    if (!state.rngState || state.steps < state.stepCredit ||
        (state.steps - state.stepCredit) % 100 != 0 ||
        state.encounters != (state.steps - state.stepCredit) / 100 ||
        state.captures > state.encounters || state.encounters > state.sequence ||
        static_cast<unsigned>(state.message) > static_cast<unsigned>(Message::Trained)) return false;
    if (state.phase == Phase::Home) {
        return state.wildHp == 0 && state.wildMaxHp == 0 && state.captureAttempts == 0 &&
               !state.cardUsed && state.attackBoost == 0 && state.shield == 0 && state.wildSpecies == Species::None;
    }
    if (state.phase != Phase::Encounter || !state.encounters ||
        state.wildSpecies < Species::Flicker || state.wildSpecies > Species::Cinder || (legacy ? (state.wildMaxHp < 24 || state.wildMaxHp > 30) :
        state.wildMaxHp != maxHp(state.wildSpecies, 1)) || state.wildHp < 1 || state.wildHp > state.wildMaxHp ||
        state.captureAttempts > 3 || (state.attackBoost != 0 && state.attackBoost != 5) ||
        state.shield > 12 || (state.attackBoost && state.shield) ||
        (!state.cardUsed && (state.attackBoost || state.shield))) return false;
    // Auto has no partially resolved durable state. It waits untouched for an
    // explicit Auto event, then commits only the terminal result.
    if (state.battleMode == BattleMode::Auto && (state.wildHp != state.wildMaxHp ||
        state.captureAttempts || state.cardUsed || state.attackBoost || state.shield)) return false;
    return true;
}

bool isValid(const State& state) { return validForVersion(state, false); }

Error apply(State& state, Action action, std::uint32_t value) {
    if (!isValid(state)) return Error::InvalidState;
    if (static_cast<unsigned>(action) > static_cast<unsigned>(Action::Auto)) return Error::InvalidAction;
    if (state.sequence == kMax) return Error::CounterOverflow;
    if (action == Action::Hatch) {
        if (!combat::starterSpecies(value)) return Error::InvalidValue;
        if (state.onboardingComplete) return Error::AlreadyHatched;
    } else if (!state.onboardingComplete) {
        return Error::WrongPhase;
    } else if (action == Action::Mode) {
        if (value > 1) return Error::InvalidValue;
        if (state.phase != Phase::Home) return Error::WrongPhase;
    } else if (action == Action::Walk) {
        if (value < 1 || value > 1000) return Error::InvalidValue;
        if (state.steps > kMax - value || state.stepCredit > kMax - value)
            return Error::CounterOverflow;
    } else if (action == Action::Card) {
        if (value != 1 && value != 2) return Error::InvalidValue;
    } else if (action == Action::Select) {
        if (value < 1 || value > kCollectionCapacity) return Error::InvalidValue;
    } else if (value != 0) {
        return Error::InvalidValue;
    }
    if (action == Action::Auto) return applyAuto(state);
    if (state.phase == Phase::Encounter && state.battleMode == BattleMode::Auto && action != Action::Walk)
        return Error::WrongMode;
    State next = state;
    switch (action) {
    case Action::Mode:
        next.battleMode = static_cast<BattleMode>(value);
        break;
    case Action::Auto: return Error::InvalidAction; // Handled above without nested state mutation.
    case Action::Hatch:
        next.collection[0] = freshMember(1, static_cast<Species>(combat::starterSpecies(value)), 0);
        next.collectionCount = next.activeCreatureId = 1;
        next.starterId = value;
        next.onboardingComplete = true;
        next.phase = Phase::Home;
        next.message = Message::Hatched;
        loadActive(next);
        break;
    case Action::Feed:
        if (next.phase != Phase::Home) return Error::WrongPhase;
        next.fullness = cappedAdd(next.fullness, 15, 100);
        next.energy = cappedAdd(next.energy, 3, 100);
        next.mood = cappedAdd(next.mood, 2, 100);
        next.message = Message::Fed;
        addBond(next, 2);
        break;
    case Action::Play:
        if (next.phase != Phase::Home) return Error::WrongPhase;
        if (next.energy < 5) return Error::LowEnergy;
        next.energy -= 5;
        next.mood = cappedAdd(next.mood, 12, 100);
        next.message = Message::Played;
        addBond(next, 5);
        break;
    case Action::Rest:
        if (next.phase != Phase::Home) return Error::WrongPhase;
        next.hp = cappedAdd(next.hp, 25, maxHp(next.collection[next.activeCreatureId - 1].species, next.level));
        next.energy = cappedAdd(next.energy, 25, 100);
        next.message = Message::Rested;
        addBond(next, 1);
        break;
    case Action::Walk:
        next.steps += value;
        next.stepCredit += value;
        next.message = Message::Walked;
        if (next.phase == Phase::Home && next.stepCredit >= 100) {
            next.stepCredit -= 100;
            ++next.encounters;
            next.phase = Phase::Encounter;
            next.wildSpecies = static_cast<Species>(static_cast<unsigned>(Species::Flicker) + (next.encounters - 1) % 3);
            next.wildMaxHp = next.wildHp = maxHp(next.wildSpecies, 1);
            next.message = Message::Encounter;
        }
        break;
    case Action::Card:
        if (next.phase != Phase::Encounter) return Error::WrongPhase;
        if (next.cardUsed) return Error::CardAlreadyUsed;
        next.cardUsed = true;
        if (value == 1) {
            next.attackBoost = 5;
            next.message = Message::AttackCard;
        } else {
            next.shield = 12;
            next.message = Message::ShieldCard;
        }
        break;
    case Action::Attack:
    case Action::Heavy:
    case Action::Magic: {
        if (next.phase != Phase::Encounter) return Error::WrongPhase;
        const auto cost = action == Action::Heavy ? 6u : 2u;
        if (action == Action::Heavy && next.energy < cost) return Error::LowEnergy;
        const auto move = action == Action::Heavy ? combat::Move::Heavy :
                          action == Action::Magic ? combat::Move::Magic : combat::Move::Physical;
        const auto damage = combat::resolve(
            static_cast<std::uint32_t>(next.collection[next.activeCreatureId - 1].species), next.level,
            static_cast<std::uint32_t>(next.wildSpecies), 1, move, combat::Defense::None).damage + next.attackBoost;
        next.attackBoost = 0;
        next.energy = next.energy > cost ? next.energy - cost : 0;
        if (damage >= next.wildHp) {
            home(next);
            next.message = Message::Won;
            addBond(next, 8);
        } else {
            next.wildHp -= damage;
            next.message = Message::Attacked;
            wildResponse(next);
        }
        break;
    }
    case Action::Capture:
        if (next.phase != Phase::Encounter) return Error::WrongPhase;
        if (next.collectionCount >= kCollectionCapacity) return Error::CollectionFull;
        if (next.wildHp > next.wildMaxHp / 2) return Error::WildTooStrong;
        if (next.captureAttempts >= 3) return Error::CaptureLimit;
        ++next.captureAttempts;
        if (random(next) % 100 < 70 + next.level * 5) {
            next.collection[next.collectionCount] = freshMember(next.collectionCount + 1, next.wildSpecies, next.sequence + 1);
            ++next.collectionCount;
            ++next.captures;
            home(next);
            next.message = Message::Captured;
            addBond(next, 12);
        } else {
            next.message = Message::CaptureMissed;
            wildResponse(next);
        }
        break;
    case Action::Select:
        if (next.phase != Phase::Home) return Error::WrongPhase;
        if (value > next.collectionCount) return Error::UnknownMember;
        next.activeCreatureId = value;
        loadActive(next);
        next.message = Message::Selected;
        break;
    default:
        return Error::InvalidAction;
    }
    ++next.sequence;
    storeActive(next);
    if (!isValid(next)) return Error::InvalidState;
    state = next;
    return Error::None;
}

Error applyAuto(State& state, autobattle::Trace* trace) {
    if (trace) { trace->count = 0; trace->outcome = autobattle::Outcome::None; }
    if (!isValid(state)) return Error::InvalidState;
    if (state.phase != Phase::Encounter) return Error::WrongPhase;
    if (state.battleMode != BattleMode::Auto) return Error::WrongMode;
    if (state.sequence == kMax) return Error::CounterOverflow;
    const auto startSequence = state.sequence;
    auto policy = state.seed ^ 0x9e3779b9u ^ (state.encounters * 0x85ebca6bu) ^ startSequence;
    State next = state;
    // Reuse the exact Tactical transitions on a private candidate. Internal
    // turns share the outer event's sequence, including capture timestamps.
    next.battleMode = BattleMode::Tactical;
    if (trace) {
        trace->kind = autobattle::Kind::Wild; trace->combatRulesVersion=3; trace->playerFormId=trace->enemyFormId=0;
        trace->startSequence = startSequence; trace->endSequence = startSequence + 1;
        trace->playerSpecies = static_cast<std::uint32_t>(state.collection[state.activeCreatureId - 1].species);
        trace->playerLevel = state.level;
        trace->enemySpecies = static_cast<std::uint32_t>(state.wildSpecies); trace->enemyLevel = 1;
    }
    for (std::uint32_t turn = 0; turn < autobattle::kMaxTraceSteps; ++turn) {
        Action chosen;
        if (next.collectionCount < kCollectionCapacity && next.wildHp <= next.wildMaxHp / 2 && next.captureAttempts < 3)
            chosen = Action::Capture;
        else {
            constexpr Action moves[]{Action::Attack, Action::Magic, Action::Heavy};
            chosen = moves[autobattle::nextRandom(policy) % (next.energy >= 6 ? 3u : 2u)];
        }
        autobattle::Step frame;
        frame.action = chosen == Action::Capture ? autobattle::Move::Capture : chosen == Action::Heavy ?
            autobattle::Move::Heavy : chosen == Action::Magic ? autobattle::Move::Magic : autobattle::Move::Physical;
        frame.playerHpBefore = next.hp; frame.enemyHpBefore = next.wildHp;
        auto enemyRemaining = next.wildHp;
        if (chosen != Action::Capture) {
            const auto move = chosen == Action::Heavy ? combat::Move::Heavy : chosen == Action::Magic ? combat::Move::Magic : combat::Move::Physical;
            const auto damage = combat::resolve(static_cast<std::uint32_t>(next.collection[next.activeCreatureId - 1].species),
                next.level, static_cast<std::uint32_t>(next.wildSpecies), 1, move, combat::Defense::None).damage;
            enemyRemaining = damage >= enemyRemaining ? 0 : enemyRemaining - damage;
        }
        const auto members = next.collectionCount;
        const auto result = apply(next, chosen);
        if (result != Error::None) { if (trace) trace->count = 0; return result; }
        frame.captured = next.collectionCount > members;
        // Trace combat HP before post-battle care changes. A gentle retreat
        // restores some saved HP, and a reward may train/grow max HP; neither is
        // an extra combat heal. The terminal State contains those durable effects.
        frame.playerHpAfter = next.phase == Phase::Encounter ? next.hp :
            frame.captured || !enemyRemaining ? frame.playerHpBefore : 0;
        frame.enemyHpAfter = next.phase == Phase::Encounter ? next.wildHp : enemyRemaining;
        frame.opponentAction = frame.captured || !enemyRemaining ? autobattle::Move::None : autobattle::Move::Physical;
        if (trace) { trace->steps[turn] = frame; trace->count = turn + 1; }
        if (next.phase == Phase::Home) {
            next.battleMode = BattleMode::Auto;
            next.lastAutoTurns = turn + 1; next.lastAutoSequence = startSequence + 1;
            next.lastAutoOutcome = frame.captured ? autobattle::Outcome::Captured :
                !enemyRemaining ? autobattle::Outcome::Won : autobattle::Outcome::Retreated;
            if (!isValid(next)) { if (trace) trace->count = 0; return Error::InvalidState; }
            if (trace) trace->outcome = next.lastAutoOutcome;
            state = next;
            return Error::None;
        }
        next.sequence = startSequence;
    }
    if (trace) trace->count = 0;
    return Error::AutoLimit;
}

const char* errorText(Error error) {
    switch (error) {
    case Error::None: return "ok";
    case Error::InvalidState: return "invalid state";
    case Error::InvalidAction: return "unknown action";
    case Error::InvalidValue: return "invalid action value";
    case Error::WrongPhase: return "action unavailable in current phase";
    case Error::LowEnergy: return "rest to recover energy for this action";
    case Error::CardAlreadyUsed: return "only one card per encounter";
    case Error::WildTooStrong: return "weaken the wild creature to half health before capture";
    case Error::CaptureLimit: return "three capture attempts already used this encounter";
    case Error::CounterOverflow: return "state counter limit reached";
    case Error::CollectionFull: return "collection is full (8 creatures); no creature was replaced";
    case Error::UnknownMember: return "that creature is not in your collection";
    case Error::AlreadyHatched: return "starter already chosen; no creature was replaced";
    case Error::WrongMode: return "action unavailable in this battle mode";
    case Error::AutoLimit: return "auto battle reached its bounded turn limit; state unchanged";
    }
    return "unknown error";
}
const char* messageText(Message message) {
    switch (message) {
    case Message::Welcome: return "Your adventure begins.";
    case Message::Fed: return "A happy snack.";
    case Message::Played: return "Time together builds your bond.";
    case Message::Rested: return "Rested and ready.";
    case Message::Walked: return "Every step counts.";
    case Message::Encounter: return "A wild creature appeared!";
    case Message::AttackCard: return "Spark card read. Your next attack is stronger.";
    case Message::ShieldCard: return "Shelter card read. A gentle shield surrounds you.";
    case Message::Attacked: return "Your creature landed a skill.";
    case Message::Won: return "A friendly battle won.";
    case Message::Captured: return "A new friend joined your collection!";
    case Message::CaptureMissed: return "The wild creature slipped away from the capture beam.";
    case Message::Retreated: return "A gentle retreat. Rest whenever you are ready.";
    case Message::Evolved: return "Your bond helped your creature evolve!";
    case Message::Selected: return "Your companion is ready.";
    case Message::EggReady: return "Choose an egg to meet your Rookie partner.";
    case Message::Hatched: return "Your Rookie partner has hatched!";
    case Message::Trained: return "Your bond helped your Rookie gain a level!";
    }
    return "Unknown message.";
}
const char* creatureName(const State& state) {
    if (state.activeCreatureId < 1 || state.activeCreatureId > kCollectionCapacity) return "Unknown";
    return memberName(state.collection[state.activeCreatureId - 1]);
}
const char* speciesId(Species species) {
    return combat::speciesName(static_cast<std::uint32_t>(species));
}
const char* memberName(const CreatureMember& member) {
    if (!combat::validProfile(static_cast<std::uint32_t>(member.species), member.level)) return "Unknown";
    return combat::profile(static_cast<std::uint32_t>(member.species), member.level).name;
}
const char* wildName(const State& state) {
    CreatureMember wild;
    wild.species = state.wildSpecies;
    wild.level = 1;
    return memberName(wild);
}
bool parseAction(const char* name, Action& action) {
    if (!name) return false;
    const char* names[] = {"feed", "play", "rest", "walk", "card", "attack", "capture", "select", "heavy", "magic", "hatch", "mode", "auto"};
    if (std::strcmp(name, "physical") == 0) { action = Action::Attack; return true; }
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (std::strcmp(name, names[i]) == 0) {
            action = static_cast<Action>(i);
            return true;
        }
    }
    return false;
}
std::size_t writeJson(const State& s, char* output, std::size_t capacity) {
    if (!output || !capacity) return 0;
    output[0] = '\0';
    if (!isValid(s)) return 0;
    std::size_t used = 0;
    bool ok = true;
    const auto append = [&](const char* format, ...) {
        if (!ok) return;
        va_list args;
        va_start(args, format);
        const int length = std::vsnprintf(output + used, capacity - used, format, args);
        va_end(args);
        if (length < 0 || static_cast<std::size_t>(length) >= capacity - used) { ok = false; return; }
        used += static_cast<std::size_t>(length);
    };
    char message[128];
    if (s.message == Message::Encounter)
        std::snprintf(message, sizeof(message), "A wild %s appeared!", wildName(s));
    else if (s.message == Message::Captured)
        std::snprintf(message, sizeof(message), "%s joined your collection!",
                      s.collectionCount > 1 ? memberName(s.collection[s.collectionCount - 1]) : "Flicker");
    else if (s.message == Message::CaptureMissed)
        std::snprintf(message, sizeof(message), "%s slipped away from the capture beam.", wildName(s));
    else std::snprintf(message, sizeof(message), "%s", messageText(s.message));
    append("{\"schemaVersion\":%u,\"rulesVersion\":%u,\"sequence\":%u,\"seed\":%u,"
           "\"rngState\":%u,\"steps\":%u,\"stepCredit\":%u,\"hp\":%u,\"energy\":%u,"
           "\"fullness\":%u,\"mood\":%u,\"bond\":%u,\"level\":%u,\"captures\":%u,"
           "\"encounters\":%u,\"phase\":\"%s\",\"wildHp\":%u,\"wildMaxHp\":%u,"
           "\"captureAttempts\":%u,\"cardUsed\":%s,\"attackBoost\":%u,\"shield\":%u,",
           static_cast<unsigned>(kSchemaVersion), static_cast<unsigned>(kRulesVersion),
           static_cast<unsigned>(s.sequence), static_cast<unsigned>(s.seed),
           static_cast<unsigned>(s.rngState), static_cast<unsigned>(s.steps),
           static_cast<unsigned>(s.stepCredit), static_cast<unsigned>(s.hp),
           static_cast<unsigned>(s.energy), static_cast<unsigned>(s.fullness),
           static_cast<unsigned>(s.mood), static_cast<unsigned>(s.bond),
           static_cast<unsigned>(s.level), static_cast<unsigned>(s.captures),
           static_cast<unsigned>(s.encounters), s.phase == Phase::Egg ? "egg" : s.phase == Phase::Home ? "home" : "encounter",
           static_cast<unsigned>(s.wildHp), static_cast<unsigned>(s.wildMaxHp),
           static_cast<unsigned>(s.captureAttempts), s.cardUsed ? "true" : "false",
           static_cast<unsigned>(s.attackBoost), static_cast<unsigned>(s.shield));
    const auto activeSpecies = s.onboardingComplete ? s.collection[s.activeCreatureId - 1].species : Species::None;
    if (s.onboardingComplete)
        append("\"creature\":\"%s\",\"message\":\"%s\",\"species\":\"%s\",", creatureName(s), message, speciesId(activeSpecies));
    else append("\"creature\":null,\"message\":\"%s\",\"species\":null,", message);
    append("\"activeCreatureId\":%u,\"collectionCapacity\":%u,\"legacyCaptures\":%u",
           static_cast<unsigned>(s.activeCreatureId), static_cast<unsigned>(kCollectionCapacity),
           static_cast<unsigned>(s.legacyCaptures));
    append(",\"onboarding\":{\"completed\":%s,\"starterId\":", s.onboardingComplete ? "true" : "false");
    if (s.starterId) append("%u}", static_cast<unsigned>(s.starterId));
    else append("null}");
    append(",\"battleMode\":\"%s\",\"lastAutoBattle\":", s.battleMode == BattleMode::Auto ? "auto" : "tactical");
    if (s.lastAutoOutcome == autobattle::Outcome::None) append("null");
    else append("{\"sequence\":%u,\"turns\":%u,\"outcome\":\"%s\"}", static_cast<unsigned>(s.lastAutoSequence),
                static_cast<unsigned>(s.lastAutoTurns), autobattle::outcomeName(s.lastAutoOutcome));
    append(",\"stage\":%s", combat::stageName(static_cast<std::uint32_t>(activeSpecies)) ? "\"Rookie\"" : "null");
    char combatJson[combat::kProfileJsonCapacity]{};
    if (s.onboardingComplete) {
        if (!combat::writeProfileJson(static_cast<std::uint32_t>(activeSpecies), s.level, combatJson, sizeof(combatJson))) ok = false;
        append(",\"combat\":%s", combatJson);
    } else append(",\"combat\":null");
    if (s.phase == Phase::Encounter) {
        if (!combat::writeProfileJson(static_cast<std::uint32_t>(s.wildSpecies), 1,
                                     combatJson, sizeof(combatJson))) ok = false;
        append(",\"wildSpecies\":\"%s\",\"wildName\":\"%s\",\"wildCombat\":%s",
               speciesId(s.wildSpecies), wildName(s), combatJson);
    } else append(",\"wildSpecies\":null,\"wildName\":null,\"wildCombat\":null");
    append(",\"collection\":[");
    for (std::size_t i = 0; i < s.collectionCount; ++i) {
        const auto& m = s.collection[i];
        append("%s{\"id\":%u,\"species\":\"%s\",\"name\":\"%s\",\"hp\":%u,\"energy\":%u,"
               "\"fullness\":%u,\"mood\":%u,\"bond\":%u,\"level\":%u,\"capturedAtSequence\":%u",
               i ? "," : "", static_cast<unsigned>(m.id), speciesId(m.species), memberName(m),
               static_cast<unsigned>(m.hp), static_cast<unsigned>(m.energy),
               static_cast<unsigned>(m.fullness), static_cast<unsigned>(m.mood),
               static_cast<unsigned>(m.bond), static_cast<unsigned>(m.level),
               static_cast<unsigned>(m.capturedAtSequence));
        if (!combat::writeProfileJson(static_cast<std::uint32_t>(m.species), m.level,
                                     combatJson, sizeof(combatJson))) ok = false;
        append(",\"stage\":%s,\"combat\":%s}",
               combat::stageName(static_cast<std::uint32_t>(m.species)) ? "\"Rookie\"" : "null", combatJson);
    }
    append("]}");
    if (!ok) { output[0] = '\0'; return 0; }
    return used;
}

bool encodeSnapshot(const State& s, Snapshot& snapshot) {
    static_assert(kSnapshotSize == 8 + (22 + 4 + kCollectionCapacity * 9 + 6) * 4 + 4);
    if (!isValid(s)) return false;
    Snapshot next;
    auto* bytes = next.bytes;
    std::memcpy(bytes, "DGVS", 4);
    bytes[4] = 6;
    constexpr auto payload = kSnapshotSize - 12;
    bytes[6] = static_cast<std::uint8_t>(payload);
    bytes[7] = static_cast<std::uint8_t>(payload >> 8);
    const std::uint32_t fields[] = {
        kRulesVersion, s.sequence, s.seed, s.rngState, s.steps, s.stepCredit,
        s.hp, s.energy, s.fullness, s.mood, s.bond, s.level, s.captures, s.encounters,
        s.wildHp, s.wildMaxHp, s.captureAttempts, static_cast<std::uint32_t>(s.phase),
        s.cardUsed ? 1u : 0u, s.attackBoost, s.shield, static_cast<std::uint32_t>(s.message),
        s.legacyCaptures, s.activeCreatureId, s.collectionCount, static_cast<std::uint32_t>(s.wildSpecies)
    };
    std::size_t offset = 8;
    for (const auto value : fields) { put32(bytes + offset, value); offset += 4; }
    for (const auto& member : s.collection) {
        const std::uint32_t values[] = {member.id, static_cast<std::uint32_t>(member.species),
            member.hp, member.energy, member.fullness, member.mood, member.bond, member.level, member.capturedAtSequence};
        for (const auto value : values) { put32(bytes + offset, value); offset += 4; }
    }
    put32(bytes + offset, s.onboardingComplete ? 1u : 0u);
    put32(bytes + offset + 4, s.starterId);
    put32(bytes + offset + 8, static_cast<std::uint32_t>(s.battleMode));
    put32(bytes + offset + 12, static_cast<std::uint32_t>(s.lastAutoOutcome));
    put32(bytes + offset + 16, s.lastAutoTurns);
    put32(bytes + offset + 20, s.lastAutoSequence);
    put32(bytes + kSnapshotSize - 4, crc32(bytes, kSnapshotSize - 4));
    snapshot = next;
    return true;
}

SnapshotStatus decodeSnapshot(const std::uint8_t* bytes, std::size_t length, State& state) {
    if (!bytes || length < 8) return SnapshotStatus::InvalidLength;
    if (std::memcmp(bytes, "DGVS", 4) != 0) return SnapshotStatus::BadMagic;
    const auto version = static_cast<unsigned>(bytes[4]) | (static_cast<unsigned>(bytes[5]) << 8);
    if (version < 1 || version > 6) return SnapshotStatus::UnsupportedVersion;
    const auto required = version == 1 ? kLegacySnapshotSize : version == 2 ? kV2SnapshotSize :
                          version < 5 ? kPreviousSnapshotSize : version == 5 ? kV5SnapshotSize : kSnapshotSize;
    const auto payload = static_cast<unsigned>(bytes[6]) | (static_cast<unsigned>(bytes[7]) << 8);
    if (length != required || payload != length - 12) return SnapshotStatus::InvalidLength;
    if (get32(bytes + length - 4) != crc32(bytes, length - 4)) return SnapshotStatus::BadChecksum;
    if (get32(bytes + 8) != (version < 3 ? 1u : version == 3 ? 2u : kRulesVersion)) return SnapshotStatus::UnsupportedRules;
    std::size_t offset = 12;
    const auto read = [&]() { const auto value = get32(bytes + offset); offset += 4; return value; };
    State next;
    next.sequence = read(); next.seed = read(); next.rngState = read();
    next.steps = read(); next.stepCredit = read(); next.hp = read();
    next.energy = read(); next.fullness = read(); next.mood = read();
    next.bond = read(); next.level = read(); next.captures = read();
    next.encounters = read(); next.wildHp = read(); next.wildMaxHp = read();
    next.captureAttempts = read();
    const auto phase = read();
    const auto card = read();
    next.attackBoost = read();
    next.shield = version == 1 ? 0 : read();
    const auto message = read();
    const auto lastMessage = version < 3 ? Message::Evolved : version < 5 ? Message::Selected : Message::Trained;
    if (phase > (version < 5 ? 1u : 2u) || card > 1 || message > static_cast<unsigned>(lastMessage))
        return SnapshotStatus::InvalidState;
    next.phase = static_cast<Phase>(phase);
    next.cardUsed = card != 0;
    next.message = static_cast<Message>(message);
    if (version < 3) {
        next.legacyCaptures = next.captures;
        next.collection[0] = freshMember(1, Species::Mote, 0);
        next.wildSpecies = next.phase == Phase::Encounter ? Species::Flicker : Species::None;
        storeActive(next);
    } else {
        next.legacyCaptures = read(); next.activeCreatureId = read(); next.collectionCount = read();
        const auto wild = read();
        if (wild > static_cast<unsigned>(Species::Cinder)) return SnapshotStatus::InvalidState;
        next.wildSpecies = static_cast<Species>(wild);
        for (auto& member : next.collection) {
            member.id = read();
            const auto species = read();
            if (species > static_cast<unsigned>(version < 5 ? Species::Cinder : Species::Renamon)) return SnapshotStatus::InvalidState;
            member.species = static_cast<Species>(species);
            member.hp = read(); member.energy = read(); member.fullness = read();
            member.mood = read(); member.bond = read(); member.level = read(); member.capturedAtSequence = read();
        }
    }
    if (version >= 5) {
        const auto completed = read();
        if (completed > 1) return SnapshotStatus::InvalidState;
        next.onboardingComplete = completed != 0;
        next.starterId = read();
    }
    if (version >= 6) {
        const auto mode = read(), outcome = read();
        if (mode > 1 || outcome > 3) return SnapshotStatus::InvalidState;
        next.battleMode = static_cast<BattleMode>(mode);
        next.lastAutoOutcome = static_cast<autobattle::Outcome>(outcome);
        next.lastAutoTurns = read(); next.lastAutoSequence = read();
    }
    if (!validForVersion(next, version < 4)) return SnapshotStatus::InvalidState;
    if (version < 4) {
        for (std::size_t i = 0; i < next.collectionCount; ++i) {
            auto& member = next.collection[i];
            member.hp = scaleHp(member.hp, 100, maxHp(member.species, member.level));
        }
        loadActive(next);
        if (next.phase == Phase::Encounter) {
            const auto maximum = maxHp(next.wildSpecies, 1);
            next.wildHp = scaleHp(next.wildHp, next.wildMaxHp, maximum);
            next.wildMaxHp = maximum;
        }
        if (!isValid(next)) return SnapshotStatus::InvalidState;
    }
    state = next;
    return version < 6 ? SnapshotStatus::Migrated : SnapshotStatus::Ok;
}
const char* snapshotStatusText(SnapshotStatus status) {
    switch (status) {
    case SnapshotStatus::Ok: return "ok";
    case SnapshotStatus::Migrated: return "migrated legacy snapshot to version 6";
    case SnapshotStatus::InvalidLength: return "invalid snapshot length";
    case SnapshotStatus::BadMagic: return "invalid snapshot magic";
    case SnapshotStatus::UnsupportedVersion: return "unsupported snapshot version";
    case SnapshotStatus::UnsupportedRules: return "unsupported rules version";
    case SnapshotStatus::BadChecksum: return "snapshot checksum mismatch";
    case SnapshotStatus::InvalidState: return "snapshot contains invalid game state";
    }
    return "unknown snapshot status";
}
} // namespace digivice::legacy_v3
