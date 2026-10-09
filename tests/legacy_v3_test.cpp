// Historical regression suite retained for the frozen rules-3 executor.
#include "legacy_v3.hpp"
#include "legacy_combat_v3.hpp"

#include <cstdio>
#include <cstring>
#include <limits>
#include <initializer_list>

namespace {
namespace autobattle = digivice::autobattle;
int checks = 0;
int failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (false)

bool same(const digivice::legacy_v3::State& first, const digivice::legacy_v3::State& second) {
    digivice::legacy_v3::Snapshot a, b;
    return digivice::legacy_v3::encodeSnapshot(first, a) && digivice::legacy_v3::encodeSnapshot(second, b) &&
           std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}
void step(digivice::legacy_v3::State& state, digivice::legacy_v3::Action action, std::uint32_t value = 0) {
    CHECK(digivice::legacy_v3::apply(state, action, value) == digivice::legacy_v3::Error::None);
    CHECK(digivice::legacy_v3::isValid(state));
}
void rejects(digivice::legacy_v3::State& state, digivice::legacy_v3::Action action, std::uint32_t value, digivice::legacy_v3::Error expected) {
    const auto before = state;
    unsigned char rawBefore[sizeof(state)];
    std::memcpy(rawBefore, &state, sizeof(state));
    CHECK(digivice::legacy_v3::apply(state, action, value) == expected);
    CHECK(same(state, before));
    CHECK(std::memcmp(rawBefore, &state, sizeof(state)) == 0);
}
void mirrorActive(digivice::legacy_v3::State& state) {
    auto& member = state.collection[state.activeCreatureId - 1];
    member.hp = state.hp; member.energy = state.energy; member.fullness = state.fullness;
    member.mood = state.mood; member.bond = state.bond; member.level = state.level;
}
bool sameMember(const digivice::legacy_v3::CreatureMember& a, const digivice::legacy_v3::CreatureMember& b) {
    return a.id == b.id && a.species == b.species && a.hp == b.hp && a.energy == b.energy &&
           a.fullness == b.fullness && a.mood == b.mood && a.bond == b.bond &&
           a.level == b.level && a.capturedAtSequence == b.capturedAtSequence;
}
void put32(std::uint8_t* bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
}
std::uint32_t crc(const std::uint8_t* bytes, std::size_t length) {
    std::uint32_t value = 0xffffffffu;
    for (std::size_t i = 0; i < length; ++i) {
        value ^= bytes[i];
        for (int j = 0; j < 8; ++j) value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0);
    }
    return ~value;
}
void seal(std::uint8_t* bytes, std::size_t length) { put32(bytes + length - 4, crc(bytes, length - 4)); }
struct PreviousSnapshot { std::uint8_t bytes[digivice::legacy_v3::kPreviousSnapshotSize]{}; };
PreviousSnapshot oldCollectionSnapshot(const digivice::legacy_v3::State& state, unsigned version = 4) {
    digivice::legacy_v3::Snapshot current;
    CHECK(digivice::legacy_v3::encodeSnapshot(state, current));
    PreviousSnapshot old;
    std::memcpy(old.bytes, current.bytes, sizeof(old.bytes) - 4);
    old.bytes[4] = static_cast<std::uint8_t>(version);
    old.bytes[6] = static_cast<std::uint8_t>((sizeof(old.bytes) - 12) & 255);
    old.bytes[7] = static_cast<std::uint8_t>((sizeof(old.bytes) - 12) >> 8);
    put32(old.bytes + 8, version == 3 ? 2 : 3);
    seal(old.bytes, sizeof(old.bytes));
    return old;
}

void replayAndCards() {
    auto first = digivice::legacy_v3::newGame();
    auto second = digivice::legacy_v3::newGame();
    const digivice::legacy_v3::Action actions[] = {digivice::legacy_v3::Action::Feed, digivice::legacy_v3::Action::Play,
        digivice::legacy_v3::Action::Walk, digivice::legacy_v3::Action::Card, digivice::legacy_v3::Action::Attack};
    const unsigned values[] = {0, 0, 100, 1, 0};
    for (unsigned i = 0; i < 5; ++i) {
        step(first, actions[i], values[i]);
        step(second, actions[i], values[i]);
        CHECK(same(first, second));
    }
    CHECK(first.sequence == 5 && first.steps == 100 && first.encounters == 1);
    CHECK(first.cardUsed && first.attackBoost == 0);
    rejects(first, digivice::legacy_v3::Action::Card, 2, digivice::legacy_v3::Error::CardAlreadyUsed);
    rejects(first, digivice::legacy_v3::Action::Feed, 0, digivice::legacy_v3::Error::WrongPhase);
    while (first.phase == digivice::legacy_v3::Phase::Encounter) step(first, digivice::legacy_v3::Action::Attack);
    CHECK(!first.cardUsed && first.shield == 0 && first.attackBoost == 0);
    step(first, digivice::legacy_v3::Action::Walk, 100);
    step(first, digivice::legacy_v3::Action::Card, 2);
    const auto before = first.hp;
    step(first, digivice::legacy_v3::Action::Attack);
    CHECK(first.hp == before && first.shield < 12);
    CHECK(digivice::legacy_v3::newGame(0).rngState != 0);
}

void captureAndGentleness() {
    auto state = digivice::legacy_v3::newGame();
    rejects(state, digivice::legacy_v3::Action::Capture, 0, digivice::legacy_v3::Error::WrongPhase);
    step(state, digivice::legacy_v3::Action::Walk, 100);
    rejects(state, digivice::legacy_v3::Action::Capture, 0, digivice::legacy_v3::Error::WildTooStrong);
    while (state.wildHp > state.wildMaxHp / 2) step(state, digivice::legacy_v3::Action::Attack);
    step(state, digivice::legacy_v3::Action::Capture);
    CHECK(state.captures == 1 && state.phase == digivice::legacy_v3::Phase::Home);
    rejects(state, digivice::legacy_v3::Action::Capture, 0, digivice::legacy_v3::Error::WrongPhase);

    bool found = false;
    for (unsigned seed = 1; seed <= 10000 && !found; ++seed) {
        auto trial = digivice::legacy_v3::newGame(seed);
        digivice::legacy_v3::apply(trial, digivice::legacy_v3::Action::Walk, 100);
        while (trial.wildHp > trial.wildMaxHp / 2) digivice::legacy_v3::apply(trial, digivice::legacy_v3::Action::Attack);
        for (unsigned attempt = 0; attempt < 3 && trial.phase == digivice::legacy_v3::Phase::Encounter; ++attempt)
            digivice::legacy_v3::apply(trial, digivice::legacy_v3::Action::Capture);
        if (trial.phase == digivice::legacy_v3::Phase::Encounter && trial.captureAttempts == 3) {
            rejects(trial, digivice::legacy_v3::Action::Capture, 0, digivice::legacy_v3::Error::CaptureLimit);
            found = true;
        }
    }
    CHECK(found);
    step(state, digivice::legacy_v3::Action::Walk, 100);
    state.hp = 1; // Valid near-empty HP boundary: a loss must never delete progress.
    mirrorActive(state);
    const auto captures = state.captures;
    step(state, digivice::legacy_v3::Action::Attack);
    CHECK(state.phase == digivice::legacy_v3::Phase::Home && state.hp == 10 && state.captures == captures);
    CHECK(state.message == digivice::legacy_v3::Message::Retreated);
    step(state, digivice::legacy_v3::Action::Rest);
    CHECK(state.hp == 35);
    for (unsigned i = 0; i < 100; ++i) step(state, digivice::legacy_v3::Action::Feed);
    CHECK(state.level == 3 && state.bond == 200 && state.fullness == 100);
    CHECK(std::strcmp(digivice::legacy_v3::creatureName(state), "Lumen") == 0);
    state.energy = 0;
    mirrorActive(state);
    rejects(state, digivice::legacy_v3::Action::Play, 0, digivice::legacy_v3::Error::LowEnergy);
    step(state, digivice::legacy_v3::Action::Rest);
    step(state, digivice::legacy_v3::Action::Play);
}

void walkingAndBounds() {
    auto state = digivice::legacy_v3::newGame();
    rejects(state, digivice::legacy_v3::Action::Walk, 0, digivice::legacy_v3::Error::InvalidValue);
    rejects(state, digivice::legacy_v3::Action::Walk, 1001, digivice::legacy_v3::Error::InvalidValue);
    rejects(state, digivice::legacy_v3::Action::Feed, 1, digivice::legacy_v3::Error::InvalidValue);
    rejects(state, digivice::legacy_v3::Action::Card, 3, digivice::legacy_v3::Error::InvalidValue);
    rejects(state, static_cast<digivice::legacy_v3::Action>(255), 0, digivice::legacy_v3::Error::InvalidAction);
    step(state, digivice::legacy_v3::Action::Walk, 99);
    CHECK(state.phase == digivice::legacy_v3::Phase::Home);
    step(state, digivice::legacy_v3::Action::Walk, 151);
    CHECK(state.stepCredit == 150 && state.phase == digivice::legacy_v3::Phase::Encounter);
    step(state, digivice::legacy_v3::Action::Walk, 150);
    CHECK(state.steps == 400 && state.stepCredit == 300 && state.encounters == 1);
    while (state.phase == digivice::legacy_v3::Phase::Encounter) step(state, digivice::legacy_v3::Action::Attack);
    step(state, digivice::legacy_v3::Action::Walk, 1);
    CHECK(state.stepCredit == 201 && state.encounters == 2);
    auto overflow = digivice::legacy_v3::newGame();
    overflow.steps = overflow.stepCredit = std::numeric_limits<std::uint32_t>::max();
    rejects(overflow, digivice::legacy_v3::Action::Walk, 1, digivice::legacy_v3::Error::CounterOverflow);
    overflow = digivice::legacy_v3::newGame();
    overflow.sequence = std::numeric_limits<std::uint32_t>::max();
    rejects(overflow, digivice::legacy_v3::Action::Rest, 0, digivice::legacy_v3::Error::CounterOverflow);
}

void persistence() {
    auto state = digivice::legacy_v3::newGame();
    step(state, digivice::legacy_v3::Action::Walk, 150);
    step(state, digivice::legacy_v3::Action::Card, 2);
    step(state, digivice::legacy_v3::Action::Attack);
    digivice::legacy_v3::Snapshot snapshot;
    CHECK(digivice::legacy_v3::encodeSnapshot(state, snapshot));
    auto restored = digivice::legacy_v3::newGame(99);
    CHECK(digivice::legacy_v3::decodeSnapshot(snapshot.bytes, sizeof(snapshot.bytes), restored) == digivice::legacy_v3::SnapshotStatus::Ok);
    CHECK(same(state, restored));
    step(state, digivice::legacy_v3::Action::Attack);
    step(restored, digivice::legacy_v3::Action::Attack);
    CHECK(same(state, restored));
    for (std::size_t byte = 0; byte < sizeof(snapshot.bytes); ++byte) {
        auto corrupt = snapshot;
        corrupt.bytes[byte] ^= 0x40;
        const auto before = restored;
        const auto status = digivice::legacy_v3::decodeSnapshot(corrupt.bytes, sizeof(corrupt.bytes), restored);
        CHECK(status != digivice::legacy_v3::SnapshotStatus::Ok && status != digivice::legacy_v3::SnapshotStatus::Migrated);
        CHECK(same(before, restored));
    }
    CHECK(digivice::legacy_v3::decodeSnapshot(snapshot.bytes, sizeof(snapshot.bytes) - 1, restored) == digivice::legacy_v3::SnapshotStatus::InvalidLength);
    CHECK(digivice::legacy_v3::decodeSnapshot(nullptr, 0, restored) == digivice::legacy_v3::SnapshotStatus::InvalidLength);
    auto future = snapshot;
    future.bytes[4] = 7;
    seal(future.bytes, sizeof(future.bytes));
    CHECK(digivice::legacy_v3::decodeSnapshot(future.bytes, sizeof(future.bytes), restored) == digivice::legacy_v3::SnapshotStatus::UnsupportedVersion);
    future = snapshot;
    put32(future.bytes + 8, 99);
    seal(future.bytes, sizeof(future.bytes));
    CHECK(digivice::legacy_v3::decodeSnapshot(future.bytes, sizeof(future.bytes), restored) == digivice::legacy_v3::SnapshotStatus::UnsupportedRules);
    future = snapshot;
    put32(future.bytes + 76, 256); // Reject before narrowing the serialized enum.
    seal(future.bytes, sizeof(future.bytes));
    CHECK(digivice::legacy_v3::decodeSnapshot(future.bytes, sizeof(future.bytes), restored) == digivice::legacy_v3::SnapshotStatus::InvalidState);

    char json[digivice::legacy_v3::kJsonCapacity];
    CHECK(digivice::legacy_v3::writeJson(restored, json, sizeof(json)) > 0);
    CHECK(std::strstr(json, "\"schemaVersion\":6") != nullptr);
    CHECK(std::strstr(json, "\"rulesVersion\":3") != nullptr);
    char tiny[2] = {'x', 'x'};
    CHECK(digivice::legacy_v3::writeJson(restored, tiny, sizeof(tiny)) == 0 && tiny[0] == '\0');
    auto invalid = restored;
    invalid.hp = 0;
    const auto before = snapshot;
    CHECK(!digivice::legacy_v3::encodeSnapshot(invalid, snapshot));
    CHECK(std::memcmp(before.bytes, snapshot.bytes, sizeof(snapshot.bytes)) == 0);
}

// Play only legal actions until a new member is captured. Bounded retries also exercise
// missed captures and friendly wins without hand-editing collection state.
void captureOne(digivice::legacy_v3::State& state) {
    const auto count = state.collectionCount;
    for (unsigned encounter = 0; encounter < 30 && state.collectionCount == count; ++encounter) {
        do { step(state, digivice::legacy_v3::Action::Rest); }
        while (state.hp < digivice::legacy_v3::combat::profile(static_cast<std::uint32_t>(state.collection[state.activeCreatureId - 1].species), state.level).stats.maxHp);
        step(state, digivice::legacy_v3::Action::Walk, 100);
        step(state, digivice::legacy_v3::Action::Card, 2);
        while (state.phase == digivice::legacy_v3::Phase::Encounter && state.wildHp > state.wildMaxHp / 2)
            step(state, digivice::legacy_v3::Action::Attack);
        while (state.phase == digivice::legacy_v3::Phase::Encounter && state.captureAttempts < 3)
            step(state, digivice::legacy_v3::Action::Capture);
        while (state.phase == digivice::legacy_v3::Phase::Encounter) step(state, digivice::legacy_v3::Action::Attack);
    }
    CHECK(state.collectionCount == count + 1);
}

void collectionAndSelection() {
    auto state = digivice::legacy_v3::newGame();
    CHECK(state.collectionCount == 1 && state.activeCreatureId == 1 && state.legacyCaptures == 0);
    CHECK(state.collection[0].id == 1 && state.collection[0].species == digivice::legacy_v3::Species::Mote);
    CHECK(state.collection[0].capturedAtSequence == 0 && state.wildSpecies == digivice::legacy_v3::Species::None);
    rejects(state, digivice::legacy_v3::Action::Select, 0, digivice::legacy_v3::Error::InvalidValue);
    rejects(state, digivice::legacy_v3::Action::Select, 9, digivice::legacy_v3::Error::InvalidValue);
    rejects(state, digivice::legacy_v3::Action::Select, 2, digivice::legacy_v3::Error::UnknownMember);
    // The new deterministic vector changes damage while the frozen migration retains old results.
    step(state, digivice::legacy_v3::Action::Feed);
    step(state, digivice::legacy_v3::Action::Play);
    step(state, digivice::legacy_v3::Action::Walk, 100);
    CHECK(state.wildSpecies == digivice::legacy_v3::Species::Flicker && state.wildHp == 88);
    rejects(state, digivice::legacy_v3::Action::Select, 1, digivice::legacy_v3::Error::WrongPhase);
    rejects(state, digivice::legacy_v3::Action::Select, 2, digivice::legacy_v3::Error::WrongPhase);
    step(state, digivice::legacy_v3::Action::Card, 1);
    step(state, digivice::legacy_v3::Action::Attack);
    CHECK(state.wildHp == 69 && state.hp == 86);
    step(state, digivice::legacy_v3::Action::Attack);
    step(state, digivice::legacy_v3::Action::Attack);
    CHECK(state.wildHp == 41 && state.hp == 58);
    step(state, digivice::legacy_v3::Action::Capture);
    CHECK(state.collectionCount == 2 && state.captures == 1 && state.activeCreatureId == 1);
    CHECK(state.sequence == 8 && state.rngState == 3336926330u && state.bond == 19);
    CHECK(state.wildSpecies == digivice::legacy_v3::Species::None);
    const auto founder = state.collection[0];
    const auto& captured = state.collection[1];
    CHECK(captured.id == 2 && captured.species == digivice::legacy_v3::Species::Flicker);
    CHECK(captured.hp == 88 && captured.energy == 80 && captured.fullness == 70 && captured.mood == 80);
    CHECK(captured.bond == 0 && captured.level == 1 && captured.capturedAtSequence == 8);
    step(state, digivice::legacy_v3::Action::Select, 2);
    CHECK(state.hp == 88 && state.bond == 0 && std::strcmp(digivice::legacy_v3::creatureName(state), "Flicker") == 0);
    for (unsigned i = 0; i < 55; ++i) step(state, digivice::legacy_v3::Action::Feed);
    CHECK(state.bond == 110 && state.level == 1); // Flicker has no evolution line in this pack.
    CHECK(sameMember(founder, state.collection[0]));
    const auto caredFor = state.collection[1];
    step(state, digivice::legacy_v3::Action::Select, 1);
    CHECK(state.hp == founder.hp && state.energy == founder.energy && state.bond == founder.bond);
    step(state, digivice::legacy_v3::Action::Rest);
    CHECK(sameMember(caredFor, state.collection[1]));
    step(state, digivice::legacy_v3::Action::Select, 2);
    CHECK(state.hp == caredFor.hp && state.bond == 110 && state.fullness == 100);
    digivice::legacy_v3::Snapshot snapshot;
    CHECK(digivice::legacy_v3::encodeSnapshot(state, snapshot));
    auto restored = digivice::legacy_v3::newGame(99);
    CHECK(digivice::legacy_v3::decodeSnapshot(snapshot.bytes, sizeof(snapshot.bytes), restored) == digivice::legacy_v3::SnapshotStatus::Ok);
    CHECK(same(state, restored));
    step(state, digivice::legacy_v3::Action::Play);
    step(restored, digivice::legacy_v3::Action::Play);
    CHECK(same(state, restored));
}

void encounterCycleAndEvolution() {
    auto state = digivice::legacy_v3::newGame();
    const digivice::legacy_v3::Species species[] = {digivice::legacy_v3::Species::Flicker, digivice::legacy_v3::Species::Rill,
                                        digivice::legacy_v3::Species::Cinder};
    for (unsigned i = 0; i < 6; ++i) {
        const auto expectedRandom = state.rngState;
        step(state, digivice::legacy_v3::Action::Walk, 100);
        CHECK(state.wildSpecies == species[i % 3]);
        CHECK(state.rngState == expectedRandom && state.wildMaxHp == digivice::legacy_v3::combat::profile(static_cast<std::uint32_t>(state.wildSpecies), 1).stats.maxHp);
        while (state.phase == digivice::legacy_v3::Phase::Encounter) step(state, digivice::legacy_v3::Action::Attack);
        CHECK(state.wildSpecies == digivice::legacy_v3::Species::None);
        step(state, digivice::legacy_v3::Action::Rest);
    }

    // Find both new base species through legal captures, then care for each independently.
    state = digivice::legacy_v3::newGame();
    for (unsigned i = 0; i < 7; ++i) captureOne(state);
    CHECK(state.collectionCount == 8);
    if (state.collectionCount != 8) return;
    std::uint32_t rillId = 0, cinderId = 0;
    for (std::uint32_t i = 0; i < state.collectionCount; ++i) {
        if (state.collection[i].species == digivice::legacy_v3::Species::Rill) rillId = state.collection[i].id;
        if (state.collection[i].species == digivice::legacy_v3::Species::Cinder) cinderId = state.collection[i].id;
    }
    CHECK(rillId != 0 && cinderId != 0);
    if (!rillId || !cinderId) return;
    const std::uint32_t ids[] = {rillId, cinderId};
    const char* middle[] = {"Brine", "Scoria"};
    const char* final[] = {"Pelagia", "Pyrel"};
    for (unsigned line = 0; line < 2; ++line) {
        const auto otherId = ids[1 - line];
        const auto other = state.collection[otherId - 1];
        step(state, digivice::legacy_v3::Action::Select, ids[line]);
        for (unsigned i = 0; i < 19; ++i) step(state, digivice::legacy_v3::Action::Feed);
        CHECK(state.bond == 38 && state.level == 1);
        step(state, digivice::legacy_v3::Action::Feed);
        CHECK(state.bond == 40 && state.level == 2 && state.message == digivice::legacy_v3::Message::Evolved);
        CHECK(std::strcmp(digivice::legacy_v3::creatureName(state), middle[line]) == 0);
        for (unsigned i = 0; i < 29; ++i) step(state, digivice::legacy_v3::Action::Feed);
        CHECK(state.bond == 98 && state.level == 2);
        step(state, digivice::legacy_v3::Action::Feed);
        CHECK(state.bond == 100 && state.level == 3 && state.message == digivice::legacy_v3::Message::Evolved);
        CHECK(std::strcmp(digivice::legacy_v3::creatureName(state), final[line]) == 0);
        CHECK(sameMember(other, state.collection[otherId - 1]));
    }

    // At capacity a legal capture must reject transactionally, including RNG and attempt count.
    step(state, digivice::legacy_v3::Action::Select, 2);
    step(state, digivice::legacy_v3::Action::Rest);
    for (unsigned attempts = 0; attempts < 10; ++attempts) {
        step(state, digivice::legacy_v3::Action::Walk, 100);
        while (state.phase == digivice::legacy_v3::Phase::Encounter && state.wildHp > state.wildMaxHp / 2)
            step(state, digivice::legacy_v3::Action::Attack);
        if (state.phase == digivice::legacy_v3::Phase::Encounter) break;
    }
    CHECK(state.phase == digivice::legacy_v3::Phase::Encounter);
    rejects(state, digivice::legacy_v3::Action::Capture, 0, digivice::legacy_v3::Error::CollectionFull);
    CHECK(state.collectionCount == 8 && state.captures == 7);
    char json[digivice::legacy_v3::kJsonCapacity];
    CHECK(digivice::legacy_v3::writeJson(state, json, sizeof(json)) > 0);
    CHECK(std::strstr(json, "\"id\":8") != nullptr);
}

void legacyMigration() {
    for (unsigned version = 1; version <= 2; ++version) {
        // Historical captures were aggregate-only. An in-progress fourth encounter
        // must migrate without inventing three creatures or changing the next RNG draw.
        std::uint8_t bytes[100]{};
        const std::size_t length = version == 1 ? 96 : 100;
        std::memcpy(bytes, "DGVS", 4);
        bytes[4] = static_cast<std::uint8_t>(version);
        bytes[6] = static_cast<std::uint8_t>(length - 12);
        const std::uint32_t fields[] = {
            1, 20, 12345, 1955480042, 400, 0, 93, 76, 85, 94, 19, 1,
            3, 4, 11, 27, 0, 1, 1, 0
        };
        for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) put32(bytes + 8 + i * 4, fields[i]);
        if (version == 2) put32(bytes + 88, 7); // V2 shield survives migration.
        put32(bytes + (version == 1 ? 88 : 92), static_cast<std::uint32_t>(digivice::legacy_v3::Message::Attacked));
        seal(bytes, length);
        auto restored = digivice::legacy_v3::newGame(99);
        CHECK(digivice::legacy_v3::decodeSnapshot(bytes, length, restored) == digivice::legacy_v3::SnapshotStatus::Migrated);
        CHECK(digivice::legacy_v3::isValid(restored));
        CHECK(restored.collectionCount == 1 && restored.activeCreatureId == 1 && restored.legacyCaptures == 3);
        CHECK(restored.captures == 3 && restored.hp == 93 && restored.bond == 19);
        CHECK(restored.rngState == 1955480042 && restored.wildSpecies == digivice::legacy_v3::Species::Flicker);
        CHECK(restored.collection[0].species == digivice::legacy_v3::Species::Mote && restored.collection[0].hp == 93);
        CHECK(restored.shield == (version == 1 ? 0u : 7u));
        step(restored, digivice::legacy_v3::Action::Capture);
        CHECK(restored.collectionCount == 2 && restored.captures == 4 && restored.legacyCaptures == 3);
        CHECK(restored.collection[1].id == 2 && restored.collection[1].species == digivice::legacy_v3::Species::Flicker);
        CHECK(restored.collection[1].capturedAtSequence == 21);
        step(restored, digivice::legacy_v3::Action::Walk, 100);
        CHECK(restored.wildSpecies == digivice::legacy_v3::Species::Rill); // Fifth encounter uses its historical index.
        digivice::legacy_v3::Snapshot current;
        CHECK(digivice::legacy_v3::encodeSnapshot(restored, current));
        auto again = digivice::legacy_v3::newGame();
        CHECK(digivice::legacy_v3::decodeSnapshot(current.bytes, sizeof(current.bytes), again) == digivice::legacy_v3::SnapshotStatus::Ok);
        CHECK(same(restored, again));
    }
}

void invalidCollectionSnapshots() {
    const auto state = digivice::legacy_v3::newGame();
    auto mismatched = state;
    mismatched.hp = 99;
    CHECK(!digivice::legacy_v3::isValid(mismatched));
    CHECK(digivice::legacy_v3::apply(mismatched, digivice::legacy_v3::Action::Rest) == digivice::legacy_v3::Error::InvalidState);
    digivice::legacy_v3::Snapshot snapshot;
    CHECK(digivice::legacy_v3::encodeSnapshot(state, snapshot));
    // CRC-valid malformed collection metadata must be rejected before indexing or narrowing.
    const std::uint32_t offsets[] = {100, 104, 104, 108, 112, 116, 120, 144, 96};
    const std::uint32_t values[] =  {  9,   0,   9, 256,   2, 256,   0,   1,  1};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        auto corrupt = snapshot;
        put32(corrupt.bytes + offsets[i], values[i]);
        seal(corrupt.bytes, sizeof(corrupt.bytes));
        auto restored = state;
        CHECK(digivice::legacy_v3::decodeSnapshot(corrupt.bytes, sizeof(corrupt.bytes), restored) == digivice::legacy_v3::SnapshotStatus::InvalidState);
        CHECK(same(restored, state));
    }
}
void statsAndMoves() {
    auto base = digivice::legacy_v3::newGame();
    step(base, digivice::legacy_v3::Action::Walk, 100);
    const auto rng = base.rngState;
    auto physical = base, heavy = base, magic = base;
    step(physical, digivice::legacy_v3::Action::Attack);
    step(heavy, digivice::legacy_v3::Action::Heavy);
    step(magic, digivice::legacy_v3::Action::Magic);
    CHECK(physical.wildHp == 74 && heavy.wildHp == 66 && magic.wildHp == 76);
    CHECK(physical.hp == 86 && heavy.hp == 86 && magic.hp == 86);
    CHECK(physical.energy == 78 && heavy.energy == 74 && magic.energy == 78);
    CHECK(physical.rngState == rng && heavy.rngState == rng && magic.rngState == rng);
    base.energy = 5; mirrorActive(base);
    rejects(base, digivice::legacy_v3::Action::Heavy, 0, digivice::legacy_v3::Error::LowEnergy);
    rejects(base, digivice::legacy_v3::Action::Magic, 1, digivice::legacy_v3::Error::InvalidValue);
    base.energy = 0; mirrorActive(base);
    step(base, digivice::legacy_v3::Action::Magic); // No zero-energy softlock.
    CHECK(base.energy == 0);
    digivice::legacy_v3::Action parsed;
    CHECK(digivice::legacy_v3::parseAction("physical", parsed) && parsed == digivice::legacy_v3::Action::Attack);

    auto evolved = digivice::legacy_v3::newGame();
    evolved.hp = 50; mirrorActive(evolved);
    for (unsigned i = 0; i < 20; ++i) step(evolved, digivice::legacy_v3::Action::Feed);
    CHECK(evolved.level == 2 && evolved.hp == 58); // ceil(50 * 116 / 100)
    for (unsigned i = 0; i < 30; ++i) step(evolved, digivice::legacy_v3::Action::Feed);
    CHECK(evolved.level == 3 && evolved.hp == 68); // ceil(58 * 136 / 116)
    for (unsigned i = 0; i < 4; ++i) step(evolved, digivice::legacy_v3::Action::Rest);
    CHECK(evolved.hp == 136);
    evolved.hp = 137; mirrorActive(evolved);
    CHECK(!digivice::legacy_v3::isValid(evolved));
}

void maximumJsonBudget() {
    auto state = digivice::legacy_v3::newGame(0xffffffffu);
    state.rngState = state.sequence = state.steps = 0xffffffffu;
    state.stepCredit = 95;
    state.encounters = state.captures = 42949672;
    state.legacyCaptures = state.captures - 7;
    state.collectionCount = 8;
    for (unsigned i = 0; i < 8; ++i) {
        state.collection[i] = {i + 1, i == 0 ? digivice::legacy_v3::Species::Mote : digivice::legacy_v3::Species::Rill,
                              i == 0 ? 136u : 142u, 100, 100, 100, 200, 3,
                              i == 0 ? 0u : 0xfffffff7u + i};
    }
    state.activeCreatureId = 8;
    state.hp = 142; state.energy = state.fullness = state.mood = 100;
    state.bond = 200; state.level = 3;
    state.phase = digivice::legacy_v3::Phase::Encounter;
    state.wildSpecies = digivice::legacy_v3::Species::Cinder;
    state.wildHp = state.wildMaxHp = 96;
    state.captureAttempts = 3;
    state.message = digivice::legacy_v3::Message::CaptureMissed;
    state.lastAutoOutcome = digivice::autobattle::Outcome::Retreated;
    state.lastAutoTurns = 48; state.lastAutoSequence = UINT32_MAX;
    CHECK(digivice::legacy_v3::isValid(state));
    char json[digivice::legacy_v3::kJsonCapacity];
    const auto size = digivice::legacy_v3::writeJson(state, json, sizeof(json));
    CHECK(size > 0 && size < 4096);
    std::printf("Bounded full collection JSON: %zu bytes\n", size);
}

void collectionSnapshotMigration() {
    auto oldShape = digivice::legacy_v3::newGame();
    captureOne(oldShape);
    captureOne(oldShape);
    step(oldShape, digivice::legacy_v3::Action::Select, 3);
    for (unsigned i = 0; i < 20; ++i) step(oldShape, digivice::legacy_v3::Action::Feed);
    CHECK(oldShape.collection[2].species == digivice::legacy_v3::Species::Rill && oldShape.level == 2);
    auto bytes = oldCollectionSnapshot(oldShape, 3);
    const unsigned health[] = {97, 1, 50};
    for (unsigned i = 0; i < 3; ++i) put32(bytes.bytes + 120 + i * 36, health[i]);
    put32(bytes.bytes + 32, 50);
    seal(bytes.bytes, sizeof(bytes.bytes));
    auto migrated = digivice::legacy_v3::newGame();
    CHECK(digivice::legacy_v3::decodeSnapshot(bytes.bytes, sizeof(bytes.bytes), migrated) == digivice::legacy_v3::SnapshotStatus::Migrated);
    CHECK(migrated.collectionCount == 3 && migrated.activeCreatureId == 3);
    CHECK(migrated.collection[0].hp == 97 && migrated.collection[1].hp == 1 && migrated.hp == 60);
    CHECK(migrated.collection[2].capturedAtSequence == oldShape.collection[2].capturedAtSequence);
    CHECK(migrated.rngState == oldShape.rngState && migrated.legacyCaptures == 0);
    auto invalid = bytes;
    put32(invalid.bytes + 120, 101); // Old bounds checked before proportional conversion.
    seal(invalid.bytes, sizeof(invalid.bytes));
    const auto before = migrated;
    CHECK(digivice::legacy_v3::decodeSnapshot(invalid.bytes, sizeof(invalid.bytes), migrated) == digivice::legacy_v3::SnapshotStatus::InvalidState);
    CHECK(same(before, migrated));
}

void onboardingAndOldSaves() {
    using namespace digivice::legacy_v3;
    auto egg = newDevice();
    CHECK(isValid(egg) && !egg.onboardingComplete && egg.starterId == 0);
    CHECK(egg.phase == Phase::Egg && egg.sequence == 0 && egg.activeCreatureId == 0 && egg.collectionCount == 0);
    CHECK(egg.hp == 0 && egg.level == 0 && egg.rngState == 12345);
    CHECK(isValid(newDevice(0)) && newDevice(0).rngState == newGame(0).rngState);
    char json[kJsonCapacity];
    CHECK(writeJson(egg, json, sizeof(json)) > 0);
    CHECK(std::strstr(json, "\"collection\":[]") && std::strstr(json, "\"creature\":null") &&
          std::strstr(json, "\"species\":null") && std::strstr(json, "\"combat\":null"));
    for (const auto action : {Action::Feed, Action::Play, Action::Rest, Action::Walk, Action::Card,
                              Action::Attack, Action::Capture, Action::Select, Action::Heavy, Action::Magic})
        rejects(egg, action, 0, Error::WrongPhase);
    rejects(egg, Action::Hatch, 0, Error::InvalidValue);
    rejects(egg, Action::Hatch, 9, Error::InvalidValue);
    rejects(egg, Action::Hatch, UINT32_MAX, Error::InvalidValue);
    Snapshot pending;
    CHECK(encodeSnapshot(egg, pending));
    auto restored = newGame();
    CHECK(decodeSnapshot(pending.bytes, sizeof(pending.bytes), restored) == SnapshotStatus::Ok && same(restored, egg));

    for (unsigned id = 1; id <= combat::kStarterCount; ++id) {
        auto state = restored;
        step(state, Action::Hatch, id);
        CHECK(state.sequence == 1 && state.rngState == restored.rngState && state.seed == restored.seed);
        CHECK(state.onboardingComplete && state.starterId == id && state.phase == Phase::Home);
        CHECK(state.collectionCount == 1 && state.activeCreatureId == 1 && state.captures == 0);
        CHECK(state.collection[0].id == 1 && state.collection[0].capturedAtSequence == 0);
        CHECK(static_cast<unsigned>(state.collection[0].species) == combat::starterSpecies(id));
        CHECK(std::strcmp(creatureName(state), combat::starterName(id)) == 0);
        CHECK(state.hp == combat::profile(combat::starterSpecies(id), 1).stats.maxHp);
        CHECK(state.energy == 80 && state.fullness == 70 && state.mood == 80 && state.level == 1 && state.bond == 0);
        rejects(state, Action::Hatch, id, Error::AlreadyHatched);
        rejects(state, Action::Hatch, id == 8 ? 1 : id + 1, Error::AlreadyHatched);
        Snapshot hatched;
        CHECK(encodeSnapshot(state, hatched));
        auto again = newDevice(99);
        CHECK(decodeSnapshot(hatched.bytes, sizeof(hatched.bytes), again) == SnapshotStatus::Ok && same(again, state));
        rejects(again, Action::Hatch, id, Error::AlreadyHatched);
        CHECK(writeJson(state, json, sizeof(json)) > 0 && std::strstr(json, "\"stage\":\"Rookie\""));
        // Numeric training retains the selected Rookie name and health fraction.
        const auto oldMax = state.hp;
        state.hp = 1; mirrorActive(state);
        for (unsigned n = 0; n < 20; ++n) step(state, Action::Feed);
        CHECK(state.level == 2 && state.message == Message::Trained && state.hp == (oldMax + 16 + oldMax - 1) / oldMax);
        for (unsigned n = 0; n < 30; ++n) step(state, Action::Feed);
        CHECK(state.level == 3 && state.message == Message::Trained);
        CHECK(std::strcmp(creatureName(state), combat::starterName(id)) == 0);
        CHECK(state.collectionCount == 1 && state.starterId == id);
    }
    auto existing = newGame();
    rejects(existing, Action::Hatch, 1, Error::AlreadyHatched);
    CHECK(existing.onboardingComplete && existing.starterId == 0 && existing.sequence == 0);
    // Both zero-event and ongoing V4 saves remain completed, with gameplay state
    // unchanged; no inference based on sequence or the absence of captures.
    for (unsigned progress = 0; progress < 3; ++progress) {
        if (progress == 1) { step(existing, Action::Walk, 100); step(existing, Action::Card, 2); }
        if (progress == 2) { step(existing, Action::Attack); step(existing, Action::Magic); }
        const auto old = oldCollectionSnapshot(existing);
        auto migrated = newDevice();
        CHECK(decodeSnapshot(old.bytes, sizeof(old.bytes), migrated) == SnapshotStatus::Migrated);
        CHECK(same(existing, migrated) && migrated.onboardingComplete && migrated.starterId == 0);
        rejects(migrated, Action::Hatch, 8, Error::AlreadyHatched);
        if (progress == 2) {
            step(existing, Action::Attack); step(migrated, Action::Attack);
            CHECK(same(existing, migrated));
        }
    }
    // CRC-valid tampering cannot turn completed saves into a different starter,
    // create a populated egg, invent completion flags, or narrow invalid enums.
    for (const auto offset : {400u, 404u, 76u, 104u, 112u}) {
        auto bad = pending;
        put32(bad.bytes + offset, offset == 400 ? 2 : 1);
        seal(bad.bytes, sizeof(bad.bytes));
        auto target = newGame(); const auto before = target;
        CHECK(decodeSnapshot(bad.bytes, sizeof(bad.bytes), target) == SnapshotStatus::InvalidState);
        CHECK(same(target, before));
    }
    auto hatched = newDevice(); step(hatched, Action::Hatch, 1);
    Snapshot bytes; CHECK(encodeSnapshot(hatched, bytes));
    put32(bytes.bytes + 404, 2); seal(bytes.bytes, sizeof(bytes.bytes));
    CHECK(decodeSnapshot(bytes.bytes, sizeof(bytes.bytes), hatched) == SnapshotStatus::InvalidState);
}

void rookieCaptureAndJsonBudget() {
    using namespace digivice::legacy_v3;
    auto state = newDevice(); step(state, Action::Hatch, 1);
    step(state, Action::Walk, 100); step(state, Action::Magic);
    CHECK(state.wildHp == 68 && state.hp == 76 && state.rngState == 12345);
    step(state, Action::Magic); step(state, Action::Magic); step(state, Action::Capture);
    CHECK(state.collectionCount == 2 && state.collection[1].species == Species::Flicker);
    CHECK(state.rngState == 3336926330u && state.starterId == 1);
    const auto founder = state.collection[0];
    step(state, Action::Select, 2); step(state, Action::Feed); step(state, Action::Select, 1);
    CHECK(sameMember(founder, state.collection[0]));
    rejects(state, Action::Hatch, 2, Error::AlreadyHatched);

    // Bound all eight possible founders with a full original-roster collection,
    // wide counters, longest evolved names and a simultaneous wild profile.
    std::size_t maximum = 0;
    for (unsigned starter = 1; starter <= combat::kStarterCount; ++starter) {
        auto full = newDevice(UINT32_MAX); step(full, Action::Hatch, starter);
        full.sequence = full.rngState = full.steps = UINT32_MAX;
        full.stepCredit = 95; full.encounters = 42949672; full.captures = 7; full.collectionCount = 8;
        full.collection[0].hp = combat::profile(combat::starterSpecies(starter), 3).stats.maxHp;
        full.collection[0].energy = full.collection[0].fullness = full.collection[0].mood = 100;
        full.collection[0].bond = 200; full.collection[0].level = 3;
        for (unsigned i = 1; i < 8; ++i)
            full.collection[i] = {i + 1, Species::Rill, 142, 100, 100, 100, 200, 3, 0xfffffff7u + i};
        full.hp = full.collection[0].hp; full.energy = full.fullness = full.mood = 100; full.bond = 200; full.level = 3;
        full.phase = Phase::Encounter; full.wildSpecies = Species::Cinder; full.wildHp = full.wildMaxHp = 96;
        full.captureAttempts = 3; full.message = Message::CaptureMissed;
        full.lastAutoOutcome = autobattle::Outcome::Retreated; full.lastAutoTurns = 48; full.lastAutoSequence = UINT32_MAX;
        CHECK(isValid(full)); char json[kJsonCapacity];
        const auto length = writeJson(full, json, sizeof(json));
        CHECK(length > 0 && length < sizeof(json));
        if (length > maximum) maximum = length;
        char tiny[10]; CHECK(writeJson(full, tiny, sizeof(tiny)) == 0 && tiny[0] == '\0');
    }
    std::printf("Bounded Rookie full collection JSON: %zu bytes\n", maximum);
}

void battleModesAndAuto() {
    using namespace digivice::legacy_v3;
    auto egg = newDevice();
    rejects(egg, Action::Mode, 1, Error::WrongPhase);
    rejects(egg, Action::Auto, 0, Error::WrongPhase);
    auto tactical = newGame();
    CHECK(tactical.battleMode == BattleMode::Tactical && tactical.lastAutoOutcome == autobattle::Outcome::None);
    rejects(tactical, Action::Mode, 2, Error::InvalidValue);
    rejects(tactical, Action::Auto, 0, Error::WrongPhase);
    step(tactical, Action::Walk, 100);
    rejects(tactical, Action::Mode, 1, Error::WrongPhase);
    rejects(tactical, Action::Auto, 0, Error::WrongMode);
    step(tactical, Action::Attack); // Existing direct Tactical retaliation remains exact.
    CHECK(tactical.hp == 86 && tactical.wildHp == 74 && tactical.rngState == 12345);

    auto ready = newDevice(); step(ready, Action::Hatch, 1); step(ready, Action::Mode, 1);
    CHECK(ready.sequence == 2 && ready.phase == Phase::Home && ready.battleMode == BattleMode::Auto);
    step(ready, Action::Walk, 100);
    CHECK(ready.sequence == 3 && ready.phase == Phase::Encounter && ready.wildHp == 88 && ready.hp == 92);
    rejects(ready, Action::Mode, 0, Error::WrongPhase);
    rejects(ready, Action::Card, 1, Error::WrongMode);
    rejects(ready, Action::Capture, 0, Error::WrongMode);
    rejects(ready, Action::Attack, 0, Error::WrongMode);
    rejects(ready, Action::Magic, 0, Error::WrongMode);
    rejects(ready, Action::Heavy, 0, Error::WrongMode);
    rejects(ready, Action::Auto, 1, Error::InvalidValue);
    const auto before = ready;
    auto withoutTrace = ready;
    autobattle::Trace trace; trace.combatRulesVersion=3;
    CHECK(applyAuto(ready, &trace) == Error::None);
    step(withoutTrace, Action::Auto);
    CHECK(same(ready, withoutTrace));
    CHECK(ready.sequence == before.sequence + 1 && ready.collectionCount == 2 && ready.captures == 1);
    CHECK(ready.collection[1].capturedAtSequence == ready.sequence && ready.rngState == 3336926330u);
    CHECK(ready.hp == 44 && ready.energy == 74 && ready.bond == 12);
    CHECK(trace.count == 4 && trace.startSequence == 3 && trace.endSequence == 4);
    CHECK(trace.outcome == autobattle::Outcome::Captured && trace.playerSpecies == 5 && trace.enemySpecies == 2);
    CHECK(trace.steps[0].action == autobattle::Move::Physical && trace.steps[0].enemyHpAfter == 76);
    CHECK(trace.steps[1].action == autobattle::Move::Magic && trace.steps[1].enemyHpAfter == 56);
    CHECK(trace.steps[2].enemyHpAfter == 36 && trace.steps[2].playerHpAfter == 44);
    CHECK(trace.steps[3].captured && trace.steps[3].opponentAction == autobattle::Move::None && trace.steps[3].enemyHpAfter == 36);
    auto training = before;
    training.bond = 38; training.hp = 91; mirrorActive(training);
    CHECK(applyAuto(training, &trace) == Error::None && training.level == 2);
    CHECK(trace.playerLevel == 1 && trace.outcome == autobattle::Outcome::Captured);
    const auto combatHp = trace.steps[trace.count - 1].playerHpAfter;
    CHECK(combatHp <= 92 && training.hp == (combatHp * 108 + 91) / 92);
    rejects(ready, Action::Auto, 0, Error::WrongPhase);
    const auto summarySequence = ready.lastAutoSequence, summaryTurns = ready.lastAutoTurns;
    step(ready, Action::Rest); step(ready, Action::Mode, 0);
    CHECK(ready.lastAutoSequence == summarySequence && ready.lastAutoTurns == summaryTurns && ready.lastAutoOutcome == autobattle::Outcome::Captured);
    Snapshot saved; CHECK(encodeSnapshot(ready, saved)); auto rebooted = newDevice();
    CHECK(decodeSnapshot(saved.bytes, sizeof(saved.bytes), rebooted) == SnapshotStatus::Ok && same(ready, rebooted));

    // Every permitted profile/energy boundary terminates; policy RNG never leaks
    // into the capture RNG stream. Capture attempts alone consume that stream.
    unsigned misses = 0, retreats = 0, captures = 0, maximumTurns = 0;
    for (unsigned id = 1; id <= 8; ++id) for (unsigned seed = 1; seed <= 32; ++seed) {
        auto trial = newDevice(seed); step(trial, Action::Hatch, id); step(trial, Action::Mode, 1);
        trial.energy = seed % 2 ? 0 : 6; mirrorActive(trial);
        if (seed % 8 == 0) { trial.hp = 1; mirrorActive(trial); }
        step(trial, Action::Walk, 100); const auto original = trial; auto repeated = trial;
        CHECK(applyAuto(trial, &trace) == Error::None && isValid(trial));
        step(repeated, Action::Auto); CHECK(same(trial, repeated));
        CHECK(trial.sequence == original.sequence + 1 && trace.count >= 1 && trace.count <= 48);
        CHECK(trial.phase == Phase::Home && trial.hp > 0 && !trial.cardUsed);
        auto expectedRng = original.rngState; unsigned attempts = 0;
        for (std::size_t i = 0; i < trace.count; ++i) {
            const auto& frame = trace.steps[i];
            CHECK(!frame.reflected && !frame.defending);
            if (frame.action == autobattle::Move::Capture) {
                ++attempts; CHECK(frame.enemyHpBefore <= original.wildMaxHp / 2);
                expectedRng ^= expectedRng << 13; expectedRng ^= expectedRng >> 17; expectedRng ^= expectedRng << 5;
                if (!frame.captured) ++misses;
            }
        }
        CHECK(attempts <= 3 && trial.rngState == expectedRng);
        CHECK(trial.lastAutoSequence == trial.sequence && trial.lastAutoTurns == trace.count);
        if (trace.count > maximumTurns) maximumTurns = static_cast<unsigned>(trace.count);
        if (trace.outcome == autobattle::Outcome::Captured) {
            ++captures; CHECK(trial.collectionCount == 2 && trial.bond == 12);
            CHECK(trial.collection[1].capturedAtSequence == trial.sequence);
        } else if (trace.outcome == autobattle::Outcome::Retreated) {
            ++retreats; CHECK(trial.collectionCount == 1 && trial.bond == 0);
            CHECK(trace.steps[trace.count - 1].playerHpAfter == 0 && trial.hp > 0);
        } else CHECK(trace.outcome == autobattle::Outcome::Won && trial.bond == 8);
    }
    CHECK(misses > 0 && retreats > 0 && captures > 0);
    std::printf("Auto sampled256 profile/energy/seed boundaries; max turns=%u\n", maximumTurns);

    auto full = newGame(); for (unsigned i = 0; i < 7; ++i) captureOne(full);
    while (full.hp < combat::profile(1, full.level).stats.maxHp) step(full, Action::Rest);
    step(full, Action::Mode, 1); step(full, Action::Walk, 100); const auto rng = full.rngState;
    CHECK(applyAuto(full, &trace) == Error::None);
    CHECK(full.collectionCount == 8 && full.captures == 7 && full.rngState == rng);
    for (std::size_t i = 0; i < trace.count; ++i) CHECK(trace.steps[i].action != autobattle::Move::Capture);

    // Old V5 includes onboarding but has no mode/Auto fields. Preserve chosen
    // partner and exact pending encounter, defaulting to Tactical without autoplay.
    auto v5State = newDevice(); step(v5State, Action::Hatch, 8); step(v5State, Action::Walk, 100);
    CHECK(encodeSnapshot(v5State, saved));
    std::uint8_t v5[kV5SnapshotSize]{}; std::memcpy(v5, saved.bytes, sizeof(v5) - 4);
    v5[4] = 5; v5[6] = 144; v5[7] = 1; seal(v5, sizeof(v5));
    CHECK(decodeSnapshot(v5, sizeof(v5), rebooted) == SnapshotStatus::Migrated && same(rebooted, v5State));
    CHECK(rebooted.battleMode == BattleMode::Tactical && rebooted.lastAutoOutcome == autobattle::Outcome::None);
    CHECK(encodeSnapshot(ready, saved));
    const unsigned offsets[]{408, 412, 416, 420};
    const unsigned values[]{256, 4, 49, UINT32_MAX};
    for (unsigned i = 0; i < 4; ++i) {
        auto corrupt = saved; put32(corrupt.bytes + offsets[i], values[i]); seal(corrupt.bytes, sizeof(corrupt.bytes));
        const auto original = rebooted;
        CHECK(decodeSnapshot(corrupt.bytes, sizeof(corrupt.bytes), rebooted) == SnapshotStatus::InvalidState && same(rebooted, original));
    }
    // A structurally maximal trace cannot overflow the host-only bounded writer.
    trace.count = autobattle::kMaxTraceSteps; trace.startSequence = UINT32_MAX - 1; trace.endSequence = UINT32_MAX;
    trace.outcome = autobattle::Outcome::Won;
    for (auto& frame : trace.steps) frame = {false, autobattle::Move::Physical, autobattle::Move::Physical, 148, 148, 148, 148, false, false};
    char text[autobattle::kTraceJsonCapacity]; const auto traceBytes = autobattle::writeJson(trace, text, sizeof(text));
    CHECK(traceBytes > 0 && traceBytes < sizeof(text));
    CHECK(std::strstr(text, "rngState") == nullptr && std::strstr(text, "enemyChoice") == nullptr);
    char small[32]; CHECK(autobattle::writeJson(trace, small, sizeof(small)) == 0 && small[0] == '\0');
    std::printf("Bounded48-step trace JSON: %zu bytes; Trace=%zu bytes\n", traceBytes, sizeof(trace));
}

} // namespace

int main() {
    replayAndCards();
    captureAndGentleness();
    walkingAndBounds();
    persistence();
    collectionAndSelection();
    encounterCycleAndEvolution();
    legacyMigration();
    invalidCollectionSnapshots();
    statsAndMoves();
    collectionSnapshotMigration();
    maximumJsonBudget();
    onboardingAndOldSaves();
    rookieCaptureAndJsonBudget();
    battleModesAndAuto();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
