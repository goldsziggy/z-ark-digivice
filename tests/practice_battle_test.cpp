#include "practice_battle.hpp"
#include "combat.hpp"
#include "forms.hpp"
#include "practice_auto_policy.hpp"
#include "legacy_combat_v8.hpp"
#include "legacy_combat_v3.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace p = digivice::practice;
namespace c = digivice::combat;
namespace {
int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::fprintf(stderr, "%d: %s\n", __LINE__, #x); } } while (false)
bool same(const p::State& a, const p::State& b) {
    p::Snapshot first, second;
    return p::encodeSnapshot(a, first) && p::encodeSnapshot(b, second) &&
        std::memcmp(first.bytes, second.bytes, p::kSnapshotSize) == 0;
}
p::State legacyBattle(std::uint32_t seed, std::uint32_t species, std::uint32_t level,
                     std::uint32_t enemy, std::uint32_t enemyLevel) {
    auto s = p::newBattle(seed, species, level, enemy, enemyLevel);
    s.rulesVersion = 2; s.playerFormId = s.enemyFormId = 0;
    s.playerHp = digivice::legacy_v3::combat::profile(species, level).stats.maxHp;
    s.enemyHp = digivice::legacy_v3::combat::profile(enemy, enemyLevel).stats.maxHp;
    return s;
}
void step(p::State& s, const char* action, unsigned value = 0) {
    CHECK(p::apply(s, action, value) == p::Error::None); CHECK(p::isValid(s));
}
void rejects(p::State& s, const char* action, unsigned value, p::Error error) {
    const auto before = s;
    CHECK(p::apply(s, action, value) == error); CHECK(same(before, s));
}
// Controlled hidden commitment fixtures exercise every valid matrix cell, without
// making the public API accept enemy choices or changing the seeded production AI.
void commit(p::State& s, p::Move move) {
    const auto base = s.phase == p::Phase::Attack ? 4u : 1u;
    s.enemyChoice = move;
    s.excludedChoice = static_cast<p::Move>(base + (static_cast<unsigned>(move) - base + 1) % 3);
    CHECK(p::isValid(s));
}
void matrix() {
    // Fixed roster example: Mote Lv.1 attacking Flicker Lv.1, then vice versa.
    constexpr unsigned outgoing[3][3] = {{7,14,14},{22,11,22},{12,12,6}};
    constexpr unsigned incoming[3][3] = {{7,14,14},{22,11,22},{6,6,3}};
        for (unsigned row = 0; row < 3; ++row) for (unsigned col = 0; col < 3; ++col) {
            const auto attacking = static_cast<p::Move>(row + 1), defending = static_cast<p::Move>(col + 4);
            const bool reflects = row == 1 && col == 1;
            const auto amount = outgoing[row][col];
            auto s = legacyBattle(12345, 1, 1, 2, 1);
            commit(s, defending);
            step(s, p::moveName(attacking));
            CHECK(s.playerHp == (reflects ? 100 - amount : 100));
            CHECK(s.enemyHp == (reflects ? 88 : 88 - amount));
            CHECK(s.lastReflected == reflects && s.phase == p::Phase::Defend);
            CHECK(s.lastPlayerDamage == (reflects ? amount : 0));
            CHECK(s.lastEnemyDamage == (reflects ? 0 : amount));
            // Valid symmetric defensive phase: damage applies to the correct actor.
            s.playerHp = 100; s.enemyHp = 88;
            commit(s, attacking);
            step(s, p::moveName(defending));
            CHECK(s.playerHp == (reflects ? 100 : 100 - incoming[row][col]));
            CHECK(s.enemyHp == (reflects ? 88 - incoming[row][col] : 88));
            CHECK(s.lastReflected == reflects && s.phase == p::Phase::Attack);
        }
}
void cardsAndErrors() {
    auto s = legacyBattle(12345, 1, 1, 2, 1);
    CHECK(legacyBattle(0, 1, 1, 2, 1).rngState != 0);
    CHECK(!p::isValid(legacyBattle(1, 1, 999, 2, 1)));
    CHECK(!p::isValid(legacyBattle(1, 2, 2, 1, 1)));
    rejects(s, "brace", 0, p::Error::WrongPhase);
    rejects(s, "capture", 0, p::Error::InvalidAction);
    rejects(s, "physical", 1, p::Error::InvalidValue);
    rejects(s, "card", 0, p::Error::InvalidValue);
    const auto rng = s.rngState; const auto intent = s.enemyChoice; const auto excluded = s.excludedChoice;
    step(s, "card", 1);
    CHECK(s.rngState == rng && s.enemyChoice == intent && s.excludedChoice == excluded);
    CHECK(s.exchanges == 0 && s.sequence == 1 && s.attackBoost == 5);
    rejects(s, "card", 2, p::Error::CardUsed);
    commit(s, p::Move::Counter);
    step(s, "heavy");
    CHECK(s.playerHp == 89 && s.attackBoost == 5); // Countered attacks do not spend Spark.
    commit(s, p::Move::Heavy);
    step(s, "counter");
    CHECK(s.enemyHp == 77 && s.attackBoost == 5); // Spark cannot amplify reflected damage.
    commit(s, p::Move::Brace);
    step(s, "physical");
    CHECK(s.lastEnemyDamage == 12 && s.attackBoost == 0);

    s = legacyBattle(12345, 1, 1, 2, 1);
    step(s, "card", 2);
    commit(s, p::Move::Counter);
    step(s, "heavy");
    CHECK(s.lastReflected && s.lastPlayerDamage == 0 && s.playerHp == 100 && s.shield == 1);
    s = legacyBattle(12345, 1, 1, 2, 1);
    step(s, "card", 2); commit(s, p::Move::Brace); step(s, "physical");
    commit(s, p::Move::Heavy); step(s, "ward");
    CHECK(s.lastPlayerDamage == 10 && s.playerHp == 90 && s.shield == 0);
    step(s, "retreat");
    CHECK(s.status == p::Status::Retreated && s.phase == p::Phase::Finished);
    rejects(s, "card", 1, p::Error::Finished);
    rejects(s, "physical", 0, p::Error::Finished);
    auto before = legacyBattle(123, 1, 1, 2, 1); step(before, "retreat");
    CHECK(before.sequence == 1 && before.exchanges == 0);
}
void endings() {
    auto s = legacyBattle(12345, 1, 1, 1, 1);
    // The matched low-damage choices can reach the bounded draw at 30 exchanges.
    for (unsigned i = 0; i < 30; ++i) {
        commit(s, s.phase == p::Phase::Attack ? p::Move::Ward : p::Move::Magic);
        step(s, s.phase == p::Phase::Attack ? "magic" : "ward");
    }
    CHECK(s.status == p::Status::Draw && s.playerHp == 40 && s.enemyHp == 40);
    CHECK(s.enemyChoice == p::Move::None && s.excludedChoice == p::Move::None);
    rejects(s, "physical", 0, p::Error::Finished);
    for (bool win : {false, true}) {
        s = legacyBattle(456, 1, 3, 2, 1);
        for (unsigned i = 0; i < 30 && s.status == p::Status::Active; ++i) {
            const bool offense = s.phase == p::Phase::Attack;
            commit(s, offense ? p::Move::Counter : p::Move::Heavy);
            step(s, offense ? (win ? "physical" : "heavy") : (win ? "counter" : "ward"));
        }
        CHECK(s.status == (win ? p::Status::Won : p::Status::Lost));
        CHECK((win ? s.enemyHp : s.playerHp) == 0);
        CHECK(s.lastEnemyDamage <= 88 && s.lastPlayerDamage <= 136);
    }
}
void persistenceAndPrivacy() {
    auto a = legacyBattle(12345, 1, 2, 2, 1), b = a;
    for (unsigned i = 0; i < 8; ++i) {
        p::Snapshot snapshot; CHECK(p::encodeSnapshot(a, snapshot));
        CHECK(p::decodeSnapshot(snapshot.bytes, p::snapshotSize(a), b));
        const char* choice = a.phase == p::Phase::Attack ? "magic" : "brace";
        step(a, choice); step(b, choice); CHECK(same(a, b));
        CHECK(a.enemyChoice != a.excludedChoice);
        char json[p::kJsonCapacity]; CHECK(p::writePublicJson(a, json, sizeof(json)) > 0);
        CHECK(std::strstr(json, "rngState") == nullptr && std::strstr(json, "excludedChoice") == nullptr);
        CHECK(std::strstr(json, "seed") == nullptr && std::strstr(json, "snapshot") == nullptr);
    }
    p::Snapshot snapshot; CHECK(p::encodeSnapshot(a, snapshot));
    for (std::size_t i = 0; i < p::kV2SnapshotSize; ++i) {
        auto bad = snapshot; bad.bytes[i] ^= 0x40;
        const auto before = b;
        CHECK(!p::decodeSnapshot(bad.bytes, p::snapshotSize(a), b)); CHECK(same(before, b));
    }
    CHECK(!p::decodeSnapshot(nullptr, 0, b));
    CHECK(!p::decodeSnapshot(snapshot.bytes, p::kSnapshotSize - 1, b));
    char tiny[2] = {'x','x'};
    CHECK(p::writePublicJson(a, tiny, sizeof(tiny)) == 0 && tiny[0] == '\0');
}
void profiles() {
    auto young = legacyBattle(42, 1, 1, 2, 1), evolved = legacyBattle(42, 1, 3, 2, 1);
    CHECK(young.playerHp == 100 && evolved.playerHp == 136);
    CHECK(young.enemyHp == 88 && evolved.enemyHp == 88 && evolved.enemyLevel == 1);
    commit(young, p::Move::Brace); commit(evolved, p::Move::Brace);
    step(young, "physical"); step(evolved, "physical");
    CHECK(young.lastEnemyDamage == 7 && evolved.lastEnemyDamage == 14);
    commit(young, p::Move::Physical); commit(evolved, p::Move::Physical);
    step(young, "brace"); step(evolved, "brace");
    CHECK(young.lastPlayerDamage == 7 && evolved.lastPlayerDamage == 2);
    auto tide = legacyBattle(42, 3, 1, 4, 1), ember = legacyBattle(42, 4, 1, 3, 1);
    commit(tide, p::Move::Counter); commit(ember, p::Move::Counter);
    step(tide, "magic"); step(ember, "magic");
    CHECK(tide.lastEnemyDamage == 20 && ember.lastEnemyDamage == 6);
    char json[p::kJsonCapacity]; CHECK(p::writePublicJson(evolved, json, sizeof(json)) > 0);
    CHECK(std::strstr(json, "\"playerCombat\":{\"maxHp\":136") != nullptr);
    CHECK(std::strstr(json, "\"enemyCombat\":{\"maxHp\":88") != nullptr);
    CHECK(std::strstr(json, "\"magic\":\"Solar Bloom\"") != nullptr);
    CHECK(std::strstr(json, "speed") == nullptr);
}
using LegacyFields = std::array<std::uint32_t, 22>;
std::array<std::uint8_t, p::kLegacySnapshotSize> legacyRecord(const LegacyFields& fields) {
    std::array<std::uint8_t, p::kLegacySnapshotSize> bytes{};
    std::memcpy(bytes.data(), "DGBP", 4); bytes[4] = 1; bytes[6] = 88;
    const auto put = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
    };
    for (std::size_t i = 0; i < fields.size(); ++i) put(8 + i * 4, fields[i]);
    auto crc = 0xffffffffu;
    for (std::size_t i = 0; i < 96; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    put(96, ~crc); return bytes;
}
void migration() {
    // Genuine v1 history: Lv.2 start, Spark, physical, brace. Old HP 80/79.
    const LegacyFields active{{1,3,0xc3b81262u,2,80,79,2,0,0,1,0,0,6,4,6,2,4,2,20,0,0,1}};
    auto bytes = legacyRecord(active);
    auto state = legacyBattle(5, 1, 1, 2, 1);
    CHECK(!p::decodeSnapshot(bytes.data(), bytes.size(), state)); // Never implicit.
    CHECK(p::migrateV1(bytes.data(), bytes.size(), 1, 2, state));
    CHECK(state.playerHp == 93 && state.enemyHp == 70);
    CHECK(state.playerSpecies == 1 && state.playerLevel == 2 && state.enemySpecies == 2 && state.enemyLevel == 1);
    CHECK(state.sequence == 3 && state.exchanges == 2 && state.cardUsed);
    CHECK(state.rngState == 0xc3b81262u && state.enemyChoice == p::Move::Ward && state.excludedChoice == p::Move::Brace);
    CHECK(state.lastPlayerDamage == 20 && state.lastEnemyMove == p::Move::Heavy && state.previousEnemyMove == p::Move::Ward);
    p::Snapshot current; CHECK(p::encodeSnapshot(state, current));
    auto restored = state; CHECK(p::decodeSnapshot(current.bytes, p::snapshotSize(state), restored));
    step(state, "magic"); step(restored, "magic"); CHECK(same(state, restored)); CHECK(state.lastEnemyDamage == 9);
    auto low = active; low[4] = low[5] = 1; low[11] = 7;
    bytes = legacyRecord(low); CHECK(p::migrateV1(bytes.data(), bytes.size(), 1, 2, state));
    CHECK(state.playerHp == 2 && state.enemyHp == 1 && state.shield == 7);
    for (unsigned status = 1; status <= 4; ++status) {
        auto ended = active; ended[7] = 2; ended[8] = status; ended[12] = ended[13] = 0;
        if (status == 1) ended[5] = 0;
        if (status == 2) ended[4] = 0;
        if (status == 3) { ended[6] = 30; ended[1] = 31; }
        if (status == 4) ended[1] = 4;
        bytes = legacyRecord(ended); CHECK(p::migrateV1(bytes.data(), bytes.size(), 1, 2, state));
        CHECK(static_cast<unsigned>(state.status) == status && state.phase == p::Phase::Finished);
        CHECK(state.playerHp == (status == 2 ? 0u : 93u) && state.enemyHp == (status == 1 ? 0u : 70u));
    }
    const auto before = state; bytes = legacyRecord(active);
    CHECK(!p::migrateV1(bytes.data(), bytes.size(), 1, 1, state)); CHECK(same(before, state));
    CHECK(!p::migrateV1(bytes.data(), bytes.size(), 2, 2, state)); CHECK(same(before, state));
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        auto corrupted = bytes; corrupted[i] ^= 1;
        CHECK(!p::migrateV1(corrupted.data(), corrupted.size(), 1, 2, state)); CHECK(same(before, state));
    }
    auto invalid = active; invalid[12] = 256;
    bytes = legacyRecord(invalid); CHECK(!p::migrateV1(bytes.data(), bytes.size(), 1, 2, state)); CHECK(same(before, state));
    CHECK(!p::migrateV1(current.bytes, sizeof(current.bytes), 1, 2, state));
}
void automaticBattles() {
    namespace a = digivice::autobattle;
    for (unsigned seed = 0; seed < 128; ++seed) {
        const auto initial = legacyBattle(seed, 1, 1 + seed % 3, 2, 1);
        auto first = initial, second = initial, replay = initial;
        a::Trace trace, again;
        CHECK(p::runAuto(first, trace) == p::Error::None && p::isValid(first));
        CHECK(p::runAuto(second, again) == p::Error::None && same(first, second));
        CHECK(first.phase == p::Phase::Finished && first.status != p::Status::Active);
        CHECK(!first.cardUsed && first.attackBoost == 0 && first.shield == 0);
        CHECK(trace.kind == a::Kind::Practice && trace.count > 0 && trace.count <= p::kMaxExchanges);
        CHECK(trace.startSequence == 0 && trace.endSequence == first.sequence && trace.count == first.exchanges);
        CHECK(trace.playerSpecies == initial.playerSpecies && trace.playerLevel == initial.playerLevel);
        CHECK(trace.enemySpecies == initial.enemySpecies && trace.enemyLevel == initial.enemyLevel);
        for (std::size_t i = 0; i < trace.count; ++i) {
            const auto& event = trace.steps[i];
            CHECK(event.playerHpBefore == replay.playerHp && event.enemyHpBefore == replay.enemyHp);
            CHECK(event.defending == (replay.phase == p::Phase::Defend) && !event.captured);
            CHECK(p::apply(replay, a::moveName(event.action)) == p::Error::None);
            CHECK(event.playerHpAfter == replay.playerHp && event.enemyHpAfter == replay.enemyHp);
            CHECK(event.opponentAction == static_cast<a::Move>(replay.lastOpponentChoice));
            CHECK(event.reflected == replay.lastReflected);
        }
        CHECK(same(first, replay)); // Trace is produced by the actual Tactical resolver.
        char json[a::kTraceJsonCapacity], repeated[a::kTraceJsonCapacity];
        CHECK(a::writeJson(trace, json, sizeof(json)) > 0);
        CHECK(a::writeJson(again, repeated, sizeof(repeated)) > 0 && std::strcmp(json, repeated) == 0);
        CHECK(!std::strstr(json, "rngState") && !std::strstr(json, "excludedChoice") && !std::strstr(json, "seed"));
        const auto before = first;
        CHECK(p::runAuto(first, again) == p::Error::InvalidState && same(first, before));
        CHECK(a::writeJson(again, repeated, sizeof(repeated)) > 0 && std::strcmp(json, repeated) == 0);
    }
    // Change only private commitment/hint, retaining the same policy seed.
    // Every chosen player action remains identical until one run terminates.
    auto left = legacyBattle(12345, 1, 1, 2, 1), right = left;
    commit(left, p::Move::Brace); commit(right, p::Move::Counter);
    a::Trace l, r;
    CHECK(p::runAuto(left, l) == p::Error::None && p::runAuto(right, r) == p::Error::None);
    for (std::size_t i = 0; i < l.count && i < r.count; ++i)
        CHECK(l.steps[i].action == r.steps[i].action && l.steps[i].defending == r.steps[i].defending);
    auto used = legacyBattle(6, 1, 1, 2, 1); step(used, "card", 1);
    auto before = used;
    CHECK(p::runAuto(used, l) == p::Error::InvalidState && same(used, before));
    used = legacyBattle(6, 1, 1, 2, 1); --used.playerHp; before = used;
    CHECK(p::runAuto(used, l) == p::Error::InvalidState && same(used, before));
    used = legacyBattle(6, 1, 1, 2, 1); step(used, "physical"); before = used;
    CHECK(p::runAuto(used, l) == p::Error::InvalidState && same(used, before));
    for (std::uint32_t species = 1; species <= c::kSpeciesCount; ++species) {
        auto state = legacyBattle(987, species, 1, 4, 1);
        CHECK(p::runAuto(state, l) == p::Error::None && p::isValid(state));
        CHECK(l.playerSpecies == species && l.playerLevel == 1 && l.enemySpecies == 4);
    }
}
void rpgForms() {
    namespace f = digivice::forms;
    for (std::uint32_t id = 1; id <= f::kFormCount; ++id) {
        const auto* form = f::find(id); CHECK(form != nullptr);
        if (!form) continue;
        for (const auto level : {static_cast<std::uint32_t>(form->minLevel), 20u}) {
            auto state = p::newBattleWithForms(1900 + id, form->lineage, level, id, 2, 1, f::initialForm(2));
            CHECK(p::isValid(state) && state.rulesVersion == 7 && state.playerFormId == id);
            CHECK(state.playerHp == c::formProfile(id, level).stats.maxHp);
            p::Snapshot initial; CHECK(p::encodeSnapshot(state, initial) && p::snapshotSize(state) == 120);
            CHECK(initial.bytes[4] == 7 && initial.bytes[6] == 108);
            auto restored = state; CHECK(p::decodeSnapshot(initial.bytes, 120, restored) && same(state, restored));
            digivice::autobattle::Trace trace, replay;
            CHECK(p::runAuto(state, trace) == p::Error::None && p::isValid(state));
            CHECK(p::runAuto(restored, replay) == p::Error::None && same(state, restored));
            CHECK(trace.combatRulesVersion == 4 && trace.playerFormId == id && trace.count <= p::maxExchanges(state));
            CHECK(state.playerFormId == id && state.playerLevel == level); // Practice grants no progression.
            char json[p::kJsonCapacity], traceJson[digivice::autobattle::kTraceJsonCapacity];
            CHECK(p::writePublicJson(state, json, sizeof(json)) > 0);
            CHECK(std::strstr(json, "\"rulesVersion\":7") && std::strstr(json, "\"playerFormName\":"));
            CHECK(digivice::autobattle::writeJson(trace, traceJson, sizeof(traceJson)) > 0);
            CHECK(std::strstr(traceJson, form->name));
        }
        const auto wrongLineage = form->lineage == 1 ? 2u : 1u;
        CHECK(!p::isValid(p::newBattleWithForms(2, wrongLineage, 20, id, 2, 1, f::initialForm(2))));
        if (form->minLevel > 1) CHECK(!p::isValid(p::newBattleWithForms(2, form->lineage, form->minLevel - 1, id, 2, 1, f::initialForm(2))));
    }
    CHECK(!p::isValid(p::newBattle(2, 1, 51, 2, 1)));
    auto state = p::newBattle(77, 5, 20, 2, 1);
    p::Snapshot bytes; CHECK(p::encodeSnapshot(state, bytes));
    for (std::size_t i = 0; i < p::kSnapshotSize; ++i) {
        auto bad = bytes; bad.bytes[i] ^= 1;
        const auto before = state;
        CHECK(!p::decodeSnapshot(bad.bytes, p::kSnapshotSize, state) && same(before, state));
    }
}

void versionedLimits() {
 for(unsigned version=2;version<=7;++version) {
  auto s=version==2?legacyBattle(12345,1,1,2,1):p::newBattle(12345,1,1,2,1);s.rulesVersion=version;
  CHECK(p::isValid(s)&&p::maxExchanges(s)==(version>=5?40u:30u));
  // Restored active battle at the last legal exchange, with valid public/private
  // history. Minimum-damage defense preserves both HP to exercise the cap itself.
  s.exchanges=29;s.sequence=29;s.phase=p::Phase::Defend;s.lastPhase=p::Phase::Attack;
  s.lastPlayerChoice=p::Move::Physical;s.lastOpponentChoice=s.lastEnemyMove=p::Move::Brace;
  s.previousEnemyMove=p::Move::Physical;s.lastPlayerDamage=0;s.lastEnemyDamage=1;s.lastReflected=false;
  commit(s,p::Move::Magic);step(s,"ward");
  CHECK(s.exchanges==30 && (version>=5?s.status==p::Status::Active:s.status==p::Status::Draw));
  if(version>=5) {s.exchanges=39;s.sequence=39;s.phase=p::Phase::Defend;s.lastPhase=p::Phase::Attack;
   s.lastPlayerChoice=p::Move::Physical;s.lastOpponentChoice=s.lastEnemyMove=p::Move::Brace;
   s.previousEnemyMove=p::Move::Physical;s.lastPlayerDamage=0;s.lastEnemyDamage=1;s.lastReflected=false;
   commit(s,p::Move::Magic);step(s,"ward");CHECK(s.exchanges==40&&s.status==p::Status::Draw);
  }
  p::Snapshot saved;CHECK(p::encodeSnapshot(s,saved));auto restored=s;
  CHECK(p::decodeSnapshot(saved.bytes,p::snapshotSize(s),restored)&&same(s,restored));
 }
}

void versionedDamageFloor() {
    CHECK(p::minimumRawDamage(1,248)==0 && p::minimumRawDamage(8,248)==0);
    CHECK(p::minimumRawDamage(6,0)==0 && p::minimumRawDamage(6,UINT32_MAX)==0);
    for(unsigned version=2;version<=5;++version) CHECK(p::minimumRawDamage(version,248)==4);
    CHECK(p::minimumRawDamage(6,1)==4 && p::minimumRawDamage(6,80)==4);
    CHECK(p::minimumRawDamage(6,81)==5 && p::minimumRawDamage(6,248)==13);
    CHECK(p::minimumRawDamage(6,640)==32 && p::minimumRawDamage(6,641)==0);
    namespace f=digivice::forms;
    for(unsigned id=1;id<=f::kFormCount;++id) for(unsigned level=f::find(id)->minLevel;level<=20;++level) {
        const auto hp=c::formProfile(id,level).stats.maxHp;
        const auto floor=p::minimumRawDamage(6,hp);
        CHECK(floor>=4 && floor<=32);
        unsigned outgoing=0,reflection=0;
        for(unsigned m=0;m<3;++m) for(unsigned g=1;g<4;++g) {
            const auto hit=c::resolveForms(id,level,id,level,static_cast<c::Move>(m),static_cast<c::Defense>(g),floor);
            if(hit.reflected) {if(hit.damage>reflection)reflection=hit.damage;}
            else if(hit.damage>outgoing)outgoing=hit.damage;
        }
        // Necessary finishability condition: even perfect cooperative play must
        // have enough possible damage within20 attack+20 defense exchanges.
        CHECK(20*(outgoing+reflection)>=hp);
    }
    // Both directions use the INTENDED defender's maximum, even if Counter
    // reflects the resulting hit. Test different HP profiles with a low raw hit.
    for(unsigned version: {5u,6u,7u}) {
        const auto* a=f::find(223);const auto* b=f::find(235);
        auto initial=p::newBattleWithForms(7,a->lineage,15,a->id,b->lineage,17,b->id);
        initial.rulesVersion=version;CHECK(p::isValid(initial));
        for(const auto defending:{p::Move::Brace,p::Move::Counter,p::Move::Ward}) {
            auto s=initial;commit(s,defending);
            const auto before=s;
            const auto guard=defending==p::Move::Brace?c::Defense::Brace:defending==p::Move::Counter?c::Defense::Counter:c::Defense::Ward;
            const auto expected=c::resolveForms(a->id,15,b->id,17,c::Move::Heavy,guard,p::minimumRawDamage(version,p::maxHp(s,true)));
            step(s,"heavy");
            CHECK(s.playerHp==before.playerHp-(expected.reflected?expected.damage:0));
            CHECK(s.enemyHp==before.enemyHp-(expected.reflected?0:expected.damage));
        }
        auto s=initial;commit(s,p::Move::Brace);step(s,"physical");commit(s,p::Move::Heavy);
        const auto before=s;
        const auto expected=c::resolveForms(b->id,17,a->id,15,c::Move::Heavy,c::Defense::Counter,p::minimumRawDamage(version,p::maxHp(s)));
        step(s,"counter");CHECK(s.playerHp==before.playerHp && s.enemyHp==before.enemyHp-expected.damage);
    }
    const auto* form=f::find(223);
    auto spark=p::newBattleWithForms(7,form->lineage,15,223,form->lineage,15,223);
    const auto initialHp=spark.playerHp;
    step(spark,"card",1);commit(spark,p::Move::Counter);step(spark,"heavy");
    CHECK(spark.playerHp==initialHp-6 && spark.attackBoost==5);
    commit(spark,p::Move::Heavy);step(spark,"counter");
    CHECK(spark.enemyHp==initialHp-6 && spark.attackBoost==5);
    commit(spark,p::Move::Brace);step(spark,"magic");
    CHECK(spark.enemyHp==initialHp-24 && spark.attackBoost==0);
    auto shield=p::newBattleWithForms(7,form->lineage,15,223,form->lineage,15,223);
    step(shield,"card",2);commit(shield,p::Move::Counter);step(shield,"heavy");
    CHECK(shield.playerHp==initialHp && shield.shield==6);
    commit(shield,p::Move::Physical);step(shield,"ward");
    CHECK(shield.playerHp==initialHp-7 && shield.shield==0);

}


void publicAutoPolicy() {
    namespace policy = p::auto_policy;
    // Hand-calculated Mote1 -> Flicker1 average HP swing: Physical35,
    // Heavy33 (one possible self-reflection), Magic30. No RNG on unique best.
    policy::View view{p::Phase::Attack,7,1,1,4,1,100,88};
    std::uint32_t random=12345;
    CHECK(policy::choose(view,random)==p::Move::Physical && random==12345);
    view.phase=p::Phase::Finished;
    CHECK(policy::choose(view,random)==p::Move::None && random==12345);
    view.phase=p::Phase::Attack;view.rulesVersion=8;
    CHECK(policy::choose(view,random)==p::Move::None && random==12345);
    // Equal13 raw floor gives basic sums32 each, Heavy20 after reflection.
    // Exact ties alone advance the separate deterministic random stream.
    view={p::Phase::Attack,7,223,15,223,15,248,248};
    unsigned seen=0;
    for(unsigned seed=1;seed<=32;++seed) {
        auto a=seed,b=seed;
        const auto choice=policy::choose(view,a);
        CHECK(choice==policy::choose(view,b) && a==b && a!=seed);
        CHECK(choice==p::Move::Physical || choice==p::Move::Magic);
        seen |= choice==p::Move::Physical?1u:2u;
    }
    CHECK(seen==3);
    view.enemyHp=1;
    for(unsigned seed=1;seed<=8;++seed) {
        auto rng=seed;CHECK(policy::choose(view,rng)!=p::Move::Heavy);
    }
    // Alter only hidden commitment and hint, not public stats/HP or policy seed:
    // the first choice must stay identical. Later choices may use changed HP.
    auto left=p::newBattle(12345,1,1,2,1),right=left;
    commit(left,p::Move::Brace);commit(right,p::Move::Counter);
    digivice::autobattle::Trace a,b;
    CHECK(p::runAuto(left,a)==p::Error::None && p::runAuto(right,b)==p::Error::None);
    CHECK(a.steps[0].action==b.steps[0].action);
    // Replay a complete new Auto using only its public projection each turn.
    // The native Tactical resolver still produces every recorded HP transition.
    auto state=p::newBattleWithForms(4567,1223,15,223,1223,15,223);
    const auto initial=state;digivice::autobattle::Trace trace;
    CHECK(p::runAuto(state,trace)==p::Error::None);
    auto replay=initial;std::uint32_t rng=replay.rngState^0xa341316cu;
    if(!rng)rng=0x9e3779b9u;
    for(std::size_t i=0;i<trace.count;++i) {
        const auto choice=policy::choose({replay.phase,replay.rulesVersion,replay.playerFormId,replay.playerLevel,
            replay.enemyFormId,replay.enemyLevel,replay.playerHp,replay.enemyHp},rng);
        CHECK(static_cast<unsigned>(trace.steps[i].action)==static_cast<unsigned>(choice));
        CHECK(p::apply(replay,p::moveName(choice))==p::Error::None);
        CHECK(trace.steps[i].playerHpAfter==replay.playerHp && trace.steps[i].enemyHpAfter==replay.enemyHp);
    }
    CHECK(same(state,replay) && trace.count<=40 && !state.cardUsed);
    CHECK(sizeof(policy::View)==32); // Fixed public input; no hidden state/heap.
}

void frozenProfileRouting() {
    namespace old = digivice::legacy_v8::combat;
    for(unsigned version:{5u,6u}) for(unsigned form:{3u,7u}) {
        const auto* definition=digivice::forms::find(form);
        auto state=p::newBattleWithForms(54321,definition->lineage,20,form,2,20,4);
        state.rulesVersion=version;state.playerHp=old::formProfile(form,20).stats.maxHp;
        CHECK(p::isValid(state) && p::maxHp(state)==state.playerHp);
        CHECK(p::maxHp(state)!=c::formProfile(form,20).stats.maxHp);
        char json[p::kJsonCapacity],profile[old::kProfileJsonCapacity];
        CHECK(p::writePublicJson(state,json,sizeof(json))>0);
        CHECK(old::writeFormProfileJson(form,20,profile,sizeof(profile))>0 && std::strstr(json,profile));
        digivice::autobattle::Trace trace;
        CHECK(p::runAuto(state,trace)==p::Error::None && trace.combatRulesVersion==8);
        char rendered[digivice::autobattle::kTraceJsonCapacity];
        CHECK(digivice::autobattle::writeJson(trace,rendered,sizeof(rendered))>0 && std::strstr(rendered,profile));
    }
}

} // namespace
int main() {
    matrix(); cardsAndErrors(); endings(); persistenceAndPrivacy(); profiles(); migration(); automaticBattles(); rpgForms(); versionedLimits(); versionedDamageFloor(); publicAutoPolicy(); frozenProfileRouting();
    std::printf("%d practice checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
