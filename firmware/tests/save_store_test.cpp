#include "save_store.hpp"
#include "../../tests/snapshot_test_helpers.hpp"
#include "legacy_v4_snapshot.hpp"
#include "legacy_v5_snapshot.hpp"
#include "legacy_v6_snapshot.hpp"
#include "legacy_v7_snapshot.hpp"
#include "legacy_v8_profile_snapshots.hpp"
#include "legacy_v15_snapshot.hpp"
#include "../runtime/evolution_choice.hpp"
#include "forms.hpp"
#include "catalog_fixture.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace digivice;
using namespace digivice::storage;

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); std::exit(1); \
} } while (false)

class MemoryBackend final : public Backend {
public:
    Slot slots[2];
    bool present[2] = {false, false};
    bool failBeforeWrite = false;
    bool failAfterWrite = false;
    bool corruptWrite = false;
    bool unreadable = false;
    unsigned writes = 0;
    ReadStatus readSlot(unsigned index, Slot& out) override {
        if (unreadable) return ReadStatus::Unreadable;
        if (!present[index]) return ReadStatus::Missing;
        out = slots[index];
        return ReadStatus::Present;
    }
    bool writeSlot(unsigned index, const Snapshot& snapshot) override {
        ++writes;
        if (failBeforeWrite) return false;
        std::memcpy(slots[index].bytes, snapshot.bytes, kSnapshotSize);
        slots[index].length = kSnapshotSize;
        present[index] = true;
        if (corruptWrite) slots[index].bytes[kSnapshotSize - 1] ^= 0x40;
        return !failAfterWrite;
    }
};

static bool sameSnapshot(const State& left, const State& right) {
    Snapshot a, b;
    CHECK(encodeSnapshot(left, a) && encodeSnapshot(right, b));
    return std::memcmp(a.bytes, b.bytes, kSnapshotSize) == 0;
}

static void pendingEncounterSavePolicy() {
    // These bytes came from the actual schema15 encoder at the installed source
    // commit. Migration must preserve every previous gameplay byte, including
    // partially earned walking progress and a fight already in progress.
    const char* const fixtures[] = {kLegacyV15HomeHex, kLegacyV15EncounterHex};
    for (unsigned fixture = 0; fixture < 2; ++fixture) {
        MemoryBackend flash;
        flash.present[0] = true;
        flash.slots[0].length = std::strlen(fixtures[fixture]) / 2;
        CHECK(flash.slots[0].length == kV15SnapshotSize && kV15SnapshotSize == 636);
        const auto nibble = [](char c) { return c >= 'a' ? c - 'a' + 10 : c - '0'; };
        for (std::size_t i = 0; i < flash.slots[0].length; ++i)
            flash.slots[0].bytes[i] = static_cast<std::uint8_t>(
                nibble(fixtures[fixture][i * 2]) * 16 + nibble(fixtures[fixture][i * 2 + 1]));
        const auto original = flash.slots[0];
        SaveStore store(flash);
        auto state = newDevice();
        CHECK(store.restore(state) == BootStatus::Migrated && store.writable());
        CHECK(flash.writes == 0 && state.phase == (fixture ? Phase::Encounter : Phase::Home));
        CHECK(!state.pendingEncounter.formId && !state.pendingEncounter.level && !state.pendingEncounter.rules);
        CHECK(state.foregroundSequence == state.sequence);
        CHECK(state.sequence == (fixture ? 4u : 3u) && state.explorationSteps == (fixture ? 110u : 10u));
        Snapshot migrated;
        CHECK(encodeSnapshot(state, migrated) && kSnapshotSize > kV15SnapshotSize);
        CHECK(snapshot_test::sameOldPayload(original.bytes, migrated.bytes, kV15SnapshotSize));
        CHECK(store.checkpoint(state) && flash.slots[1].length == kSnapshotSize);
        CHECK(std::memcmp(flash.slots[0].bytes, original.bytes, kSnapshotSize) == 0);
        SaveStore restart(flash);
        State restored;
        // Identical canonical snapshots at one sequence are not a conflict.
        CHECK(restart.restore(restored) == BootStatus::Migrated && restart.writable());
        CHECK(sameSnapshot(state, restored) && flash.writes == 1);
        if(fixture) CHECK(needsTestEncounterResolution(restored) && apply(restored,Action::Attack)==Error::InvalidAction);
        CHECK(apply(restored, fixture ? Action::ResolveTestEncounter : Action::Feed) == Error::None);
        CHECK(restart.checkpoint(restored));
        SaveStore nextBoot(flash);
        State after;
        CHECK(nextBoot.restore(after) == BootStatus::Loaded && sameSnapshot(after, restored));
    }

    // An old synthetic capture record survives byte-for-byte. Its active test
    // fight is explicitly cleared before background walking resumes, so the
    // historical outcome cannot be replayed as a newly earned result.
    MemoryBackend captureFlash;
    captureFlash.present[0] = true;
    captureFlash.slots[0].length = (sizeof(kLegacyV15CaptureHex) - 1) / 2;
    CHECK(captureFlash.slots[0].length == kV15SnapshotSize);
    const auto nibble = [](char c) { return c >= 'a' ? c - 'a' + 10 : c - '0'; };
    for (std::size_t i = 0; i < captureFlash.slots[0].length; ++i)
        captureFlash.slots[0].bytes[i] = static_cast<std::uint8_t>(
            nibble(kLegacyV15CaptureHex[i * 2]) * 16 + nibble(kLegacyV15CaptureHex[i * 2 + 1]));
    SaveStore captureStore(captureFlash);
    State captured;
    CHECK(captureStore.restore(captured) == BootStatus::Migrated && captureFlash.writes == 0);
    CHECK(captured.lastCapture.result == CaptureResult::Miss &&
          captured.lastCapture.sequence == captured.sequence && captured.foregroundSequence == captured.sequence);
    Snapshot migratedCapture;
    CHECK(encodeSnapshot(captured, migratedCapture));
    CHECK(snapshot_test::sameOldPayload(captureFlash.slots[0].bytes, migratedCapture.bytes, kV15SnapshotSize));
    const auto captureSequence = captured.foregroundSequence;
    const auto oldCapture=captured.lastCapture;
    CHECK(needsTestEncounterResolution(captured) && apply(captured,Action::ResolveTestEncounter,0)==Error::None);
    CHECK(captured.foregroundSequence==captureSequence+1 && captureStore.checkpoint(captured));
    CHECK(apply(captured, Action::AccrueSteps, 1000) == Error::None && captured.pendingEncounter.formId);
    CHECK(captured.lastCapture.sequence == captureSequence && captured.foregroundSequence == captureSequence+1 &&
          !std::memcmp(&oldCapture,&captured.lastCapture,sizeof(oldCapture)) &&
          captured.sequence > captureSequence && captureStore.checkpoint(captured));
    SaveStore captureRestart(captureFlash);
    State reloadedCapture;
    CHECK(captureRestart.restore(reloadedCapture) == BootStatus::Loaded && sameSnapshot(reloadedCapture, captured));

    // A queued foe survives restarting at Home, is consumed exactly once, and
    // can coexist with the current fight without replacing any battle fields.
    auto initial = newDevice(99);
    CHECK(apply(initial, Action::Hatch, 2) == Error::None);
    CHECK(apply(initial, Action::Mode, 1) == Error::None);
    CHECK(apply(initial, Action::EncounterSeed, 99) == Error::None);
    auto queued = initial;
    CHECK(apply(queued, Action::AccrueSteps, 1000) == Error::None);
    CHECK(queued.phase == Phase::Home && queued.pendingEncounter.formId && queued.encounters == 0);
    MemoryBackend flash;
    SaveStore store(flash);
    State restored;
    CHECK(store.restore(restored) == BootStatus::Empty && store.checkpoint(queued));
    SaveStore restart(flash);
    CHECK(restart.restore(restored) == BootStatus::Loaded && sameSnapshot(restored, queued));
    CHECK(apply(restored, Action::PresentEncounter) == Error::None);
    CHECK(restored.phase == Phase::Encounter && !restored.pendingEncounter.formId && restored.encounters == 1);
    CHECK(restored.wildFormId == queued.pendingEncounter.formId && restored.wildLevel == queued.pendingEncounter.level);
    CHECK(restart.checkpoint(restored));
    const auto active = restored;
    CHECK(apply(restored, Action::PresentEncounter) != Error::None && sameSnapshot(restored, active));
    CHECK(apply(restored, Action::AccrueSteps, 1000) == Error::None && restored.pendingEncounter.formId);
    CHECK(restored.phase == active.phase && restored.wildFormId == active.wildFormId &&
          restored.wildLevel == active.wildLevel && restored.wildRules == active.wildRules &&
          restored.wildHp == active.wildHp && restored.wildTurn == active.wildTurn &&
          restored.encounters == active.encounters && restored.hp == active.hp);
    const auto following = restored.pendingEncounter;
    const auto rng = restored.encounterRng;
    const auto target = restored.encounterTarget;
    CHECK(apply(restored, Action::AccrueSteps, 1000) == Error::None);
    CHECK(restored.pendingEncounter.formId == following.formId && restored.pendingEncounter.level == following.level &&
          restored.pendingEncounter.rules == following.rules && restored.encounterRng == rng &&
          restored.encounterTarget == target && restored.encounterProgress == 0);
    CHECK(restart.checkpoint(restored));
    SaveStore battleBoot(flash);
    State battle;
    CHECK(battleBoot.restore(battle) == BootStatus::Loaded && sameSnapshot(battle, restored));
    CHECK(apply(battle, Action::Auto) == Error::None && battle.phase == Phase::Home);
    CHECK(battle.pendingEncounter.formId == following.formId && battleBoot.checkpoint(battle));
    CHECK(apply(battle, Action::PresentEncounter) == Error::None && battle.pendingEncounter.formId == 0);
    CHECK(battle.encounters == 2 && battle.wildFormId == following.formId && battle.wildLevel == following.level);
    CHECK(battleBoot.checkpoint(battle));
    SaveStore consumedBoot(flash);
    CHECK(consumedBoot.restore(restored) == BootStatus::Loaded && sameSnapshot(restored, battle));
    CHECK(apply(restored, Action::PresentEncounter) != Error::None && sameSnapshot(restored, battle));

    // Candidate/checkpoint/ack applies to both earning and presenting a foe.
    // Before-write failure keeps the old state; lost ACK restores exactly the
    // landed state; corruption preserves the fallback and freezes writes.
    for (unsigned transition = 0; transition < 2; ++transition) {
        const auto before = transition ? queued : initial;
        auto candidate = before;
        CHECK(apply(candidate, transition ? Action::PresentEncounter : Action::AccrueSteps,
                    transition ? 0 : 1000) == Error::None);
        for (unsigned failure = 0; failure < 3; ++failure) {
            MemoryBackend faulty;
            SaveStore writer(faulty);
            State ram = before;
            CHECK(writer.restore(ram) == BootStatus::Empty && writer.checkpoint(before));
            faulty.failBeforeWrite = failure == 0;
            faulty.failAfterWrite = failure == 1;
            faulty.corruptWrite = failure == 2;
            CHECK(!writer.checkpoint(candidate) && !writer.writable() && sameSnapshot(ram, before));
            const auto writes = faulty.writes;
            CHECK(!writer.checkpoint(candidate) && faulty.writes == writes);
            faulty.failBeforeWrite = faulty.failAfterWrite = faulty.corruptWrite = false;
            SaveStore recovered(faulty);
            State reloaded;
            CHECK(recovered.restore(reloaded) == (failure == 2 ? BootStatus::RecoveryRequired : BootStatus::Loaded));
            CHECK(sameSnapshot(reloaded, failure == 1 ? candidate : before));
            CHECK(recovered.writable() == (failure != 2) && faulty.writes == writes);
        }
    }
    std::puts("PASS pending encounter save policy: three frozen schema15 fixtures, capture foreground identity, one queued foe across Home/fight restart, exact-once consumption, no backlog, six write-fault recovery cases");
}

static void xpCompanionSavePolicy() {
    auto source=newDevice(12345);CHECK(apply(source,Action::Hatch,1)==Error::None);
    // Schema 21 stores sixty 44-byte members and no party. The later slots of a
    // current save stay empty, and migration grants the three dungeon keys.
    constexpr unsigned count=kRoster60Capacity;
    source.sequence=source.foregroundSequence=400;source.collectionCount=count;
    source.captures=source.encounters=count-1;source.steps=100*source.encounters;source.nextMemberId=count+1;
    source.dungeonKeys=3;
    for(unsigned i=1;i<count;++i){source.collection[i]=source.collection[0];source.collection[i].id=i+1;source.collection[i].capturedAtSequence=i;}
    CHECK(isValid(source));Snapshot current;CHECK(encodeSnapshot(source,current));
    std::uint8_t schema26[kSchema26SnapshotSize]{};snapshot_test::schema26Image(current.bytes,schema26);
    std::uint8_t schema22[kV22SnapshotSize]{};snapshot_test::rules15Image(schema26,schema22);
    Slot old{};old.length=kV21SnapshotSize;std::memcpy(old.bytes,schema22,kV21SnapshotSize-4);
    auto put=[](std::uint8_t* p,std::uint32_t v){for(unsigned i=0;i<4;++i)p[i]=static_cast<std::uint8_t>(v>>(8*i));};
    old.bytes[4]=21;old.bytes[5]=0;old.bytes[6]=(kV21SnapshotSize-12)&255;old.bytes[7]=(kV21SnapshotSize-12)>>8;put(old.bytes+8,14);
    std::uint32_t crc=~0u;for(std::size_t i=0;i<kV21SnapshotSize-4;++i){crc^=old.bytes[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));}put(old.bytes+kV21SnapshotSize-4,~crc);
    for(unsigned failure=0;failure<3;++failure){
        MemoryBackend flash;flash.present[0]=flash.present[1]=true;flash.slots[0]=flash.slots[1]=old;
        SaveStore saves(flash);State state;CHECK(saves.restore(state)==BootStatus::Migrated && saves.writable() && !flash.writes);
        CHECK(sameSnapshot(state,source) && partyCount(state)==0 && state.collectionCount==count);
        auto candidate=state;CHECK(apply(candidate,Action::PartyAdd,count)==Error::None);
        flash.failBeforeWrite=failure==1;flash.failAfterWrite=failure==2;
        const bool durable=saves.checkpoint(candidate);CHECK(durable==(failure==0));
        if(durable)state=candidate;
        CHECK(partyCount(state)==(durable?1u:0u)); // Publish only a verified checkpoint.
        CHECK(std::memcmp(flash.slots[0].bytes,old.bytes,kV21SnapshotSize)==0);
        flash.failBeforeWrite=flash.failAfterWrite=false;SaveStore reboot(flash);State restored;
        const auto result=reboot.restore(restored);CHECK(result==(failure==1?BootStatus::Migrated:BootStatus::Loaded));
        CHECK(sameSnapshot(restored,failure==1?source:candidate));
        CHECK(restored.activeCreatureId==source.activeCreatureId && restored.collectionCount==count);
        for(unsigned i=0;i<count;++i)CHECK(restored.collection[i].id==source.collection[i].id);
        if(failure!=1){CHECK(apply(restored,Action::PartyRemove,count)==Error::None && reboot.checkpoint(restored));SaveStore again(flash);State removed;CHECK(again.restore(removed)==BootStatus::Loaded && !partyCount(removed) && sameSnapshot(removed,restored));}
    }
    std::puts("PASS schema21 ->22 companion save policy:60 members retained, initially empty extras, verified add/remove, before/after-commit faults, reboot recovery");
}

int main() {
    xpCompanionSavePolicy();
    MemoryBackend flash;
    auto state = newGame();
    SaveStore first(flash);
    CHECK(first.restore(state) == BootStatus::Empty);
    CHECK(first.checkpoint(state));
    CHECK(apply(state, Action::Walk, 100) == Error::None);
    CHECK(first.checkpoint(state));

    SaveStore restarted(flash);
    auto restored = newGame(99);
    CHECK(restarted.restore(restored) == BootStatus::Loaded);
    CHECK(restored.sequence == 1 && restored.phase == Phase::Encounter);
    CHECK(restored.seed == kDevelopmentSeed);

    // Failure before commit leaves the last acknowledged state recoverable.
    auto next = restored;
    CHECK(apply(next, Action::Attack) == Error::None);
    flash.failBeforeWrite = true;
    CHECK(!restarted.checkpoint(next));
    CHECK(!restarted.writable());
    const auto attemptedWrites = flash.writes;
    CHECK(!restarted.checkpoint(next));
    CHECK(flash.writes == attemptedWrites);
    flash.failBeforeWrite = false;
    SaveStore afterFailure(flash);
    CHECK(afterFailure.restore(restored) == BootStatus::Loaded);
    CHECK(restored.sequence == 1);

    // An uncertain commit may have landed; reboot selects the newest valid
    // state rather than replaying the action and accidentally rewarding twice.
    flash.failAfterWrite = true;
    CHECK(!afterFailure.checkpoint(next));
    flash.failAfterWrite = false;
    SaveStore afterUncertain(flash);
    CHECK(afterUncertain.restore(restored) == BootStatus::Loaded);
    CHECK(restored.sequence == 2);

    // Corrupt one slot: recover the valid fallback but freeze writes so the
    // evidence is never replaced by an automatic factory reset.
    flash.slots[0].bytes[kSnapshotSize - 1] ^= 0x80;
    const auto beforeCorruptRecovery = flash.writes;
    SaveStore damaged(flash);
    CHECK(damaged.restore(restored) == BootStatus::RecoveryRequired);
    CHECK(restored.sequence == 1);
    CHECK(!damaged.checkpoint(restored));
    CHECK(flash.writes == beforeCorruptRecovery);

    // An oversized future schema must not cause unbounded reads or clobbering.
    flash.slots[0].length = kSnapshotSize + 4000;
    SaveStore future(flash);
    CHECK(future.restore(restored) == BootStatus::RecoveryRequired);
    CHECK(!future.writable());

    MemoryBackend readbackFailure;
    SaveStore verifying(readbackFailure);
    auto initial = newGame();
    CHECK(verifying.restore(initial) == BootStatus::Empty);
    CHECK(verifying.checkpoint(initial));
    readbackFailure.corruptWrite = true;
    CHECK(apply(initial, Action::Feed) == Error::None);
    CHECK(!verifying.checkpoint(initial));
    CHECK(!verifying.writable());
    SaveStore readbackRecovery(readbackFailure);
    CHECK(readbackRecovery.restore(restored) == BootStatus::RecoveryRequired);
    CHECK(restored.sequence == 0);

    // Two checksummed states with the same sequence but different contents
    // indicate divergent history; never silently choose one as writable.
    MemoryBackend conflicting;
    Snapshot a, b;
    CHECK(encodeSnapshot(newGame(1), a));
    CHECK(encodeSnapshot(newGame(2), b));
    CHECK(conflicting.writeSlot(0, a));
    CHECK(conflicting.writeSlot(1, b));
    SaveStore conflict(conflicting);
    CHECK(conflict.restore(restored) == BootStatus::RecoveryRequired);
    CHECK(!conflict.writable());

    // A genuinely missing save can persist an egg before presenting choices.
    // Reopening the egg must not count as another first boot or hatch it early.
    MemoryBackend eggFlash;
    SaveStore eggStore(eggFlash);
    auto egg = newGame();
    CHECK(eggStore.restore(egg) == BootStatus::Empty);
    egg = newDevice();
    CHECK(eggStore.checkpoint(egg));
    SaveStore eggRestart(eggFlash);
    CHECK(eggRestart.restore(restored) == BootStatus::Loaded);
    CHECK(!restored.onboardingComplete && restored.phase == Phase::Egg && restored.collectionCount == 0);
    CHECK(restored.sequence == 0 && restored.starterId == 0);
    auto hatched = restored;
    CHECK(apply(hatched, Action::Hatch, 8) == Error::None);
    eggFlash.failAfterWrite = true;
    CHECK(!eggRestart.checkpoint(hatched));
    CHECK(!eggRestart.writable());
    CHECK(!restored.onboardingComplete && restored.phase == Phase::Egg); // No speculative RAM completion.
    const auto uncertainWrites = eggFlash.writes;
    CHECK(!eggRestart.checkpoint(hatched) && eggFlash.writes == uncertainWrites);
    eggFlash.failAfterWrite = false;
    SaveStore hatchRestart(eggFlash);
    CHECK(hatchRestart.restore(restored) == BootStatus::Loaded);
    CHECK(restored.onboardingComplete && restored.starterId == 8 && restored.sequence == 1);
    CHECK(restored.phase == Phase::Home && restored.collectionCount == 1);
    CHECK(std::strcmp(creatureName(restored), "Renamon") == 0);
    CHECK(apply(restored, Action::Hatch, 1) == Error::AlreadyHatched);

    // An unreadable partition must never be mistaken for fresh NVS.
    MemoryBackend unreadable;
    unreadable.unreadable = true;
    SaveStore unavailable(unreadable);
    restored = newGame();
    CHECK(unavailable.restore(restored) == BootStatus::RecoveryRequired);
    CHECK(!unavailable.writable() && unreadable.writes == 0 && restored.onboardingComplete);

    MemoryBackend legacy;
    legacy.present[0] = true;
    legacy.slots[0].length = (sizeof(kLegacyV4Hex) - 1) / 2;
    CHECK(legacy.slots[0].length == 404);
    const auto nibble = [](char c) { return c >= 'a' ? c - 'a' + 10 : c - '0'; };
    for (std::size_t i = 0; i < legacy.slots[0].length; ++i)
        legacy.slots[0].bytes[i] = static_cast<std::uint8_t>(nibble(kLegacyV4Hex[2 * i]) * 16 + nibble(kLegacyV4Hex[2 * i + 1]));
    SaveStore legacyRestore(legacy);
    restored = newDevice();
    CHECK(legacyRestore.restore(restored) == BootStatus::Migrated);
    CHECK(restored.onboardingComplete && restored.starterId == 0 && restored.sequence == 0);
    CHECK(restored.phase == Phase::Home && std::strcmp(creatureName(restored), "Mote") == 0);
    CHECK(legacy.writes == 0 && legacy.slots[0].length == 404);
    CHECK(apply(restored, Action::Hatch, 1) == Error::AlreadyHatched);

    // Whole Auto resolution is one candidate/checkpoint. An acknowledgment loss
    // must restore its terminal result instead of repeating capture/reward work.
    MemoryBackend autoFlash;
    SaveStore autoStore(autoFlash);
    auto automatic = newGame();
    CHECK(autoStore.restore(automatic) == BootStatus::Empty);
    CHECK(autoStore.checkpoint(automatic));
    CHECK(apply(automatic, Action::Mode, 1) == Error::None);
    CHECK(autoStore.checkpoint(automatic));
    CHECK(apply(automatic, Action::Walk, 100) == Error::None);
    CHECK(autoStore.checkpoint(automatic));
    CHECK(automatic.battleMode == BattleMode::Auto && automatic.phase == Phase::Encounter);
    const auto waiting = automatic;
    auto terminal = automatic;
    CHECK(apply(terminal, Action::Auto) == Error::None);
    CHECK(terminal.phase == Phase::Home && terminal.sequence == waiting.sequence + 1);
    CHECK(terminal.lastAutoSequence == terminal.sequence && terminal.lastAutoTurns > 0);
    CHECK(terminal.captures <= waiting.captures + 1 && terminal.collectionCount <= waiting.collectionCount + 1);
    autoFlash.failAfterWrite = true;
    CHECK(!autoStore.checkpoint(terminal) && !autoStore.writable());
    CHECK(automatic.phase == Phase::Encounter && automatic.sequence == waiting.sequence);
    const auto autoWrites = autoFlash.writes;
    CHECK(!autoStore.checkpoint(terminal) && autoFlash.writes == autoWrites);
    autoFlash.failAfterWrite = false;
    SaveStore autoRestart(autoFlash);
    CHECK(autoRestart.restore(restored) == BootStatus::Loaded);
    Snapshot expectedAuto, restoredAuto, unchangedAuto;
    CHECK(encodeSnapshot(terminal, expectedAuto) && encodeSnapshot(restored, restoredAuto));
    CHECK(std::memcmp(expectedAuto.bytes, restoredAuto.bytes, kSnapshotSize) == 0);
    CHECK(apply(restored, Action::Auto) != Error::None);
    CHECK(encodeSnapshot(restored, unchangedAuto));
    CHECK(std::memcmp(expectedAuto.bytes, unchangedAuto.bytes, kSnapshotSize) == 0);
    auto replayedAuto = waiting;
    CHECK(apply(replayedAuto, Action::Auto) == Error::None && encodeSnapshot(replayedAuto, unchangedAuto));
    CHECK(std::memcmp(expectedAuto.bytes, unchangedAuto.bytes, kSnapshotSize) == 0);

    // A real old active fight, including its played card, becomes Tactical.
    // No automatic battle is invented or executed merely by booting the save.
    MemoryBackend oldFight;
    oldFight.present[0] = true;
    oldFight.slots[0].length = (sizeof(kLegacyV5Hex) - 1) / 2;
    CHECK(oldFight.slots[0].length == 412);
    for (std::size_t i = 0; i < oldFight.slots[0].length; ++i)
        oldFight.slots[0].bytes[i] = static_cast<std::uint8_t>(nibble(kLegacyV5Hex[2 * i]) * 16 + nibble(kLegacyV5Hex[2 * i + 1]));
    SaveStore oldFightRestore(oldFight);
    CHECK(oldFightRestore.restore(restored) == BootStatus::Migrated);
    CHECK(restored.battleMode == BattleMode::Tactical && restored.phase == Phase::Encounter);
    CHECK(restored.sequence == 2 && restored.cardUsed && restored.attackBoost == 5);
    CHECK(restored.hp == 100 && restored.wildHp == 88 && restored.lastAutoSequence == 0 && restored.lastAutoTurns == 0);
    CHECK(oldFight.writes == 0 && oldFight.slots[0].length == 412);
    CHECK(apply(restored, Action::Auto) != Error::None);

    // Actual pre-RPG Glint Lv.2 maps to its existing form and RPG Lv.5/XP400.
    MemoryBackend rpgLegacy;
    rpgLegacy.present[0]=true; rpgLegacy.slots[0].length=(sizeof(kLegacyV6Hex)-1)/2;
    for(std::size_t i=0;i<rpgLegacy.slots[0].length;i++)
        rpgLegacy.slots[0].bytes[i]=static_cast<std::uint8_t>(nibble(kLegacyV6Hex[i*2])*16+nibble(kLegacyV6Hex[i*2+1]));
    const auto oldSlot=rpgLegacy.slots[0];
    SaveStore rpgRestore(rpgLegacy);
    CHECK(rpgRestore.restore(restored)==BootStatus::Migrated && rpgLegacy.writes==0);
    CHECK(restored.level==5 && restored.collection[0].xp==xpForLevel(5));
    CHECK(std::strcmp(creatureName(restored),"Glint")==0 && restored.bond==40);
    CHECK(rpgLegacy.slots[0].length==428 && !std::memcmp(oldSlot.bytes,rpgLegacy.slots[0].bytes,428));
    CHECK(apply(restored,Action::Feed)==Error::None && rpgRestore.checkpoint(restored));
    CHECK(rpgLegacy.slots[1].length==kSnapshotSize && !std::memcmp(oldSlot.bytes,rpgLegacy.slots[0].bytes,428));

    MemoryBackend evolutionFlash; SaveStore evolutionStore(evolutionFlash);
    auto ready=newDevice(); CHECK(apply(ready,Action::Hatch,2)==Error::None);
    const auto target=forms::find(ready.collection[0].formId)->children[0];
    const forms::EvolutionEdge* edge=nullptr;
    for(unsigned i=0;i<2;++i){const auto* candidate=forms::outgoing(ready.collection[0].formId,i); if(candidate && candidate->to==target) edge=candidate;}
    CHECK(edge);
    const auto need=forms::evolutionNeed(*edge);
    ready.level=ready.collection[0].level=need.level;
    ready.collection[0].xp=xpForLevel(ready.level); ready.bond=ready.collection[0].bond=need.bond;
    ready.collection[0].careState=need.care;
    ready.hp=ready.collection[0].hp=combat::formProfile(ready.collection[0].formId,need.level).stats.maxHp;
    CHECK(isValid(ready) && evolutionStore.restore(ready)==BootStatus::Empty && evolutionStore.checkpoint(ready));
    controls::EvolutionChoice choice; State evolved;
    CHECK(choice.propose(ready,target) && choice.confirm(ready,evolved));
    evolutionFlash.failAfterWrite=true;
    CHECK(!evolutionStore.checkpoint(evolved) && !evolutionStore.writable());
    CHECK(ready.collection[0].formId!=target); // No speculative replacement before durable ACK.
    const auto evolutionWrites=evolutionFlash.writes;
    CHECK(!evolutionStore.checkpoint(evolved) && evolutionFlash.writes==evolutionWrites);
    evolutionFlash.failAfterWrite=false; SaveStore evolvedRestart(evolutionFlash);
    CHECK(evolvedRestart.restore(restored)==BootStatus::Loaded && restored.collection[0].formId==target);
    CHECK(restored.sequence==evolved.sequence && restored.collection[0].xp==evolved.collection[0].xp);
    CHECK(apply(restored,Action::Evolve,target)==Error::EvolutionUnavailable);

    // Confirmed release updates only after a durable acknowledgement. A lost
    // ACK restores the same stable-ID result, including obtained-form history.
    MemoryBackend releaseFlash; SaveStore releaseStore(releaseFlash);
    auto beforeRelease = stableMemberFixture(67);
    CHECK(isValid(beforeRelease) && releaseStore.restore(beforeRelease)==BootStatus::Empty && releaseStore.checkpoint(beforeRelease));
    auto released = beforeRelease;
    CHECK(apply(released,Action::Release,19)==Error::ActiveMemberRelease);
    CHECK(apply(released,Action::Release,1)==Error::None && released.collectionCount==1);
    CHECK(activeMember(released)->id==19 && released.collection[0].id==19 && released.nextMemberId==20);
    CHECK(hasObtained(released,1) && hasObtained(released,67));
    releaseFlash.failAfterWrite=true;
    CHECK(!releaseStore.checkpoint(released) && !releaseStore.writable());
    CHECK(findMember(beforeRelease,1) && beforeRelease.collectionCount==2);
    const auto releaseWrites=releaseFlash.writes;
    CHECK(!releaseStore.checkpoint(released) && releaseFlash.writes==releaseWrites);
    releaseFlash.failAfterWrite=false; SaveStore releasedRestart(releaseFlash);
    CHECK(releasedRestart.restore(restored)==BootStatus::Loaded && !findMember(restored,1));
    CHECK(restored.activeCreatureId==19 && restored.nextMemberId==20 && hasObtained(restored,1) && hasObtained(restored,67));
    CHECK(apply(restored,Action::Release,1)==Error::UnknownMember && restored.sequence==released.sequence);

    MemoryBackend catalogLegacy;
    catalogLegacy.present[0]=true; catalogLegacy.slots[0].length=(sizeof(kLegacyV7Hex)-1)/2;
    for(std::size_t i=0;i<catalogLegacy.slots[0].length;++i)
        catalogLegacy.slots[0].bytes[i]=static_cast<std::uint8_t>(nibble(kLegacyV7Hex[i*2])*16+nibble(kLegacyV7Hex[i*2+1]));
    const auto originalV7=catalogLegacy.slots[0]; SaveStore catalogRestore(catalogLegacy);
    CHECK(catalogRestore.restore(restored)==BootStatus::Migrated && catalogLegacy.writes==0);
    CHECK(restored.phase==Phase::Encounter && restored.wildRules==4 && restored.wildFormId==4 && restored.sequence==3);
    CHECK(restored.cardUsed && restored.attackBoost==5 && restored.nextMemberId==2 && hasObtained(restored,18));
    CHECK(wildGuard(restored)==combat::Defense::None && restored.starterId==2);
    CHECK(!std::memcmp(originalV7.bytes,catalogLegacy.slots[0].bytes,500));
    CHECK(needsTestEncounterResolution(restored));
    CHECK(apply(restored,Action::Attack)==Error::InvalidAction); // Test fights cannot continue in production.
    CHECK(apply(restored,Action::ResolveTestEncounter,0)==Error::None && catalogRestore.checkpoint(restored));
    CHECK(catalogLegacy.slots[1].length==kSnapshotSize && !std::memcmp(originalV7.bytes,catalogLegacy.slots[0].bytes,500));

    // Actual old schema11 snapshots from the frozen8 encoder. Restore performs
    // no writes. Home HP scales once; active named battles retain their rules.
    // Synthetic battles require an explicit durable cleanup before gameplay.
    const auto snapshotMatchesHex = [&](const State& candidate, const char* hex) {
        Snapshot bytes; CHECK(encodeSnapshot(candidate, bytes));
        CHECK(std::strlen(hex) == kV13SnapshotSize * 2);
        // Frozen schemas11/12 contain 576 bytes; newer schemas append metadata.
        // Verify all old gameplay bytes, and independently check the new defaults.
        CHECK(bytes.bytes[4]==kSchemaVersion && bytes.bytes[8]==kRulesVersion);
        CHECK(bytes.bytes[6]==static_cast<std::uint8_t>((kSnapshotSize-12)&255) &&
              bytes.bytes[7]==static_cast<std::uint8_t>((kSnapshotSize-12)>>8));
        for (std::size_t i=0; i<4; ++i)
            CHECK(bytes.bytes[i] == static_cast<std::uint8_t>(nibble(hex[i*2])*16+nibble(hex[i*2+1])));
        for (std::size_t i=4; i<12; ++i) {
            if(i==4 || i==6 || i==7 || i==8) continue;
            CHECK(bytes.bytes[i] == static_cast<std::uint8_t>(nibble(hex[i*2])*16+nibble(hex[i*2+1])));
        }
        const auto projected=snapshot_test::eightSlotBytes(bytes.bytes);
        for (std::size_t i=12; i<kV13SnapshotSize-4; ++i)
            CHECK(projected[i] == static_cast<std::uint8_t>(nibble(hex[i*2])*16+nibble(hex[i*2+1])));
        CHECK(candidate.explorationSteps==0 && candidate.walkingEncounters==0 &&
              candidate.encounterRng==0 && candidate.encounterTarget==0 &&
              candidate.encounterProgress==0 && candidate.encounterRate==EncounterRate::Normal);
    };
    for (const auto& fixture : profile_migration_fixture::cases) {
        MemoryBackend profileFlash; profileFlash.present[0]=true;
        profileFlash.slots[0].length=kV13SnapshotSize;
        for (std::size_t i=0; i<kV13SnapshotSize; ++i)
            profileFlash.slots[0].bytes[i]=static_cast<std::uint8_t>(nibble(fixture.oldHex[i*2])*16+nibble(fixture.oldHex[i*2+1]));
        const auto original=profileFlash.slots[0];
        SaveStore profileStore(profileFlash); auto migrated=newDevice();
        CHECK(profileStore.restore(migrated)==BootStatus::Migrated && profileFlash.writes==0 && profileStore.writable());
        snapshotMatchesHex(migrated,fixture.migratedHex);
        CHECK(migrated.onboardingComplete && migrated.activeCreatureId==2);
        auto candidate=migrated;
        if(fixture.attacks) {
            CHECK(migrated.phase==Phase::Encounter && migrated.wildRules==8 && migrated.hp==17);
            if(needsTestEncounterResolution(candidate)) {
                CHECK(apply(candidate,Action::Attack)==Error::InvalidAction);
                CHECK(apply(candidate,Action::ResolveTestEncounter,0)==Error::None && candidate.phase==Phase::Home);
                CHECK(candidate.sequence==migrated.sequence+1 && candidate.rngState==migrated.rngState &&
                      candidate.steps==migrated.steps && candidate.captures==migrated.captures);
            } else {
                for(unsigned i=0;i<fixture.attacks;++i) CHECK(apply(candidate,Action::Attack)==Error::None);
                CHECK(candidate.phase==Phase::Home); snapshotMatchesHex(candidate,fixture.terminalHex);
            }
        } else {
            CHECK(migrated.phase==Phase::Home && migrated.hp==21);
            CHECK(apply(candidate,Action::Rest)==Error::None);
        }
        profileFlash.failAfterWrite=true;
        CHECK(!profileStore.checkpoint(candidate) && !profileStore.writable());
        snapshotMatchesHex(migrated,fixture.migratedHex); // Caller RAM was not replaced.
        CHECK(!std::memcmp(profileFlash.slots[0].bytes,original.bytes,kSnapshotSize));
        const auto writes=profileFlash.writes;CHECK(!profileStore.checkpoint(candidate) && profileFlash.writes==writes);
        profileFlash.failAfterWrite=false;
        SaveStore profileRestart(profileFlash);State recovered;
        CHECK(profileRestart.restore(recovered)==BootStatus::Loaded && profileFlash.writes==writes);
        Snapshot want,actual;CHECK(encodeSnapshot(candidate,want) && encodeSnapshot(recovered,actual));
        CHECK(!std::memcmp(want.bytes,actual.bytes,kSnapshotSize) && actual.bytes[4]==kSchemaVersion);
        CHECK(!std::memcmp(profileFlash.slots[0].bytes,original.bytes,kSnapshotSize));

        // The historical schema12 save also restores with zero writes,
        // preserving old wildRules8 and all fields; no second HP scaling.
        MemoryBackend previous;previous.present[0]=true;previous.slots[0].length=kV13SnapshotSize;
        for(std::size_t i=0;i<kV13SnapshotSize;++i)
            previous.slots[0].bytes[i]=static_cast<std::uint8_t>(nibble(fixture.migratedHex[i*2])*16+nibble(fixture.migratedHex[i*2+1]));
        const auto previousSlot=previous.slots[0];SaveStore upgrade(previous);State upgraded;
        CHECK(upgrade.restore(upgraded)==BootStatus::Migrated && previous.writes==0);
        snapshotMatchesHex(upgraded,fixture.migratedHex);
        CHECK(upgrade.checkpoint(upgraded) && previous.writes==1);
        CHECK(!std::memcmp(previous.slots[0].bytes,previousSlot.bytes,kSnapshotSize));
        SaveStore upgradeBoot(previous);State upgradedAgain;
        // Equal-sequence canonical states may select the original older slot.
        CHECK(upgradeBoot.restore(upgradedAgain)==BootStatus::Migrated && previous.writes==1);
        snapshotMatchesHex(upgradedAgain,fixture.migratedHex);
    }
    std::printf("PASS firmware schema11/12 ->%u migration: four frozen native fixtures, exact restored payload, Home scaling once, explicit synthetic-fight cleanup, uncertain checkpoint/reboot, original slot retained\n", static_cast<unsigned>(kSchemaVersion));

    pendingEncounterSavePolicy();

    std::puts("PASS firmware save policy: restore, commit loss, uncertain commit, corruption, future length, readback, conflict, egg, hatch, legacy onboarding, atomic Auto, Tactical migration, schema6 RPG migration, atomic confirmed evolution/release, stable IDs, retained journal, schema7 frozen-encounter migration");
}
