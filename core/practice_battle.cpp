#include "practice_battle.hpp"
#include "combat.hpp"
#include "forms.hpp"
#include "legacy_combat_v3.hpp"
#include "legacy_combat_v7.hpp"
#include "legacy_forms_v7.hpp"
#include "legacy_combat_v8.hpp"
#include "legacy_forms_v8.hpp"
#include "practice_auto_policy.hpp"
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace digivice::practice {
namespace frozen = digivice::legacy_v3::combat;
namespace prior = digivice::legacy_v7::combat;
namespace priorForms = digivice::legacy_v7::forms;
namespace baseline = digivice::legacy_v8::combat;
namespace baselineForms = digivice::legacy_v8::forms;
namespace {
bool attack(Move move) { return move >= Move::Physical && move <= Move::Magic; }
bool defense(Move move) { return move >= Move::Brace && move <= Move::Ward; }
std::uint32_t random(State& s) {
    auto x = s.rngState; x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return s.rngState = x;
}
Move firstEnemyMove(Phase phase) { return phase == Phase::Attack ? Move::Brace : Move::Physical; }
void commitEnemy(State& s) {
    const auto first = static_cast<unsigned>(firstEnemyMove(s.phase));
    const auto choice = random(s) % 3;
    // One of the two *other* choices is excluded. Never show a false telegraph.
    const auto excluded = (choice + 1 + random(s) % 2) % 3;
    s.enemyChoice = static_cast<Move>(first + choice);
    s.excludedChoice = static_cast<Move>(first + excluded);
}
void finish(State& s, Status status) {
    s.phase = Phase::Finished; s.status = status;
    s.enemyChoice = s.excludedChoice = Move::None;
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
    std::uint32_t value = 0xffffffffu;
    for (std::size_t i = 0; i < length; ++i) {
        value ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
    }
    return ~value;
}
const char* phaseName(Phase phase) {
    return phase == Phase::Attack ? "attack" : phase == Phase::Defend ? "defend" : "finished";
}
const char* statusName(Status status) {
    switch (status) {
    case Status::Active: return "active";
    case Status::Won: return "won";
    case Status::Lost: return "lost";
    case Status::Draw: return "draw";
    case Status::Retreated: return "retreated";
    }
    return "invalid";
}
} // namespace

// Practice6+ chip floor uses the intended defender's full public max HP.
// Saved2..5 keep their original raw floor; type and guards still apply afterward.
constexpr std::uint32_t kMinimumDamageDivisor = 20;
std::uint32_t minimumRawDamage(std::uint32_t rulesVersion, std::uint32_t intendedDefenderMaxHp) {
    if (rulesVersion < 2 || rulesVersion > 7 || !intendedDefenderMaxHp) return 0;
    if (rulesVersion < 6) return 4;
    const auto floor = 1u + (intendedDefenderMaxHp - 1u) / kMinimumDamageDivisor;
    return floor > 32 ? 0 : floor < 4 ? 4 : floor;
}
std::uint32_t maxExchanges(const State& s) { return s.rulesVersion>=5 ? kMaxExchanges : kLegacyMaxExchanges; }
std::size_t snapshotSize(const State& s) { return s.rulesVersion == 2 ? kV2SnapshotSize : (s.rulesVersion >= 3 && s.rulesVersion <= 7) ? kSnapshotSize : 0; }
std::size_t snapshotSize(const std::uint8_t* bytes, std::size_t available) {
    if (!bytes || available < 8 || std::memcmp(bytes, "DGBP", 4)) return 0;
    const auto length = bytes[4] == 2 ? kV2SnapshotSize : (bytes[4] >= 3 && bytes[4] <= 7) ? kSnapshotSize : 0;
    return length <= available ? length : 0;
}
std::uint32_t maxHp(const State& s, bool enemy) {
    if (s.rulesVersion == 2) return frozen::profile(enemy ? s.enemySpecies : s.playerSpecies,
                                                   enemy ? s.enemyLevel : s.playerLevel).stats.maxHp;
    if (s.rulesVersion == 3 || s.rulesVersion == 4) return prior::formProfile(enemy ? s.enemyFormId : s.playerFormId,
                                                       enemy ? s.enemyLevel : s.playerLevel).stats.maxHp;
    if (s.rulesVersion == 5 || s.rulesVersion == 6) return baseline::formProfile(enemy ? s.enemyFormId : s.playerFormId,
                                                       enemy ? s.enemyLevel : s.playerLevel).stats.maxHp;
    if (s.rulesVersion == 7) return combat::formProfile(enemy ? s.enemyFormId : s.playerFormId,
                                                       enemy ? s.enemyLevel : s.playerLevel).stats.maxHp;
    return 0;
}
std::uint32_t selectRivalForm(std::uint32_t seed,std::uint32_t playerFormId,std::uint32_t level) {
    if(!forms::find(playerFormId)||level<1||level>20) return 0;
    const auto tier=forms::combatTier(playerFormId);std::uint32_t count=0;
    for(std::uint32_t id=1;id<=forms::kFormCount;++id) if(combat::validFormProfile(id,level)&&forms::combatTier(id)<=tier)++count;
    if(!count)return 0;
    auto chosen=seed%count;
    for(std::uint32_t id=1;id<=forms::kFormCount;++id) if(combat::validFormProfile(id,level)&&forms::combatTier(id)<=tier&&chosen--==0)return id;
    return 0;
}
std::uint32_t selectProductionRivalForm(std::uint32_t seed,std::uint32_t playerFormId,std::uint32_t level) {
    if(!forms::productionForm(playerFormId)||level<1||level>20) return 0;
    const auto tier=forms::combatTier(playerFormId);std::uint32_t count=0;
    for(std::uint32_t id=forms::kFirstProductionFormId;id<=forms::kFormCount;++id) if(combat::validFormProfile(id,level)&&forms::combatTier(id)<=tier)++count;
    if(!count)return 0;
    auto chosen=seed%count;
    for(std::uint32_t id=forms::kFirstProductionFormId;id<=forms::kFormCount;++id) if(combat::validFormProfile(id,level)&&forms::combatTier(id)<=tier&&chosen--==0)return id;
    return 0;
}
State newBattle(std::uint32_t seed, std::uint32_t playerSpecies, std::uint32_t playerLevel,
                std::uint32_t enemySpecies, std::uint32_t enemyLevel) {
    return newBattleWithForms(seed, playerSpecies, playerLevel, forms::initialForm(playerSpecies),
                              enemySpecies, enemyLevel, forms::initialForm(enemySpecies));
}
State newBattleWithForms(std::uint32_t seed, std::uint32_t playerSpecies, std::uint32_t playerLevel,
                std::uint32_t playerFormId, std::uint32_t enemySpecies, std::uint32_t enemyLevel,
                std::uint32_t enemyFormId) {
    State s;
    s.rngState = seed ? seed : 0x6d2b79f5u;
    s.playerSpecies = playerSpecies; s.playerLevel = playerLevel;
    s.enemySpecies = enemySpecies; s.enemyLevel = enemyLevel;
    s.playerFormId = playerFormId; s.enemyFormId = enemyFormId;
    s.playerHp = maxHp(s); s.enemyHp = maxHp(s, true);
    commitEnemy(s);
    return s;
}

namespace {
bool validState(const State& s, bool legacy) {
    if (legacy) { if (s.playerLevel < 1 || s.playerLevel > 3) return false; }
    else if (s.rulesVersion == 2) {
        if (s.playerFormId || s.enemyFormId || !frozen::validProfile(s.playerSpecies, s.playerLevel) ||
            !frozen::validProfile(s.enemySpecies, s.enemyLevel)) return false;
    } else if (s.rulesVersion >= 3 && s.rulesVersion <= 7) {
        if(s.rulesVersion==3 && (s.playerFormId>66||s.enemyFormId>66||s.playerSpecies>12||s.enemySpecies>12)) return false;
        const bool valid=s.rulesVersion<5 ?
            priorForms::validForLineage(s.playerFormId,s.playerSpecies)&&priorForms::validForLineage(s.enemyFormId,s.enemySpecies)&&
            prior::validFormProfile(s.playerFormId,s.playerLevel)&&prior::validFormProfile(s.enemyFormId,s.enemyLevel) :
            s.rulesVersion<7 ?
            baselineForms::validForLineage(s.playerFormId,s.playerSpecies)&&baselineForms::validForLineage(s.enemyFormId,s.enemySpecies)&&
            baseline::validFormProfile(s.playerFormId,s.playerLevel)&&baseline::validFormProfile(s.enemyFormId,s.enemyLevel) :
            forms::validForLineage(s.playerFormId,s.playerSpecies)&&forms::validForLineage(s.enemyFormId,s.enemySpecies)&&
            combat::validFormProfile(s.playerFormId,s.playerLevel)&&combat::validFormProfile(s.enemyFormId,s.enemyLevel);
        if(!valid) return false;
    } else return false;
    const auto playerMax = legacy ? 100u : maxHp(s);
    const auto enemyMax = legacy ? 100u : maxHp(s, true);
    const auto playerDamageMax = legacy ? 29u : (playerMax > 29 ? playerMax : 29u);
    const auto enemyDamageMax = legacy ? 29u : (enemyMax > 29 ? enemyMax : 29u);
    if (!s.rngState || s.playerHp > playerMax || s.enemyHp > enemyMax ||
        s.exchanges > maxExchanges(s) || s.sequence > maxExchanges(s) + 2 ||
        (s.attackBoost != 0 && s.attackBoost != 5) || s.shield > 12 ||
        (s.attackBoost && s.shield) || (!s.cardUsed && (s.attackBoost || s.shield)) ||
        s.lastPlayerDamage > playerDamageMax || s.lastEnemyDamage > enemyDamageMax ||
        static_cast<unsigned>(s.phase) > 2 || static_cast<unsigned>(s.status) > 4 ||
        static_cast<unsigned>(s.previousEnemyMove) > 6 || static_cast<unsigned>(s.lastEnemyMove) > 6 ||
        static_cast<unsigned>(s.lastPlayerChoice) > 6 || static_cast<unsigned>(s.lastOpponentChoice) > 6 ||
        static_cast<unsigned>(s.lastPhase) > 1) return false;
    const auto expectedSequence = s.exchanges + (s.cardUsed ? 1 : 0) + (s.status == Status::Retreated ? 1 : 0);
    if (s.sequence != expectedSequence) return false;
    if (s.exchanges == 0) {
        if (s.previousEnemyMove != Move::None || s.lastEnemyMove != Move::None ||
            s.lastPlayerChoice != Move::None || s.lastOpponentChoice != Move::None ||
            s.lastPlayerDamage || s.lastEnemyDamage || s.lastReflected || s.lastPhase != Phase::Attack) return false;
    } else {
        if (s.lastEnemyMove != s.lastOpponentChoice || s.lastEnemyMove == Move::None ||
            (s.exchanges == 1 ? s.previousEnemyMove != Move::None : s.previousEnemyMove == Move::None)) return false;
        if (s.lastPhase != (s.exchanges % 2 ? Phase::Attack : Phase::Defend)) return false;
        if (s.lastPhase == Phase::Attack ? (!attack(s.lastPlayerChoice) || !defense(s.lastOpponentChoice)) :
            (!defense(s.lastPlayerChoice) || !attack(s.lastOpponentChoice))) return false;
        if (s.exchanges > 1 && (s.lastPhase == Phase::Attack ? !attack(s.previousEnemyMove) : !defense(s.previousEnemyMove))) return false;
        const bool reflected = s.lastPhase == Phase::Attack ?
            s.lastPlayerChoice == Move::Heavy && s.lastOpponentChoice == Move::Counter :
            s.lastOpponentChoice == Move::Heavy && s.lastPlayerChoice == Move::Counter;
        if (s.lastReflected != reflected) return false;
    }
    if (s.status == Status::Active) {
        if (!s.playerHp || !s.enemyHp || s.exchanges >= maxExchanges(s) ||
            s.phase != (s.exchanges % 2 ? Phase::Defend : Phase::Attack)) return false;
        if (s.phase == Phase::Attack ? (!defense(s.enemyChoice) || !defense(s.excludedChoice)) :
            (!attack(s.enemyChoice) || !attack(s.excludedChoice))) return false;
        return s.enemyChoice != s.excludedChoice;
    }
    if (s.phase != Phase::Finished || s.enemyChoice != Move::None || s.excludedChoice != Move::None) return false;
    if (s.status == Status::Won) return s.enemyHp == 0 && s.playerHp > 0 && s.exchanges > 0;
    if (s.status == Status::Lost) return s.playerHp == 0 && s.enemyHp > 0 && s.exchanges > 0;
    if (s.status == Status::Draw) return s.exchanges == maxExchanges(s) && s.playerHp > 0 && s.enemyHp > 0;
    return s.status == Status::Retreated && s.exchanges < maxExchanges(s) && s.playerHp > 0 && s.enemyHp > 0;
}
} // namespace
bool isValid(const State& s) { return validState(s, false); }

Error apply(State& state, const char* action, std::uint32_t value) {
    if (!isValid(state)) return Error::InvalidState;
    if (state.status != Status::Active) return Error::Finished;
    if (!action) return Error::InvalidAction;
    State next = state;
    if (std::strcmp(action, "card") == 0) {
        if (value != 1 && value != 2) return Error::InvalidValue;
        if (next.cardUsed) return Error::CardUsed;
        next.cardUsed = true;
        if (value == 1) next.attackBoost = 5;
        else next.shield = 12;
    } else if (std::strcmp(action, "retreat") == 0) {
        if (value != 0) return Error::InvalidValue;
        finish(next, Status::Retreated);
    } else {
        if (value != 0) return Error::InvalidValue;
        Move choice = Move::None;
        for (unsigned i = 1; i <= 6; ++i) {
            if (std::strcmp(action, moveName(static_cast<Move>(i))) == 0) choice = static_cast<Move>(i);
        }
        if (choice == Move::None) return Error::InvalidAction;
        if (next.phase == Phase::Attack ? !attack(choice) : !defense(choice)) return Error::WrongPhase;
        const auto attacking = next.phase == Phase::Attack ? choice : next.enemyChoice;
        const auto defending = next.phase == Phase::Attack ? next.enemyChoice : choice;
        const bool playerAttacking = next.phase == Phase::Attack;
        const auto move = attacking == Move::Physical ? combat::Move::Physical : attacking == Move::Heavy ? combat::Move::Heavy : combat::Move::Magic;
        const auto guard = defending == Move::Brace ? combat::Defense::Brace : defending == Move::Counter ? combat::Defense::Counter : combat::Defense::Ward;
        combat::Hit hit{};
        if (next.rulesVersion == 2) {
            const auto old = frozen::resolve(playerAttacking ? next.playerSpecies : next.enemySpecies,
                playerAttacking ? next.playerLevel : next.enemyLevel, playerAttacking ? next.enemySpecies : next.playerSpecies,
                playerAttacking ? next.enemyLevel : next.playerLevel, static_cast<frozen::Move>(move), static_cast<frozen::Defense>(guard));
            hit = {old.damage, old.reflected, old.typePercent};
        } else if(next.rulesVersion<5) {
            const auto old=prior::resolveForms(playerAttacking?next.playerFormId:next.enemyFormId,
                playerAttacking?next.playerLevel:next.enemyLevel,playerAttacking?next.enemyFormId:next.playerFormId,
                playerAttacking?next.enemyLevel:next.playerLevel,static_cast<prior::Move>(move),static_cast<prior::Defense>(guard));
            hit={old.damage,old.reflected,old.typePercent};
        } else if(next.rulesVersion<7) {
            const auto old=baseline::resolveForms(playerAttacking?next.playerFormId:next.enemyFormId,
                playerAttacking?next.playerLevel:next.enemyLevel,playerAttacking?next.enemyFormId:next.playerFormId,
                playerAttacking?next.enemyLevel:next.playerLevel,static_cast<baseline::Move>(move),static_cast<baseline::Defense>(guard),
                minimumRawDamage(next.rulesVersion,maxHp(next,playerAttacking)));
            hit={old.damage,old.reflected,old.typePercent};
        } else hit = combat::resolveForms(playerAttacking ? next.playerFormId : next.enemyFormId,
            playerAttacking ? next.playerLevel : next.enemyLevel, playerAttacking ? next.enemyFormId : next.playerFormId,
            playerAttacking ? next.enemyLevel : next.playerLevel, move, guard,
            minimumRawDamage(next.rulesVersion, maxHp(next, playerAttacking)));
        const bool reflected = hit.reflected;
        auto damage = hit.damage;
        const bool hitsPlayer = reflected ? next.phase == Phase::Attack : next.phase == Phase::Defend;
        if (!hitsPlayer && !reflected && next.attackBoost) {
            damage += next.attackBoost;
            next.attackBoost = 0;
        }
        if (hitsPlayer) {
            const auto blocked = damage < next.shield ? damage : next.shield;
            damage -= blocked;
            next.shield -= blocked;
        }
        auto& hp = hitsPlayer ? next.playerHp : next.enemyHp;
        const auto received = damage < hp ? damage : hp;
        hp -= received;
        next.lastPhase = next.phase;
        next.lastPlayerChoice = choice;
        next.lastOpponentChoice = next.enemyChoice;
        next.lastPlayerDamage = hitsPlayer ? received : 0;
        next.lastEnemyDamage = hitsPlayer ? 0 : received;
        next.lastReflected = reflected;
        next.previousEnemyMove = next.lastEnemyMove;
        next.lastEnemyMove = next.enemyChoice;
        ++next.exchanges;
        if (!next.enemyHp) finish(next, Status::Won);
        else if (!next.playerHp) finish(next, Status::Lost);
        else if (next.exchanges >= maxExchanges(next)) finish(next, Status::Draw);
        else {
            next.phase = next.phase == Phase::Attack ? Phase::Defend : Phase::Attack;
            commitEnemy(next);
        }
    }
    ++next.sequence;
    if (!isValid(next)) return Error::InvalidState;
    state = next;
    return Error::None;
}

namespace {
// This policy receives only the alternating public phase and its OWN PRNG.
// Never pass State here: enemyChoice/excludedChoice are already committed.
Move automaticChoice(Phase phase, std::uint32_t& policyRng) {
    const auto x = autobattle::nextRandom(policyRng);
    const auto first = phase == Phase::Attack ? Move::Physical : Move::Brace;
    return static_cast<Move>(static_cast<unsigned>(first) + x % 3);
}
}
Error runAuto(State& state, autobattle::Trace& trace) {
    if (!isValid(state) || state.status != Status::Active || state.phase != Phase::Attack ||
        state.sequence != 0 || state.exchanges != 0 || state.cardUsed ||
        state.playerHp != maxHp(state) || state.enemyHp != maxHp(state, true))
        return Error::InvalidState;
    static_assert(kMaxExchanges <= autobattle::kMaxTraceSteps);
    static_assert(static_cast<unsigned>(Move::Ward) == static_cast<unsigned>(autobattle::Move::Ward));
    State next = state;
    autobattle::Trace result{};
    result.kind = autobattle::Kind::Practice;
    result.combatRulesVersion = state.rulesVersion == 2 ? 3 : state.rulesVersion<5 ? 7 : state.rulesVersion<7 ? 8 : 4;
    result.includeFormIds=state.rulesVersion>=4;
    result.playerFormId = state.playerFormId; result.enemyFormId = state.enemyFormId;
    result.startSequence = state.sequence;
    result.playerSpecies = state.playerSpecies; result.playerLevel = state.playerLevel;
    result.enemySpecies = state.enemySpecies; result.enemyLevel = state.enemyLevel;
    // Separate deterministic stream derived once from the initial seeded state.
    // It neither consumes the opponent stream nor reads its choices/hints.
    std::uint32_t policyRng = next.rngState ^ 0xa341316cu;
    if (!policyRng) policyRng = 0x9e3779b9u;
    while (next.status == Status::Active && result.count < maxExchanges(next)) {
        autobattle::Step step{};
        step.defending = next.phase == Phase::Defend;
        step.playerHpBefore = next.playerHp; step.enemyHpBefore = next.enemyHp;
        const auto choice = next.rulesVersion < 7 ? automaticChoice(next.phase, policyRng) :
            auto_policy::choose({next.phase, next.rulesVersion, next.playerFormId, next.playerLevel,
                next.enemyFormId, next.enemyLevel, next.playerHp, next.enemyHp}, policyRng);
        const auto error = apply(next, moveName(choice));
        if (error != Error::None) return error;
        step.action = static_cast<autobattle::Move>(choice);
        // This is now a resolved PAST move, never an unrevealed commitment.
        step.opponentAction = static_cast<autobattle::Move>(next.lastOpponentChoice);
        step.playerHpAfter = next.playerHp; step.enemyHpAfter = next.enemyHp;
        step.reflected = next.lastReflected;
        result.steps[result.count++] = step;
    }
    if (next.status == Status::Active || !isValid(next)) return Error::InvalidState;
    result.outcome = next.status == Status::Won ? autobattle::Outcome::Won :
        next.status == Status::Lost ? autobattle::Outcome::Lost : autobattle::Outcome::Draw;
    result.endSequence = next.sequence;
    state = next; trace = result;
    return Error::None;
}

const char* moveName(Move move) {
    switch (move) {
    case Move::Physical: return "physical";
    case Move::Heavy: return "heavy";
    case Move::Magic: return "magic";
    case Move::Brace: return "brace";
    case Move::Counter: return "counter";
    case Move::Ward: return "ward";
    case Move::None: return "none";
    }
    return "invalid";
}
const char* errorText(Error error) {
    switch (error) {
    case Error::None: return "ok";
    case Error::InvalidState: return "invalid practice battle state";
    case Error::InvalidAction: return "unknown practice battle action";
    case Error::WrongPhase: return "choose a move for the current attack or defend phase";
    case Error::InvalidValue: return "only card accepts a value (1 or 2)";
    case Error::CardUsed: return "only one card may be used per practice battle";
    case Error::Finished: return "this practice battle has ended";
    }
    return "unknown error";
}

std::size_t writePublicJson(const State& s, char* output, std::size_t capacity) {
    if (!output || !capacity) return 0;
    output[0] = '\0';
    if (!isValid(s)) return 0;
    std::size_t used = 0; bool ok = true;
    const auto speciesName = s.rulesVersion >= 5 && s.rulesVersion < 7 ? baseline::speciesName : combat::speciesName;
    const auto append = [&](const char* format, ...) {
        if (!ok) return;
        va_list args; va_start(args, format);
        const auto n = std::vsnprintf(output + used, capacity - used, format, args);
        va_end(args);
        if (n < 0 || static_cast<std::size_t>(n) >= capacity - used) { ok = false; return; }
        used += static_cast<std::size_t>(n);
    };
    append("{\"schemaVersion\":%" PRIu32 ",\"rulesVersion\":%" PRIu32 ",\"sequence\":%" PRIu32 ",\"phase\":\"%s\",\"status\":\"%s\","
           "\"playerHp\":%" PRIu32 ",\"enemyHp\":%" PRIu32 ",\"playerLevel\":%" PRIu32 ",\"enemyLevel\":%" PRIu32 ",\"exchanges\":%" PRIu32 ","
           "\"cardUsed\":%s,\"attackBoost\":%" PRIu32 ",\"shield\":%" PRIu32 ",\"playerSpecies\":\"%s\",\"enemySpecies\":\"%s\",",
           s.rulesVersion, s.rulesVersion, s.sequence, phaseName(s.phase), statusName(s.status), s.playerHp, s.enemyHp,
           s.playerLevel, s.enemyLevel, s.exchanges, s.cardUsed ? "true" : "false", s.attackBoost, s.shield,
           speciesName(s.playerSpecies), speciesName(s.enemySpecies));
    if (s.rulesVersion >= 5) append("\"maxExchanges\":%" PRIu32 ",",maxExchanges(s));
    if (s.rulesVersion >= 3) append("\"playerFormId\":%" PRIu32 ",\"enemyFormId\":%" PRIu32 ",\"playerFormName\":\"%s\",\"enemyFormName\":\"%s\",",
        s.playerFormId, s.enemyFormId,
        s.rulesVersion<5 ? prior::formProfile(s.playerFormId,s.playerLevel).name : s.rulesVersion<7 ? baseline::formProfile(s.playerFormId,s.playerLevel).name : combat::formProfile(s.playerFormId,s.playerLevel).name,
        s.rulesVersion<5 ? prior::formProfile(s.enemyFormId,s.enemyLevel).name : s.rulesVersion<7 ? baseline::formProfile(s.enemyFormId,s.enemyLevel).name : combat::formProfile(s.enemyFormId,s.enemyLevel).name);
    const auto writeCombat = [&](const char* field, std::uint32_t species, std::uint32_t level, std::uint32_t formId) {
        char json[combat::kProfileJsonCapacity];
        const auto count = s.rulesVersion == 2 ? frozen::writeProfileJson(species, level, json, sizeof(json)) :
            s.rulesVersion<5 ? prior::writeFormProfileJson(formId,level,json,sizeof(json)) :
            s.rulesVersion<7 ? baseline::writeFormProfileJson(formId,level,json,sizeof(json)) :
            combat::writeFormProfileJson(formId, level, json, sizeof(json));
        if (!count) { ok = false; return; }
        append("\"%s\":%s,", field, json);
    };
    writeCombat("playerCombat", s.playerSpecies, s.playerLevel, s.playerFormId);
    writeCombat("enemyCombat", s.enemySpecies, s.enemyLevel, s.enemyFormId);
    append("\"enemyHint\":[");
    if (s.status == Status::Active) {
        bool comma = false;
        const auto first = static_cast<unsigned>(firstEnemyMove(s.phase));
        for (unsigned i = first; i < first + 3; ++i) {
            const auto move = static_cast<Move>(i);
            if (move != s.excludedChoice) { append("%s\"%s\"", comma ? "," : "", moveName(move)); comma = true; }
        }
    }
    append("],\"lastEnemyMoves\":[");
    if (s.previousEnemyMove != Move::None) append("\"%s\",", moveName(s.previousEnemyMove));
    if (s.lastEnemyMove != Move::None) append("\"%s\"", moveName(s.lastEnemyMove));
    append("],\"lastTurn\":");
    if (!s.exchanges) append("null");
    else append("{\"phase\":\"%s\",\"playerChoice\":\"%s\",\"enemyChoice\":\"%s\","
                "\"playerDamage\":%" PRIu32 ",\"enemyDamage\":%" PRIu32 ",\"reflected\":%s}", phaseName(s.lastPhase),
                moveName(s.lastPlayerChoice), moveName(s.lastOpponentChoice), s.lastPlayerDamage,
                s.lastEnemyDamage, s.lastReflected ? "true" : "false");
    append("}");
    if (!ok) { output[0] = '\0'; return 0; }
    return used;
}

bool encodeSnapshot(const State& s, Snapshot& snapshot) {
    if (!isValid(s)) return false;
    Snapshot next;
    const auto length = snapshotSize(s);
    std::memcpy(next.bytes, "DGBP", 4); next.bytes[4] = static_cast<std::uint8_t>(s.rulesVersion); next.bytes[6] = static_cast<std::uint8_t>(length - 12);
    const std::uint32_t fields[] = {s.rulesVersion, s.sequence, s.rngState, s.playerLevel, s.playerHp, s.enemyHp,
        s.exchanges, static_cast<unsigned>(s.phase), static_cast<unsigned>(s.status), s.cardUsed ? 1u : 0u,
        s.attackBoost, s.shield, static_cast<unsigned>(s.enemyChoice), static_cast<unsigned>(s.excludedChoice),
        static_cast<unsigned>(s.previousEnemyMove), static_cast<unsigned>(s.lastEnemyMove),
        static_cast<unsigned>(s.lastPlayerChoice), static_cast<unsigned>(s.lastOpponentChoice),
        s.lastPlayerDamage, s.lastEnemyDamage, s.lastReflected ? 1u : 0u, static_cast<unsigned>(s.lastPhase),
        s.playerSpecies, s.enemySpecies, s.enemyLevel, s.playerFormId, s.enemyFormId};
    static_assert(sizeof(fields) + 12 == kSnapshotSize);
    for (std::size_t i = 0; i < (length - 12) / 4; ++i) put32(next.bytes + 8 + i * 4, fields[i]);
    put32(next.bytes + length - 4, crc32(next.bytes, length - 4)); snapshot = next; return true;
}
namespace {
bool decodeRecord(const std::uint8_t* bytes, std::size_t length, State& state, bool legacy) {
    const auto expectedLength = legacy ? kLegacySnapshotSize : snapshotSize(bytes, length);
    const auto version = legacy ? 1u : (bytes && length>=8 ? static_cast<unsigned>(bytes[4]) : 0u);
    if (!bytes || !expectedLength || length != expectedLength || std::memcmp(bytes, "DGBP", 4) != 0 ||
        bytes[4] != version || bytes[5] != 0 || bytes[6] != expectedLength - 12 || bytes[7] != 0 ||
        get32(bytes + 8) != version || get32(bytes + length - 4) != crc32(bytes, length - 4)) return false;
    std::uint32_t fields[26]{};
    for (unsigned i = 0; i < (expectedLength - 16) / 4; ++i) fields[i] = get32(bytes + 12 + i * 4);
    if (fields[6] > 2 || fields[7] > 4 || fields[8] > 1 || fields[19] > 1 || fields[20] > 1) return false;
    for (unsigned i = 11; i <= 16; ++i) if (fields[i] > 6) return false;
    State s;
    s.rulesVersion = legacy ? 2 : version;
    s.sequence = fields[0]; s.rngState = fields[1]; s.playerLevel = fields[2];
    s.playerHp = fields[3]; s.enemyHp = fields[4]; s.exchanges = fields[5];
    s.phase = static_cast<Phase>(fields[6]); s.status = static_cast<Status>(fields[7]); s.cardUsed = fields[8] != 0;
    s.attackBoost = fields[9]; s.shield = fields[10]; s.enemyChoice = static_cast<Move>(fields[11]);
    s.excludedChoice = static_cast<Move>(fields[12]); s.previousEnemyMove = static_cast<Move>(fields[13]);
    s.lastEnemyMove = static_cast<Move>(fields[14]); s.lastPlayerChoice = static_cast<Move>(fields[15]);
    s.lastOpponentChoice = static_cast<Move>(fields[16]); s.lastPlayerDamage = fields[17];
    s.lastEnemyDamage = fields[18]; s.lastReflected = fields[19] != 0; s.lastPhase = static_cast<Phase>(fields[20]);
    if (!legacy) { s.playerSpecies = fields[21]; s.enemySpecies = fields[22]; s.enemyLevel = fields[23]; }
    if (version >= 3) { s.playerFormId = fields[24]; s.enemyFormId = fields[25]; }
    if (!validState(s, legacy)) return false;
    state = s; return true;
}
} // namespace
bool decodeSnapshot(const std::uint8_t* bytes, std::size_t length, State& state) {
    return decodeRecord(bytes, length, state, false);
}
bool migrateV1(const std::uint8_t* bytes, std::size_t length, std::uint32_t playerSpecies,
               std::uint32_t playerLevel, State& state) {
    if (!frozen::validProfile(playerSpecies, playerLevel)) return false;
    State migrated;
    if (!decodeRecord(bytes, length, migrated, true) || migrated.playerLevel != playerLevel) return false;
    migrated.playerSpecies = playerSpecies; migrated.enemySpecies = 2; migrated.enemyLevel = 1;
    const auto playerMax = frozen::profile(playerSpecies, playerLevel).stats.maxHp;
    const auto enemyMax = frozen::profile(2, 1).stats.maxHp;
    migrated.playerHp = static_cast<std::uint32_t>((static_cast<std::uint64_t>(migrated.playerHp) * playerMax + 99u) / 100u);
    migrated.enemyHp = static_cast<std::uint32_t>((static_cast<std::uint64_t>(migrated.enemyHp) * enemyMax + 99u) / 100u);
    if (!isValid(migrated)) return false;
    state = migrated; return true;
}
} // namespace digivice::practice
