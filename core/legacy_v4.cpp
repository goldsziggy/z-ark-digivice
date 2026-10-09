// Frozen rules-4 game from 916c0d8. Host historical replay only.
#include "legacy_v4.hpp"
#include "legacy_combat_v5.hpp"
#include "legacy_forms_v5.hpp"
#include "legacy_combat_v3.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>

namespace digivice::legacy_v4 {
namespace combat = digivice::legacy_v5::combat;
namespace forms = digivice::legacy_v5::forms;
namespace {
constexpr std::uint32_t kMax = std::numeric_limits<std::uint32_t>::max();
std::uint32_t cappedAdd(std::uint32_t current, std::uint32_t amount, std::uint32_t cap) {
    return amount >= cap - current ? cap : current + amount;
}
std::uint32_t legacyLevelFor(Species species, std::uint32_t bond) {
    return species == Species::Flicker ? 1 : bond >= 100 ? 3 : bond >= 40 ? 2 : 1;
}
std::uint32_t legacyMaxHp(Species species,std::uint32_t level) {
    return legacy_v3::combat::profile(static_cast<std::uint32_t>(species),level).stats.maxHp;
}
std::uint32_t maxHp(std::uint32_t formId, std::uint32_t level) {
    return combat::formProfile(formId, level).stats.maxHp;
}
std::uint32_t rootForm(Species species) { return forms::initialForm(static_cast<std::uint32_t>(species)); }
std::uint32_t scaleHp(std::uint32_t hp, std::uint32_t oldMax, std::uint32_t newMax) {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(hp) * newMax + oldMax - 1) / oldMax);
}
CreatureMember freshMember(std::uint32_t id, Species species, std::uint32_t sequence) {
    return {id, species, maxHp(rootForm(species), 1), 80, 70, 80, 0, 1, sequence, 0, rootForm(species)};
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
void addBond(State& state, std::uint32_t amount) { state.bond = cappedAdd(state.bond, amount, 200); }
void addXp(State& state,std::uint32_t amount) {
    auto& member=state.collection[state.activeCreatureId-1];
    const auto oldMax=maxHp(member.formId,state.level);
    member.xp=cappedAdd(member.xp,amount,kMaxXp);
    const auto level=levelForXp(member.xp);
    if(level>state.level) {
        state.hp=scaleHp(state.hp,oldMax,maxHp(member.formId,level));
        state.level=level; state.message=Message::Trained;
    }
}
void home(State& state) {
    state.phase = Phase::Home;
    state.wildHp = state.wildMaxHp = state.captureAttempts = 0;
    state.attackBoost = state.shield = 0;
    state.cardUsed = false;
    state.wildSpecies = Species::None;
    state.wildLevel=state.wildTurn=0;
}
void wildResponse(State& state) {
    const auto activeForm = state.collection[state.activeCreatureId - 1].formId;
    const auto move=state.wildTurn % 2 ? combat::Move::Magic : combat::Move::Physical;
    const auto damage = combat::resolveForms(rootForm(state.wildSpecies),state.wildLevel,
        activeForm,state.level,move,combat::Defense::None).damage;
    ++state.wildTurn;
    const auto blocked = state.shield < damage ? state.shield : damage;
    state.shield -= blocked;
    const auto received = damage - blocked;
    if (received >= state.hp) {
        home(state);
        state.hp = (maxHp(activeForm, state.level) + 9) / 10;
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

std::uint32_t xpForLevel(std::uint32_t level) { return level>=1 && level<=kMaxLevel ? 20*(level-1)*level : kMaxXp; }
std::uint32_t levelForXp(std::uint32_t xp) {
    std::uint32_t level=1; while(level<kMaxLevel && xp>=xpForLevel(level+1)) ++level; return level;
}
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
           !m.mood && !m.bond && !m.level && !m.capturedAtSequence && !m.xp && !m.formId;
}

static bool validLegacyForVersion(const State& state, bool legacy) {
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
        if (member.id != i + 1 || !legacy_v3::combat::validProfile(static_cast<std::uint32_t>(member.species), member.level) ||
            member.bond > 200 || member.level != legacyLevelFor(member.species, member.bond) ||
            member.hp < 1 || member.hp > (legacy ? 100 : legacyMaxHp(member.species, member.level)) || member.energy > 100 || member.fullness > 100 ||
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
        state.wildMaxHp != legacyMaxHp(state.wildSpecies, 1)) || state.wildHp < 1 || state.wildHp > state.wildMaxHp ||
        state.captureAttempts > 3 || (state.attackBoost != 0 && state.attackBoost != 5) ||
        state.shield > 12 || (state.attackBoost && state.shield) ||
        (!state.cardUsed && (state.attackBoost || state.shield))) return false;
    // Auto has no partially resolved durable state. It waits untouched for an
    // explicit Auto event, then commits only the terminal result.
    if (state.battleMode == BattleMode::Auto && (state.wildHp != state.wildMaxHp ||
        state.captureAttempts || state.cardUsed || state.attackBoost || state.shield)) return false;
    return true;
}

static bool validForVersion(const State& state, bool /* legacy */) {
    if (static_cast<unsigned>(state.battleMode) > 1 || static_cast<unsigned>(state.lastAutoOutcome) > 3) return false;
    if (state.lastAutoOutcome == autobattle::Outcome::None) {
        if (state.lastAutoTurns || state.lastAutoSequence) return false;
    } else if (!state.lastAutoTurns || state.lastAutoTurns > autobattle::kMaxTraceSteps ||
               !state.lastAutoSequence || state.lastAutoSequence > state.sequence ||
               (state.lastAutoOutcome == autobattle::Outcome::Captured && !state.captures)) return false;
    if (!state.onboardingComplete) {
        if (state.phase != Phase::Egg || state.starterId || state.sequence ||
            state.rngState != (state.seed ? state.seed : 0x6d2b79f5u) ||
            state.steps || state.stepCredit || state.hp || state.energy || state.fullness ||
            state.mood || state.bond || state.level || state.captures || state.encounters ||
            state.wildHp || state.wildMaxHp || state.captureAttempts || state.wildTurn || state.cardUsed ||
            state.attackBoost || state.shield || state.legacyCaptures || state.activeCreatureId ||
            state.collectionCount || state.wildSpecies != Species::None || state.wildLevel || state.wildTurn || state.message != Message::EggReady ||
            state.battleMode != BattleMode::Tactical || state.lastAutoOutcome != autobattle::Outcome::None)
            return false;
        for (const auto& m : state.collection) if (!emptyMember(m)) return false;
        return true;
    }
    if (state.starterId > combat::kStarterCount || (state.starterId && (!state.sequence || state.legacyCaptures)) ||
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
        if (member.id != i + 1 || member.formId > 66 || !forms::validForLineage(member.formId,static_cast<std::uint32_t>(member.species)) || !combat::validFormProfile(member.formId,member.level) ||
            member.bond > 200 || member.bond < forms::find(member.formId)->minBond ||
            member.xp > kMaxXp || member.level != levelForXp(member.xp) ||
            member.hp < 1 || member.hp > maxHp(member.formId, member.level) || member.energy > 100 || member.fullness > 100 ||
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
               !state.cardUsed && state.attackBoost == 0 && state.shield == 0 && state.wildSpecies == Species::None && !state.wildLevel && !state.wildTurn;
    }
    if (state.phase != Phase::Encounter || !state.encounters ||
        state.wildSpecies < Species::Flicker || state.wildSpecies > Species::Cinder || (state.wildLevel<1 || state.wildLevel>kMaxLevel || state.wildTurn>1000 || state.wildMaxHp != maxHp(rootForm(state.wildSpecies), state.wildLevel)) || state.wildHp < 1 || state.wildHp > state.wildMaxHp ||
        state.captureAttempts > 3 || (state.attackBoost != 0 && state.attackBoost != 5) ||
        state.shield > 12 || (state.attackBoost && state.shield) ||
        (!state.cardUsed && (state.attackBoost || state.shield))) return false;
    // Auto has no partially resolved durable state. It waits untouched for an
    // explicit Auto event, then commits only the terminal result.
    if (state.battleMode == BattleMode::Auto && (state.wildHp != state.wildMaxHp ||
        state.captureAttempts || state.wildTurn || state.cardUsed || state.attackBoost || state.shield)) return false;
    return true;
}

bool isValid(const State& state) { return validForVersion(state, false); }

Error apply(State& state, Action action, std::uint32_t value) {
    if (!isValid(state)) return Error::InvalidState;
    if (static_cast<unsigned>(action) > static_cast<unsigned>(Action::Evolve)) return Error::InvalidAction;
    if (state.sequence == kMax) return Error::CounterOverflow;
    if (action == Action::Hatch) {
        if (!combat::starterSpecies(value)) return Error::InvalidValue;
        if (state.onboardingComplete) return Error::AlreadyHatched;
    } else if (!state.onboardingComplete) {
        return Error::WrongPhase;
    } else if (action == Action::Evolve) {
        if (value > 66 || !forms::find(value)) return Error::InvalidValue;
        if (state.phase != Phase::Home) return Error::WrongPhase;
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
    case Action::Evolve: {
        auto& member=next.collection[next.activeCreatureId-1];
        const auto* target=forms::find(value); const auto* current=forms::find(member.formId);
        if (!target || !current || (current->children[0]!=value && current->children[1]!=value) ||
            target->lineage!=static_cast<unsigned>(member.species) || next.level<target->minLevel || next.bond<target->minBond)
            return Error::EvolutionUnavailable;
        const auto oldMax=maxHp(member.formId,next.level); member.formId=value;
        next.hp=scaleHp(next.hp,oldMax,maxHp(value,next.level)); next.message=Message::Evolved; break;
    }
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
    case Action::Feed: {
        if (next.phase != Phase::Home) return Error::WrongPhase;
        const bool useful=next.fullness<100;
        next.fullness = cappedAdd(next.fullness, 15, 100);
        next.energy = cappedAdd(next.energy, 3, 100);
        next.mood = cappedAdd(next.mood, 2, 100);
        next.message = Message::Fed;
        if(useful) addBond(next, 2);
        break; }
    case Action::Play: {
        if (next.phase != Phase::Home) return Error::WrongPhase;
        if (next.energy < 5) return Error::LowEnergy;
        const bool useful=next.mood<100;
        next.energy -= 5;
        next.mood = cappedAdd(next.mood, 12, 100);
        next.message = Message::Played;
        if(useful) addBond(next, 5);
        break; }
    case Action::Rest: {
        if (next.phase != Phase::Home) return Error::WrongPhase;
        const auto maximum=maxHp(next.collection[next.activeCreatureId - 1].formId,next.level);
        const bool useful=next.hp<maximum || next.energy<100;
        next.hp = cappedAdd(next.hp, 25, maximum);
        next.energy = cappedAdd(next.energy, 25, 100);
        next.message = Message::Rested;
        if(useful) addBond(next, 1);
        break; }
    case Action::Walk:
        next.steps += value;
        next.stepCredit += value;
        next.message = Message::Walked;
        if (next.phase == Phase::Home && next.stepCredit >= 100) {
            next.stepCredit -= 100;
            ++next.encounters;
            next.phase = Phase::Encounter;
            next.wildSpecies = static_cast<Species>(static_cast<unsigned>(Species::Flicker) + (next.encounters - 1) % 3);
            next.wildLevel=next.level; next.wildTurn=0;
            next.wildMaxHp = next.wildHp = maxHp(rootForm(next.wildSpecies), next.wildLevel);
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
        const auto damage = combat::resolveForms(
            next.collection[next.activeCreatureId - 1].formId,next.level,
            rootForm(next.wildSpecies),next.wildLevel,move,combat::Defense::None).damage + next.attackBoost;
        next.attackBoost = 0;
        next.energy = next.energy > cost ? next.energy - cost : 0;
        if (damage >= next.wildHp) {
            const auto xpReward=20+6*next.wildLevel;
            home(next);
            next.message = Message::Won;
            addBond(next, 8); addXp(next,xpReward);
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
        if (random(next) % 100 < 70 + (next.level < 3 ? next.level : 3) * 5) {
            auto captured=freshMember(next.collectionCount+1,next.wildSpecies,next.sequence+1);
            captured.level=next.wildLevel; captured.xp=xpForLevel(captured.level);
            captured.hp=maxHp(captured.formId,captured.level);
            const auto xpReward=20+6*next.wildLevel;
            next.collection[next.collectionCount]=captured;
            ++next.collectionCount;
            ++next.captures;
            home(next);
            next.message = Message::Captured;
            addBond(next, 12); addXp(next,xpReward);
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
        trace->kind = autobattle::Kind::Wild;
        trace->startSequence = startSequence; trace->endSequence = startSequence + 1;
        trace->playerSpecies = static_cast<std::uint32_t>(state.collection[state.activeCreatureId - 1].species);
        trace->playerLevel = state.level; trace->combatRulesVersion=5;
        trace->playerFormId=state.collection[state.activeCreatureId-1].formId;
        trace->enemyFormId=rootForm(state.wildSpecies);
        trace->enemySpecies = static_cast<std::uint32_t>(state.wildSpecies); trace->enemyLevel = state.wildLevel;
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
            const auto damage = combat::resolveForms(next.collection[next.activeCreatureId - 1].formId,
                next.level,rootForm(next.wildSpecies),next.wildLevel,move,combat::Defense::None).damage;
            enemyRemaining = damage >= enemyRemaining ? 0 : enemyRemaining - damage;
        }
        const auto opponentMove=next.wildTurn%2 ? autobattle::Move::Magic : autobattle::Move::Physical;
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
        frame.opponentAction = frame.captured || !enemyRemaining ? autobattle::Move::None : opponentMove;
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
    // A bounded unresolved fight is a terminal gentle retreat, never a retry loop.
    home(next); next.message=Message::Retreated;
    next.hp=(maxHp(next.collection[next.activeCreatureId-1].formId,next.level)+9)/10;
    next.sequence=startSequence+1; next.battleMode=BattleMode::Auto;
    next.lastAutoTurns=autobattle::kMaxTraceSteps; next.lastAutoSequence=next.sequence;
    next.lastAutoOutcome=autobattle::Outcome::Retreated; storeActive(next);
    if(!isValid(next)) return Error::InvalidState;
    if(trace) trace->outcome=next.lastAutoOutcome;
    state=next; return Error::None;
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
    case Error::EvolutionUnavailable: return "evolution requires a legal next form and its level/bond thresholds";
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
    case Message::Trained: return "Your companion gained a level from battle experience!";
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
    return combat::formProfile(member.formId,member.level).name;
}
const char* wildName(const State& state) {
    CreatureMember wild;
    wild.species = state.wildSpecies;
    wild.level = state.wildLevel;
    wild.formId=rootForm(state.wildSpecies);
    return memberName(wild);
}
bool parseAction(const char* name, Action& action) {
    if (!name) return false;
    const char* names[] = {"feed", "play", "rest", "walk", "card", "attack", "capture", "select", "heavy", "magic", "hatch", "mode", "auto", "evolve"};
    if (std::strcmp(name, "physical") == 0) { action = Action::Attack; return true; }
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (std::strcmp(name, names[i]) == 0) {
            action = static_cast<Action>(i);
            return true;
        }
    }
    return false;
}
std::size_t writeJson(const State& s,char* output,std::size_t capacity) {
    if(!output || !capacity) return 0;
    output[0]='\0'; if(!isValid(s)) return 0;
    std::size_t used=0; bool ok=true;
    const auto append=[&](const char* format,...) {
        if(!ok) return;
        va_list args; va_start(args,format);
        const auto n=std::vsnprintf(output+used,capacity-used,format,args); va_end(args);
        if(n<0 || static_cast<std::size_t>(n)>=capacity-used){ok=false;return;}
        used+=static_cast<std::size_t>(n);
    };
    const auto number=[&](std::uint32_t value){return static_cast<unsigned>(value);};
    const auto metadata=[&](std::uint32_t formId) {
        const auto* f=forms::find(formId);
        append("\"stage\":");
        if(!f || f->stage==forms::Stage::Original) append("null"); else append("\"%s\"",forms::stageName(f->stage));
        append(",\"artId\":"); if(f && f->artId) append("\"%s\"",f->artId); else append("null");
    };
    const auto profile=[&](std::uint32_t formId,std::uint32_t level) {
        char json[combat::kProfileJsonCapacity];
        if(!combat::writeFormProfileJson(formId,level,json,sizeof(json))){ok=false;return;}
        append("\"combat\":%s",json);
    };
    const auto progress=[&](const CreatureMember& m) {
        append("\"formId\":%u,\"xp\":%u,\"xpToNext\":%u,",number(m.formId),number(m.xp),number(m.level<kMaxLevel ? xpForLevel(m.level+1)-m.xp : 0));
    };
    char message[128];
    if(s.message==Message::Encounter) std::snprintf(message,sizeof(message),"A wild %s appeared!",wildName(s));
    else if(s.message==Message::Captured) std::snprintf(message,sizeof(message),"%s joined your collection!",memberName(s.collection[s.collectionCount-1]));
    else if(s.message==Message::CaptureMissed) std::snprintf(message,sizeof(message),"%s slipped away from the capture beam.",wildName(s));
    else std::snprintf(message,sizeof(message),"%s",messageText(s.message));
    append("{\"schemaVersion\":%u,\"rulesVersion\":%u,\"sequence\":%u,\"seed\":%u,\"rngState\":%u,\"steps\":%u,\"stepCredit\":%u,"
           "\"hp\":%u,\"energy\":%u,\"fullness\":%u,\"mood\":%u,\"bond\":%u,\"level\":%u,\"maxLevel\":20,\"captures\":%u,\"encounters\":%u,"
           "\"phase\":\"%s\",\"wildHp\":%u,\"wildMaxHp\":%u,\"wildLevel\":%u,\"wildTurn\":%u,\"captureAttempts\":%u,\"cardUsed\":%s,\"attackBoost\":%u,\"shield\":%u,",
           number(kSchemaVersion),number(kRulesVersion),number(s.sequence),number(s.seed),number(s.rngState),number(s.steps),number(s.stepCredit),
           number(s.hp),number(s.energy),number(s.fullness),number(s.mood),number(s.bond),number(s.level),number(s.captures),number(s.encounters),
           s.phase==Phase::Egg ? "egg" : s.phase==Phase::Home ? "home" : "encounter",number(s.wildHp),number(s.wildMaxHp),number(s.wildLevel),number(s.wildTurn),
           number(s.captureAttempts),s.cardUsed ? "true" : "false",number(s.attackBoost),number(s.shield));
    if(s.onboardingComplete) {
        const auto& m=s.collection[s.activeCreatureId-1];
        append("\"creature\":\"%s\",\"species\":\"%s\",",memberName(m),speciesId(m.species));
        progress(m); metadata(m.formId); append(","); profile(m.formId,m.level);
    } else append("\"creature\":null,\"species\":null,\"formId\":0,\"xp\":0,\"xpToNext\":0,\"stage\":null,\"artId\":null,\"combat\":null");
    append(",\"message\":\"%s\",\"activeCreatureId\":%u,\"collectionCapacity\":8,\"legacyCaptures\":%u,\"onboarding\":{\"completed\":%s,\"starterId\":",
           message,number(s.activeCreatureId),number(s.legacyCaptures),s.onboardingComplete ? "true" : "false");
    if(s.starterId) append("%u}",number(s.starterId)); else append("null}");
    append(",\"battleMode\":\"%s\",\"lastAutoBattle\":",s.battleMode==BattleMode::Auto ? "auto" : "tactical");
    if(s.lastAutoOutcome==autobattle::Outcome::None) append("null");
    else append("{\"sequence\":%u,\"turns\":%u,\"outcome\":\"%s\"}",number(s.lastAutoSequence),number(s.lastAutoTurns),autobattle::outcomeName(s.lastAutoOutcome));
    if(s.phase==Phase::Encounter) {
        char json[combat::kProfileJsonCapacity];
        if(!combat::writeFormProfileJson(rootForm(s.wildSpecies),s.wildLevel,json,sizeof(json))) ok=false;
        append(",\"wildSpecies\":\"%s\",\"wildName\":\"%s\",\"wildCombat\":%s",speciesId(s.wildSpecies),wildName(s),json);
    } else append(",\"wildSpecies\":null,\"wildName\":null,\"wildCombat\":null");
    append(",\"collection\":[");
    for(std::size_t i=0;i<s.collectionCount;++i) {
        const auto& m=s.collection[i];
        append("%s{\"id\":%u,\"species\":\"%s\",\"name\":\"%s\",\"hp\":%u,\"energy\":%u,\"fullness\":%u,\"mood\":%u,\"bond\":%u,\"level\":%u,\"capturedAtSequence\":%u,",
               i ? "," : "",number(m.id),speciesId(m.species),memberName(m),number(m.hp),number(m.energy),number(m.fullness),number(m.mood),number(m.bond),number(m.level),number(m.capturedAtSequence));
        progress(m); metadata(m.formId); append(","); profile(m.formId,m.level); append("}");
    }
    append("],\"evolution\":{\"options\":[");
    if(s.onboardingComplete) {
        const auto& m=s.collection[s.activeCreatureId-1]; const auto* current=forms::find(m.formId); bool comma=false;
        for(const auto id:current->children) if(id) {
            const auto* next=forms::find(id); const auto preview=m.level<next->minLevel ? next->minLevel : m.level;
            const auto eligible=s.phase==Phase::Home && m.level>=next->minLevel && m.bond>=next->minBond;
            append("%s{\"formId\":%u,\"name\":\"%s\",",comma ? "," : "",number(id),next->name); metadata(id);
            append(",\"requiredLevel\":%u,\"requiredBond\":%u,\"previewLevel\":%u,\"eligible\":%s,",number(next->minLevel),number(next->minBond),number(preview),eligible ? "true" : "false");
            profile(id,preview); append("}"); comma=true;
        }
    }
    append("]}}"); if(!ok){output[0]='\0';return 0;} return used;
}

bool encodeSnapshot(const State& s, Snapshot& snapshot) {
    static_assert(kSnapshotSize == 8 + (22 + 4 + kCollectionCapacity * 11 + 8) * 4 + 4);
    if (!isValid(s)) return false;
    Snapshot next;
    auto* bytes = next.bytes;
    std::memcpy(bytes, "DGVS", 4);
    bytes[4] = 7;
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
            member.hp, member.energy, member.fullness, member.mood, member.bond, member.level, member.capturedAtSequence, member.xp, member.formId};
        for (const auto value : values) { put32(bytes + offset, value); offset += 4; }
    }
    put32(bytes + offset, s.onboardingComplete ? 1u : 0u);
    put32(bytes + offset + 4, s.starterId);
    put32(bytes + offset + 8, static_cast<std::uint32_t>(s.battleMode));
    put32(bytes + offset + 12, static_cast<std::uint32_t>(s.lastAutoOutcome));
    put32(bytes + offset + 16, s.lastAutoTurns);
    put32(bytes + offset + 20, s.lastAutoSequence);
    put32(bytes + offset + 24,s.wildLevel); put32(bytes + offset + 28,s.wildTurn);
    put32(bytes + kSnapshotSize - 4, crc32(bytes, kSnapshotSize - 4));
    snapshot = next;
    return true;
}

SnapshotStatus decodeSnapshot(const std::uint8_t* bytes, std::size_t length, State& state) {
    if (!bytes || length < 8) return SnapshotStatus::InvalidLength;
    if (std::memcmp(bytes, "DGVS", 4) != 0) return SnapshotStatus::BadMagic;
    const auto version = static_cast<unsigned>(bytes[4]) | (static_cast<unsigned>(bytes[5]) << 8);
    if (version < 1 || version > 7) return SnapshotStatus::UnsupportedVersion;
    const auto required = version == 1 ? kLegacySnapshotSize : version == 2 ? kV2SnapshotSize :
                          version < 5 ? kPreviousSnapshotSize : version == 5 ? kV5SnapshotSize : version == 6 ? kV6SnapshotSize : kSnapshotSize;
    const auto payload = static_cast<unsigned>(bytes[6]) | (static_cast<unsigned>(bytes[7]) << 8);
    if (length != required || payload != length - 12) return SnapshotStatus::InvalidLength;
    if (get32(bytes + length - 4) != crc32(bytes, length - 4)) return SnapshotStatus::BadChecksum;
    if (get32(bytes + 8) != (version < 3 ? 1u : version == 3 ? 2u : version < 7 ? 3u : kRulesVersion)) return SnapshotStatus::UnsupportedRules;
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
            if(version>=7){member.xp=read();member.formId=read();}
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
    if(version>=7) {next.wildLevel=read();next.wildTurn=read();}
    if(version<7) {
        if(!validLegacyForVersion(next,version<4)) return SnapshotStatus::InvalidState;
        for(std::size_t i=0;i<next.collectionCount;++i) {
            auto& m=next.collection[i]; const auto oldTier=m.level;
            const auto oldMax=version<4 ? 100u : legacyMaxHp(m.species,oldTier);
            m.formId=forms::migrateLegacyForm(static_cast<std::uint32_t>(m.species),oldTier);
            m.level=oldTier==1 ? 1 : oldTier==2 ? 5 : 10; m.xp=xpForLevel(m.level);
            m.hp=scaleHp(m.hp,oldMax,maxHp(m.formId,m.level));
        }
        if(next.onboardingComplete) loadActive(next);
        if(next.phase==Phase::Encounter) {
            next.wildLevel=1; next.wildTurn=0;
            const auto maximum=maxHp(rootForm(next.wildSpecies),1);
            next.wildHp=scaleHp(next.wildHp,next.wildMaxHp,maximum); next.wildMaxHp=maximum;
        }
    }
    if(!isValid(next)) return SnapshotStatus::InvalidState;
    state = next;
    return version < 7 ? SnapshotStatus::Migrated : SnapshotStatus::Ok;
}
const char* snapshotStatusText(SnapshotStatus status) {
    switch (status) {
    case SnapshotStatus::Ok: return "ok";
    case SnapshotStatus::Migrated: return "migrated legacy snapshot to version 7";
    case SnapshotStatus::InvalidLength: return "invalid snapshot length";
    case SnapshotStatus::BadMagic: return "invalid snapshot magic";
    case SnapshotStatus::UnsupportedVersion: return "unsupported snapshot version";
    case SnapshotStatus::UnsupportedRules: return "unsupported rules version";
    case SnapshotStatus::BadChecksum: return "snapshot checksum mismatch";
    case SnapshotStatus::InvalidState: return "snapshot contains invalid game state";
    }
    return "unknown snapshot status";
}
} // namespace digivice
