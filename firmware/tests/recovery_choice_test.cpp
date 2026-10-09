#include "recovery_choice.hpp"
#include "park_save_backend.hpp"
#include "catalog_fixture.hpp"
#include <cstdio>
#include <limits>

namespace {
using namespace digivice;
using namespace digivice::controls;
using namespace digivice::storage;
unsigned checks=0,failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);}}while(false)
State tired() {
    auto state=stableMemberFixture(223); // High HP form + stable member19, not array-index identity.
    // The selectable alternate must be a production partner. The shared
    // stable-ID history fixture deliberately retains the old Mote test starter.
    auto rookie=newDevice(223);(void)apply(rookie,Action::Hatch,1);
    state.collection[0]=rookie.collection[0];state.starterId=rookie.starterId;
    state.journal[(rookie.collection[0].formId-1)/32]|=1u<<((rookie.collection[0].formId-1)%32);
    state.hp=state.collection[1].hp=1;state.energy=state.collection[1].energy=0;
    return state;
}
void exactRestAndNoop() {
    auto state=tired();CHECK(isValid(state));const auto before=state;
    RecoveryChoice choice;CHECK(choice.propose(state));const auto count=choice.count();
    CHECK(count>1 && count<=40 && sameSavedState(state,before));
    auto manual=state;for(std::uint32_t i=0;i<count;++i)CHECK(apply(manual,Action::Rest)==Error::None);
    State candidate=newDevice();CHECK(choice.confirm(state,candidate));
    CHECK(sameSavedState(candidate,manual) && sameSavedState(state,before) && choice.count()==0);
    CHECK(candidate.collection[1].xp==state.collection[1].xp && candidate.sequence==state.sequence+count);
    CHECK(candidate.hp==combat::formProfile(candidate.collection[1].formId,candidate.level).stats.maxHp && candidate.energy==100);
    ParkSaveBackend flash;SaveStore saves(flash);CHECK(saves.restore(state)==BootStatus::Empty && saves.checkpoint(state));
    CHECK(saves.checkpoint(candidate));state=candidate;
    CHECK(flash.writes==2 && !choice.propose(state) && !choice.confirm(state,candidate) && flash.writes==2);
    SaveStore reboot(flash);auto restored=newDevice();
    CHECK(reboot.restore(restored)==BootStatus::Loaded && sameSavedState(restored,state) && !choice.propose(restored));
    std::printf("  Full recovery: %lu ordinary Rest events, one verified checkpoint, XP unchanged, restored complete.\n",static_cast<unsigned long>(count));
}
void staleAndCancel() {
    auto state=tired();RecoveryChoice choice;State output=newDevice();const auto untouched=output;
    CHECK(choice.propose(state));choice.cancel();CHECK(!choice.confirm(state,output) && sameSavedState(output,untouched));
    CHECK(choice.propose(state));CHECK(apply(state,Action::Feed)==Error::None);
    CHECK(!choice.confirm(state,output) && sameSavedState(output,untouched));
    state=tired();CHECK(choice.propose(state));CHECK(apply(state,Action::Select,1)==Error::None);
    CHECK(!choice.confirm(state,output) && sameSavedState(output,untouched));
    state=tired();CHECK(choice.propose(state));CHECK(apply(state,Action::Walk,100)==Error::None);
    CHECK(!choice.confirm(state,output) && !choice.propose(state) && sameSavedState(output,untouched));
    state=newDevice();CHECK(!choice.propose(state));
    state=tired();state.sequence=std::numeric_limits<std::uint32_t>::max()-1;
    CHECK(isValid(state) && recoveryRestCount(state)==0 && !choice.propose(state));
    // Direct mismatch defense still rejects even if a caller failed to advance sequence.
    state=tired();CHECK(choice.propose(state));auto other=stableMemberFixture(3);other.sequence=state.sequence;
    CHECK(!choice.confirm(other,output) && sameSavedState(output,untouched));
}
void uncertainSave(bool landed) {
    auto state=tired();const auto before=state;ParkSaveBackend flash;SaveStore saves(flash);
    CHECK(saves.restore(state)==BootStatus::Empty && saves.checkpoint(state));
    RecoveryChoice choice;State candidate;
    CHECK(choice.propose(state) && choice.confirm(state,candidate));
    flash.failBefore=!landed;flash.failAfter=landed;
    CHECK(!saves.checkpoint(candidate) && !saves.writable());
    CHECK(sameSavedState(state,before));const auto writes=flash.writes;
    CHECK(!choice.confirm(state,candidate) && !saves.checkpoint(candidate) && flash.writes==writes);
    flash.failBefore=flash.failAfter=false;SaveStore reboot(flash);auto restored=newDevice();
    CHECK(reboot.restore(restored)==BootStatus::Loaded);
    CHECK(sameSavedState(restored,landed?candidate:before));
    CHECK(choice.propose(restored)==!landed); // Recovered committed batch is never repeated.
    CHECK(flash.writes==writes);
}
}
int main() {
    exactRestAndNoop();staleAndCancel();uncertainSave(false);uncertainSave(true);
    std::printf("RecoveryChoice: %u checks, %u failures; controller=%zu bytes. No new healing/XP rules.\n",checks,failures,sizeof(RecoveryChoice));
    return failures?1:0;
}
