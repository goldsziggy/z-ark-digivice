// Host-only audit: all damage and state transitions call the real native core.
// Synthetic matchup fixtures are validated; they are never persisted as saves.
#include "combat.hpp"
#include "forms.hpp"
#ifndef WORLD_DS_COMBAT_ONLY
#include "game.hpp"
#include "practice_battle.hpp"
#endif
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace combat = digivice::combat;
namespace forms = digivice::forms;
struct Case { unsigned id, player, level, enemy, enemyLevel, seed, seedIndex; };
Case current{};
void require(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "audit failure case=%u player=%u/%u enemy=%u/%u seed=%u: %s\n",
                     current.id, current.player, current.level, current.enemy,
                     current.enemyLevel, current.seed, message);
        std::exit(1);
    }
}
void prefix(const char* kind) {
    std::printf("{\"kind\":\"%s\",\"case\":%u,\"player\":%u,\"level\":%u,"
                "\"enemy\":%u,\"enemyLevel\":%u,\"seed\":%u,\"seedIndex\":%u,",
                kind, current.id, current.player, current.level, current.enemy,
                current.enemyLevel, current.seed, current.seedIndex);
}
void profiles() {
    for (unsigned id = 1; id <= forms::kFormCount; ++id) {
        const auto* form = forms::find(id);
        require(form != nullptr, "catalog ID missing");
        for (unsigned level = form->minLevel; level <= forms::kMaxRpgLevel; ++level) {
            const auto stats = forms::stats(id, level);
            std::printf("{\"kind\":\"profile\",\"form\":%u,\"lineage\":%u,\"level\":%u,"
                        "\"name\":\"%s\",\"type\":\"%s\",\"tier\":\"%s\","
                        "\"stats\":[%u,%u,%u,%u,%u]}\n", id, form->lineage, level,
                        form->name, form->type, forms::combatTierName(forms::combatTier(id)),
                        stats.maxHp, stats.attack, stats.defense, stats.magic, stats.resistance);
        }
    }
}
void damage() {
    prefix("damage");
    std::printf("\"hits\":[");
    for (unsigned guard = 0; guard < 4; ++guard) {
        std::printf("%s[", guard ? "," : "");
        for (unsigned move = 0; move < 3; ++move) {
            const auto hit = combat::resolveForms(current.player, current.level,
                current.enemy, current.enemyLevel, static_cast<combat::Move>(move),
                static_cast<combat::Defense>(guard));
            require(hit.damage > 0, "valid profile produced no resolver damage");
            require(hit.reflected == (move == 1 && guard == 2), "counter matrix changed");
            std::printf("%s%u", move ? "," : "", hit.damage);
        }
        std::printf("]");
    }
    std::printf("]}\n");
}

#ifndef WORLD_DS_COMBAT_ONLY
namespace game = digivice;
namespace practice = digivice::practice;

// Deliberately omits private RNG/committed intent. The policy is shallow and
// maximizes immediate expected HP swing over the two publicly possible moves.
struct PublicDuel {
    practice::Phase phase;
    practice::Move excluded;
    unsigned rulesVersion;
    unsigned player, level, enemy, enemyLevel, playerHp, enemyHp;
};
PublicDuel publicView(const practice::State& state) {
    return {state.phase, state.excludedChoice, state.rulesVersion, state.playerFormId, state.playerLevel,
            state.enemyFormId, state.enemyLevel, state.playerHp, state.enemyHp};
}
practice::Move choosePractice(const PublicDuel& state) {
    const bool attack = state.phase == practice::Phase::Attack;
    const unsigned first = attack ? 1 : 4, otherFirst = attack ? 4 : 1;
    auto best = static_cast<practice::Move>(first);
    int bestScore = -100000;
    for (unsigned choice = first; choice < first + 3; ++choice) {
        int score = 0;
        for (unsigned other = otherFirst; other < otherFirst + 3; ++other) {
            if (other == static_cast<unsigned>(state.excluded)) continue;
            const auto hit = combat::resolveForms(attack ? state.player : state.enemy,
                attack ? state.level : state.enemyLevel, attack ? state.enemy : state.player,
                attack ? state.enemyLevel : state.level,
                static_cast<combat::Move>((attack ? choice : other) - 1),
                static_cast<combat::Defense>((attack ? other : choice) - 3),
                practice::minimumRawDamage(state.rulesVersion,
                    combat::formProfile(attack ? state.enemy : state.player,
                        attack ? state.enemyLevel : state.level).stats.maxHp));
            const bool hurtsEnemy = attack != hit.reflected;
            score += static_cast<int>(std::min(hit.damage,
                hurtsEnemy ? state.enemyHp : state.playerHp)) * (hurtsEnemy ? 1 : -1);
        }
        if (score > bestScore) { bestScore = score; best = static_cast<practice::Move>(choice); }
    }
    return best;
}
const char* practiceOutcome(practice::Status status) {
    switch (status) {
    case practice::Status::Won: return "won";
    case practice::Status::Lost: return "lost";
    case practice::Status::Draw: return "draw";
    case practice::Status::Retreated: return "retreated";
    default: return "active";
    }
}
void practiceRun(bool automatic) {
    auto state = practice::newBattleWithForms(current.seed,
        forms::find(current.player)->lineage, current.level, current.player,
        forms::find(current.enemy)->lineage, current.enemyLevel, current.enemy);
    require(practice::isValid(state), "practice fixture invalid");
    const auto initialHp = state.playerHp;
    std::array<unsigned, 7> choices{};
    unsigned reflections = 0;
    if (automatic) {
        game::autobattle::Trace trace;
        require(practice::runAuto(state, trace) == practice::Error::None, "practice Auto failed");
        for (unsigned i = 0; i < trace.count; ++i) {
            const auto action = static_cast<unsigned>(trace.steps[i].action);
            require(action >= 1 && action <= 6, "practice trace action invalid");
            ++choices[action]; reflections += trace.steps[i].reflected;
        }
    } else {
        while (state.status == practice::Status::Active && state.exchanges < practice::kMaxExchanges) {
            const auto move = choosePractice(publicView(state));
            ++choices[static_cast<unsigned>(move)];
            require(practice::apply(state, practice::moveName(move)) == practice::Error::None,
                    "practice public-choice action failed");
            reflections += state.lastReflected;
        }
    }
    require(state.status != practice::Status::Active && state.exchanges <= practice::kMaxExchanges,
            "practice failed to terminate within its native bound");
    require(practice::isValid(state), "practice terminal invalid");
    prefix("practice");
    std::printf("\"policy\":\"%s\",\"outcome\":\"%s\",\"turns\":%u,"
                "\"initialHp\":%u,\"combatHpEnd\":%u,\"reflections\":%u,"
                "\"choices\":[%u,%u,%u,%u,%u,%u]}\n", automatic ? "auto" : "public-hint",
                practiceOutcome(state.status), state.exchanges, initialHp, state.playerHp,
                reflections, choices[1], choices[2], choices[3], choices[4], choices[5], choices[6]);
}
void markObtained(game::State& state, unsigned form) {
    state.journal[(form - 1) / 32] |= 1u << ((form - 1) % 32);
}
game::CreatureMember fixtureMember(unsigned id, unsigned form, unsigned level, unsigned sequence) {
    game::CreatureMember member;
    member.id = id; member.formId = form;
    member.species = static_cast<game::Species>(forms::find(form)->lineage);
    member.level = level; member.xp = game::xpForLevel(level);
    member.hp = forms::stats(form, level).maxHp;
    member.energy = 80; member.fullness = member.mood = 100;
    member.bond = forms::find(form)->minBond; member.capturedAtSequence = sequence;
    return member;
}
game::State wildFixture(bool automatic, bool full) {
    auto state = game::newGame(current.seed);
    // Both objective fixtures retain identical historical counters/seeds. The
    // two-member variant models released prior captures; no release is executed.
    state.sequence = 100; state.captures = 7; state.nextMemberId = 9;
    state.encounters = 7 + current.seed % 3; state.steps = state.encounters * 100;
    state.collectionCount = full ? 8 : 2; state.activeCreatureId = 2;
    state.collection[1] = fixtureMember(2, current.player, current.level, 2);
    markObtained(state, current.player); markObtained(state, 4);
    for (unsigned i = 2; i < state.collectionCount; ++i)
        state.collection[i] = fixtureMember(i + 1, 4, 1, i + 1);
    const auto& member = state.collection[1];
    state.hp = member.hp; state.energy = member.energy; state.fullness = member.fullness;
    state.mood = member.mood; state.level = member.level; state.bond = member.bond;
    state.battleMode = automatic ? game::BattleMode::Auto : game::BattleMode::Tactical;
    require(game::isValid(state), "home fixture invalid");
    require(game::apply(state, game::Action::Walk, 100) == game::Error::None, "fixture Walk failed");
    state.wildFormId = current.enemy;
    state.wildSpecies = static_cast<game::Species>(forms::find(current.enemy)->lineage);
    state.wildLevel = current.enemyLevel; state.wildRules = game::kRulesVersion;
    state.wildHp = state.wildMaxHp = forms::stats(current.enemy, current.enemyLevel).maxHp;
    require(game::isValid(state), "encounter fixture invalid");
    return state;
}
enum class Policy { Auto, Greedy, Economy, IgnoreGuard };
game::Action chooseWild(const game::State& state, Policy policy, bool capture) {
    if (capture && state.collectionCount < game::kCollectionCapacity &&
        state.wildHp <= state.wildMaxHp / 2 && state.captureAttempts < 3)
        return game::Action::Capture;
    auto best = game::Action::Attack;
    int bestScore = -100000;
    for (const auto action : {game::Action::Attack, game::Action::Magic, game::Action::Heavy}) {
        if (action == game::Action::Heavy && (policy == Policy::Economy || state.energy < 6)) continue;
        const auto move = action == game::Action::Attack ? combat::Move::Physical :
            action == game::Action::Heavy ? combat::Move::Heavy : combat::Move::Magic;
        const auto hit = combat::resolveForms(current.player, state.level, state.wildFormId,
            state.wildLevel, move, policy == Policy::IgnoreGuard ? combat::Defense::None : game::wildGuard(state));
        const int score = static_cast<int>(std::min(hit.damage,
            hit.reflected ? state.hp : state.wildHp)) * (hit.reflected ? -1 : 1);
        if (score > bestScore) { bestScore = score; best = action; }
    }
    return best;
}
void wildRun(Policy policy, bool full) {
    auto state = wildFixture(policy == Policy::Auto, full);
    const auto startSequence = state.sequence, startCaptures = state.captures;
    const auto startXp = game::activeMember(state)->xp, initialHp = state.hp;
    unsigned turns = 0, physical = 0, heavy = 0, magic = 0, captures = 0, reflected = 0;
    unsigned heavyOnCounter = 0, avoidedCounter = 0, combatHpEnd = state.hp;
    if (policy == Policy::Auto) {
        game::autobattle::Trace trace;
        require(game::applyAuto(state, &trace) == game::Error::None, "wild Auto failed");
        turns = static_cast<unsigned>(trace.count);
        require(turns > 0 && turns <= game::autobattle::kMaxTraceSteps, "Auto trace bound");
        require(state.sequence == startSequence + 1, "Auto did not commit once");
        combatHpEnd = trace.steps[turns - 1].playerHpAfter;
        for (unsigned i = 0; i < turns; ++i) {
            const auto& step = trace.steps[i];
            physical += step.action == game::autobattle::Move::Physical;
            heavy += step.action == game::autobattle::Move::Heavy;
            magic += step.action == game::autobattle::Move::Magic;
            captures += step.action == game::autobattle::Move::Capture;
            reflected += step.reflected;
            heavyOnCounter += step.action == game::autobattle::Move::Heavy && step.guard == game::autobattle::Move::Counter;
            avoidedCounter += step.guard == game::autobattle::Move::Counter &&
                (step.action == game::autobattle::Move::Physical || step.action == game::autobattle::Move::Magic);
        }
    } else {
        while (state.phase == game::Phase::Encounter && turns < 512) {
            const auto action = chooseWild(state, policy, !full);
            const auto guard = game::wildGuard(state);
            const auto hpBefore = state.hp;
            physical += action == game::Action::Attack; heavy += action == game::Action::Heavy;
            magic += action == game::Action::Magic; captures += action == game::Action::Capture;
            reflected += action == game::Action::Heavy && guard == combat::Defense::Counter;
            heavyOnCounter += action == game::Action::Heavy && guard == combat::Defense::Counter;
            avoidedCounter += guard == combat::Defense::Counter &&
                (action == game::Action::Attack || action == game::Action::Magic);
            require(game::apply(state, action) == game::Error::None, "wild action failed");
            ++turns;
            combatHpEnd = state.phase == game::Phase::Encounter ? state.hp :
                state.message == game::Message::Retreated ? 0 : hpBefore;
        }
    }
    require(state.phase == game::Phase::Home && game::isValid(state), "wild did not terminate validly");
    const char* result = state.captures > startCaptures ? "captured" :
        state.message == game::Message::Retreated ? "retreated" : "won";
    const auto xp = game::activeMember(state)->xp - startXp;
    require(std::strcmp(result, "retreated") || xp == 0, "retreat awarded XP");
    require(!full || state.captures == startCaptures, "full collection captured");
    const auto hpAfterHome = state.hp, energySpent = 80 - state.energy;
    unsigned rests = 0;
    const auto recoveryHp = forms::stats(current.player, state.level).maxHp;
    while ((state.hp < recoveryHp || state.energy < 80) && rests < 64) {
        require(game::apply(state, game::Action::Rest) == game::Error::None, "recovery failed");
        ++rests;
    }
    require(state.hp == recoveryHp && state.energy >= 80, "recovery bound exceeded");
    require(game::activeMember(state)->xp == startXp + xp, "recovery awarded XP");
    const char* name = policy == Policy::Auto ? "auto" : policy == Policy::Greedy ? "public-greedy" :
                       policy == Policy::Economy ? "public-economy" : "ignore-guard";
    prefix("wild");
    std::printf("\"policy\":\"%s\",\"objective\":\"%s\",\"outcome\":\"%s\","
                "\"turns\":%u,\"initialHp\":%u,\"combatHpEnd\":%u,\"hpAfterHome\":%u,"
                "\"energySpent\":%u,\"recoveryRests\":%u,\"xpAwarded\":%u,\"reflections\":%u,"
                "\"heavyOnCounter\":%u,\"avoidedCounter\":%u,\"choices\":[%u,%u,%u,%u]}\n",
                name, full ? "defeat-full-collection" : "capture-first", result, turns, initialHp,
                combatHpEnd, hpAfterHome, energySpent, rests, xp, reflected, heavyOnCounter,
                avoidedCounter, physical, heavy, magic, captures);
}
#endif

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--profiles") == 0) { profiles(); return 0; }
    const bool probesOnly = argc == 2 && std::strcmp(argv[1], "--probes") == 0;
    require(probesOnly || (argc == 2 && std::strcmp(argv[1], "--battles") == 0), "expected --profiles/--probes/--battles");
#ifdef WORLD_DS_COMBAT_ONLY
    require(probesOnly, "combat-only build cannot simulate battles");
#endif
    while (std::cin >> current.id >> current.player >> current.level >> current.enemy >>
           current.enemyLevel >> current.seed >> current.seedIndex) {
        require(combat::validFormProfile(current.player, current.level) &&
                combat::validFormProfile(current.enemy, current.enemyLevel), "case profile invalid");
        if (current.seedIndex == 0) damage();
#ifndef WORLD_DS_COMBAT_ONLY
        if (!probesOnly) {
            practiceRun(false); practiceRun(true);
            for (const bool full : {false, true})
                for (const auto policy : {Policy::Auto, Policy::Greedy, Policy::Economy, Policy::IgnoreGuard})
                    wildRun(policy, full);
        }
#endif
    }
    require(std::cin.eof(), "malformed audit case input");
    return 0;
}
