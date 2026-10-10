// Rules 17: care mistakes, care-quality routes, injury and Treat.
#include "game.hpp"
#include "forms.hpp"
#include "legacy_v16.hpp"
#include <cstdio>
#include <cstring>

using namespace digivice;
namespace {
unsigned checks = 0, failures = 0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)
void step(State& s, Action a, unsigned value = 0) { const auto e = apply(s, a, value); CHECK(e == Error::None); if (e != Error::None) std::fprintf(stderr, "  error: %s\n", errorText(e)); CHECK(isValid(s)); }
void reject(State& s, Action a, unsigned value, Error expected) {
    State before = s; const auto e = apply(s, a, value); CHECK(e == expected);
    if (e != expected) std::fprintf(stderr, "  got: %s\n", errorText(e));
    Snapshot x, y; CHECK(encodeSnapshot(before, x) && encodeSnapshot(s, y) && std::memcmp(x.bytes, y.bytes, sizeof(x.bytes)) == 0);
}
CreatureMember& partner(State& s) { return *const_cast<CreatureMember*>(activeMember(s)); }
void tick(State& s) { step(s, Action::CareMinute, s.careMinute + 1); }

State hatched() { auto s = newDevice(17); step(s, Action::Hatch, 1); return s; }

// Put the active partner on `formId` at `level`, fully bonded and cared for.
void become(State& s, unsigned formId, unsigned level) {
    const auto* f = forms::find(formId); CHECK(f);
    auto& m = partner(s);
    m.formId = formId; m.species = static_cast<Species>(f->lineage);
    m.level = level; m.xp = xpForLevel(level); m.bond = 200;
    m.careState = 100; // 100 care points, no toilet need, no mistakes or injury.
    m.hp = combat::formProfile(formId, level).stats.maxHp;
    s.journal[(formId - 1) / 32] |= 1u << ((formId - 1) % 32);
    s.hp = m.hp; s.level = m.level; s.bond = m.bond;
    CHECK(isValid(s));
}

// First production form with two production routes whose destinations have `stage`.
unsigned branchingForm(forms::Stage stage) {
    for (unsigned id = forms::kFirstProductionFormId; id <= forms::kFormCount; ++id) {
        const auto* a = forms::outgoing(id, 0); const auto* b = forms::outgoing(id, 1);
        if (a && b && forms::productionForm(a->to) && forms::productionForm(b->to) && forms::find(a->to)->stage == stage) return id;
    }
    return 0;
}
unsigned singleRouteForm() {
    for (unsigned id = forms::kFirstProductionFormId; id <= forms::kFormCount; ++id) {
        const auto* a = forms::outgoing(id, 0);
        if (a && !forms::outgoing(id, 1) && forms::productionForm(a->to)) return id;
    }
    return 0;
}
unsigned routeLevel(unsigned formId, unsigned index) { return forms::evolutionNeed(*forms::outgoing(formId, index)).level; }

void versions() {
    CHECK(kSchemaVersion == 24 && kRulesVersion == 17 && kSnapshotSize == 3216);
    CHECK(legacy_v16::kSchemaVersion == 23 && legacy_v16::kRulesVersion == 16);
    Action a; CHECK(parseAction("treat", a) && a == Action::Treat);
}

void toiletAndHunger() {
    auto s = hatched();
    step(s, Action::CareMinute, 1);
    while (toiletNeed(partner(s)) < 100) tick(s);
    CHECK(careMistakes(partner(s)) == 1 && careWasMissed(partner(s)));
    tick(s); tick(s); // Overflow is counted once per episode.
    CHECK(careMistakes(partner(s)) == 1);
    step(s, Action::Toilet);
    while (toiletNeed(partner(s)) < 100) tick(s);
    CHECK(careMistakes(partner(s)) == 2);
    step(s, Action::Toilet);

    auto h = hatched();
    step(h, Action::CareMinute, 1);
    unsigned guard = 0;
    while (h.fullness > 0 && guard++ < 200) { if (toiletNeed(partner(h)) >= 90) step(h, Action::Toilet); tick(h); }
    CHECK(h.fullness == 0 && careMistakes(partner(h)) == 1);
    tick(h); CHECK(careMistakes(partner(h)) == 1); // Staying empty is not recounted.
    step(h, Action::Feed);
    // Gaps (power-off, sleep) never punish.
    auto g = hatched();
    step(g, Action::CareMinute, 1); step(g, Action::CareMinute, 5000);
    CHECK(careMistakes(partner(g)) == 0 && toiletNeed(partner(g)) == 8);
}

void saturation() {
    auto s = hatched();
    step(s, Action::CareMinute, 1);
    for (unsigned episode = 0; episode < 10; ++episode) {
        while (toiletNeed(partner(s)) < 100) { if (s.fullness < 20) step(s, Action::Feed); tick(s); }
        step(s, Action::Toilet);
    }
    CHECK(careMistakes(partner(s)) == 7);
    // Saturated mistakes keep the low care fields intact.
    CHECK(carePoints(partner(s)) <= 100 && toiletNeed(partner(s)) == 0);
}

// Starts an encounter and leaves the partner one hit from defeat by a sturdy foe.
void doomedEncounter(State& s) {
    step(s, Action::Explore, 1000);
    CHECK(s.phase == Phase::Encounter && s.wildRules == 17);
    s.wildLevel = 50; s.wildMaxHp = s.wildHp = combat::formProfile(s.wildFormId, 50).stats.maxHp;
    for (unsigned g = 0; g < 4 && wildGuard(s) == combat::Defense::Counter; ++g) ++s.wildTurn;
    s.hp = 1; partner(s).hp = 1;
    CHECK(isValid(s));
}

void injury() {
    auto s = hatched();
    reject(s, Action::Treat, 0, Error::InvalidAction);
    doomedEncounter(s);
    reject(s, Action::Treat, 0, Error::WrongPhase);
    step(s, Action::Attack);
    CHECK(s.phase == Phase::Home && s.message == Message::Retreated);
    CHECK(injuryLevel(partner(s)) == 1 && careMistakes(partner(s)) == 1);
    static char json[kJsonCapacity];
    CHECK(writeJson(s, json, sizeof(json)) && std::strstr(json, "Treat it at Home") && std::strstr(json, "\"injury\":1") && std::strstr(json, "\"careMistakes\":1"));

    const auto full = combat::formProfile(partner(s).formId, s.level).stats.maxHp;
    const auto half = (full + 1) / 2;
    const auto rests = recoveryRestCount(s);
    CHECK(rests > 0);
    for (unsigned i = 0; i < 12; ++i) step(s, Action::Rest);
    CHECK(s.hp == half && s.energy == 100 && recoveryRestCount(s) == 0);
    const auto bond = s.bond;
    step(s, Action::Rest); CHECK(s.hp == half && s.bond == bond); // Not useful: no bond or care reward.

    // Snapshot keeps the rules-17 bits.
    Snapshot snap; State back;
    CHECK(encodeSnapshot(s, snap) && decodeSnapshot(snap.bytes, sizeof(snap.bytes), back) == SnapshotStatus::Ok);
    CHECK(injuryLevel(*activeMember(back)) == 1 && careMistakes(*activeMember(back)) == 1);

    step(s, Action::Treat);
    CHECK(!isInjured(partner(s)) && s.message == Message::Treated && careMistakes(partner(s)) == 1);
    for (unsigned i = 0; i < 12; ++i) step(s, Action::Rest);
    CHECK(s.hp == full);

    // An explicit retreat is not a knockout.
    auto r = hatched();
    step(r, Action::Explore, 1000);
    step(r, Action::Retreat);
    CHECK(!isInjured(partner(r)) && careMistakes(partner(r)) == 0);

    // Defeat while already hurt counts again but does not reset the neglect clock.
    auto twice = hatched();
    doomedEncounter(twice); step(twice, Action::Attack);
    step(twice, Action::CareMinute, kInjuryNeglectMinutes); // Gap from zero: just sets the clock.
    tick(twice);
    while (twice.careMinute % kInjuryNeglectMinutes) tick(twice);
    CHECK(injuryLevel(partner(twice)) == 2);
    doomedEncounter(twice); step(twice, Action::Attack);
    CHECK(injuryLevel(partner(twice)) == 2 && careMistakes(partner(twice)) == 2);
}

void neglect() {
    auto s = hatched();
    doomedEncounter(s); step(s, Action::Attack);
    step(s, Action::CareMinute, 1);
    unsigned minutes = 0;
    while (injuryLevel(partner(s)) < 3 && minutes < 40) { if (toiletNeed(partner(s)) >= 90) step(s, Action::Toilet); tick(s); ++minutes; }
    CHECK(injuryLevel(partner(s)) == 3);
    CHECK(minutes <= 2 * kInjuryNeglectMinutes);
    CHECK(careMistakes(partner(s)) == 2); // Knockout plus neglect.
    for (unsigned i = 0; i < 25; ++i) { if (toiletNeed(partner(s)) >= 90) step(s, Action::Toilet); tick(s); }
    CHECK(injuryLevel(partner(s)) == 3 && careMistakes(partner(s)) == 2);
    step(s, Action::Treat);
    CHECK(injuryLevel(partner(s)) == 0);
}

void routes() {
    const unsigned champ = branchingForm(forms::Stage::Champion);
    const unsigned mega = branchingForm(forms::Stage::Mega);
    const unsigned single = singleRouteForm();
    CHECK(champ && mega && single);
    std::printf("Branching examples: champion-from %u, mega-from %u, single-route %u\n", champ, mega, single);
    CHECK(cleanRouteMistakeLimit(forms::outgoing(champ, 0)->to) == 3 && cleanRouteMistakeLimit(forms::outgoing(mega, 0)->to) == 1);

    // Clean route locks after too many mistakes; the second route stays open.
    for (unsigned mistakes = 0; mistakes <= 4; ++mistakes) {
        auto s = hatched();
        const auto level = routeLevel(champ, 0) > routeLevel(champ, 1) ? routeLevel(champ, 0) : routeLevel(champ, 1);
        become(s, champ, level);
        partner(s).careState |= mistakes << 27;
        const auto clean = forms::outgoing(champ, 0)->to, rough = forms::outgoing(champ, 1)->to;
        CHECK(careRouteOpen(partner(s), 0) == (mistakes <= 3) && careRouteOpen(partner(s), 1));
        static char json[kJsonCapacity];
        CHECK(writeJson(s, json, sizeof(json)) && std::strstr(json, "\"maxCareMistakes\":3") && std::strstr(json, "\"maxCareMistakes\":null"));
        if (mistakes <= 3) {
            step(s, Action::Evolve, clean);
        } else {
            reject(s, Action::Evolve, clean, Error::CareRouteLocked);
            step(s, Action::Evolve, rough);
        }
        // Digivolution spends care and clears mistakes.
        CHECK(carePoints(partner(s)) == 0 && careMistakes(partner(s)) == 0);
        // The next stage needs care again.
        const auto* next = forms::outgoing(partner(s).formId, 0);
        if (next) {
            become(s, partner(s).formId, s.level);
            partner(s).careState = 0;
            const auto need = forms::evolutionNeed(*next);
            auto& m = partner(s); m.level = need.level; m.xp = xpForLevel(need.level); s.level = need.level;
            m.hp = s.hp = combat::formProfile(m.formId, m.level).stats.maxHp;
            CHECK(isValid(s));
            reject(s, Action::Evolve, next->to, Error::EvolutionUnavailable);
        }
    }

    // Mega limit is one mistake.
    {
        auto s = hatched();
        const auto level = routeLevel(mega, 0) > routeLevel(mega, 1) ? routeLevel(mega, 0) : routeLevel(mega, 1);
        become(s, mega, level);
        partner(s).careState |= 2u << 27;
        reject(s, Action::Evolve, forms::outgoing(mega, 0)->to, Error::CareRouteLocked);
        partner(s).careState = 100u | (1u << 27);
        step(s, Action::Evolve, forms::outgoing(mega, 0)->to);
    }
    // A single route is never care-locked.
    {
        auto s = hatched();
        become(s, single, routeLevel(single, 0));
        partner(s).careState |= 7u << 27;
        CHECK(careRouteOpen(partner(s), 0));
        step(s, Action::Evolve, forms::outgoing(single, 0)->to);
    }
    // Injury blocks Digivolution until treated; benched members too.
    {
        auto s = hatched();
        become(s, single, routeLevel(single, 0));
        partner(s).careState |= 1u << 30;
        reject(s, Action::Evolve, forms::outgoing(single, 0)->to, Error::MemberInjured);
        static char json[kJsonCapacity];
        CHECK(writeJson(s, json, sizeof(json)) && std::strstr(json, "\"eligible\":false"));
        step(s, Action::Treat);
        step(s, Action::Evolve, forms::outgoing(single, 0)->to);
    }
}

void migration() {
    // A schema-23 snapshot from the frozen rules-16 executor migrates with clean bits.
    auto old = legacy_v16::newDevice(17);
    CHECK(legacy_v16::apply(old, legacy_v16::Action::Hatch, 1) == legacy_v16::Error::None);
    CHECK(legacy_v16::apply(old, legacy_v16::Action::CareMinute, 1) == legacy_v16::Error::None);
    for (unsigned m = 2; m < 20; ++m) CHECK(legacy_v16::apply(old, legacy_v16::Action::CareMinute, m) == legacy_v16::Error::None);
    legacy_v16::Snapshot snap; State current;
    CHECK(legacy_v16::encodeSnapshot(old, snap));
    CHECK(decodeSnapshot(snap.bytes, sizeof(snap.bytes), current) == SnapshotStatus::Migrated);
    CHECK(careMistakes(*activeMember(current)) == 0 && !isInjured(*activeMember(current)));
    CHECK(toiletNeed(*activeMember(current)) == ((old.collection[0].careState >> 7) & 0x7fu));
    // A schema-23 header claiming rules-17 bits is rejected.
    State tampered = current; tampered.collection[0].careState |= 1u << 27;
    Snapshot fresh; CHECK(encodeSnapshot(tampered, fresh));
    fresh.bytes[4] = 23; fresh.bytes[8] = 16;
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i + 4 < sizeof(fresh.bytes); ++i) { crc ^= fresh.bytes[i]; for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u))); }
    crc = ~crc; for (unsigned i = 0; i < 4; ++i) fresh.bytes[sizeof(fresh.bytes) - 4 + i] = static_cast<std::uint8_t>(crc >> (8 * i));
    State rejected;
    CHECK(decodeSnapshot(fresh.bytes, sizeof(fresh.bytes), rejected) == SnapshotStatus::InvalidState);
    // Frozen rules 16 never injures.
    auto frozen = legacy_v16::newDevice(17);
    CHECK(legacy_v16::apply(frozen, legacy_v16::Action::Hatch, 1) == legacy_v16::Error::None);
    legacy_v16::Action treat; CHECK(!legacy_v16::parseAction("treat", treat));
}
} // namespace

int main() {
    versions(); toiletAndHunger(); saturation(); injury(); neglect(); routes(); migration();
    std::printf("rules 17: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
