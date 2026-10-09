// Bounded host-only balance probes. All state transitions/damage use native rules.
// Fixtures set explicit progression for matchup isolation; they are never saves.
#include "game.hpp"
#include "practice_battle.hpp"
#include "combat.hpp"
#include "forms.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

namespace game = digivice;
namespace combat = digivice::combat;
namespace practice = digivice::practice;
namespace forms = digivice::forms;
void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "balance simulation failed: %s\n", message); std::exit(1); }
}
struct Actor { std::uint32_t lineage, form, level; const char* route; };
std::vector<Actor> actors() {
    std::vector<Actor> result;
    for (std::uint32_t lineage = 5; lineage <= 12; ++lineage)
        for (const auto level : {1u, 5u, 10u, 15u, 20u})
            result.push_back({lineage, forms::initialForm(lineage), level, "rookie"});
    // Two authored Agumon branches; a sample, not coverage of all 66 forms.
    const auto base = forms::initialForm(6);
    for (const auto level : {5u, 10u, 15u, 20u}) {
        const auto offset = level < 10 ? 1u : level < 15 ? 2u : 3u;
        result.push_back({6, base + offset, level, "branch-a"});
        result.push_back({6, base + offset + 3, level, "branch-b"});
    }
    return result;
}
std::uint32_t enemyLevel(std::uint32_t level, int delta) {
    return static_cast<std::uint32_t>(std::clamp(static_cast<int>(level) + delta, 1, 20));
}
std::uint32_t seedFor(unsigned index) { return 0x9e3779b9u * (index + 1u) ^ 0x51f15e5du; }

// Explicit public projection: this type cannot hold hidden choice or RNG state.
struct PublicPractice {
    practice::Phase phase;
    practice::Move excluded;
    std::uint32_t playerForm, playerLevel, enemyForm, enemyLevel;
    std::uint32_t playerHp, enemyHp;
};
PublicPractice publicView(const practice::State& s) {
    return {s.phase, s.excludedChoice, s.playerFormId, s.playerLevel,
        s.enemyFormId, s.enemyLevel, s.playerHp, s.enemyHp};
}
practice::Move chooseTactical(const PublicPractice& p) {
    // Greedy expected HP swing over the two public possibilities, equal weight.
    // Uses resolveForms; fixed tie order; no rollout, private commitment, or RNG.
    const bool attacking = p.phase == practice::Phase::Attack;
    const unsigned first = attacking ? 1 : 4;
    const unsigned enemyFirst = attacking ? 4 : 1;
    auto best = static_cast<practice::Move>(first);
    int bestScore = -100000;
    for (unsigned choice = first; choice < first + 3; ++choice) {
        int score = 0;
        for (unsigned opponent = enemyFirst; opponent < enemyFirst + 3; ++opponent) {
            if (opponent == static_cast<unsigned>(p.excluded)) continue;
            const auto move = static_cast<combat::Move>((attacking ? choice : opponent) - 1);
            const auto defense = static_cast<combat::Defense>((attacking ? opponent : choice) - 3);
            const auto hit = combat::resolveForms(attacking ? p.playerForm : p.enemyForm,
                attacking ? p.playerLevel : p.enemyLevel, attacking ? p.enemyForm : p.playerForm,
                attacking ? p.enemyLevel : p.playerLevel, move, defense);
            const bool hurtsEnemy = attacking != hit.reflected;
            const auto hp = hurtsEnemy ? p.enemyHp : p.playerHp;
            score += static_cast<int>(std::min(hit.damage, hp)) * (hurtsEnemy ? 1 : -1);
        }
        if (score > bestScore) { bestScore = score; best = static_cast<practice::Move>(choice); }
    }
    return best;
}
const char* outcome(practice::Status status) {
    switch (status) {
    case practice::Status::Won: return "won";
    case practice::Status::Lost: return "lost";
    case practice::Status::Draw: return "draw";
    case practice::Status::Retreated: return "retreated";
    default: return "active";
    }
}
void common(const char* kind, const Actor& actor, std::uint32_t rival, int delta,
            std::uint32_t opponentLevel) {
    std::printf("{\"kind\":\"%s\",\"lineage\":%u,\"form\":%u,\"route\":\"%s\",\"level\":%u,"
        "\"rival\":%u,\"delta\":%d,\"enemyLevel\":%u,", kind, actor.lineage, actor.form,
        actor.route, actor.level, rival, delta, opponentLevel);
}
void practiceRun(const Actor& actor, std::uint32_t rival, int delta, unsigned seedIndex,
                 bool automatic, unsigned card = 0) {
    const auto rivalLevel = enemyLevel(actor.level, delta);
    auto state = practice::newBattleWithForms(seedFor(seedIndex), actor.lineage, actor.level,
        actor.form, rival, rivalLevel, forms::initialForm(rival));
    require(practice::isValid(state), "invalid practice fixture");
    const auto initialHp = state.playerHp;
    std::array<unsigned, 7> choices{};
    if (automatic) {
        require(card == 0, "Auto must not gain a simulated card");
        digivice::autobattle::Trace trace;
        require(practice::runAuto(state, trace) == practice::Error::None, "native practice Auto failed");
        for (std::size_t i = 0; i < trace.count; ++i) {
            const auto action = static_cast<unsigned>(trace.steps[i].action);
            require(action < choices.size(), "practice trace action out of range");
            ++choices[action];
        }
    } else {
        if (card) require(practice::apply(state, "card", card) == practice::Error::None, "card rejected");
        while (state.status == practice::Status::Active && state.exchanges <= practice::kMaxExchanges) {
            const auto move = chooseTactical(publicView(state));
            ++choices[static_cast<unsigned>(move)];
            require(practice::apply(state, practice::moveName(move)) == practice::Error::None,
                    "public-hint Tactical action rejected");
        }
    }
    require(state.status != practice::Status::Active, "practice failed to terminate");
    common("practice", actor, rival, delta, rivalLevel);
    std::printf("\"seedIndex\":%u,\"policy\":\"%s\",\"card\":%u,\"outcome\":\"%s\","
        "\"turns\":%u,\"initialHp\":%u,\"hp\":%u,\"choices\":[%u,%u,%u,%u,%u,%u]}\n",
        seedIndex, automatic ? "auto" : "public-hint", card, outcome(state.status), state.exchanges,
        initialHp, state.playerHp, choices[1], choices[2], choices[3], choices[4], choices[5], choices[6]);
}
void damageProbe(const Actor& actor, std::uint32_t rival, int delta) {
    const auto rivalLevel = enemyLevel(actor.level, delta);
    const auto rivalForm = forms::initialForm(rival);
    std::array<unsigned, 3> outgoing{}, incoming{};
    for (unsigned i = 0; i < 3; ++i) {
        outgoing[i] = combat::resolveForms(actor.form, actor.level, rivalForm, rivalLevel,
            static_cast<combat::Move>(i), combat::Defense::None).damage;
        incoming[i] = combat::resolveForms(rivalForm, rivalLevel, actor.form, actor.level,
            static_cast<combat::Move>(i), combat::Defense::None).damage;
    }
    const auto stats = forms::stats(actor.form, actor.level);
    common("damage", actor, rival, delta, rivalLevel);
    std::printf("\"hp\":%u,\"outgoing\":[%u,%u,%u],\"incoming\":[%u,%u,%u]}\n", stats.maxHp,
        outgoing[0], outgoing[1], outgoing[2], incoming[0], incoming[1], incoming[2]);
}

game::Action wildChoice(const game::State& s, bool economy, bool capture) {
    if (capture && s.collectionCount < game::kCollectionCapacity && s.captureAttempts < 3 &&
        s.wildHp <= s.wildMaxHp / 2) return game::Action::Capture;
    const auto& member = s.collection[s.activeCreatureId - 1];
    const auto rivalForm = forms::initialForm(static_cast<unsigned>(s.wildSpecies));
    auto best = game::Action::Attack;
    unsigned bestDamage = 0;
    // Ties prefer a 2-energy move; Heavy must provide an actual damage benefit.
    for (const auto action : {game::Action::Attack, game::Action::Magic, game::Action::Heavy}) {
        if (action == game::Action::Heavy && (economy || s.energy < 6)) continue;
        const auto move = action == game::Action::Attack ? combat::Move::Physical :
            action == game::Action::Magic ? combat::Move::Magic : combat::Move::Heavy;
        const auto damage = combat::resolveForms(member.formId, member.level, rivalForm,
            s.wildLevel, move, combat::Defense::None).damage;
        if (damage > bestDamage) { best = action; bestDamage = damage; }
    }
    return best;
}
game::State wildFixture(const Actor& actor, unsigned rival, int delta, unsigned seedIndex,
                       bool automatic, bool full, unsigned energy) {
    auto state = game::newDevice(seedFor(seedIndex));
    require(game::apply(state, game::Action::Hatch, actor.lineage - 4) == game::Error::None, "hatch fixture");
    auto& member = state.collection[0];
    member.xp = game::xpForLevel(actor.level); member.level = actor.level; member.formId = actor.form;
    member.hp = forms::stats(actor.form, actor.level).maxHp; member.energy = energy;
    member.bond = forms::find(actor.form)->minBond;
    state.hp = member.hp; state.energy = member.energy; state.bond = member.bond; state.level = member.level;
    if (full) {
        state.sequence = 100; state.encounters = state.captures = 7; state.steps = 700;
        state.collectionCount = game::kCollectionCapacity;
        for (unsigned i = 1; i < game::kCollectionCapacity; ++i) {
            auto& captured = state.collection[i];
            captured = member; captured.id = i + 1; captured.species = game::Species::Flicker;
            captured.formId = forms::initialForm(2); captured.level = 1; captured.xp = 0;
            captured.hp = forms::stats(captured.formId, 1).maxHp; captured.bond = 0;
            captured.capturedAtSequence = i + 1;
        }
    }
    require(game::isValid(state), "invalid home fixture");
    if (automatic) require(game::apply(state, game::Action::Mode, 1) == game::Error::None, "Auto mode fixture");
    require(game::apply(state, game::Action::Walk, 100) == game::Error::None, "walk fixture");
    // Isolate the authored rival and level delta; preserve every state invariant.
    state.wildSpecies = static_cast<game::Species>(rival);
    state.wildLevel = enemyLevel(actor.level, delta);
    state.wildHp = state.wildMaxHp = forms::stats(forms::initialForm(rival), state.wildLevel).maxHp;
    require(game::isValid(state), "invalid encounter fixture");
    return state;
}
void wildRun(const Actor& actor, unsigned rival, int delta, unsigned seedIndex,
             const char* policy, bool full = false, unsigned energy = 80) {
    const bool automatic = policy[0] == 'a', economy = policy[0] == 'e';
    auto state = wildFixture(actor, rival, delta, seedIndex, automatic, full, energy);
    const auto initialHp = state.hp, initialXp = state.collection[0].xp, initialSequence = state.sequence;
    const auto initialCaptures = state.captures;
    const auto rivalLevel = state.wildLevel;
    unsigned turns = 0, physical = 0, heavy = 0, magic = 0, capture = 0;
    if (automatic) {
        game::autobattle::Trace trace;
        require(game::applyAuto(state, &trace) == game::Error::None, "native wild Auto failed");
        turns = static_cast<unsigned>(trace.count);
        for (std::size_t i = 0; i < trace.count; ++i) {
            switch (trace.steps[i].action) {
            case game::autobattle::Move::Physical: ++physical; break;
            case game::autobattle::Move::Heavy: ++heavy; break;
            case game::autobattle::Move::Magic: ++magic; break;
            case game::autobattle::Move::Capture: ++capture; break;
            default: require(false, "unexpected wild trace action");
            }
        }
        require(state.sequence == initialSequence + 1, "Auto did not commit once");
    } else while (state.phase == game::Phase::Encounter && turns < 100) {
        const auto action = wildChoice(state, economy, !full);
        physical += action == game::Action::Attack; heavy += action == game::Action::Heavy;
        magic += action == game::Action::Magic; capture += action == game::Action::Capture;
        require(game::apply(state, action) == game::Error::None, "native wild action failed"); ++turns;
    }
    require(state.phase == game::Phase::Home, "wild failed to terminate");
    // Level-up can replace the presentation message with Trained; inspect the
    // durable capture counter before using the terminal reward message.
    const char* result = state.captures > initialCaptures ? "captured" :
        state.message == game::Message::Won || state.message == game::Message::Trained ? "won" : "retreated";
    const auto hp = state.hp, remainingEnergy = state.energy, xpAwarded = state.collection[0].xp - initialXp;
    unsigned rests = 0;
    const auto maximum = forms::stats(state.collection[0].formId, state.level).maxHp;
    while ((state.hp < maximum || state.energy < energy) && rests < 30) {
        require(game::apply(state, game::Action::Rest) == game::Error::None, "recovery rest failed"); ++rests;
    }
    common("wild", actor, rival, delta, rivalLevel);
    std::printf("\"seedIndex\":%u,\"policy\":\"%s\",\"full\":%s,\"startEnergy\":%u,"
        "\"outcome\":\"%s\",\"turns\":%u,\"initialHp\":%u,\"hp\":%u,"
        "\"energySpent\":%u,\"recoveryRests\":%u,\"xpAwarded\":%u,\"choices\":[%u,%u,%u,%u]}\n",
        seedIndex, policy, full ? "true" : "false", energy, result, turns, initialHp, hp,
        energy - remainingEnergy, rests, xpAwarded, physical, heavy, magic, capture);
}
void progression(const Actor& actor, unsigned branch) {
    auto state = game::newDevice(seedFor(0));
    require(game::apply(state, game::Action::Hatch, actor.lineage - 4) == game::Error::None, "progression hatch");
    unsigned rests = 0, victories = 0, retreats = 0, lastMilestone = 1;
    for (unsigned encounters = 1; encounters <= 500 && state.level < game::kMaxLevel; ++encounters) {
        require(game::apply(state, game::Action::Walk, 100) == game::Error::None, "progression walk");
        unsigned turns = 0;
        while (state.phase == game::Phase::Encounter && turns++ < 100)
            require(game::apply(state, wildChoice(state, false, false)) == game::Error::None, "progression fight");
        require(state.phase == game::Phase::Home, "progression fight bound");
        victories += state.message == game::Message::Won || state.message == game::Message::Trained;
        retreats += state.message == game::Message::Retreated;
        if (branch) {
            const auto* form = forms::find(state.collection[0].formId);
            const auto child = form->children[form->stage == forms::Stage::Rookie ? branch - 1 : 0];
            const auto* next = forms::find(child);
            if (next && state.level >= next->minLevel && state.bond >= next->minBond)
                require(game::apply(state, game::Action::Evolve, child) == game::Error::None, "progression evolution");
        }
        if ((state.level >= 5 && lastMilestone < 5) || (state.level >= 10 && lastMilestone < 10) ||
            (state.level >= 15 && lastMilestone < 15) || (state.level == 20 && lastMilestone < 20)) {
            lastMilestone = state.level;
            std::printf("{\"kind\":\"progression\",\"lineage\":%u,\"branch\":%u,\"level\":%u,\"form\":%u,"
                "\"xp\":%u,\"bond\":%u,\"encounters\":%u,\"victories\":%u,\"retreats\":%u,\"rests\":%u,\"sequence\":%u}\n",
                actor.lineage, branch, state.level, state.collection[0].formId, state.collection[0].xp,
                state.bond, encounters, victories, retreats, rests, state.sequence);
        }
        const auto maxHp = forms::stats(state.collection[0].formId, state.level).maxHp;
        while (state.hp < maxHp || state.energy < 80) {
            require(game::apply(state, game::Action::Rest) == game::Error::None, "progression recovery"); ++rests;
        }
    }
    require(state.level == game::kMaxLevel, "progression could not reach level cap in 500 encounters");
}
void freeCare() {
    for (const auto action : {game::Action::Feed, game::Action::Rest, game::Action::Play}) {
        auto state = game::newDevice(seedFor(0));
        require(game::apply(state, game::Action::Hatch, 1) == game::Error::None, "care hatch");
        for (unsigned i = 0; i < 200; ++i) {
            if (action == game::Action::Play && state.energy < 5)
                require(game::apply(state, game::Action::Rest) == game::Error::None, "care recovery");
            require(game::apply(state, action) == game::Error::None, "care cycle");
        }
        require(state.collection[0].xp == 0 && state.level == 1, "free care generated XP or levels");
        std::printf("{\"kind\":\"free-care\",\"action\":%u,\"actions\":200,\"level\":%u,\"xp\":%u,\"bond\":%u}\n",
            static_cast<unsigned>(action), state.level, state.collection[0].xp, state.bond);
    }
}

int main() {
    const auto samples = actors();
    for (const auto& actor : samples) {
        require(combat::validFormProfile(actor.form, actor.level), "invalid actor profile");
        const auto profile = combat::formProfile(actor.form, actor.level);
        std::printf("{\"kind\":\"profile\",\"lineage\":%u,\"form\":%u,\"route\":\"%s\",\"level\":%u,"
            "\"name\":\"%s\",\"type\":\"%s\",\"stats\":{\"maxHp\":%u,\"attack\":%u,\"defense\":%u,\"magic\":%u,\"resistance\":%u}}\n",
            actor.lineage, actor.form, actor.route, actor.level, profile.name, profile.type,
            profile.stats.maxHp, profile.stats.attack, profile.stats.defense, profile.stats.magic, profile.stats.resistance);
        for (std::uint32_t rival = 2; rival <= 4; ++rival)
            for (const auto delta : {-2, 0, 2}) {
                damageProbe(actor, rival, delta);
                for (unsigned seed = 0; seed < 16; ++seed) {
                    practiceRun(actor, rival, delta, seed, true);
                    practiceRun(actor, rival, delta, seed, false);
                    for (const auto* policy : {"auto", "greedy", "economy"})
                        wildRun(actor, rival, delta, seed, policy);
                }
            }
        if (actor.route[0] == 'r' && actor.level == 5)
            for (std::uint32_t rival = 2; rival <= 4; ++rival)
                for (unsigned seed = 0; seed < 16; ++seed)
                    for (const unsigned card : {1u, 2u}) practiceRun(actor, rival, 0, seed, false, card);
    }
    // Focused energy/full-collection probes, not an all-axis Cartesian expansion.
    for (const auto& actor : samples) if (actor.route[0] == 'r' && actor.level == 5)
        for (unsigned rival = 2; rival <= 4; ++rival)
            for (unsigned seed = 0; seed < 16; ++seed)
                for (const auto* policy : {"auto", "greedy", "economy"}) {
                    wildRun(actor, rival, 0, seed, policy, true);
                    if (seed == 0) for (const auto energy : {0u, 5u, 6u})
                        wildRun(actor, rival, 0, seed, policy, false, energy);
                }
    for (unsigned lineage = 5; lineage <= 12; ++lineage)
        progression({lineage, forms::initialForm(lineage), 1, "rookie"}, 0);
    for (unsigned branch : {1u, 2u}) progression({6, forms::initialForm(6), 1, "branch"}, branch);
    freeCare();
    return 0;
}
