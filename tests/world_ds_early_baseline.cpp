// Small before/after probe. Compile this same fixture/policy with either core.
// Every damage calculation and transition is native. Fixtures are never saved.
#include "game.hpp"
#include "combat.hpp"
#include "forms.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
namespace g = digivice;
namespace c = digivice::combat;
namespace f = digivice::forms;
unsigned player, level, enemy, seedIndex, seed;
void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "early baseline failed %u/%u vs%u seed%u: %s\n", player, level, enemy, seed, message); std::exit(1); }
}
c::Defense guard(const g::State& state) {
#ifdef WORLD_DS_OLD_BASELINE
    (void)state; return c::Defense::None;
#else
    return g::wildGuard(state);
#endif
}
g::State fixture(bool automatic, bool full) {
    auto state = g::newDevice(seed);
    const auto* profile = f::find(player);
    require(g::apply(state, g::Action::Hatch, profile->lineage - 4) == g::Error::None, "hatch");
    auto& member = state.collection[0];
    member.xp = g::xpForLevel(level); member.level = level; member.formId = player;
    member.hp = f::stats(player, level).maxHp; member.energy = 80; member.bond = profile->minBond;
    member.fullness = member.mood = 100;
    state.sequence = 100; state.encounters = 7 + seed % 3; state.steps = state.encounters * 100;
    state.hp = member.hp; state.energy = member.energy; state.bond = member.bond; state.level = member.level;
    state.fullness = member.fullness; state.mood = member.mood;
    if (full) {
        state.captures = 7; state.collectionCount = 8;
        for (unsigned i = 1; i < 8; ++i) {
            auto& captured = state.collection[i]; captured = member;
            captured.id = i + 1; captured.species = g::Species::Flicker;
            captured.formId = f::initialForm(2); captured.level = 1; captured.xp = 0;
            captured.hp = f::stats(captured.formId, 1).maxHp; captured.bond = 0;
            captured.capturedAtSequence = i + 1;
        }
    }
#ifndef WORLD_DS_OLD_BASELINE
    state.nextMemberId = full ? 9 : 2;
    if (full) state.journal[0] |= 1u << (f::initialForm(2) - 1);
#endif
    state.battleMode = automatic ? g::BattleMode::Auto : g::BattleMode::Tactical;
    require(g::isValid(state), "home fixture");
    require(g::apply(state, g::Action::Walk, 100) == g::Error::None, "walk");
    state.wildSpecies = static_cast<g::Species>(f::find(enemy)->lineage);
    state.wildLevel = level; state.wildHp = state.wildMaxHp = f::stats(enemy, level).maxHp;
#ifndef WORLD_DS_OLD_BASELINE
    state.wildFormId = enemy; state.wildRules = g::kRulesVersion;
#endif
    require(g::isValid(state), "encounter fixture"); return state;
}
g::Action choose(const g::State& state, bool full, bool economy) {
    if (!full && state.wildHp <= state.wildMaxHp / 2 && state.captureAttempts < 3) return g::Action::Capture;
    auto best = g::Action::Attack; int score = -100000;
    for (const auto action : {g::Action::Attack, g::Action::Magic, g::Action::Heavy}) {
        if (action == g::Action::Heavy && (economy || state.energy < 6)) continue;
        const auto move = action == g::Action::Attack ? c::Move::Physical : action == g::Action::Heavy ? c::Move::Heavy : c::Move::Magic;
        const auto hit = c::resolveForms(player, level, enemy, level, move, guard(state));
        const int value = static_cast<int>(std::min(hit.damage, hit.reflected ? state.hp : state.wildHp)) * (hit.reflected ? -1 : 1);
        if (value > score) { best = action; score = value; }
    }
    return best;
}
void fight(unsigned policy, bool full) {
    auto state = fixture(policy == 0, full);
    const auto initialCaptures = state.captures, initialSequence = state.sequence;
    const auto initialRng = state.rngState, initialXp = state.collection[0].xp;
    unsigned turns = 0, physical = 0, heavy = 0, magic = 0, capture = 0;
    unsigned braces = 0, wards = 0, counters = 0;
    if (policy == 0) {
        g::autobattle::Trace trace;
        require(g::applyAuto(state, &trace) == g::Error::None, "Auto");
        require(state.sequence == initialSequence + 1, "single Auto commit");
        turns = static_cast<unsigned>(trace.count);
        for (unsigned i = 0; i < turns; ++i) {
            const auto move = trace.steps[i].action;
            physical += move == g::autobattle::Move::Physical; heavy += move == g::autobattle::Move::Heavy;
            magic += move == g::autobattle::Move::Magic; capture += move == g::autobattle::Move::Capture;
#ifndef WORLD_DS_OLD_BASELINE
            braces += trace.steps[i].guard == g::autobattle::Move::Brace;
            wards += trace.steps[i].guard == g::autobattle::Move::Ward;
            counters += trace.steps[i].guard == g::autobattle::Move::Counter;
#endif
        }
    } else while (state.phase == g::Phase::Encounter && turns < 100) {
        const auto action = choose(state, full, policy == 2);
        const auto stance = guard(state);
        braces += stance == c::Defense::Brace; wards += stance == c::Defense::Ward; counters += stance == c::Defense::Counter;
        physical += action == g::Action::Attack; heavy += action == g::Action::Heavy;
        magic += action == g::Action::Magic; capture += action == g::Action::Capture;
        require(g::apply(state, action) == g::Error::None, "Tactical action"); ++turns;
    }
    require(state.phase == g::Phase::Home && g::isValid(state), "terminal state");
    const char* outcome = state.captures > initialCaptures ? "captured" : state.message == g::Message::Retreated ? "retreated" : "won";
    const auto hp = state.hp, energy = 80 - state.energy, xp = state.collection[0].xp - initialXp;
    unsigned rests = 0;
    while ((state.hp < f::stats(player, state.level).maxHp || state.energy < 80) && rests < 30) {
        require(g::apply(state, g::Action::Rest) == g::Error::None, "recovery"); ++rests;
    }
    require(rests < 30, "recovery bound");
    std::printf("{\"rules\":%u,\"player\":%u,\"level\":%u,\"enemy\":%u,\"seedIndex\":%u,\"seed\":%u,\"fightRng\":%u,"
                "\"policy\":\"%s\",\"objective\":\"%s\",\"outcome\":\"%s\",\"turns\":%u,\"hpAfterHome\":%u,"
                "\"energySpent\":%u,\"recoveryRests\":%u,\"xp\":%u,\"choices\":[%u,%u,%u,%u],\"guards\":[%u,%u,%u]}\n",
                g::kRulesVersion, player, level, enemy, seedIndex, seed, initialRng,
                policy == 0 ? "auto" : policy == 1 ? "public-greedy" : "public-economy",
                full ? "defeat-full-collection" : "capture-first", outcome, turns, hp, energy, rests, xp,
                physical, heavy, magic, capture, braces, wards, counters);
}
int main() {
    while (std::cin >> player >> level >> enemy >> seedIndex >> seed)
        for (const bool full : {false, true}) for (unsigned policy = 0; policy < 3; ++policy) fight(policy, full);
    require(std::cin.eof(), "case input"); return 0;
}
