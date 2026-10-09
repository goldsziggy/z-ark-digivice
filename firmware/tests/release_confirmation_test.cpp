#include "release_confirmation.hpp"
#include "park_save_backend.hpp"
#include "catalog_fixture.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %u: %s\n",__LINE__,#x); std::exit(1); } } while(false)

void encounterRelease(bool automatic,bool uncertain) {
    using namespace digivice;
    using namespace digivice::storage;
    auto state=stableMemberFixture(223);
    // Valid full roster with earlier releases; permanent IDs are not slot indices.
    const auto active=state.collection[1];
    for(std::size_t i=2;i<kCollectionCapacity;++i) {
        state.collection[i]=active;state.collection[i].id=static_cast<std::uint32_t>(19+i);
        state.collection[i].capturedAtSequence=static_cast<std::uint32_t>(100+i);
    }
    state.collectionCount=kCollectionCapacity;
    state.sequence=state.foregroundSequence=200;
    state.nextMemberId=kCollectionCapacity+19;state.captures=state.encounters=state.nextMemberId-2;state.steps=100*state.encounters;
    CHECK(isValid(state));
    if(automatic)CHECK(apply(state,Action::Mode,1)==Error::None);
    CHECK(apply(state,Action::Walk,100)==Error::None && state.wildRules==kRulesVersion);
    CHECK(captureChance(state)==0);
    const auto before=state;
    std::uint32_t id=0;CHECK(controls::parseReleaseConfirmation("21 confirm",id));
    ParkSaveBackend flash;SaveStore saves(flash);
    CHECK(saves.restore(state)==BootStatus::Empty && saves.checkpoint(state));
    auto candidate=state;
    CHECK(apply(candidate,Action::Release,id)==Error::None);
    CHECK(candidate.collectionCount==kCollectionCapacity-1 && findMember(candidate,21)==nullptr && candidate.activeCreatureId==19);
    CHECK(candidate.sequence==before.sequence+1 && candidate.phase==Phase::Encounter && candidate.battleMode==before.battleMode);
    CHECK(candidate.rngState==before.rngState && candidate.hp==before.hp && candidate.energy==before.energy &&
        candidate.wildHp==before.wildHp && candidate.wildMaxHp==before.wildMaxHp && candidate.wildTurn==before.wildTurn &&
        candidate.wildFormId==before.wildFormId && candidate.captureAttempts==before.captureAttempts &&
        candidate.shield==before.shield && candidate.attackBoost==before.attackBoost && candidate.cardUsed==before.cardUsed);
    CHECK(std::memcmp(candidate.journal,before.journal,sizeof(candidate.journal))==0 && candidate.nextMemberId==before.nextMemberId);
    flash.failAfter=uncertain;
    if(uncertain) {
        CHECK(!saves.checkpoint(candidate) && !saves.writable() && sameSavedState(state,before));
        const auto writes=flash.writes;CHECK(!saves.checkpoint(candidate) && flash.writes==writes);
    } else { CHECK(saves.checkpoint(candidate));state=candidate; }
    flash.failAfter=false;SaveStore reboot(flash);auto restored=newDevice();
    CHECK(reboot.restore(restored)==BootStatus::Loaded && sameSavedState(restored,candidate));
    const auto restoredBefore=restored;
    CHECK(apply(restored,Action::Release,id)!=Error::None && sameSavedState(restored,restoredBefore));
    CHECK(apply(restored,Action::Release,restored.activeCreatureId)!=Error::None && sameSavedState(restored,restoredBefore));
    auto old=before;old.wildRules=9;CHECK(isValid(old));const auto oldBefore=old;
    CHECK(apply(old,Action::Release,id)!=Error::None && sameSavedState(old,oldBefore));
}

int main() {
    std::uint32_t id = 31;
    for (const char* input : std::initializer_list<const char*>{nullptr, "", "confirm", "1", "1confirm", "0 confirm", "-1 confirm", "+1 confirm",
        "4294967296 confirm", "2 CONFIRM", "2 confirm now", "2 confirmx", "2 confirmed", "2.0 confirm", "2\nconfirm", "2 confirm\n"}) {
        CHECK(!digivice::controls::parseReleaseConfirmation(input, id) && id == 31);
    }
    CHECK(digivice::controls::parseReleaseConfirmation("19 confirm", id) && id == 19);
    CHECK(digivice::controls::parseReleaseConfirmation(" \t0000042\t confirm\t", id) && id == 42);
    CHECK(digivice::controls::parseReleaseConfirmation("4294967295 confirm", id) && id == UINT32_MAX);
    char huge[64]; std::memset(huge, ' ', 63); huge[63] = 0;
    CHECK(!digivice::controls::parseReleaseConfirmation(huge, id) && id == UINT32_MAX);
    encounterRelease(false,false);encounterRelease(true,false);encounterRelease(false,true);encounterRelease(true,true);
    std::printf("PASS release confirmation: %u checks; explicit bounded stable-ID parser\n", checks);
}
