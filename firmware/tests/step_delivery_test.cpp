#include "step_delivery.hpp"
#include "park_save_backend.hpp"
#include <cstdio>
#include <limits>

namespace {
using namespace digivice;
using namespace digivice::motion;
using namespace digivice::storage;
unsigned checks=0,failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);}}while(false)

void longEncounter() {
    auto state=newGame();CHECK(apply(state,Action::Walk,100)==Error::None);
    const auto initial=state;
    ParkSaveBackend flash;SaveStore saves(flash);
    CHECK(saves.restore(state)==BootStatus::Empty && saves.checkpoint(state));
    MotionCounter counter(1);StepDelivery delivery;counter.observe(1000,0);
    std::uint32_t peak=0;unsigned commits=0;
    for(std::uint32_t second=1;second<=3600;++second) {
        const auto now=std::uint64_t(second)*1000;
        const auto sample=counter.observe(1000+second*2,now);
        CHECK(sample.status==Status::Tracking && sample.newSteps==2);
        if(sample.pendingSteps>peak)peak=sample.pendingSteps;
        const auto result=delivery.pump(counter,state,saves,now,true);
        CHECK(result.result==DeliveryResult::Committed || result.result==DeliveryResult::Deferred);
        if(result.result==DeliveryResult::Committed)++commits;
        CHECK(state.phase==Phase::Encounter && state.rngState==initial.rngState && state.wildHp==initial.wildHp &&
              state.hp==initial.hp && state.energy==initial.energy && state.wildTurn==initial.wildTurn && state.encounters==initial.encounters);
    }
    CHECK(state.steps==initial.steps+7200 && state.stepCredit==initial.stepCredit+7200);
    CHECK(counter.poll(3600000).pendingSteps==0 && peak==60 && commits==120 && flash.writes==121);
    SaveStore restarted(flash);auto restored=newDevice();
    CHECK(restarted.restore(restored)==BootStatus::Loaded && sameSavedState(restored,state));
    MotionCounter rebooted(2);CHECK(rebooted.observe(8200,0).newSteps==0); // No previous boot replay.
    std::printf("  One-hour encounter: 7200 synthetic steps, %u verified commits, peak pending=%lu, battle unchanged.\n",commits,static_cast<unsigned long>(peak));
}
void guardOverflowAndReanchor() {
    auto state=newGame();ParkSaveBackend flash;SaveStore saves(flash);
    CHECK(saves.restore(state)==BootStatus::Empty && saves.checkpoint(state));
    MotionCounter counter(3);StepDelivery delivery;counter.observe(0,0);
    for(std::uint32_t second=1;second<=2050;++second) {
        counter.observe(second*2,std::uint64_t(second)*1000);
        CHECK(delivery.pump(counter,state,saves,std::uint64_t(second)*1000,false).result==DeliveryResult::Blocked);
    }
    CHECK(state.steps==0 && state.phase==Phase::Home && flash.writes==1);
    CHECK(counter.poll(2050000).status==Status::PendingOverflow && counter.poll(2050000).pendingSteps==4096);
    CHECK(!delivery.reanchor(counter,saves,2050000));
    unsigned drained=0;
    while(counter.poll(2050000).pendingSteps) {
        const auto result=delivery.pump(counter,state,saves,2050000,true);
        CHECK(result.result==DeliveryResult::Committed);drained+=result.steps;
    }
    CHECK(drained==4096 && state.steps==4096 && state.phase==Phase::Encounter && state.encounters==1);
    CHECK(counter.poll(2050000).status==Status::PendingOverflow);
    CHECK(delivery.reanchor(counter,saves,2050000));
    CHECK(counter.observe(5000,2050000).newSteps==0); // Explicitly uncredited gap.
    CHECK(counter.observe(5002,2051000).newSteps==2);
    CHECK(delivery.pump(counter,state,saves,2080000,true).result==DeliveryResult::Committed);
    CHECK(state.steps==4098 && counter.poll(2080000).pendingSteps==0 && !delivery.halted());
}
void uncertainSave(bool landed, bool force=false) {
    auto state=newGame();CHECK(apply(state,Action::Walk,100)==Error::None);
    const auto before=state;ParkSaveBackend flash;SaveStore saves(flash);
    CHECK(saves.restore(state)==BootStatus::Empty && saves.checkpoint(state));
    MotionCounter counter(4);StepDelivery delivery;counter.observe(0,0);counter.observe(2,1000);
    flash.failAfter=landed;flash.failBefore=!landed;
    CHECK(delivery.pump(counter,state,saves,force?1000:30000,true,force).result==DeliveryResult::SaveRecovery);
    CHECK(sameSavedState(before,state) && counter.poll(30000).pendingSteps==2 && delivery.halted() && !saves.writable());
    const auto writes=flash.writes;
    CHECK(delivery.pump(counter,state,saves,31000,true,force).result==DeliveryResult::SaveRecovery && flash.writes==writes);
    CHECK(!delivery.reanchor(counter,saves,31000));
    flash.failAfter=flash.failBefore=false;SaveStore reboot(flash);auto restored=newDevice();
    CHECK(reboot.restore(restored)==BootStatus::Loaded);
    CHECK(restored.steps==before.steps+(landed?2:0) && restored.sequence==before.sequence+(landed?1:0));
    MotionCounter newCounter(5);CHECK(newCounter.observe(2,0).newSteps==0);
    StepDelivery newDelivery;CHECK(newDelivery.pump(newCounter,restored,reboot,30000,true).result==DeliveryResult::Idle);
    CHECK(flash.writes==writes); // Reboot resolves ambiguity, never repeats previous RAM batch.
}
void guardsAndBounds() {
    auto state=newDevice();ParkSaveBackend flash;SaveStore saves(flash);
    CHECK(saves.restore(state)==BootStatus::Empty && saves.checkpoint(state));
    MotionCounter counter(6);StepDelivery delivery;counter.observe(0,0);counter.observe(2,1000);
    CHECK(delivery.pump(counter,state,saves,30000,true).result==DeliveryResult::Blocked && flash.writes==1);
    state=newGame();state.sequence=std::numeric_limits<std::uint32_t>::max();
    CHECK(isValid(state));
    CHECK(delivery.pump(counter,state,saves,30000,true).result==DeliveryResult::CoreRejected);
    CHECK(delivery.halted() && counter.poll(30000).pendingSteps==2 && flash.writes==1);
    Config bad;bad.maximumStepsPerSecond=0;MotionCounter invalid(7,bad);StepDelivery clean;
    CHECK(!clean.reanchor(invalid,saves,30000));
    CHECK(countRequiresRecovery(Status::CounterReset) && countRequiresRecovery(Status::PendingOverflow));
    CHECK(!countRequiresRecovery(Status::Stale) && !countRequiresRecovery(Status::Tracking));
}
void shutdownFlush() {
    auto state=newGame();ParkSaveBackend flash;SaveStore saves(flash);
    CHECK(saves.restore(state)==BootStatus::Empty && saves.checkpoint(state));
    MotionCounter counter(8);StepDelivery delivery;counter.observe(0,0);counter.observe(2,1000);
    CHECK(delivery.pump(counter,state,saves,1000,true).result==DeliveryResult::Deferred);
    CHECK(delivery.pump(counter,state,saves,1000,false,true).result==DeliveryResult::Blocked);
    CHECK(counter.poll(1000).pendingSteps==2 && state.steps==0 && flash.writes==1);
    CHECK(delivery.pump(counter,state,saves,1000,true,true).result==DeliveryResult::Committed);
    CHECK(counter.poll(1000).pendingSteps==0 && state.steps==2 && flash.writes==2);
    SaveStore restarted(flash);auto restored=newDevice();
    CHECK(restarted.restore(restored)==BootStatus::Loaded && sameSavedState(restored,state));
    CHECK(delivery.pump(counter,state,saves,1000,true,true).result==DeliveryResult::Idle && flash.writes==2);
    counter.observe(4,2000);
    CHECK(delivery.pump(counter,state,saves,2000,true).result==DeliveryResult::Deferred);
    flash.failBefore=true;
    CHECK(delivery.pump(counter,state,saves,2000,true,true).result==DeliveryResult::SaveRecovery);
    CHECK(counter.poll(2000).pendingSteps==2 && state.steps==2 && delivery.halted());
    const auto writes=flash.writes;
    flash.failBefore=false;
    CHECK(delivery.pump(counter,state,saves,2000,true,true).result==DeliveryResult::SaveRecovery && flash.writes==writes);

    auto unpaired=newDevice();ParkSaveBackend blank;SaveStore unpairedSaves(blank);
    CHECK(unpairedSaves.restore(unpaired)==BootStatus::Empty && unpairedSaves.checkpoint(unpaired));
    MotionCounter beforePair(9);StepDelivery blocked;beforePair.observe(0,0);beforePair.observe(2,1000);
    CHECK(blocked.pump(beforePair,unpaired,unpairedSaves,1000,true,true).result==DeliveryResult::Blocked);
    CHECK(beforePair.poll(1000).pendingSteps==2 && blank.writes==1);
    unpaired=newGame();unpaired.phase=Phase::Egg;
    CHECK(blocked.pump(beforePair,unpaired,unpairedSaves,1000,true,true).result==DeliveryResult::Blocked);
    CHECK(beforePair.poll(1000).pendingSteps==2 && blank.writes==1);
}
}
int main() {
    longEncounter();guardOverflowAndReanchor();uncertainSave(false);uncertainSave(true);
    uncertainSave(false,true);uncertainSave(true,true);guardsAndBounds();shutdownFlush();
    std::printf("StepDelivery: %u checks, %u failures; policy=%zu bytes. Synthetic motion + NVS faults; no sensor calibration claim.\n",checks,failures,sizeof(StepDelivery));
    return failures?1:0;
}
