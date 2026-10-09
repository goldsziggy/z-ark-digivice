#include "handheld_runtime_double.hpp"
#include "esp_timer.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>

using namespace digivice;
namespace fake {
std::uint64_t now=0;
unsigned randomCalls=0,delays=0;
std::function<void()> delayed;
std::vector<char> order;
}
std::int64_t esp_timer_get_time() {return static_cast<std::int64_t>(fake::now*1000);}
std::uint32_t esp_random() {++fake::randomCalls;return 0x12345678u;}
void vTaskDelay(TickType_t ms) {fake::now+=ms;++fake::delays;if(fake::delayed)fake::delayed();}
unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x);std::exit(1);}}while(false)

struct UsageMemory final:usage::Backend {
    usage::Record records[2]{};bool present[2]{};unsigned writes=0;
    bool failBefore=false,failAfter=false;
    usage::Read read(unsigned n,usage::Record& out) override {if(!present[n])return usage::Read::Missing;out=records[n];return usage::Read::Present;}
    bool write(unsigned n,const usage::Record& in) override {
        ++writes;fake::order.push_back('u');if(failBefore)return false;
        records[n]=in;present[n]=true;return !failAfter;
    }
};
struct GameMemory final:storage::Backend {
    storage::Slot slots[2]{};bool present[2]{};unsigned writes=0;
    bool failBefore=false,failAfter=false;
    storage::ReadStatus readSlot(unsigned n,storage::Slot& out) override {if(!present[n])return storage::ReadStatus::Missing;out=slots[n];return storage::ReadStatus::Present;}
    bool writeSlot(unsigned n,const Snapshot& in) override {
        ++writes;fake::order.push_back('g');if(failBefore)return false;
        std::memcpy(slots[n].bytes,in.bytes,kSnapshotSize);slots[n].length=kSnapshotSize;present[n]=true;return !failAfter;
    }
};
struct Harness {
    State state=newGame(12345);
    UsageMemory usage;
    GameMemory game;
    storage::SaveStore saves{game};
    HandheldRuntime runtime{state,saves,usage};
    Harness() {
        fake::now=0;fake::randomCalls=fake::delays=0;fake::delayed={};fake::order.clear();
        CHECK(saves.restore(state)==storage::BootStatus::Empty);
        CHECK(apply(state,Action::WorldSeed,state.seed)==Error::None);CHECK(saves.checkpoint(state));
        CHECK(runtime.usage_.restore(0));runtime.imu_.sample.status=motion::StepStatus::Tracking;
        game.writes=0;fake::order.clear();
    }
    void seeded() {
        CHECK(apply(state,Action::EncounterSeed,99)==Error::None);
        CHECK(apply(state,Action::EncounterRate,1)==Error::None);
        CHECK(saves.checkpoint(state));game.writes=0;fake::order.clear();
    }
    bool tick(std::uint32_t count,std::uint64_t now,bool force=false) {
        fake::now=now;runtime.imu_.sample.acceptedSteps=count;runtime.imu_.sample.observedAtMs=now;
        return runtime.pollUsage(now,force);
    }
    State restoredGame() {storage::SaveStore restored(game);State out;CHECK(restored.restore(out)==storage::BootStatus::Loaded);return out;}
    std::uint64_t restoredUsage() {usage::Store restored(usage);CHECK(restored.restore(0));return restored.total();}
};
void deliver(nearby::Protocol& from,nearby::Protocol& to,const nearby::Mac& origin,std::uint64_t now) {
    nearby::Datagram message;unsigned n=0;
    while(from.pop(message)){CHECK(++n<=nearby::kTxCapacity);CHECK(to.receive(origin,message.bytes,message.length,now));}
}
void firstPeerExchange(Harness& h) {
    h.seeded();h.runtime.beginNearby();h.runtime.pollNearby(0);
    nearby::Mac local{{2,4,6,8,10,12}},other{{2,4,6,8,10,14}};
    nearby::Protocol peer;CHECK(peer.open(other,{1,4,1},0,77));
    CHECK(peer.receive(local,h.runtime.nearbyRadio_.lastSent,h.runtime.nearbyRadio_.lastSize,1));
    deliver(peer,h.runtime.nearby_,other,1);
    CHECK(h.runtime.nearby_.challenge(0,nearby::Mode::Auto,999,42,1));
    deliver(h.runtime.nearby_,peer,local,1);CHECK(peer.accept(1));
    deliver(peer,h.runtime.nearby_,other,1);deliver(h.runtime.nearby_,peer,local,1);deliver(peer,h.runtime.nearby_,other,1);
    h.runtime.nearby_.tick(2401);
    deliver(h.runtime.nearby_,peer,local,2401);deliver(peer,h.runtime.nearby_,other,2401);
    CHECK(h.runtime.nearby_.view().match.sequence==1);
    h.runtime.pollNearby(2401);CHECK(h.runtime.audio_.cues==1 && h.runtime.nearbyCuePhase_==1);
}
void unseededWorldIsLifetimeOnly() {
    for(bool forced:{false,true}) {
        Harness h;h.state.worldSeed=0;h.runtime.walkingPending_=15;
        h.runtime.tradeSession_.foregroundBlocked=true; // Old prepared trade defers world initialization.
        const auto before=h.state;
        CHECK(h.tick(1000,100,forced));
        CHECK(h.runtime.usage_.total()==1000&&!h.runtime.walkingPending_&&!h.game.writes&&!fake::randomCalls);
        Snapshot a,b;CHECK(encodeSnapshot(before,a)&&encodeSnapshot(h.state,b));CHECK(!std::memcmp(a.bytes,b.bytes,sizeof(a.bytes)));
    }
}
void homePanels() {
    // Real shared UI, real walking method and real core; only physical step
    // publication, clock and persistence hardware are doubled. A panel change
    // is presentation-only and never resets the current encounter interval.
    using namespace deviceui;
    for(unsigned panel=0;panel<4;++panel) {
        Harness h; Controller ui; Model model; model.writable=true;
        h.runtime.ui_.actual=&ui; ui.update(h.state,model);
        std::uint64_t at=10;
        auto tap=[&](int x,int y) {
            CHECK(!ui.touch(h.state,model,{TouchKind::Down,static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),at+=30}));
            auto intent=ui.touch(h.state,model,{TouchKind::Up,static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),at+=60});
            CHECK(intent.kind==IntentKind::Navigation); ui.resolve(); ui.update(h.state,model);
        };
        for(unsigned i=0;i<panel;++i) tap(355,190);
        CHECK(ui.screen()==Screen::Home && static_cast<unsigned>(ui.homePanel())==panel);
        CHECK(ui.walkingEligible() && ui.interactionIdle());
        CHECK(h.state.encounterRate==EncounterRate::Normal);
        if(panel==static_cast<unsigned>(HomePanel::Settings)) {
            // Menus earn a durable encounter without opening its presentation.
            tap(206,323); CHECK(ui.screen()==Screen::Settings && !ui.walkingEligible());
            CHECK(h.tick(200,1000,true)); CHECK(h.runtime.usage_.total()==200);
            CHECK(!h.runtime.walkingPending_ && h.state.explorationSteps==200 && h.state.pendingEncounter.formId && h.state.phase==Phase::Home);
            tap(206,365); CHECK(ui.screen()==Screen::Home && ui.homePanel()==HomePanel::Settings && ui.walkingEligible());
            CHECK(h.tick(200,1001)); CHECK(h.state.phase==Phase::Encounter && !h.state.pendingEncounter.formId);
            ui.update(h.state,model);
        }
        const unsigned baseline=panel==static_cast<unsigned>(HomePanel::Settings) ? 200 : 0;
        unsigned first=0;
        for(unsigned count=1;count<=200;++count) {
            CHECK(h.tick(baseline+count,2000+count*100));
            if(!first && h.state.phase==Phase::Encounter) first=count;
            ui.update(h.state,model);
        }
        CHECK(h.tick(baseline+200,23000,true));
        CHECK(h.runtime.usage_.total()==baseline+200);
        CHECK(h.state.phase==Phase::Encounter && h.state.walkingEncounters==1 && h.state.battleMode==BattleMode::Tactical);
        CHECK(baseline || (first>=40 && first<=80));
        CHECK(h.state.explorationSteps==baseline+200 && h.state.pendingEncounter.formId);
        CHECK(!h.runtime.walkingPending_ && fake::randomCalls==0);
        const auto saved=h.state; const auto writes=h.game.writes;
        for(unsigned n=0;n<20;++n) CHECK(h.tick(baseline+200,23000+n));
        CHECK(std::memcmp(&saved,&h.state,sizeof(State))==0 && h.game.writes==writes);
    }
    // A real held carousel survives the checkpoint; only presentation waits.
    Harness h; Controller ui; Model model; model.writable=true;
    h.runtime.ui_.actual=&ui; ui.update(h.state,model);
    CHECK(!ui.touch(h.state,model,{TouchKind::Down,250,190,10}));
    CHECK(ui.walkingEligible() && !ui.interactionIdle());
    CHECK(h.tick(200,100,true));
    CHECK(!h.runtime.walkingPending_ && fake::randomCalls==0 && h.state.pendingEncounter.formId && h.state.phase==Phase::Home);
    const auto queued=h.state.pendingEncounter; const auto backgroundWrites=h.game.writes;
    CHECK(h.tick(200,200,true)); CHECK(!h.runtime.walkingPending_ && h.game.writes==backgroundWrites);
    CHECK(ui.screen()==Screen::Home && !ui.interactionIdle());
    CHECK(!ui.touch(h.state,model,{TouchKind::Move,205,190,210}));
    CHECK(ui.touch(h.state,model,{TouchKind::Up,160,190,260}).kind==IntentKind::Navigation);
    ui.resolve(); CHECK(ui.screen()==Screen::Home && ui.interactionIdle());
    CHECK(h.tick(200,261)); CHECK(h.state.phase==Phase::Encounter && h.state.walkingEncounters==1);
    CHECK(h.state.explorationSteps==200 && !h.runtime.walkingPending_ && fake::randomCalls==0 && h.state.wildFormId==queued.formId);
    const auto writes=h.game.writes; CHECK(h.tick(200,262)); CHECK(h.game.writes==writes);
}
void queuedRecovery() {
    {
        Harness h;h.seeded();h.runtime.ui_.eligible=false;
        CHECK(h.tick(200,100,true));const auto queued=h.state.pendingEncounter;
        CHECK(queued.formId && h.restoredGame().pendingEncounter.formId==queued.formId && h.state.phase==Phase::Home);
        const auto rng=h.state.encounterRng,target=h.state.encounterTarget;
        CHECK(h.tick(205,101));CHECK(h.runtime.walkingPending_==5);
        CHECK(h.state.pendingEncounter.formId==queued.formId && h.state.encounterRng==rng && h.state.encounterTarget==target);
        h.runtime.ui_.eligible=true;CHECK(h.tick(205,102));
        CHECK(h.state.phase==Phase::Encounter && h.state.wildFormId==queued.formId && !h.state.pendingEncounter.formId);
        CHECK(h.state.explorationSteps==205 && !h.runtime.walkingPending_ && !h.state.encounterProgress);
        CHECK(h.tick(205,103));CHECK(!h.state.pendingEncounter.formId && h.state.walkingEncounters==1);
    }
    {
        Harness h;h.seeded();h.runtime.ui_.eligible=false;CHECK(h.tick(200,100,true));
        State restored=h.restoredGame();storage::SaveStore rebootSaves(h.game);
        CHECK(rebootSaves.restore(restored)==storage::BootStatus::Loaded);
        HandheldRuntime reboot(restored,rebootSaves,h.usage);CHECK(reboot.usage_.restore(0));
        reboot.ui_.eligible=false;reboot.imu_.sample={0,motion::StepStatus::Tracking,100};
        CHECK(reboot.pollUsage(100));CHECK(restored.phase==Phase::Home && restored.pendingEncounter.formId);
        reboot.ui_.eligible=true;CHECK(reboot.pollUsage(101));
        CHECK(restored.phase==Phase::Encounter && !restored.pendingEncounter.formId && restored.walkingEncounters==1);
        const auto writes=h.game.writes;CHECK(reboot.pollUsage(102));CHECK(h.game.writes==writes);
    }
    for(bool uncertain:{false,true}) {
        Harness h;h.seeded();h.runtime.ui_.eligible=false;CHECK(h.tick(200,100,true));
        const auto queued=h.state.pendingEncounter;
        h.runtime.ui_.eligible=true;h.game.failBefore=!uncertain;h.game.failAfter=uncertain;
        CHECK(!h.tick(200,101));
        CHECK(h.runtime.walkingFault_ && h.state.phase==Phase::Home && h.state.pendingEncounter.formId==queued.formId);
        h.game.failBefore=h.game.failAfter=false;const auto recovered=h.restoredGame();
        CHECK(recovered.phase==(uncertain?Phase::Encounter:Phase::Home));
        CHECK(recovered.pendingEncounter.formId==(uncertain?0:queued.formId));
        CHECK(recovered.walkingEncounters==(uncertain?1u:0u));
    }
    {
        Harness h;h.seeded();const auto target=encounterStepsRemaining(h.state);
        CHECK(target>1);CHECK(h.tick(target-1,100,true));CHECK(!h.state.pendingEncounter.formId);
        h.runtime.interfacePaused_=true;h.runtime.frozen=true;h.runtime.quiescent=false;
        h.runtime.imu_.sample={target,motion::StepStatus::Paused,101};
        CHECK(h.runtime.pollUsage(101));CHECK(h.runtime.usage_.total()==target-1);
        h.runtime.quiescent=true;CHECK(h.runtime.pollUsage(102,true));
        CHECK(h.runtime.usage_.total()==target && h.state.explorationSteps==target);
        CHECK(h.state.phase==Phase::Home && h.state.pendingEncounter.formId && !h.state.walkingEncounters);
        CHECK(h.restoredGame().pendingEncounter.formId==h.state.pendingEncounter.formId && h.restoredUsage()==target);
    }
    for(bool stale:{false,true}) {
        Harness h;h.seeded();h.runtime.interfacePaused_=true;
        h.runtime.imu_.sample={10,stale?motion::StepStatus::Paused:motion::StepStatus::InvalidSample,0};
        CHECK(h.runtime.pollUsage(stale?1000:100,true));
        CHECK(h.runtime.usage_.total()==10 && !h.state.explorationSteps && !h.state.pendingEncounter.formId);
    }
    {
        Harness h;h.seeded();h.runtime.ui_.eligible=false;h.runtime.uiSequence_=h.state.sequence;
        CHECK(h.tick(5,100,true));CHECK(h.runtime.uiSequence_==h.state.sequence);
        const auto observed=h.runtime.uiSequence_;
        CHECK(apply(h.state,Action::Feed)==Error::None);CHECK(h.saves.checkpoint(h.state));
        CHECK(h.tick(6,101,true));
        CHECK(h.runtime.uiSequence_==observed && h.state.message==Message::Fed && h.state.sequence>observed);
    }
}
int main() {
    unseededWorldIsLifetimeOnly();
    homePanels();queuedRecovery();
    {
        Harness h;
        CHECK(h.tick(3,100));CHECK(h.runtime.usage_.total()==3 && h.runtime.usage_.session()==3);
        CHECK(h.runtime.walkingPending_==3 && fake::randomCalls==0 && h.game.writes==1);
        const auto seed=h.state.encounterRng,target=h.state.encounterTarget;
        for(unsigned i=0;i<100;++i)CHECK(h.tick(3,101+i));
        CHECK(h.runtime.usage_.total()==3 && h.runtime.walkingPending_==3 && fake::randomCalls==0);
        CHECK(h.state.encounterRng==seed && h.state.encounterTarget==target && h.game.writes==1 && h.usage.writes==0);
        h.runtime.imu_.reads=0;h.runtime.imu_.sample={4,motion::StepStatus::Tracking,250};
        CHECK(h.runtime.pollUsage(249)); // Worker published just after caller read the clock.
        CHECK(h.runtime.imu_.reads==1 && h.runtime.walkingPending_==4 && h.runtime.usage_.total()==4);
    }
    {
        Harness h;h.seeded();
        for(unsigned n=1;n<64;++n)CHECK(h.tick(n,n*100));
        CHECK(h.game.writes==0 && h.usage.writes==0 && h.runtime.walkingPending_==63);
        CHECK(h.tick(64,6400));CHECK(h.game.writes==1 && h.usage.writes==1);
        CHECK(h.state.explorationSteps==64 && h.runtime.walkingPending_==0);
        CHECK(h.restoredUsage()==64 && h.restoredGame().explorationSteps==64);
        CHECK(h.tick(65,6500));CHECK(h.tick(65,36400));
        CHECK(h.game.writes==2 && h.usage.writes==2 && h.state.explorationSteps==65);
        for(unsigned n=0;n<100;++n)CHECK(h.tick(65,36500+n));
        CHECK(h.game.writes==2 && h.usage.writes==2);
    }
    {
        Harness h;h.seeded();CHECK(h.tick(5,100));
        h.runtime.ui_.eligible=false;CHECK(h.tick(105,1000));
        CHECK(h.runtime.usage_.total()==105 && !h.runtime.walkingPending_ && h.state.explorationSteps==105);
        CHECK(h.tick(205,31000));CHECK(!h.runtime.walkingPending_ && h.state.pendingEncounter.formId && h.state.phase==Phase::Home);
        // Forced idle/power checkpoints persist the queue without presenting it.
        const auto beforeMenu=h.state;
        CHECK(h.tick(205,31000,true)); CHECK(h.restoredUsage()==205);
        CHECK(!h.runtime.walkingPending_ && std::memcmp(&beforeMenu,&h.state,sizeof(State))==0);
        h.runtime.ui_.eligible=true;CHECK(h.tick(205,31001));
        CHECK(h.state.explorationSteps==205 && h.state.phase==Phase::Encounter && h.runtime.walkingPending_==0);
        CHECK(h.tick(205,31002));CHECK(h.state.explorationSteps==205);
        h.runtime.setup_.open=true;CHECK(h.tick(210,31003));CHECK(h.runtime.walkingPending_==5);
        h.runtime.setup_.open=false;h.runtime.practice_.allowed=false;
        CHECK(h.tick(220,31004));CHECK(h.runtime.walkingPending_==15);
        h.runtime.practice_.allowed=true;CHECK(h.tick(220,31005));CHECK(h.runtime.walkingPending_==15);
        CHECK(h.runtime.usage_.total()==220);
    }
    {
        // Raw contact can remain held after the UI cancelled its gesture.
        // Pending proposals likewise defer; neither path redraws the RNG.
        for(unsigned blocker=0;blocker<2;++blocker) {
            Harness h;
            h.runtime.touchPressed_=blocker==0; h.runtime.ui_.idle=blocker==0;
            CHECK(h.tick(1200,100,true));
            CHECK(h.runtime.usage_.total()==1200 && h.restoredUsage()==1200);
            CHECK(!h.runtime.walkingPending_ && h.state.pendingEncounter.formId && fake::randomCalls==0 && h.state.phase==Phase::Home);
            const auto writes=h.game.writes;
            for(unsigned n=0;n<20;++n) CHECK(h.tick(1200,101+n,true));
            CHECK(!h.runtime.walkingPending_ && h.game.writes==writes && fake::randomCalls==0);
            h.runtime.touchPressed_=false;h.runtime.ui_.idle=true;
            CHECK(h.tick(1200,200)); CHECK(h.state.phase==Phase::Encounter && h.state.walkingEncounters==1);
            CHECK(!h.runtime.walkingPending_ && h.state.explorationSteps==1000 && fake::randomCalls==0);
        }
    }
    {
        // Off admits lifetime counts without ever seeding an encounter. Turning
        // it back on must not replay that absolute count as fresh walking.
        Harness h; CHECK(apply(h.state,Action::EncounterRate,0)==Error::None);
        CHECK(h.saves.checkpoint(h.state));h.game.writes=0;
        CHECK(h.tick(200,100,true));
        CHECK(h.runtime.usage_.total()==200 && !h.runtime.walkingPending_ && !fake::randomCalls && !h.game.writes);
        CHECK(apply(h.state,Action::EncounterRate,2)==Error::None);CHECK(h.saves.checkpoint(h.state));
        CHECK(h.tick(200,101));CHECK(!h.runtime.walkingPending_ && !h.state.explorationSteps && !fake::randomCalls);
    }
    {
        Harness h;h.seeded();CHECK(h.tick(5,100));
        // A quiet-Home administrative flush commits under the old rate.
        // No prior committed step receives a new multiplier.
        CHECK(h.runtime.pollUsage(101,true));CHECK(h.state.encounterProgress==5);
        CHECK(apply(h.state,Action::EncounterRate,3)==Error::None);CHECK(h.saves.checkpoint(h.state));
        CHECK(h.tick(5,102));CHECK(h.state.encounterProgress==5);
        CHECK(h.tick(6,103));CHECK(h.runtime.pollUsage(104,true));CHECK(h.state.encounterProgress==8);
        CHECK(apply(h.state,Action::EncounterRate,0)==Error::None);CHECK(h.saves.checkpoint(h.state));
        CHECK(h.tick(506,200));CHECK(h.runtime.usage_.total()==506 && h.runtime.walkingPending_==0);
        CHECK(apply(h.state,Action::EncounterRate,2)==Error::None);CHECK(h.saves.checkpoint(h.state));
        CHECK(h.tick(506,201));CHECK(h.state.encounterProgress==8 && h.runtime.walkingPending_==0);
    }
    {
        Harness h;h.seeded();CHECK(h.tick(5,100));
        h.runtime.ui_.eligible=false;h.runtime.ui_.idle=false;
        CHECK(h.tick(6,200,true));CHECK(!h.runtime.walkingPending_ && h.state.encounterProgress==6);
        // The forced pre-setting flush counts the old tail under its old rate.
        CHECK(apply(h.state,Action::EncounterRate,3)==Error::None);CHECK(h.saves.checkpoint(h.state));
        h.runtime.walkingPending_=0;h.runtime.ui_.eligible=h.runtime.ui_.idle=true;
        CHECK(h.tick(6,201));CHECK(h.state.encounterProgress==6 && !h.runtime.walkingPending_);
        CHECK(h.tick(7,202,true));CHECK(h.state.encounterProgress==9 && h.state.explorationSteps==7);
    }
    {
        Harness h;h.seeded();CHECK(h.tick(1000,100));
        CHECK(h.state.phase==Phase::Encounter && h.state.walkingEncounters==1 && h.runtime.walkingPending_==0);
        const auto active=h.state.wildFormId,hp=h.state.wildHp;
        CHECK(h.tick(2000,200));CHECK(h.state.walkingEncounters==1 && h.runtime.walkingPending_==0);
        CHECK(h.state.pendingEncounter.formId && h.state.wildFormId==active && h.state.wildHp==hp && h.runtime.usage_.total()==2000);
        while(h.state.phase==Phase::Encounter)CHECK(apply(h.state,Action::Attack)==Error::None);
        CHECK(h.saves.checkpoint(h.state));CHECK(h.tick(2000,201));
        CHECK(h.state.phase==Phase::Encounter && h.state.walkingEncounters==2 && !h.state.pendingEncounter.formId && h.runtime.walkingPending_==0);
    }
    {
        Harness h;h.state=newDevice(12345);CHECK(h.tick(10,100));
        CHECK(h.state.phase==Phase::Egg && h.runtime.usage_.total()==10 && h.runtime.walkingPending_==0 && !fake::randomCalls);
        CHECK(apply(h.state,Action::Hatch,2)==Error::None);CHECK(apply(h.state,Action::WorldSeed,h.state.seed)==Error::None);CHECK(h.saves.checkpoint(h.state));
        CHECK(h.tick(10,101));CHECK(!h.runtime.walkingPending_ && h.state.explorationSteps==0);
    }
    {
        Harness h;h.seeded();State fight=newGame(18);
        CHECK(apply(fight,Action::Explore,1000)==Error::None);
        fight.battleMode=BattleMode::Auto;autobattle::Trace trace;
        CHECK(applyAuto(fight,&trace)==Error::None);CHECK(h.runtime.battle_.startAuto(trace,0));
        CHECK(h.tick(12,100));CHECK(h.runtime.usage_.total()==12 && h.runtime.walkingPending_==12);
        h.runtime.battle_.cancel();CHECK(h.tick(12,101));CHECK(h.runtime.walkingPending_==12);
        h.runtime.imu_.sample={15,motion::StepStatus::Tracking,200};
        CHECK(h.runtime.pollUsage(1000));CHECK(h.runtime.usage_.total()==15 && h.runtime.walkingPending_==12);
        CHECK(h.tick(15,1001));CHECK(h.runtime.walkingPending_==12); // Stale delta cannot later become new steps.
    }
    {
        Harness h;h.seeded();CHECK(h.tick(5,100));
        State played=h.state;CHECK(apply(played,Action::Play)==Error::None);CHECK(h.saves.checkpoint(played));h.state=played;
        const auto energy=played.energy,bond=played.bond,sequence=played.sequence;
        CHECK(h.runtime.pollUsage(101,true));CHECK(h.state.energy==energy && h.state.bond==bond && h.state.sequence==sequence+1);
    }
    {
        Harness h;h.seeded();h.usage.failBefore=true;CHECK(!h.tick(64,100));
        CHECK(h.runtime.walkingFault_ && !h.runtime.usage_.writable() && h.game.writes==0);
        CHECK(!h.tick(65,101));CHECK(h.runtime.usage_.total()==64 && h.state.explorationSteps==0);
    }
    {
        Harness h;h.seeded();h.game.failBefore=true;CHECK(!h.tick(64,100));
        CHECK(h.runtime.walkingFault_ && !h.saves.writable());
        CHECK(h.restoredUsage()==64 && h.restoredGame().explorationSteps==0);
        CHECK(fake::order.size()==2 && fake::order[0]=='u' && fake::order[1]=='g');
        CHECK(!h.tick(65,101));CHECK(h.state.explorationSteps==0);
    }
    {
        Harness h;h.game.failAfter=true;CHECK(!h.tick(3,100));CHECK(fake::randomCalls==0 && h.runtime.walkingFault_);
        h.game.failAfter=false;auto restored=h.restoredGame();CHECK(restored.encounterRng!=0 && h.state.encounterRng==0);
        storage::SaveStore rebootGame(h.game);CHECK(rebootGame.restore(restored)==storage::BootStatus::Loaded);
        HandheldRuntime reboot(restored,rebootGame,h.usage);CHECK(reboot.usage_.restore(0));
        reboot.imu_.sample={3,motion::StepStatus::Tracking,100};CHECK(reboot.pollUsage(100));CHECK(fake::randomCalls==0);
    }
    {
        Harness h;h.seeded();h.game.failAfter=true;CHECK(!h.tick(64,100));
        CHECK(h.state.explorationSteps==0 && h.restoredUsage()==64);
        h.game.failAfter=false;auto recovered=h.restoredGame();CHECK(recovered.explorationSteps==64);
        storage::SaveStore rebootGame(h.game);CHECK(rebootGame.restore(recovered)==storage::BootStatus::Loaded);
        HandheldRuntime reboot(recovered,rebootGame,h.usage);CHECK(reboot.usage_.restore(0));
        reboot.imu_.sample={3,motion::StepStatus::Tracking,100};CHECK(reboot.pollUsage(100));
        CHECK(reboot.usage_.total()==67 && reboot.walkingPending_==3 && recovered.explorationSteps==64);
        CHECK(reboot.pollUsage(101));CHECK(reboot.usage_.total()==67 && reboot.walkingPending_==3);
    }
    {
        Harness h;h.seeded();CHECK(h.tick(5,100));h.runtime.quiescent=false;fake::order.clear();
        fake::delayed=[&] {if(fake::delays==2){fake::order.push_back('p');h.runtime.imu_.sample={6,motion::StepStatus::Paused,fake::now};h.runtime.quiescent=true;}};
        CHECK(h.runtime.prepareUsageRestart());CHECK(fake::delays==2 && h.runtime.interfacePaused_);
        CHECK(h.restoredUsage()==6 && h.restoredGame().explorationSteps==6);
        CHECK(fake::order.size()==3 && fake::order[0]=='p' && fake::order[1]=='u' && fake::order[2]=='g');
        fake::delayed={};
    }
    {
        Harness h;h.seeded();CHECK(h.tick(5,100));h.runtime.quiescent=false;fake::order.clear();
        CHECK(!h.runtime.prepareUsageRestart());CHECK(fake::delays==25 && !h.runtime.interfacePaused_);
        CHECK(h.runtime.usage_.total()==5 && h.runtime.walkingPending_==5 && fake::order.empty());
    }
    {
        Harness h;h.seeded();h.usage.failBefore=true;CHECK(!h.tick(64,100));
        const auto writes=h.usage.writes;CHECK(h.runtime.prepareUsageRestart());
        CHECK(h.runtime.interfacePaused_ && h.usage.writes==writes); // Recovery reboot never claims the tail was saved.
    }
    {
        Harness h;h.seeded();CHECK(h.tick(5,100));
        h.runtime.network_.quiet=ESP_ERR_NOT_FINISHED;
        h.runtime.beginNearby();CHECK(h.runtime.nearbyPhase_==HandheldRuntime::NearbyPhase::Starting);
        CHECK(h.state.explorationSteps==5 && !h.runtime.walkingPending_);
        CHECK(h.runtime.assets_.paused_ && h.runtime.network_.paused_);
        CHECK(!h.runtime.nearbyNetworkPaused_ && !h.runtime.nearbyAssetsPaused_);
        const auto saved=h.restoredGame();const auto writes=h.game.writes;
        h.runtime.pollNearby(101);CHECK(!h.runtime.nearbyRadio_.begins && !h.runtime.network_.leaseBegins);
        h.runtime.network_.quiet=ESP_OK;h.runtime.assets_.idle=false;
        h.runtime.pollNearby(102);CHECK(!h.runtime.nearbyRadio_.begins);
        h.runtime.assets_.idle=true;h.runtime.pollNearby(103);
        CHECK(h.runtime.nearbyPhase_==HandheldRuntime::NearbyPhase::Active && h.runtime.network_.lease);
        CHECK(h.runtime.nearby_.view().stage==nearby::Stage::Discovering && h.runtime.nearbyRadio_.begins==1);
        CHECK(h.runtime.nearbyFighter_.memberId==h.state.activeCreatureId);
        CHECK(h.tick(205,200));CHECK(h.runtime.usage_.total()==205 && !h.runtime.walkingPending_ && h.state.explorationSteps==205 && h.state.pendingEncounter.formId);
        h.runtime.network_.releaseResult=ESP_ERR_NOT_FINISHED;h.runtime.closeNearby();h.runtime.pollNearby(201);
        CHECK(h.runtime.nearbyPhase_==HandheldRuntime::NearbyPhase::Stopping && h.runtime.network_.lease && h.runtime.nearbyRadio_.idle);
        CHECK(h.runtime.network_.paused_ && h.runtime.assets_.paused_ && !h.runtime.nearbyQuiescent());
        CHECK(h.tick(305,202));CHECK(h.runtime.usage_.total()==305 && !h.runtime.walkingPending_);
        h.runtime.network_.releaseResult=ESP_OK;h.runtime.pollNearby(203);
        CHECK(h.runtime.nearbyQuiescent() && !h.runtime.network_.lease && h.runtime.network_.releasedStarted);
        CHECK(!h.runtime.network_.paused_ && !h.runtime.assets_.paused_);
        CHECK(h.game.writes>writes && h.state.foregroundSequence==saved.foregroundSequence && h.state.hp==saved.hp && h.state.activeCreatureId==saved.activeCreatureId);
        CHECK(h.tick(305,204));CHECK(!h.runtime.walkingPending_ && h.state.explorationSteps==305 && h.state.phase==Phase::Encounter);
    }
    {
        Harness h;h.seeded();CHECK(h.tick(5,100));
        // OpenNearby changes the real UI screen before calling beginNearby.
        // Its forced checkpoint records walking without changing that screen
        // or the frozen fighter; presentation waits until returning Home.
        h.runtime.ui_.eligible=false;h.runtime.beginNearby();h.runtime.pollNearby(101);
        CHECK(h.runtime.nearbyPhase_==HandheldRuntime::NearbyPhase::Active && !h.runtime.walkingPending_ && h.state.explorationSteps==5);
        CHECK(h.tick(205,200,true));CHECK(!h.runtime.walkingPending_ && h.state.explorationSteps==205 && h.state.pendingEncounter.formId);
        h.runtime.closeNearby();h.runtime.pollNearby(201);CHECK(h.runtime.nearbyQuiescent());
        h.runtime.ui_.eligible=true;CHECK(h.tick(205,202,true));
        CHECK(h.state.explorationSteps==205 && h.state.phase==Phase::Encounter && !h.runtime.walkingPending_ && h.runtime.usage_.total()==205);
    }
    {
        Harness h;h.seeded();h.runtime.network_.paused_=h.runtime.assets_.paused_=true;
        h.runtime.beginNearby();h.runtime.pollNearby(1);CHECK(h.runtime.nearbyPhase_==HandheldRuntime::NearbyPhase::Active);
        h.runtime.closeNearby();h.runtime.pollNearby(2);
        CHECK(h.runtime.nearbyQuiescent() && h.runtime.network_.paused_ && h.runtime.assets_.paused_);
    }
    {
        Harness h;h.seeded();
        const auto* selected=activeMember(h.state);CHECK(selected);
        const auto care=memberCare(*selected);
        const nearby::Fighter frozen{selected->id,selected->formId,selected->level,care.offense,care.protection};
        h.runtime.network_.quiet=ESP_ERR_NOT_FINISHED;h.runtime.beginNearby();
        CHECK(nearby::sameFighter(h.runtime.nearbyFighter_,frozen));
        // Even an external owner changing care while radio startup is delayed
        // cannot update this already frozen offer. Use real care actions here.
        CHECK(h.state.fullness==70&&care.protection==0);
        CHECK(apply(h.state,Action::Feed)==Error::None);
        CHECK(memberCare(*activeMember(h.state)).protection==1);
        CHECK(h.saves.checkpoint(h.state));
        Snapshot before;CHECK(encodeSnapshot(h.state,before));const auto writes=h.game.writes;
        h.runtime.network_.quiet=ESP_OK;h.runtime.pollNearby(1);
        CHECK(nearby::sameFighter(h.runtime.nearbyFighter_,frozen));
        nearby::Mac local{{2,4,6,8,10,12}},other{{2,4,6,8,10,14}};
        nearby::Protocol peer;CHECK(peer.open(other,{1,4,1,2,5},1,77));
        CHECK(peer.receive(local,h.runtime.nearbyRadio_.lastSent,h.runtime.nearbyRadio_.lastSize,1));
        CHECK(nearby::sameFighter(peer.view().peers[0].fighter,frozen));
        deliver(peer,h.runtime.nearby_,other,1);
        deviceui::Controller ui;deviceui::Model model;model.writable=true;
        model.nearby=&h.runtime.nearby_.view();model.nearbyLocalFighter=frozen;
        ui.update(h.state,model);std::uint64_t at=100;
        const auto tap=[&](int x,int y) {
            CHECK(!ui.touch(h.state,model,{deviceui::TouchKind::Down,static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),at+=30}));
            auto intent=ui.touch(h.state,model,{deviceui::TouchKind::Up,static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),at+=60});
            ui.resolve();ui.update(h.state,model);return intent;
        };
        for(unsigned i=0;i<3;++i)CHECK(tap(355,190).kind==deviceui::IntentKind::Navigation);
        CHECK(tap(206,323).kind==deviceui::IntentKind::OpenNearby);
        CHECK(tap(120,312).kind==deviceui::IntentKind::Navigation);
        CHECK(tap(280,240).kind==deviceui::IntentKind::Navigation);
        const auto invite=tap(206,302);CHECK(invite.kind==deviceui::IntentKind::NearbyChallenge && invite.nearbyMode==nearby::Mode::Auto);fake::now=1;
        for(unsigned changed=0;changed<6;++changed) {
            auto stale=invite;
            if(changed==0)++stale.peer.bytes[5];if(changed==1)++stale.nearbyOpenNonce;
            if(changed==2)++stale.nearbyFighters[0].level;if(changed==3)++stale.nearbyFighters[1].level;
            if(changed==4)stale.value=nearby::kMaxPeers;if(changed==5)stale.nearbyMode=static_cast<nearby::Mode>(2);
            h.runtime.nearbyIntent(stale);CHECK(h.runtime.nearby_.view().stage==nearby::Stage::Discovering);
        }
        h.runtime.nearbyIntent(invite);CHECK(h.runtime.nearby_.view().stage==nearby::Stage::Outgoing && h.runtime.nearby_.view().offeredMode==nearby::Mode::Auto);
        deliver(h.runtime.nearby_,peer,local,1);CHECK(peer.accept(1));
        deliver(peer,h.runtime.nearby_,other,1);deliver(h.runtime.nearby_,peer,local,1);deliver(peer,h.runtime.nearby_,other,1);
        for(unsigned turn=1;turn<=nearby::kMaxExchanges&&h.runtime.nearby_.view().stage==nearby::Stage::Playing;++turn){
            const auto now=1+turn*nearby::kAutoPaceMs;
            h.runtime.nearby_.tick(now);deliver(h.runtime.nearby_,peer,local,now);deliver(peer,h.runtime.nearby_,other,now);
            CHECK(nearby::sameFighter(h.runtime.nearby_.view().match.fighters[0],frozen));
        }
        CHECK(h.runtime.nearby_.view().stage==nearby::Stage::Finished&&peer.view().stage==nearby::Stage::Finished);
        Snapshot after;CHECK(encodeSnapshot(h.state,after));
        CHECK(!std::memcmp(before.bytes,after.bytes,kSnapshotSize)&&h.game.writes==writes);
        h.runtime.closeNearby();h.runtime.pollNearby(2);CHECK(h.runtime.nearbyQuiescent());
        h.runtime.beginNearby();const auto updated=memberCare(*activeMember(h.state));
        CHECK(h.runtime.nearbyFighter_.offenseBonus==updated.offense&&h.runtime.nearbyFighter_.protectionBonus==updated.protection);
        CHECK(!nearby::sameFighter(h.runtime.nearbyFighter_,frozen));
    }
    {
        // Guest acceptance also binds the actually reviewed invitation; it is
        // independent of wild mode and cannot mutate either care store.
        Harness h;h.seeded();h.runtime.beginNearby();h.runtime.pollNearby(1);
        nearby::Mac local{{2,4,6,8,10,12}},other{{2,4,6,8,10,14}};
        nearby::Protocol peer;CHECK(peer.open(other,{1,4,1,2,5},1,77));
        CHECK(peer.receive(local,h.runtime.nearbyRadio_.lastSent,h.runtime.nearbyRadio_.lastSize,1));
        deliver(peer,h.runtime.nearby_,other,1);CHECK(peer.challenge(0,nearby::Mode::Auto,8877,123,1));
        deliver(peer,h.runtime.nearby_,other,1);CHECK(h.runtime.nearby_.view().stage==nearby::Stage::Incoming);
        Snapshot before;CHECK(encodeSnapshot(h.state,before));const auto writes=h.game.writes;
        deviceui::Controller ui;deviceui::Model model;model.writable=true;model.nearby=&h.runtime.nearby_.view();model.nearbyLocalFighter=h.runtime.nearbyFighter_;
        ui.update(h.state,model);std::uint64_t at=100;
        const auto tap=[&](int x,int y) {
            CHECK(!ui.touch(h.state,model,{deviceui::TouchKind::Down,static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),at+=30}));
            auto intent=ui.touch(h.state,model,{deviceui::TouchKind::Up,static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),at+=60});
            ui.resolve();ui.update(h.state,model);return intent;
        };
        for(unsigned i=0;i<3;++i)CHECK(tap(355,190).kind==deviceui::IntentKind::Navigation);
        CHECK(tap(206,323).kind==deviceui::IntentKind::OpenNearby);
        const auto accept=tap(120,252);CHECK(accept.kind==deviceui::IntentKind::NearbyAccept && accept.nearbyMode==nearby::Mode::Auto);fake::now=1;
        for(unsigned changed=0;changed<5;++changed) {
            auto stale=accept;
            if(changed==0)++stale.peer.bytes[5];if(changed==1)++stale.nearbySession;
            if(changed==2)++stale.nearbyFighters[0].level;if(changed==3)++stale.nearbyFighters[1].level;
            if(changed==4)stale.nearbyMode=nearby::Mode::Tactical;
            h.runtime.nearbyIntent(stale);CHECK(h.runtime.nearby_.view().stage==nearby::Stage::Incoming);
        }
        h.runtime.nearbyIntent(accept);CHECK(h.runtime.nearby_.view().stage==nearby::Stage::Accepting);
        deliver(h.runtime.nearby_,peer,local,1);deliver(peer,h.runtime.nearby_,other,1);deliver(h.runtime.nearby_,peer,local,1);
        for(unsigned turn=1;turn<=nearby::kMaxExchanges && peer.view().stage==nearby::Stage::Playing;++turn) {
            const auto now=1+turn*nearby::kAutoPaceMs;peer.tick(now);deliver(peer,h.runtime.nearby_,other,now);deliver(h.runtime.nearby_,peer,local,now);
        }
        CHECK(h.runtime.nearby_.view().stage==nearby::Stage::Finished && peer.view().stage==nearby::Stage::Finished);
        Snapshot after;CHECK(encodeSnapshot(h.state,after));CHECK(!std::memcmp(before.bytes,after.bytes,kSnapshotSize) && h.game.writes==writes);
    }
    {
        Harness h;h.seeded();h.runtime.network_.quiet=ESP_ERR_NOT_FINISHED;
        h.runtime.beginNearby();fake::now=15000;h.runtime.pollNearby(fake::now);
        CHECK(h.runtime.nearbyPhase_==HandheldRuntime::NearbyPhase::Stopping && !h.runtime.nearbyRadio_.begins);
        h.runtime.pollNearby(15001);CHECK(h.runtime.nearbyQuiescent() && !h.runtime.network_.leaseReleases);
        CHECK(!h.runtime.network_.paused_ && !h.runtime.assets_.paused_);
    }
    {
        Harness h;h.seeded();h.runtime.nearbyRadio_.beginResult=ESP_FAIL;
        h.runtime.beginNearby();h.runtime.pollNearby(1);CHECK(h.runtime.nearbyPhase_==HandheldRuntime::NearbyPhase::Stopping);
        CHECK(h.runtime.network_.lease);h.runtime.pollNearby(2);
        CHECK(h.runtime.nearbyQuiescent() && !h.runtime.network_.releasedStarted && !h.runtime.network_.lease);
    }
    {
        Harness h;h.seeded();h.runtime.beginNearby();h.runtime.pollNearby(1);
        h.runtime.nearbyRadio_.endResult=ESP_ERR_NOT_FINISHED;h.runtime.closeNearby();fake::now=5001;h.runtime.pollNearby(fake::now);
        CHECK(h.runtime.nearbyPhase_==HandheldRuntime::NearbyPhase::Fault && !h.runtime.nearbyQuiescent());
        CHECK(!h.runtime.network_.leaseReleases && h.runtime.network_.paused_ && h.runtime.assets_.paused_);
        h.runtime.nearbyRadio_.endResult=ESP_OK;h.runtime.network_.releaseResult=ESP_FAIL;h.runtime.pollNearby(5002);
        CHECK(h.runtime.nearbyPhase_==HandheldRuntime::NearbyPhase::Fault && h.runtime.nearbyRadio_.quiescent() && h.runtime.nearbyLease_);
        CHECK(h.tick(100,5003));CHECK(h.runtime.usage_.total()==100 && !h.runtime.walkingPending_);
    }
    {
        Harness h;h.seeded();h.runtime.beginNearby();h.runtime.pollNearby(1);
        const auto networkPauses=h.runtime.network_.pauses,assetPauses=h.runtime.assets_.pauses;
        h.runtime.frozen=true;h.runtime.closeNearby();h.runtime.pollNearby(2);
        CHECK(h.runtime.nearbyQuiescent() && h.runtime.network_.paused_ && h.runtime.assets_.paused_);
        CHECK(h.runtime.network_.pauses==networkPauses && h.runtime.assets_.pauses==assetPauses);
        CHECK(!h.runtime.nearbyNetworkPaused_ && !h.runtime.nearbyAssetsPaused_); // Root power resume owns restoring these original intents.
    }
    {
        Harness h;h.seeded();fake::now=100;
        h.runtime.imu_.sample={1000,motion::StepStatus::Tracking,100};h.runtime.beginNearby();
        CHECK(h.state.phase==Phase::Encounter && !h.runtime.nearbyBusy() && !h.runtime.nearbyRadio_.begins && !h.runtime.network_.leaseBegins);
        CHECK(!h.runtime.network_.paused_ && !h.runtime.assets_.paused_);
    }
    {
        Harness h;h.seeded();h.runtime.beginNearby();h.runtime.pollNearby(1);
        h.runtime.walkingPending_=5; // Background progress cannot replace a frozen peer session.
        const auto frozen=h.runtime.nearbyFighter_;
        CHECK(h.tick(100,100,true));CHECK(h.restoredUsage()==100 && h.state.explorationSteps==105);
        CHECK(!h.runtime.walkingPending_ && h.runtime.nearbyPhase_==HandheldRuntime::NearbyPhase::Active);
        CHECK(nearby::sameFighter(frozen,h.runtime.nearbyFighter_) && h.state.phase==Phase::Home);
    }
    {
        Harness h;firstPeerExchange(h);
        h.runtime.pollNearby(3600);CHECK(h.runtime.audio_.cues==2 && h.runtime.nearbyCuePhase_==2); // Reveal+1199ms.
        h.runtime.pollNearby(3601);CHECK(h.runtime.audio_.cues==2 && h.runtime.nearbyCuePhase_==2); // No Hit1ms later.
        h.runtime.pollNearby(4199);CHECK(h.runtime.audio_.cues==2);
        h.runtime.pollNearby(4200);CHECK(h.runtime.audio_.cues==3 && h.runtime.nearbyCuePhase_==3); // Actual600ms dwell.
        h.runtime.pollNearby(4201);CHECK(h.runtime.audio_.cues==3);
    }
    {
        Harness h;firstPeerExchange(h);h.runtime.pollNearby(3600);CHECK(h.runtime.audio_.cues==2);
        h.runtime.pollNearby(4201);CHECK(h.runtime.audio_.cues==2 && h.runtime.nearbyCuePhase_==3); // >=1800ms: drop stale Hit.
        h.runtime.pollNearby(4202);CHECK(h.runtime.audio_.cues==2);
    }
    {
        Harness h;firstPeerExchange(h);h.runtime.pollNearby(4500);
        CHECK(h.runtime.audio_.cues==1 && h.runtime.nearbyCuePhase_==3); // Both late Attack and Hit skipped after stall.
    }
    std::printf("Walking/Nearby runtime: %u checks passed; actual source methods, doubled publication/UI/radio/SDK boundary\n",checks);
}
