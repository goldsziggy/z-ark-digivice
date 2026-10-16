#include "game.hpp"
#include "forms.hpp"
#include "expeditions.hpp"
#include "capture_ring.hpp"
#include <cstdio>
#include <cstring>
#include <limits>

using namespace digivice;
namespace {
unsigned checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (false)

State ready(unsigned seed = 91, unsigned level = 20) {
    auto s = newDevice(seed);
    CHECK(apply(s, Action::Hatch, 1) == Error::None);
    auto& m = s.collection[0];
    m.level = s.level = level; m.xp = xpForLevel(level);
    m.hp = s.hp = combat::formProfile(m.formId, level).stats.maxHp;
    m.energy = s.energy = 100;
    s.dungeonKeys = 3; s.bossSigils = 1;
    CHECK(isValid(s)); return s;
}
bool same(const State& a, const State& b) {
    Snapshot x, y;
    return encodeSnapshot(a, x) && encodeSnapshot(b, y) && !std::memcmp(x.bytes, y.bytes, kSnapshotSize);
}
void rejected(State& s, Action action, unsigned value, Error expected) {
    const auto before = s;
    CHECK(apply(s, action, value) == expected); CHECK(same(s, before));
}
void roundTrip(State& s) {
    Snapshot bytes; CHECK(encodeSnapshot(s, bytes));
    State restored; CHECK(decodeSnapshot(bytes.bytes, sizeof(bytes.bytes), restored) == SnapshotStatus::Ok);
    CHECK(same(s, restored)); s = restored;
}
void finishHit(State& s) {
    // A legitimate nearly-defeated foe isolates the terminal transition from
    // balancing. Physical never reflects, so the stored one HP must resolve.
    s.wildHp = 1; CHECK(isValid(s));
    CHECK(apply(s, Action::Attack) == Error::None); CHECK(isValid(s));
}
void finishCapture(State& s) {
    s.wildHp = 1; s.captureDeferred = 0; CHECK(isValid(s));
    bool caught = false;
    for (unsigned seed = 1; seed <= 256 && !caught; ++seed) {
        auto candidate = s; candidate.rngState = seed;
        if (apply(candidate, Action::Capture) == Error::None && candidate.message == Message::Captured) {
            s = candidate; caught = true;
        }
    }
    CHECK(caught); CHECK(isValid(s));
}

void threeTiersAndAccounting() {
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        auto s = ready(130 + scenario);
        const unsigned tiers = scenario < 3 ? 3 : 1;
        const auto initialXp = s.collection[0].xp, encounters = s.encounters;
        unsigned expectedXp = initialXp;
        CHECK(apply(s, Action::StartExpedition, scenario) == Error::None);
        CHECK(s.expeditionScenario == scenario + 1 && s.expeditionTier == 1);
        CHECK(s.dungeonKeys == (scenario < 3 ? 2u : 3u) && s.bossSigils == (scenario < 3 ? 1u : 0u));
        const auto form = s.expeditionForm, level = s.expeditionLevel;
        for (unsigned tier = 1; tier <= tiers; ++tier) {
            CHECK(s.phase == Phase::Encounter && s.expeditionTier == tier);
            CHECK(s.wildFormId == form && s.wildLevel == expeditions::tierLevel(level, tier));
            CHECK(s.encounters == encounters + tier);
            roundTrip(s);
            rejected(s, Action::NextExpedition, 0, Error::WrongPhase);
            rejected(s, Action::Walk, 1, Error::WrongPhase);
            rejected(s, Action::PresentEncounter, 0, Error::WrongPhase);
            expectedXp += 20 + 6 * s.wildLevel;
            finishHit(s);
            CHECK(s.collection[0].xp == expectedXp);
            CHECK(s.dungeonKeys == (scenario < 3 ? 2u : 3u));
            CHECK(s.bossSigils == (scenario < 3 ? 1u : 0u));
            if (tier < tiers) {
                CHECK(s.phase == Phase::Expedition && s.expeditionTier == tier);
                CHECK(!s.wildFormId && !s.wildHp && !s.dungeonClears && !s.bossClears && !s.dungeonWins);
                roundTrip(s);
                rejected(s, Action::StartExpedition, scenario, Error::WrongPhase);
                rejected(s, Action::ClaimExpedition, expeditions::claimExpeditionValue(scenario, form, level), Error::WrongPhase);
                rejected(s, Action::Feed, 0, Error::WrongPhase);
                rejected(s, Action::Select, s.activeCreatureId, Error::WrongPhase);
                rejected(s, Action::Walk, 100, Error::WrongPhase);
                rejected(s, Action::NextExpedition, 1, Error::InvalidValue);
                const auto hp = s.hp, energy = s.energy;
                CHECK(apply(s, Action::NextExpedition) == Error::None);
                CHECK(s.hp == hp && s.energy == energy);
            } else {
                CHECK(s.phase == Phase::Home && !s.expeditionScenario && !s.expeditionTier && !s.expeditionForm && !s.expeditionLevel);
                CHECK(s.dungeonClears == (scenario < 3 ? 1u : 0u) && s.bossClears == (scenario < 3 ? 0u : 1u));
                CHECK(s.dungeonWins == 1);
                rejected(s, Action::NextExpedition, 0, Error::WrongPhase);
                roundTrip(s);
            }
        }
    }
}

void capturesResolveTiers() {
    auto s = ready(140);
    CHECK(apply(s, Action::StartExpedition, 0) == Error::None);
    const auto count = s.collectionCount;
    for (unsigned tier = 1; tier <= 3; ++tier) {
        const auto beforeCaptures = s.captures;
        const auto expectedXp = s.collection[0].xp + 20 + 6 * s.wildLevel;
        finishCapture(s);
        CHECK(s.captures == beforeCaptures + 1 && s.collectionCount == count + 1);
        CHECK(s.collection[0].xp == expectedXp);
        CHECK(s.lastCapture.result == CaptureResult::Captured);
        CHECK(s.dungeonWins == (tier == 3 ? 1u : 0u) && s.dungeonClears == (tier == 3 ? 1u : 0u));
        CHECK(s.dungeonKeys == 2);
        roundTrip(s);
        if (tier < 3) CHECK(apply(s, Action::NextExpedition) == Error::None);
    }
    CHECK(s.phase == Phase::Home);
}

void interruptionAndBackgroundSteps() {
    for (unsigned tier = 1; tier <= 3; ++tier) {
        auto s = ready(150);
        CHECK(apply(s, Action::AccrueSteps, 1000) == Error::None);
        CHECK(s.pendingEncounter.formId);
        const auto pending = s.pendingEncounter;
        CHECK(apply(s, Action::StartExpedition, 1) == Error::None);
        for (unsigned at = 1; at < tier; ++at) {
            finishHit(s); CHECK(apply(s, Action::NextExpedition) == Error::None);
        }
        CHECK(apply(s, Action::AccrueSteps, 17) == Error::None);
        CHECK(s.pendingEncounter.formId == pending.formId && s.pendingEncounter.level == pending.level && s.pendingEncounter.rules == pending.rules);
        CHECK(s.expeditionTier == tier && s.phase == Phase::Encounter);
        roundTrip(s);
        CHECK(apply(s, Action::Retreat) == Error::None);
        CHECK(!s.expeditionScenario && s.phase == Phase::Home && s.dungeonKeys == 2 && !s.dungeonClears && !s.dungeonWins);
        CHECK(s.pendingEncounter.formId == pending.formId);
        CHECK(apply(s, Action::PresentEncounter) == Error::None && !s.expeditionScenario);
    }
    auto s = ready(152);
    CHECK(apply(s, Action::StartExpedition, 2) == Error::None); finishHit(s);
    const auto hp = s.hp;
    CHECK(apply(s, Action::AccrueSteps, 1000) == Error::None);
    CHECK(apply(s, Action::CareMinute, 1) == Error::None);
    CHECK(s.phase == Phase::Expedition && s.expeditionTier == 1 && s.pendingEncounter.formId);
    rejected(s, Action::PresentEncounter, 0, Error::WrongPhase);
    roundTrip(s);
    CHECK(apply(s, Action::Retreat) == Error::None && s.hp == hp && !s.expeditionScenario && !s.dungeonClears && s.dungeonKeys == 2);
}

void knockoutAndFinalKeyThreshold() {
    for (unsigned tier = 1; tier <= 3; ++tier) {
        auto s = ready(191);
        CHECK(apply(s, Action::StartExpedition, 0) == Error::None);
        for (unsigned at = 1; at < tier; ++at) {
            finishHit(s); CHECK(apply(s, Action::NextExpedition) == Error::None);
        }
        s.hp = s.collection[0].hp = 1;
        CHECK(isValid(s)); CHECK(apply(s, Action::Attack) == Error::None);
        CHECK(s.phase == Phase::Home && s.message == Message::Retreated && isInjured(s.collection[0]));
        CHECK(!s.expeditionScenario && !s.dungeonClears && !s.dungeonWins && s.dungeonKeys == 2);
        roundTrip(s);
    }
    auto s = ready(193); s.dungeonWins = 7;
    CHECK(apply(s, Action::StartExpedition, 2) == Error::None);
    for (unsigned tier = 1; tier <= 3; ++tier) {
        finishHit(s);
        CHECK(s.dungeonKeys == (tier == 3 ? 3u : 2u));
        CHECK(s.dungeonWins == (tier == 3 ? 0u : 7u));
        if (tier < 3) CHECK(apply(s, Action::NextExpedition) == Error::None);
    }
    CHECK(s.dungeonClears == 1);
}

void autoStopsAtBoundary() {
    auto s = ready(161);
    CHECK(apply(s, Action::Mode, 1) == Error::None);
    CHECK(apply(s, Action::StartExpedition, 0) == Error::None);
    for (unsigned tier = 1; tier <= 3; ++tier) {
        s.wildHp = 1; CHECK(isValid(s));
        const auto form = s.wildFormId, level = s.wildLevel, seq = s.sequence;
        autobattle::Trace trace;
        CHECK(applyAutoFight(s, &trace) == Error::None);
        CHECK(trace.count == 1 && trace.outcome == autobattle::Outcome::Won && trace.enemyFormId == form && trace.enemyLevel == level);
        CHECK(trace.steps[0].enemyHpAfter == 0 && s.sequence == seq + 1);
        CHECK(s.battleMode == BattleMode::Auto && s.lastAutoOutcome == autobattle::Outcome::Won);
        CHECK(s.phase == (tier < 3 ? Phase::Expedition : Phase::Home));
        if (tier < 3) {
            rejected(s, Action::AutoFight, 0, Error::WrongPhase);
            CHECK(apply(s, Action::NextExpedition) == Error::None);
        }
    }
    CHECK(s.dungeonClears == 1 && s.dungeonWins == 1 && s.dungeonKeys == 2);
}

void invalidStateAndOverflow() {
    auto s = ready(170); CHECK(apply(s, Action::StartExpedition, 0) == Error::None);
    auto bad = s; bad.expeditionTier = 4; CHECK(!isValid(bad));
    bad = s; bad.expeditionScenario = 7; CHECK(!isValid(bad));
    bad = s; bad.expeditionForm = 13; CHECK(!isValid(bad));
    bad = s; ++bad.wildLevel; CHECK(!isValid(bad));
    bad = s; bad.wildRules = 20; CHECK(!isValid(bad));
    bad = s; bad.expeditionScenario = 0; CHECK(!isValid(bad));
    for (unsigned tier = 1; tier < 3; ++tier) { finishHit(s); CHECK(apply(s, Action::NextExpedition) == Error::None); }
    s.dungeonClears = std::numeric_limits<std::uint32_t>::max(); s.wildHp = 1;
    CHECK(isValid(s)); rejected(s, Action::Attack, 0, Error::CounterOverflow);
    auto claim = ready(172); claim.bossClears = std::numeric_limits<std::uint32_t>::max();
    rejected(claim, Action::ClaimExpedition, expeditions::claimExpeditionValue(3, 18, 1), Error::CounterOverflow);
    CHECK(expeditions::tierLevel(99, 3) == 99 && expeditions::tierCount(6) == 0);
}

void projectionAndOrdinaryEncounter() {
    Action parsed;
    CHECK(parseAction("next-expedition", parsed) && parsed == Action::NextExpedition);
    auto s = ready(180); static char json[kJsonCapacity];
    CHECK(writeJson(s, json, sizeof(json)) && std::strstr(json, "\"active\":false,\"scenarioIndex\":null,\"tier\":0,\"tiers\":0,\"awaitingNext\":false"));
    CHECK(apply(s, Action::StartExpedition, 0) == Error::None); finishHit(s);
    CHECK(writeJson(s, json, sizeof(json)) && std::strstr(json, "\"phase\":\"expedition\"") && std::strstr(json, "\"active\":true,\"scenarioIndex\":0,\"tier\":1,\"tiers\":3,\"awaitingNext\":true"));
    auto ordinary = ready(182);
    CHECK(apply(ordinary, Action::Walk, 100) == Error::None);
    CHECK(!ordinary.expeditionScenario); finishHit(ordinary);
    CHECK(ordinary.phase == Phase::Home && ordinary.dungeonWins == 1 && !ordinary.dungeonClears && !ordinary.bossClears && ordinary.dungeonKeys == 3);
    auto caught = ready(185);
    CHECK(apply(caught, Action::Walk, 100) == Error::None); finishCapture(caught);
    CHECK(caught.phase == Phase::Home && !caught.expeditionScenario && !caught.dungeonWins && !caught.dungeonClears);
}
bool focusOpen(const State& state) {
    return state.autoCapture == AutoCapture::FocusStrike || state.autoCapture == AutoCapture::FocusBlock;
}
unsigned greenPhase(unsigned form) {
    for (unsigned phase = 0; phase < capturering::kCycleMs; ++phase)
        if (capturering::sample(phase, form).grade == capturering::Grade::Green) return phase;
    CHECK(false); return 0;
}
void modeDefaultsAndGuards() {
    auto state = newDevice(611);
    CHECK(state.battleMode == BattleMode::Tactical);
    CHECK(apply(state, Action::Hatch, 1) == Error::None);
    CHECK(state.battleMode == BattleMode::Tactical);
    for (unsigned mode = 0; mode < 2; ++mode) {
        CHECK(apply(state, Action::Mode, mode) == Error::None);
        roundTrip(state);
        CHECK(static_cast<unsigned>(state.battleMode) == mode);
        CHECK(apply(state, Action::Explore, 1000) == Error::None);
        CHECK(state.phase == Phase::Encounter && static_cast<unsigned>(state.battleMode) == mode);
        rejected(state, Action::Mode, 1 - mode, Error::WrongPhase);
        rejected(state, mode ? Action::Attack : Action::AutoFight, 0, Error::WrongMode);
        CHECK(apply(state, Action::Retreat) == Error::None);
        CHECK(static_cast<unsigned>(state.battleMode) == mode);
    }
    CHECK(apply(state, Action::Mode, 0) == Error::None);
    roundTrip(state); CHECK(state.battleMode == BattleMode::Tactical);
}

// Execute an identical real transition before and after snapshot restoration.
// Matching durable bytes includes HP, capture RNG, rewards and run position.
void restoredAction(State& state, Action action, unsigned value = 0) {
    auto restored = state; roundTrip(restored);
    const auto before = state;
    autobattle::Trace trace, repeated;
    const auto execute = [&](State& target, autobattle::Trace& output) {
        if (action == Action::AutoFight) return applyAutoFight(target, &output);
        if (action == Action::AutoResume) return applyAutoResume(target, &output);
        if (action == Action::Focus) return applyFocus(target, value, &output);
        return apply(target, action, value);
    };
    CHECK(execute(state, trace) == Error::None);
    CHECK(execute(restored, repeated) == Error::None);
    CHECK(isValid(state) && same(state, restored));
    CHECK(state.sequence == before.sequence + 1 && state.battleMode == before.battleMode);
    if (action == Action::AutoFight || action == Action::AutoResume || action == Action::Focus) {
        CHECK(trace.count == repeated.count && trace.outcome == repeated.outcome);
        CHECK(trace.startSequence == before.sequence && trace.endSequence == state.sequence);
        CHECK(trace.enemyFormId == before.wildFormId && trace.enemyLevel == before.wildLevel);
        if (!trace.count) {
            CHECK(focusOpen(state) && trace.outcome == autobattle::Outcome::None);
        } else {
            static char first[autobattle::kTraceJsonCapacity], second[autobattle::kTraceJsonCapacity];
            CHECK(autobattle::writeJson(trace, first, sizeof(first)));
            CHECK(autobattle::writeJson(repeated, second, sizeof(second)));
            CHECK(!std::strcmp(first, second));
        }
    }
    roundTrip(state);
}

void ordinaryAutoSaveAndResume() {
    unsigned focuses = 0, captures = 0, resumes = 0;
    for (unsigned seed = 1; seed <= 16; ++seed) {
        auto state = ready(seed);
        CHECK(apply(state, Action::Mode, 1) == Error::None);
        CHECK(apply(state, Action::Explore, 1000) == Error::None);
        CHECK(!state.expeditionScenario);
        for (unsigned actions = 0; state.phase == Phase::Encounter && actions < 32; ++actions) {
            if (focusOpen(state)) {
                ++focuses;
                rejected(state, Action::AutoFight, 0, Error::InvalidAction);
                restoredAction(state, Action::Focus, greenPhase(state.wildFormId));
                CHECK(!focusOpen(state));
                if (state.phase == Phase::Encounter)
                    rejected(state, Action::Focus, 0, Error::InvalidAction);
            } else if (state.autoCapture == AutoCapture::Awaiting) {
                rejected(state, Action::AutoFight, 0, Error::InvalidAction);
                if (seed % 2) { ++captures; restoredAction(state, Action::RingCapture, greenPhase(state.wildFormId)); }
                else { ++resumes; restoredAction(state, Action::AutoResume); }
            } else restoredAction(state, Action::AutoFight);
        }
        CHECK(state.phase == Phase::Home && state.battleMode == BattleMode::Auto);
        CHECK(!state.expeditionScenario && !state.dungeonClears && !state.bossClears);
        rejected(state, Action::AutoFight, 0, Error::WrongPhase);
        rejected(state, Action::AutoResume, 0, Error::WrongPhase);
        rejected(state, Action::NextExpedition, 0, Error::WrongPhase);
    }
    CHECK(focuses && captures && resumes);
}

void physicalOnlyTideKnockoutKeepsResolvedRewards() {
    // Keep the former success fixture unchanged as an actual failed run:
    // basic attacks and green throws clear two fights, then lose the third.
    // No edited foe HP, RNG, healing, or synthetic terminal state is used.
    auto state = ready(1);
    CHECK(apply(state, Action::Mode, 0) == Error::None);
    CHECK(apply(state, Action::StartExpedition, 1) == Error::None);
    for (unsigned tier = 1; tier <= 3; ++tier) {
        CHECK(state.phase == Phase::Encounter && state.expeditionTier == tier);
        CHECK(state.wildHp == state.wildMaxHp);
        const auto xp = state.collection[0].xp, bond = state.bond;
        const auto reward = 20 + 6 * state.wildLevel;
        const auto captures = state.captures, nextId = state.nextMemberId;
        const auto count = state.collectionCount;
        for (unsigned actions = 0; state.phase == Phase::Encounter && actions < 32; ++actions) {
            if (captureChance(state)) restoredAction(state, Action::RingCapture, greenPhase(state.wildFormId));
            else restoredAction(state, Action::Attack);
        }
        CHECK(state.dungeonKeys == 2 && !state.dungeonClears && !state.dungeonWins && !state.bossClears);
        if (tier < 3) {
            CHECK(state.phase == Phase::Expedition && state.collection[0].xp == xp + reward);
            CHECK(state.bond > bond && !isInjured(state.collection[0]));
            const auto hp = state.hp, energy = state.energy;
            restoredAction(state, Action::NextExpedition);
            CHECK(state.hp == hp && state.energy == energy);
        } else {
            CHECK(state.phase == Phase::Home && state.message == Message::Retreated);
            CHECK(isInjured(state.collection[0]) && careMistakes(state.collection[0]) == 1);
            CHECK(state.hp == (combat::formProfile(state.collection[0].formId, state.level).stats.maxHp + 9) / 10);
            CHECK(state.collection[0].xp == xp && state.bond == bond);
            CHECK(state.captures == captures && state.nextMemberId == nextId && state.collectionCount == count);
            CHECK(!state.expeditionScenario && !state.expeditionTier && !state.expeditionForm && !state.expeditionLevel);
            // Restore and repeated terminal input cannot revive or pay this run.
            for (unsigned retry = 0; retry < 2; ++retry) {
                roundTrip(state);
                rejected(state, Action::Attack, 0, Error::WrongPhase);
                rejected(state, Action::RingCapture, 0, Error::WrongPhase);
                rejected(state, Action::NextExpedition, 0, Error::WrongPhase);
            }
        }
    }
}

void completeModesWithRestoredPauses() {
    unsigned focusKinds = 0, capturePrompts = 0, failedThrows = 0, manualMagic = 0;
    // The level-20 fixture supplies training only. Each fight starts at its
    // real selected full HP, with unmodified RNG, and carries HP between tiers.
    for (unsigned mode = 0; mode < 2; ++mode) {
        for (unsigned sample = 0; sample < (mode ? 6u : 2u); ++sample) {
            const unsigned scenario = mode ? sample / 2 : sample + 1;
            auto state = ready(mode ? 1 + sample % 2 : 1);
            CHECK(apply(state, Action::Mode, mode) == Error::None);
            CHECK(apply(state, Action::StartExpedition, scenario) == Error::None);
            unsigned tiers = 0, expectedXp = state.collection[0].xp;
            for (unsigned tier = 1; tier <= 3; ++tier) {
                CHECK(state.phase == Phase::Encounter && state.expeditionTier == tier);
                CHECK(state.wildHp == state.wildMaxHp && static_cast<unsigned>(state.battleMode) == mode);
                roundTrip(state);
                rejected(state, Action::Mode, 1 - mode, Error::WrongPhase);
                expectedXp += 20 + 6 * state.wildLevel;
                for (unsigned actions = 0; state.phase == Phase::Encounter && actions < 32; ++actions) {
                    if (focusOpen(state)) {
                        focusKinds |= state.autoCapture == AutoCapture::FocusStrike ? 1u : 2u;
                        rejected(state, Action::AutoFight, 0, Error::InvalidAction);
                        restoredAction(state, Action::Focus, greenPhase(state.wildFormId));
                        CHECK(!focusOpen(state));
                    } else if (state.autoCapture == AutoCapture::Awaiting || (!mode && captureChance(state))) {
                        ++capturePrompts;
                        restoredAction(state, Action::RingCapture, greenPhase(state.wildFormId));
                        failedThrows += state.lastCapture.result != CaptureResult::Captured;
                    } else {
                        // Manual uses the visible guard: Magic bypasses Brace;
                        // Physical avoids Ward and Counter. No private RNG or
                        // selector forecast is consulted by this player policy.
                        const auto action = mode ? Action::AutoFight :
                            wildGuard(state) == combat::Defense::Brace ? Action::Magic : Action::Attack;
                        manualMagic += action == Action::Magic;
                        restoredAction(state, action);
                    }
                }
                ++tiers;
                CHECK(state.phase == (tier < 3 ? Phase::Expedition : Phase::Home));
                CHECK(state.collection[0].xp == expectedXp && state.dungeonKeys == 2);
                CHECK(state.dungeonClears == (tier == 3 ? 1u : 0u) && state.dungeonWins == (tier == 3 ? 1u : 0u));
                // Retrying a terminal command cannot award the resolved fight
                // again, and background restore cannot start a new tier.
                rejected(state, mode ? Action::AutoFight : Action::Attack, 0, Error::WrongPhase);
                rejected(state, Action::RingCapture, 0, Error::WrongPhase);
                roundTrip(state);
                if (tier < 3) {
                    rejected(state, Action::Mode, 1 - mode, Error::WrongPhase);
                    const auto hp = state.hp, energy = state.energy;
                    restoredAction(state, Action::NextExpedition);
                    CHECK(state.hp == hp && state.energy == energy && state.dungeonKeys == 2);
                    rejected(state, Action::NextExpedition, 0, Error::WrongPhase);
                }
            }
            CHECK(tiers == 3 && !state.expeditionScenario && static_cast<unsigned>(state.battleMode) == mode);
            CHECK(apply(state, Action::Mode, 1 - mode) == Error::None);
            roundTrip(state); CHECK(static_cast<unsigned>(state.battleMode) == 1 - mode);
        }
    }
    CHECK(focusKinds == 3 && capturePrompts && failedThrows && manualMagic);
}

}
int main() {
    CHECK(kRulesVersion == 22 && kSchemaVersion == 30 && kSnapshotSize == 6876);
    threeTiersAndAccounting(); capturesResolveTiers(); interruptionAndBackgroundSteps();
    knockoutAndFinalKeyThreshold(); autoStopsAtBoundary(); invalidStateAndOverflow(); projectionAndOrdinaryEncounter();
    modeDefaultsAndGuards(); ordinaryAutoSaveAndResume(); physicalOnlyTideKnockoutKeepsResolvedRewards(); completeModesWithRestoredPauses();
    std::printf("dungeon tiers: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
