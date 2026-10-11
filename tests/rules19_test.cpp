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
    for (const auto& scenario : expeditions::kScenarios) {
        const auto count = expeditions::variantCount(scenario);
        CHECK(count >= 8 && std::strcmp(expeditions::variantName(scenario, 0), scenario.name) == 0);
        for (std::uint8_t i = 0; i < count; ++i) {
            const auto* name = expeditions::variantName(scenario, i);
            CHECK(name && name[0] && std::strlen(name) <= 22);
            for (const auto* letter = name; *letter; ++letter) {
                const char upper = *letter >= 'a' && *letter <= 'z' ? static_cast<char>(*letter - 32) : *letter;
                CHECK(upper == ' ' || (upper >= 'A' && upper <= 'Z'));
            }
            for (std::uint8_t earlier = 0; earlier < i; ++earlier)
                CHECK(std::strcmp(name, expeditions::variantName(scenario, earlier)) != 0);
        }
    }
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
    // Rules 13-18 draw only from forms 1..276. Rules 19 draws production forms
    // that have a file in the installed device pack, so later forms are not wild.
    for (unsigned partner : {12u, 13u, 14u, 125u})
        for (unsigned seed = 1; seed <= 3000; ++seed) {
            const auto old = encounters::rules18::selectProduction(1 + seed % 40, seed * 2654435761u, partner, 40);
            const auto now = encounters::selectProduction(1 + seed % 40, seed * 2654435761u, partner, 40);
            CHECK(old >= forms::kFirstProductionFormId && old <= forms::kRules18FormCount);
            CHECK(forms::productionForm(now) && forms::hasInstalledArt(now) && !forms::retiredAliasOf(now));
        }
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
        CHECK(now.wildRules == 19 && forms::productionForm(now.wildFormId) && forms::hasInstalledArt(now.wildFormId));
        newWild += now.wildFormId > forms::kRules18FormCount;
    }
    CHECK(migrated >= 100 && finished == migrated && newWild == 0);
    std::printf("Rules 19 migration: %u mid-encounter rules-18 saves finished their fight; next encounters stayed on installed art\n", migrated);
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

State raisedPartner(std::uint32_t seed, std::uint32_t level) {
    State s = newDevice(seed);
    CHECK(apply(s, Action::Hatch, 1) == Error::None);
    if (level > s.level) {
        auto& m = s.collection[0];
        m.level = level;
        m.xp = xpForLevel(level);
        m.hp = combat::formProfile(m.formId, level).stats.maxHp;
        m.energy = 100;
        s.level = level;
        s.hp = m.hp;
        s.energy = 100;
        CHECK(isValid(s));
    }
    return s;
}
bool beatExpedition(State& s, unsigned& hits) {
    hits = 0;
    const auto foe = s.wildFormId;
    const auto foeLevel = s.wildLevel;
    const auto foeHp = s.wildMaxHp;
    while (s.phase == Phase::Encounter && hits < 30) {
        const auto error = apply(s, Action::Attack);
        if (error != Error::None) {
            std::fprintf(stderr, "attack %u error %u vs form %u lv %u hp %u\n", hits, static_cast<unsigned>(error), foe, foeLevel, foeHp);
            return false;
        }
        ++hits;
    }
    const auto* form = forms::find(foe);
    const bool victory = s.phase == Phase::Home && (s.message == Message::Won || s.message == Message::Trained);
    if (!victory || hits < 4 || hits > 28) {
        std::fprintf(stderr, "fight ended after %u hits vs %s lv %u hp %u message %u\n",
            hits, form ? form->name : "?", foeLevel, foeHp, static_cast<unsigned>(s.message));
        return false;
    }
    return true;
}
void expeditionRewards() {
    Action parsed = Action::Feed;
    CHECK(parseAction("start-expedition", parsed) && parsed == Action::StartExpedition);
    CHECK(parseAction("focus", parsed) && parsed == Action::Focus);
    State wins = raisedPartner(9, 1);
    CHECK(wins.dungeonKeys == 0 && wins.bossSigils == 0 && wins.dungeonWins == 0 && wins.bossSteps == 0);
    const auto blocked = wins.sequence;
    CHECK(apply(wins, Action::StartExpedition, 0) == Error::InvalidAction && wins.sequence == blocked && wins.dungeonKeys == 0);
    CHECK(apply(wins, Action::StartExpedition, 6) == Error::InvalidValue && wins.dungeonKeys == 0);
    for (unsigned i = 0; i < 8; ++i) {
        CHECK(apply(wins, Action::Walk, 100) == Error::None && wins.phase == Phase::Encounter);
        wins.wildHp = 1;
        CHECK(isValid(wins));
        const auto err = apply(wins, Action::Attack);
        const bool victory = wins.phase == Phase::Home && (wins.message == Message::Won || wins.message == Message::Trained);
        CHECK(err == Error::None && victory);
    }
    CHECK(wins.dungeonKeys == 1 && wins.dungeonWins == 0 && wins.bossSteps == 0 && wins.bossSigils == 0);
    CHECK(apply(wins, Action::Walk, 100) == Error::None);
    wins.wildHp = 1;
    CHECK(apply(wins, Action::Attack) == Error::None && wins.dungeonKeys == 1 && wins.dungeonWins == 1);
    wins.dungeonKeys = 3;
    wins.dungeonWins = 7;
    CHECK(isValid(wins));
    CHECK(apply(wins, Action::Walk, 100) == Error::None);
    wins.wildHp = 1;
    CHECK(apply(wins, Action::Attack) == Error::None && wins.dungeonKeys == 3 && wins.dungeonWins == 7);

    State steps = raisedPartner(4, 1);
    for (unsigned i = 0; i < 5; ++i) CHECK(apply(steps, Action::AccrueSteps, 1000) == Error::None);
    CHECK(steps.bossSigils == 1 && steps.bossSteps == 0 && steps.dungeonKeys == 0);
    for (unsigned i = 0; i < 5; ++i) CHECK(apply(steps, Action::AccrueSteps, 1000) == Error::None);
    CHECK(steps.bossSigils == 1 && steps.bossSteps == expeditions::kBossStepsPerSigil - 1);
    State walking = raisedPartner(6, 1);
    CHECK(apply(walking, Action::Walk, 100) == Error::None && walking.phase == Phase::Encounter);
    CHECK(walking.bossSteps == 0 && walking.bossSigils == 0);
    steps.bossSigils = 0;
    CHECK(isValid(steps));
    CHECK(apply(steps, Action::AccrueSteps, 1) == Error::None && steps.bossSigils == 1 && steps.bossSteps == 0);
    State explored = raisedPartner(5, 1);
    CHECK(apply(explored, Action::Explore, 250) == Error::None && explored.bossSteps == 250 && explored.bossSigils == 0);

    for (const auto level : {1u, 12u}) {
        for (unsigned scenario = 0; scenario < 6; ++scenario) {
            State s = raisedPartner(20 + level * 10 + scenario, level);
            const bool dungeon = scenario < 3;
            if (dungeon) s.dungeonKeys = 1; else s.bossSigils = 1;
            CHECK(isValid(s));
            const auto partnerTier = forms::combatTier(activeMember(s)->formId);
            const auto partnerLevel = s.level;
            CHECK(apply(s, Action::StartExpedition, scenario) == Error::None);
            CHECK(s.phase == Phase::Encounter && isValid(s));
            CHECK(dungeon ? s.dungeonKeys == 0 : s.bossSigils == 0);
            const auto* foe = forms::find(s.wildFormId);
            CHECK(foe && foe->type && forms::productionForm(s.wildFormId) && forms::hasInstalledArt(s.wildFormId));
            const auto* partnerForm = forms::find(activeMember(s)->formId);
            const auto* theme = expeditions::themeName(expeditions::kScenarios[scenario].theme);
            const bool partnerMatchesTheme = partnerForm && partnerForm->type && std::strcmp(partnerForm->type, theme) == 0;
            CHECK(std::strcmp(foe->type, theme) == 0 || partnerMatchesTheme);
            CHECK(forms::combatTier(s.wildFormId) > partnerTier || s.wildLevel >= partnerLevel);
            CHECK(s.wildMaxHp == combat::formProfile(s.wildFormId, s.wildLevel).stats.maxHp && s.wildHp == s.wildMaxHp);
            unsigned hits = 0;
            CHECK(beatExpedition(s, hits));
        }
    }
    CHECK(forms::hasInstalledArt(11) && forms::hasInstalledArt(18) && !forms::hasInstalledArt(13) && !forms::hasInstalledArt(277));
    State claim = raisedPartner(40, 5);
    claim.dungeonKeys = 1;
    CHECK(isValid(claim));
    std::uint32_t form = 0, foeLevel = 0;
    CHECK(previewExpeditionFoe(claim, 0, form, foeLevel) && forms::hasInstalledArt(form));
    const auto xp = activeMember(claim)->xp;
    const auto value = expeditions::claimExpeditionValue(0, form, foeLevel);
    CHECK(apply(claim, Action::ClaimExpedition, value) == Error::None);
    CHECK(claim.phase == Phase::Home && claim.dungeonKeys == 0 && activeMember(claim)->xp > xp);
    CHECK(claim.message == Message::Won || claim.message == Message::Trained);
    CHECK(apply(claim, Action::ClaimExpedition, value) == Error::InvalidAction);
    State gap = raisedPartner(41, 5);
    gap.dungeonKeys = 1;
    CHECK(apply(gap, Action::ClaimExpedition, expeditions::claimExpeditionValue(0, 13, 1)) == Error::InvalidValue);
    CHECK(gap.dungeonKeys == 1 && gap.phase == Phase::Home);
    CHECK(parseAction("claim-expedition", parsed) && parsed == Action::ClaimExpedition);
}

void levelNinetyNine() {
    CHECK(kMaxLevel == 99 && kMaxXp == 194040 && forms::kMaxRpgLevel == 99);
    CHECK(xpForLevel(50) == 49000 && xpForLevel(99) == 194040 && levelForXp(193999) == 98 && levelForXp(194040) == 99);
    State partner = raisedPartner(3, 99);
    CHECK(partner.level == 99 && partner.collection[0].xp == 194040 && partner.collection[0].level == 99);
    Snapshot saved;
    CHECK(encodeSnapshot(partner, saved));
    State loaded;
    CHECK(decodeSnapshot(saved.bytes, sizeof(saved.bytes), loaded) == SnapshotStatus::Ok);
    CHECK(loaded.level == 99 && loaded.hp == partner.hp && loaded.collection[0].xp == 194040 && loaded.collection[0].level == 99);
    unsigned ultimates = 0, megas = 0, ultimateLow = 99, ultimateHigh = 0, megaLow = 99, megaHigh = 0;
    for (std::size_t i = 0; i < forms::edgeCount(); ++i) {
        const auto* edge = forms::edgeAt(i);
        const auto* dest = forms::find(edge->to);
        const auto need = forms::evolutionNeed(*edge);
        const auto assigned = forms::unlockLevel(dest->stage, dest->minLevel, dest->id);
        if (!assigned) continue;
        CHECK(need.level == assigned);
        if (dest->stage == forms::Stage::Mega) {
            CHECK(need.level >= 50 && need.level <= 70);
            if (need.level < megaLow) megaLow = need.level;
            if (need.level > megaHigh) megaHigh = need.level;
            ++megas;
        } else {
            CHECK(need.level >= 20 && need.level <= 35);
            if (need.level < ultimateLow) ultimateLow = need.level;
            if (need.level > ultimateHigh) ultimateHigh = need.level;
            ++ultimates;
        }
    }
    CHECK(ultimates > 8 && megas > 8 && ultimateLow == 20 && ultimateHigh == 35 && megaLow == 50 && megaHigh == 70);
}
int main() {
    versions(); retiredDuplicates(); frozenPool(); rosterAndKeys(); migrationMidEncounter(); historicalFullBox(); expeditionRewards(); levelNinetyNine();
    std::printf("rules 19: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
