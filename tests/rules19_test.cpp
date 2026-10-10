// Rules 19: the 465-form roster opens to the wild; rules 13-18 stay frozen on
// the 276-form roster; four Dawn/Dusk duplicates are retired in place.
#include "game.hpp"
#include "expeditions.hpp"
#include "forms.hpp"
#include "encounters.hpp"
#include "combat.hpp"
#include "legacy_v18.hpp"
#include "snapshot_test_helpers.hpp"
#include <cstdio>
#include <cstring>
#include <iterator>
#include <initializer_list>

using namespace digivice;
namespace {
unsigned checks = 0, failures = 0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)

void versions() {
    CHECK(kSchemaVersion == 27 && kRulesVersion == 19 && kSnapshotSize == 6860 && kCollectionCapacity == 250);
    CHECK(std::size(expeditions::kScenarios) == 6);
    CHECK(legacy_v18::kSchemaVersion == 25 && legacy_v18::kRulesVersion == 18);
    CHECK(forms::kFormCount == 465 && forms::kProductionFormCount == 451 && forms::kRules18FormCount == 276);
}

void retiredDuplicates() {
    const struct { unsigned id, alias; } expected[]{{279,146},{286,127},{287,126},{288,132}};
    unsigned retired = 0, production = 0;
    for (unsigned id = 1; id <= forms::kFormCount; ++id) {
        retired += forms::retiredAliasOf(id) != 0;
        production += forms::productionForm(id);
    }
    CHECK(retired == 4 && production == forms::kProductionFormCount);
    for (const auto& e : expected) {
        CHECK(forms::retiredAliasOf(e.id) == e.alias && !forms::productionForm(e.id) && forms::productionForm(e.alias));
        CHECK(!forms::encounterObtainable(e.id) && !forms::outgoing(e.id, 0));
        CHECK(forms::combatTier(e.id) == forms::combatTier(e.alias)); // same creature, same stage
        for (std::size_t i = 0; i < forms::edgeCount(); ++i) CHECK(forms::edgeAt(i)->to != e.id);
    }
    // Every live route moves up exactly one combat tier (stage skips and mode changes are held for review).
    for (std::size_t i = 0; i < forms::edgeCount(); ++i) {
        const auto* e = forms::edgeAt(i);
        if (e->from <= 10) continue; // decode-only original fixtures keep their historical routes
        CHECK(static_cast<int>(forms::combatTier(e->to)) == static_cast<int>(forms::combatTier(e->from)) + 1 ||
              (e->from <= forms::kRules18FormCount && e->to <= forms::kRules18FormCount)); // reviewed rules-18 routes unchanged
    }
}

void frozenPool() {
    // Rules 13-18 draw only from forms 1..276; rules 19 also reaches the new forms, never retired ones.
    bool newSeen = false;
    for (unsigned partner : {12u, 13u, 14u, 125u})
        for (unsigned seed = 1; seed <= 3000; ++seed) {
            const auto old = encounters::rules18::selectProduction(1 + seed % 40, seed * 2654435761u, partner, 40);
            const auto now = encounters::selectProduction(1 + seed % 40, seed * 2654435761u, partner, 40);
            CHECK(old >= forms::kFirstProductionFormId && old <= forms::kRules18FormCount);
            CHECK(forms::productionForm(now) && !forms::retiredAliasOf(now));
            newSeen = newSeen || now > forms::kRules18FormCount;
        }
    CHECK(newSeen);
    // Rookie-tier pools never contained the new Champion+ forms, so they are unchanged.
    for (unsigned seed = 1; seed <= 3000; ++seed)
        CHECK(encounters::rules18::selectProduction(seed, seed * 7919u, 11, 12) == encounters::selectProduction(seed, seed * 7919u, 11, 12));
    // Rules-18 route view: frozen 172 routes; the live table differs only by the reviewed additions.
    CHECK(forms::rules18::edgeCount() == 172 && forms::edgeCount() == 254);
    for (std::size_t i = 0; i < forms::rules18::edgeCount(); ++i) {
        const auto* e = forms::rules18::edgeAt(i);
        CHECK(e->from <= forms::kRules18FormCount && e->to <= forms::kRules18FormCount);
    }
    CHECK(!forms::rules18::outgoing(29, 1) && forms::outgoing(29, 1)); // gained a second route only in rules 19
}

// Partner at a given form/level, Auto mode, ready to explore (the balance harness setup).
template <typename S, typename A>
bool partner(S& s, unsigned formId, unsigned level, A select) {
    const auto* f = forms::find(formId);
    s.sequence = s.foregroundSequence = 400; s.collectionCount = 2; s.captures = s.encounters = 1; s.steps = 100; s.nextMemberId = 3;
    auto& m = s.collection[1];
    m = {}; m.id = 2; m.capturedAtSequence = 1; m.formId = formId; m.species = static_cast<decltype(m.species)>(f->lineage);
    m.level = level; m.xp = legacy_v18::xpForLevel(level); m.bond = 150; m.mood = 80; m.fullness = 60; m.careState = 100;
    m.hp = combat::formProfile(formId, level).stats.maxHp; m.energy = 100;
    s.journal[(formId - 1) / 32] |= 1u << ((formId - 1) % 32);
    return select(s);
}

void rosterAndKeys() {
    CHECK(newDevice(1).dungeonKeys == 0 && newGame(1).dungeonKeys == 0);
    State owned = newDevice(9);
    CHECK(apply(owned, Action::Hatch, 1) == Error::None && owned.collectionCount == 1);
    const auto founder = owned.collection[0];
    owned.collectionCount = 60;
    owned.captures = owned.encounters = 59;
    owned.steps = 5900;
    owned.sequence = owned.foregroundSequence = 80;
    owned.nextMemberId = 61;
    for (unsigned i = 1; i < 60; ++i) {
        owned.collection[i] = founder;
        owned.collection[i].id = i + 1;
        owned.collection[i].capturedAtSequence = i + 1;
    }
    CHECK(isValid(owned) && owned.collectionCount < kCollectionCapacity);
    owned.collectionCount = kCollectionCapacity;
    owned.captures = owned.encounters = kCollectionCapacity - 1;
    owned.steps = 100 * (kCollectionCapacity - 1);
    owned.sequence = owned.foregroundSequence = kCollectionCapacity + 20;
    owned.nextMemberId = kCollectionCapacity + 1;
    for (unsigned i = 1; i < kCollectionCapacity; ++i) {
        owned.collection[i] = founder;
        owned.collection[i].id = i + 1;
        owned.collection[i].capturedAtSequence = i + 1;
    }
    CHECK(isValid(owned));
    char json[16384];
    // The expedition catalog is near the end of a large projection; check the hatched save.
    State hatched = newDevice(4);
    CHECK(apply(hatched, Action::Hatch, 1) == Error::None);
    CHECK(writeJson(hatched, json, sizeof(json)) && std::strstr(json, "\"dungeonKeys\":0") && std::strstr(json, "Grove Dungeon"));
    Snapshot full; CHECK(encodeSnapshot(owned, full));
    State restored; CHECK(decodeSnapshot(full.bytes, sizeof(full.bytes), restored) == SnapshotStatus::Ok && restored.collectionCount == 250 && restored.collection[249].id == 250);
}

void migrationMidEncounter() {
    unsigned migrated = 0, finished = 0, newWild = 0;
    for (unsigned seed = 1; seed <= 200; ++seed) {
        auto old = legacy_v18::newDevice(seed);
        if (legacy_v18::apply(old, legacy_v18::Action::Hatch, 1 + seed % 8) != legacy_v18::Error::None) continue;
        if (legacy_v18::apply(old, legacy_v18::Action::Mode, 1) != legacy_v18::Error::None) continue;
        if (!partner(old, 13, 30, [](legacy_v18::State& s) { return legacy_v18::isValid(s) && legacy_v18::apply(s, legacy_v18::Action::Select, 2) == legacy_v18::Error::None; })) continue;
        if (legacy_v18::apply(old, legacy_v18::Action::Explore, 1000) != legacy_v18::Error::None || old.phase != legacy_v18::Phase::Encounter) continue;
        CHECK(old.wildRules == 18 && old.wildFormId <= forms::kRules18FormCount);
        legacy_v18::Snapshot snap; State now;
        CHECK(legacy_v18::encodeSnapshot(old, snap) && decodeSnapshot(snap.bytes, sizeof(snap.bytes), now) == SnapshotStatus::Migrated);
        ++migrated;
        // The encounter started under rules 18 keeps its foe and finishes; the next one is a rules-19 draw.
        CHECK(now.wildRules == 18 && now.wildFormId == old.wildFormId && now.dungeonKeys == 3 && isValid(now));
        Snapshot saved; State again;
        CHECK(encodeSnapshot(now, saved) && decodeSnapshot(saved.bytes, sizeof(saved.bytes), again) == SnapshotStatus::Ok && again.dungeonKeys == 3);
        for (unsigned guard = 0; guard < 16 && now.phase == Phase::Encounter; ++guard) {
            const bool focus = now.autoCapture == AutoCapture::FocusStrike || now.autoCapture == AutoCapture::FocusBlock;
            CHECK((focus ? applyFocus(now, kFocusNoTap) : now.autoCapture == AutoCapture::Awaiting ? applyAutoResume(now) : applyAutoFight(now)) == Error::None);
        }
        finished += now.phase == Phase::Home;
        auto& m = *const_cast<CreatureMember*>(activeMember(now));
        if (now.phase != Phase::Home || !m.hp) continue;
        m.hp = now.hp = combat::formProfile(m.formId, now.level).stats.maxHp; m.careState &= (1u << 27) - 1u;
        if (!isValid(now) || apply(now, Action::Explore, 1000) != Error::None || now.phase != Phase::Encounter) continue;
        CHECK(now.wildRules == 19 && forms::productionForm(now.wildFormId));
        newWild += now.wildFormId > forms::kRules18FormCount;
    }
    CHECK(migrated >= 100 && finished == migrated && newWild > 0);
    std::printf("Rules 19 migration: %u mid-encounter rules-18 saves finished their fight; %u next encounters met new forms\n", migrated, newWild);
}

std::uint32_t crc32(const std::uint8_t* bytes, std::size_t length) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

struct RosterCapGuard {
    explicit RosterCapGuard(std::size_t cap) { setHistoricalRosterCap(cap); }
    ~RosterCapGuard() { setHistoricalRosterCap(0); }
};

void historicalFullBox() {
    State fight = newDevice(3);
    CHECK(apply(fight, Action::Hatch, 1) == Error::None);
    CHECK(apply(fight, Action::Mode, 1) == Error::None);
    CHECK(apply(fight, Action::Explore, 1000) == Error::None && fight.phase == Phase::Encounter);
    const auto founder = fight.collection[0];
    CHECK(founder.formId != 18);
    fight.collectionCount = 60;
    fight.captures = 59;
    fight.steps = 6000;
    fight.stepCredit = 0;
    fight.encounters = 60 + fight.walkingEncounters;
    fight.sequence = fight.foregroundSequence = 80;
    fight.nextMemberId = 61;
    for (unsigned i = 1; i < 60; ++i) {
        fight.collection[i] = founder;
        fight.collection[i].id = i + 1;
        fight.collection[i].capturedAtSequence = i + 1;
    }
    fight.wildFormId = 18;
    fight.wildSpecies = Species::Agumon;
    fight.wildLevel = 1;
    fight.wildRules = 19;
    fight.wildMaxHp = fight.wildHp = combat::formProfile(18, 1).stats.maxHp;
    CHECK(isValid(fight) && fight.collection[0].formId != fight.wildFormId);
    Snapshot current;
    CHECK(encodeSnapshot(fight, current));
    std::uint8_t schema26[kSchema26SnapshotSize];
    snapshot_test::schema26Image(current.bytes, schema26);
    schema26[4] = 26;
    schema26[5] = 0;
    schema26[6] = static_cast<std::uint8_t>(3204);
    schema26[7] = static_cast<std::uint8_t>(3204 >> 8);
    snapshot_test::put32(schema26 + kSchema26SnapshotSize - 4, crc32(schema26, kSchema26SnapshotSize - 4));
    State migrated;
    CHECK(decodeSnapshot(schema26, sizeof(schema26), migrated) == SnapshotStatus::Migrated);
    CHECK(migrated.dungeonKeys == 3 && migrated.collectionCount == 60 && migrated.wildRules == 19);
    fight.wildHp = fight.wildMaxHp / 2;
    {
        RosterCapGuard guard(kRoster60Capacity);
        CHECK(captureChance(fight) == 0);
        auto blocked = fight;
        blocked.battleMode = BattleMode::Tactical;
        CHECK(apply(blocked, Action::Capture, 0) == Error::CollectionFull && blocked.collectionCount == 60);
    }
    CHECK(captureChance(fight) > 0);
    auto continued = fight;
    continued.wildHp = continued.wildMaxHp;
    {
        RosterCapGuard guard(kRoster60Capacity);
        for (unsigned guardTurn = 0; guardTurn < 16 && continued.phase == Phase::Encounter; ++guardTurn) {
            const bool focus = continued.autoCapture == AutoCapture::FocusStrike || continued.autoCapture == AutoCapture::FocusBlock;
            CHECK((focus ? applyFocus(continued, kFocusNoTap) : applyAutoFight(continued)) == Error::None);
            CHECK(continued.autoCapture != AutoCapture::Awaiting && continued.collectionCount == 60 && continued.captures == 59);
        }
        CHECK(continued.phase == Phase::Home && continued.captures == 59);
    }
    auto paused = fight;
    paused.wildHp = paused.wildMaxHp;
    for (unsigned guardTurn = 0; guardTurn < 16 && paused.phase == Phase::Encounter && paused.autoCapture != AutoCapture::Awaiting; ++guardTurn) {
        const bool focus = paused.autoCapture == AutoCapture::FocusStrike || paused.autoCapture == AutoCapture::FocusBlock;
        CHECK((focus ? applyFocus(paused, kFocusNoTap) : applyAutoFight(paused)) == Error::None);
    }
    CHECK(paused.autoCapture == AutoCapture::Awaiting && paused.collectionCount == 60 && paused.captures == 59);
}
} // namespace

int main() {
    versions(); retiredDuplicates(); frozenPool(); rosterAndKeys(); migrationMidEncounter(); historicalFullBox();
    std::printf("rules 19: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
