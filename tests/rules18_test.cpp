// Rules 18: level-scaled wild damage, guard-aware Auto and focus moments.
#include "game.hpp"
#include "forms.hpp"
#include "capture_ring.hpp"
#include "legacy_v17.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace digivice;
namespace {
unsigned checks = 0, failures = 0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)
void step(State& s, Action a, unsigned value = 0) { const auto e = apply(s, a, value); CHECK(e == Error::None); if (e != Error::None) std::fprintf(stderr, "  error: %s\n", errorText(e)); CHECK(isValid(s)); }
bool same(const State& a, const State& b) { Snapshot x, y; return encodeSnapshot(a, x) && encodeSnapshot(b, y) && !std::memcmp(x.bytes, y.bytes, sizeof(x.bytes)); }
void reject(State& s, Action a, unsigned value, Error expected) { const auto before = s; const auto e = apply(s, a, value); CHECK(e == expected); CHECK(same(before, s)); }
bool focusPending(const State& s) { return s.autoCapture == AutoCapture::FocusStrike || s.autoCapture == AutoCapture::FocusBlock; }
std::uint32_t phaseFor(capturering::Grade grade, std::uint32_t formId) {
    for (std::uint32_t p = 0; p < capturering::kCycleMs; ++p) if (capturering::sample(p, formId).grade == grade) return p;
    return kFocusNoTap;
}
State encounter(unsigned seed) {
    auto s = newDevice(seed); step(s, Action::Hatch, 1 + seed % 8); step(s, Action::Mode, 1); step(s, Action::Explore, 1000);
    return s;
}
// First AutoFight chunk that stops at a focus pause of the requested kind.
bool focusEncounter(bool strike, State& out) {
    for (unsigned seed = 1; seed < 4000; ++seed) {
        auto s = encounter(seed);
        if (applyAutoFight(s) != Error::None) continue;
        if (s.autoCapture == (strike ? AutoCapture::FocusStrike : AutoCapture::FocusBlock)) { out = s; return true; }
    }
    return false;
}

void versions() {
    CHECK(kSchemaVersion == 25 && kRulesVersion == 18 && kSnapshotSize == 3216);
    CHECK(legacy_v17::kSchemaVersion == 24 && legacy_v17::kRulesVersion == 17);
    Action a; CHECK(parseAction("focus", a) && a == Action::Focus);
    legacy_v17::Action b; CHECK(!legacy_v17::parseAction("focus", b));
}

void powerScaling() {
    const auto base = combat::resolveCareForms(11, 30, 11, 30, combat::Move::Physical, combat::Defense::None, {}, {});
    const auto scaled = combat::resolveCareForms(11, 30, 11, 30, combat::Move::Physical, combat::Defense::None, {}, {}, 4, 10);
    const auto heavy = combat::resolveCareForms(11, 30, 11, 30, combat::Move::Heavy, combat::Defense::None, {}, {}, 4, 10);
    const auto heavyBase = combat::resolveCareForms(11, 30, 11, 30, combat::Move::Heavy, combat::Defense::None, {}, {});
    CHECK(scaled.damage > base.damage && heavy.damage - heavyBase.damage >= scaled.damage - base.damage);
    CHECK(combat::resolveCareForms(11, 30, 11, 30, combat::Move::Physical, combat::Defense::None, {}, {}, 4, 17).damage == 0);
    std::printf("Lv30 same-form physical %u -> %u with +10 power; heavy %u -> %u\n", base.damage, scaled.damage, heavyBase.damage, heavy.damage);
}

void guardAwareAuto() {
    unsigned heavy = 0, reflected = 0, frames = 0;
    for (unsigned seed = 1; seed <= 256; ++seed) {
        auto s = encounter(seed);
        CHECK(s.wildRules == 18);
        autobattle::Trace t;
        if (applyAutoFight(s, &t) != Error::None) { CHECK(false); continue; }
        for (unsigned i = 0; i < t.count; ++i) { ++frames; heavy += t.steps[i].action == autobattle::Move::Heavy; reflected += t.steps[i].reflected; }
    }
    CHECK(reflected == 0 && heavy > 0);
    std::printf("Guard-aware Auto: %u frames, %u Heavy, %u reflected\n", frames, heavy, reflected);
}

void focusFlow() {
    for (bool strike : {true, false}) {
        State paused;
        CHECK(focusEncounter(strike, paused));
        CHECK(isValid(paused) && paused.phase == Phase::Encounter && paused.battleMode == BattleMode::Auto);
        CHECK(paused.wildTurn == focusTurn(paused) && focusIsStrike(paused) == strike);
        static char json[kJsonCapacity];
        CHECK(writeJson(paused, json, sizeof(json)) && std::strstr(json, strike ? "\"focus\":{\"kind\":\"strike\"" : "\"focus\":{\"kind\":\"block\""));
        // While a focus prompt is open only the answer (or Run Away) moves the fight.
        auto blocked = paused;
        reject(blocked, Action::AutoFight, 0, Error::InvalidAction);
        reject(blocked, Action::AutoResume, 0, Error::InvalidAction);
        reject(blocked, Action::Attack, 0, Error::WrongMode);
        reject(blocked, Action::Focus, kFocusNoTap + 1, Error::InvalidValue);
        // Snapshot round trip keeps the pending prompt.
        Snapshot snap; State back;
        CHECK(encodeSnapshot(paused, snap) && decodeSnapshot(snap.bytes, sizeof(snap.bytes), back) == SnapshotStatus::Ok && same(back, paused));
        // A schema-24 header can never carry a focus prompt.
        auto old = snap; old.bytes[4] = 24; old.bytes[8] = 17;
        std::uint32_t crc = 0xffffffffu;
        for (std::size_t i = 0; i + 4 < sizeof(old.bytes); ++i) { crc ^= old.bytes[i]; for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u))); }
        crc = ~crc; for (unsigned i = 0; i < 4; ++i) old.bytes[sizeof(old.bytes) - 4 + i] = static_cast<std::uint8_t>(crc >> (8 * i));
        State rejected; CHECK(decodeSnapshot(old.bytes, sizeof(old.bytes), rejected) == SnapshotStatus::InvalidState);

        // Compare green, orange and an untapped answer on identical states.
        auto green = paused, orange = paused, none = paused, replay = paused;
        autobattle::Trace tg, to, tn;
        CHECK(applyFocus(green, phaseFor(capturering::Grade::Green, paused.wildFormId), &tg) == Error::None);
        CHECK(applyFocus(orange, phaseFor(capturering::Grade::Orange, paused.wildFormId), &to) == Error::None);
        CHECK(applyFocus(none, kFocusNoTap, &tn) == Error::None);
        CHECK(applyFocus(replay, phaseFor(capturering::Grade::Green, paused.wildFormId)) == Error::None && same(replay, green));
        CHECK(tg.count >= 1 && tn.count >= 1 && to.count >= 1);
        const auto& g = tg.steps[0]; const auto& o = to.steps[0]; const auto& n = tn.steps[0];
        if (strike) {
            const auto dealt = [](const autobattle::Step& f) { return f.enemyHpBefore - f.enemyHpAfter; };
            CHECK(dealt(g) >= dealt(o) && dealt(o) >= dealt(n) && dealt(g) > dealt(n));
        } else {
            const auto taken = [](const autobattle::Step& f) { return f.playerHpBefore > f.playerHpAfter ? f.playerHpBefore - f.playerHpAfter : 0u; };
            if (g.enemyHpAfter && n.enemyHpAfter && n.playerHpAfter) { CHECK(taken(g) == 0 && taken(o) <= taken(n) && taken(n) > 0); }
        }
        // One focus per encounter: the answered fight never pauses for focus again.
        for (auto* s : {&green, &orange, &none}) {
            CHECK(!focusPending(*s));
            for (unsigned guard = 0; guard < 8 && s->phase == Phase::Encounter; ++guard) {
                if (s->autoCapture == AutoCapture::Awaiting) CHECK(applyAutoResume(*s) == Error::None);
                else CHECK(applyAutoFight(*s) == Error::None);
                CHECK(!focusPending(*s));
            }
            CHECK(s->phase == Phase::Home && s->autoCapture == AutoCapture::None);
        }
        // Run Away from an open prompt is a calm exit.
        auto run = paused; step(run, Action::Retreat);
        CHECK(run.phase == Phase::Home && run.autoCapture == AutoCapture::None && !isInjured(*activeMember(run)));
    }
}

// Unattended Auto stays challenging but fair at every stage; a good tap helps.
void balanceBand() {
    unsigned wins[2]{}, fights = 0; std::vector<unsigned> lengths;
    for (unsigned seed = 1; seed <= 400; ++seed) {
        for (int mode = 0; mode < 2; ++mode) {
            auto s = encounter(seed);
            unsigned exchanges = 0;
            for (unsigned guard = 0; guard < 12 && s.phase == Phase::Encounter; ++guard) {
                autobattle::Trace t;
                if (s.autoCapture == AutoCapture::Awaiting) CHECK(applyAutoResume(s, &t) == Error::None);
                else if (focusPending(s)) CHECK(applyFocus(s, mode ? phaseFor(capturering::Grade::Green, s.wildFormId) : kFocusNoTap, &t) == Error::None);
                else CHECK(applyAutoFight(s, &t) == Error::None);
                exchanges += t.count;
            }
            wins[mode] += s.lastAutoOutcome == autobattle::Outcome::Won;
            if (!mode) { lengths.push_back(exchanges); ++fights; }
        }
    }
    std::sort(lengths.begin(), lengths.end());
    const double untapped = 100.0 * wins[0] / fights, tapped = 100.0 * wins[1] / fights;
    std::printf("Starter Auto band: untapped win %.1f%%, green taps %.1f%%, exchanges median %u p90 %u\n", untapped, tapped, lengths[fights / 2], lengths[fights * 9 / 10]);
    CHECK(untapped >= 80.0 && untapped <= 97.0 && tapped >= untapped && lengths[fights * 9 / 10] <= 14);
}

void migration() {
    auto old = legacy_v17::newDevice(18);
    CHECK(legacy_v17::apply(old, legacy_v17::Action::Hatch, 1) == legacy_v17::Error::None);
    CHECK(legacy_v17::apply(old, legacy_v17::Action::Mode, 1) == legacy_v17::Error::None);
    CHECK(legacy_v17::apply(old, legacy_v17::Action::Explore, 1000) == legacy_v17::Error::None);
    legacy_v17::Snapshot snap; State current;
    CHECK(legacy_v17::encodeSnapshot(old, snap) && decodeSnapshot(snap.bytes, sizeof(snap.bytes), current) == SnapshotStatus::Migrated);
    // An encounter started under rules 17 finishes under rules 17: no focus pause.
    CHECK(current.wildRules == 17);
    for (unsigned guard = 0; guard < 8 && current.phase == Phase::Encounter; ++guard) {
        CHECK(!focusPending(current));
        if (current.autoCapture == AutoCapture::Awaiting) CHECK(applyAutoResume(current) == Error::None);
        else CHECK(applyAutoFight(current) == Error::None);
    }
    CHECK(current.phase == Phase::Home);
}
} // namespace

int main() {
    versions(); powerScaling(); guardAwareAuto(); focusFlow(); balanceBand(); migration();
    std::printf("rules 18: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
