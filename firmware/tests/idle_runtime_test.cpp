#include "handheld_runtime_double.hpp"
#include "esp_timer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>
using namespace digivice;
namespace fake {
std::uint64_t now=0;std::vector<char> order;
display::Status panel{};display::TouchPoint touch{};esp_err_t touchError=ESP_OK,blankError=ESP_OK;
bool displayReady=true,touchReady=true,dmaQuiet=true;
unsigned touches=0,blanks=0,wakes=0;
}
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
std::int64_t esp_timer_get_time(){return static_cast<std::int64_t>(fake::now*1000);}
std::uint32_t esp_random(){return 1234;}
namespace digivice::display {
bool displayReady(){return fake::displayReady&&!fake::panel.suspended;}
bool touchReady(){return fake::touchReady&&!fake::panel.suspended;}
bool quiescent(){return fake::dmaQuiet;}
esp_err_t pollTouch(TouchPoint& point){++fake::touches;if(fake::touchError==ESP_OK)point=fake::touch;return fake::touchError;}
esp_err_t setIdleBlank(bool value){if(value)++fake::blanks;else ++fake::wakes;fake::order.push_back(value?'b':'w');if(fake::blankError==ESP_OK)fake::panel.idleBlanked=value;return fake::blankError;}
esp_err_t setSuspended(bool value){fake::panel.suspended=value;return ESP_OK;}
}
struct UsageMemory final:usage::Backend {
    usage::Record records[2]{};bool present[2]{};unsigned writes=0;bool fail=false;
    usage::Read read(unsigned n,usage::Record& out)override{if(!present[n])return usage::Read::Missing;out=records[n];return usage::Read::Present;}
    bool write(unsigned n,const usage::Record& in)override{++writes;fake::order.push_back('u');if(fail)return false;records[n]=in;present[n]=true;return true;}
};
struct GameMemory final:storage::Backend {
    storage::Slot records[2]{};bool present[2]{};unsigned writes=0;bool fail=false,failAfter=false;
    storage::ReadStatus readSlot(unsigned n,storage::Slot& out)override{if(!present[n])return storage::ReadStatus::Missing;out=records[n];return storage::ReadStatus::Present;}
    bool writeSlot(unsigned n,const Snapshot& in)override{++writes;fake::order.push_back('g');if(fail)return false;std::memcpy(records[n].bytes,in.bytes,kSnapshotSize);records[n].length=kSnapshotSize;present[n]=true;return !failAfter;}
};
struct Harness {
    State state=newGame(12345);UsageMemory usage;GameMemory game;storage::SaveStore saves{game};HandheldRuntime runtime{state,saves,usage};
    Harness(){
        fake::now=0;fake::order.clear();fake::panel={};fake::touch={};fake::touchError=fake::blankError=ESP_OK;
        fake::displayReady=fake::touchReady=fake::dmaQuiet=true;fake::touches=fake::blanks=fake::wakes=0;
        CHECK(saves.restore(state)==storage::BootStatus::Empty);
        CHECK(apply(state,Action::WorldSeed,state.seed)==Error::None);
        CHECK(apply(state,Action::EncounterSeed,99)==Error::None);CHECK(apply(state,Action::EncounterRate,1)==Error::None);CHECK(saves.checkpoint(state));
        CHECK(runtime.usage_.restore(0));CHECK(runtime.idle_.configure(30,0));runtime.uiSequence_=state.sequence;
        runtime.imu_.sample={0,motion::StepStatus::Tracking,0};game.writes=0;fake::order.clear();
    }
    void idle(std::uint64_t now){fake::now=now;runtime.imu_.sample.observedAtMs=now;runtime.pollIdle(now);}
    void input(std::uint64_t now,display::TouchPoint p={false,0,0,true}){fake::now=now;fake::touch=p;runtime.imu_.sample.observedAtMs=now;runtime.pollInterface(now);}
};
void quietAndWrites(){
    Harness h;h.idle(30000);CHECK(h.runtime.idle_.blanked()&&fake::panel.idleBlanked);
    CHECK(fake::blanks==1&&!h.game.writes&&!h.usage.writes&&!h.runtime.imu_.paused);
    for(unsigned n=1;n<=100;++n)h.idle(30000+n*20);
    CHECK(fake::blanks==1&&!h.game.writes&&!h.usage.writes);
    // Screen idle leaves the independent count publication and owner delivery alive.
    h.runtime.imu_.sample={3,motion::StepStatus::Tracking,32020};CHECK(h.runtime.pollUsage(32020));
    CHECK(h.runtime.usage_.total()==3&&h.state.explorationSteps==3&&h.game.writes==1&&h.usage.writes==1);
    CHECK(!h.runtime.imu_.paused);
}
void resultIdle(){
    const auto startChecks=checks;
    Harness h;
    CHECK(apply(h.state,Action::Explore,1000)==Error::None);
    h.state.wildHp=h.state.wildMaxHp/2; // Synthetic eligible encounter; the receipt is produced by the real core.
    const auto encounter=h.state;bool captured=false;
    for(unsigned seed=1;seed<=100&&!captured;++seed){
        h.state=encounter;h.state.rngState=seed;
        CHECK(apply(h.state,Action::Flick,41140)==Error::None);
        captured=h.state.lastCapture.result==CaptureResult::Captured;
    }
    CHECK(captured&&h.state.phase==Phase::Home);
    CHECK(h.saves.checkpoint(h.state));h.game.writes=0;
    h.runtime.uiSequence_=h.state.sequence;h.runtime.ui_.selected=deviceui::Screen::Result;
    Snapshot before;CHECK(encodeSnapshot(h.state,before));
    CHECK(!h.runtime.idleBlocked());h.idle(30000);
    CHECK(h.runtime.idle_.blanked()&&fake::panel.idleBlanked&&fake::blanks==1);
    CHECK(!h.game.writes&&!h.usage.writes&&!h.runtime.intents);
    CHECK(h.runtime.ui_.screen()==deviceui::Screen::Result&&!h.runtime.imu_.paused);
    h.input(30020,{true,206,300,true});
    CHECK(!h.runtime.idle_.blanked()&&fake::wakes==1&&h.runtime.touch_.awaitingRelease());
    CHECK(!h.runtime.intents&&!h.runtime.ui_.downs&&!h.runtime.ui_.ups);
    h.input(30040,{false,206,300,true});
    CHECK(!h.runtime.touch_.awaitingRelease()&&!h.runtime.intents);
    h.input(30060,{true,206,300,true});h.input(30080,{false,206,300,true});
    CHECK(h.runtime.ui_.downs==1&&!h.runtime.ui_.ups); // release confirmed on the next poll (touchstream)
    h.input(30100,{false,206,300,false});
    CHECK(h.runtime.ui_.downs==1&&h.runtime.ui_.ups==1&&h.runtime.intents==1);
    Snapshot after;CHECK(encodeSnapshot(h.state,after));
    CHECK(!std::memcmp(before.bytes,after.bytes,kSnapshotSize)&&!h.game.writes&&!h.usage.writes);
    // A Result label cannot bypass an encounter, playback, held contact or I/O
    // barrier. These are the same production blockers used by other screens.
    const std::function<void(Harness&)> blocks[]{
        [](auto& r){r.runtime.battle_.locked_=true;},
        [](auto& r){r.runtime.forcePressedForTest();},
        [](auto& r){r.runtime.ui_.pending_=true;},
        [](auto& r){CHECK(apply(r.state,Action::Explore,1000)==Error::None);},
        [](auto& r){r.runtime.usbTransferLease_=true;},
        [](auto&){fake::dmaQuiet=false;},
        [](auto& r){r.runtime.walkingFault_=true;}
    };
    for(const auto& block:blocks){
        Harness guarded;guarded.runtime.ui_.selected=deviceui::Screen::Result;block(guarded);
        CHECK(guarded.runtime.idleBlocked());guarded.idle(30000);
        CHECK(!guarded.runtime.idle_.blanked()&&!fake::blanks&&!guarded.game.writes&&!guarded.usage.writes);
    }
    std::printf("Result idle: %u checks; actual idle/input policy, receipt snapshot identity and existing blockers.\n",checks-startChecks);
}
void pendingBeforeBlank(){
    Harness h;h.runtime.imu_.sample={5,motion::StepStatus::Tracking,29999};CHECK(h.runtime.pollUsage(29999));
    CHECK(h.runtime.walkingPending_==5&&!h.game.writes&&!h.usage.writes);
    h.idle(30000);CHECK(h.runtime.idle_.blanked()&&h.state.explorationSteps==5&&!h.runtime.walkingPending_);
    CHECK(fake::order==std::vector<char>({'u','g','b'}));
    const auto writes=h.game.writes;h.idle(30001);CHECK(h.game.writes==writes);
}
void flushStartsEncounter(){
    Harness h;h.runtime.imu_.sample={1000,motion::StepStatus::Tracking,30000};h.idle(30000);
    CHECK(h.state.phase==Phase::Encounter&&h.game.writes==2&&h.usage.writes==1);
    CHECK(!h.runtime.idle_.blanked()&&!fake::blanks&&h.runtime.idleBlocked());
}
void homeWalkingWhileBlanked(){
    Harness h;CHECK(apply(h.state,Action::EncounterRate,2)==Error::None);CHECK(h.saves.checkpoint(h.state));
    h.runtime.uiSequence_=h.state.sequence;h.idle(30000);
    CHECK(h.runtime.idle_.blanked()&&h.runtime.touch_.awaitingRelease());
    // Backlight-only idle does not disable Home walking or require touch.
    h.runtime.imu_.sample={200,motion::StepStatus::Tracking,32000};CHECK(h.runtime.pollUsage(32000));
    CHECK(h.runtime.usage_.total()==200&&h.state.phase==Phase::Encounter&&h.state.walkingEncounters==1);
    h.input(32020);CHECK(!h.runtime.idle_.blanked()&&fake::wakes==1&&!h.runtime.intents);
    const auto writes=h.game.writes;CHECK(h.runtime.pollUsage(32021));
    CHECK(h.state.walkingEncounters==1&&h.game.writes==writes);
}
void menuIdleQueuesEncounter(){
    Harness h;h.runtime.imu_.sample={5,motion::StepStatus::Tracking,100};CHECK(h.runtime.pollUsage(100));
    h.runtime.ui_.selected=deviceui::Screen::Settings;
    h.runtime.imu_.sample={205,motion::StepStatus::Tracking,30000};h.idle(30000);
    CHECK(h.runtime.idle_.blanked()&&h.runtime.usage_.total()==205);
    CHECK(!h.runtime.walkingPending_&&h.state.explorationSteps==205&&h.state.pendingEncounter.formId&&h.state.phase==Phase::Home);
    const auto queued=h.state.pendingEncounter.formId;
    h.runtime.ui_.selected=deviceui::Screen::Home;
    h.runtime.imu_.sample.observedAtMs=30001;CHECK(h.runtime.pollUsage(30001));
    CHECK(h.state.explorationSteps==205&&!h.runtime.walkingPending_&&h.game.writes==2);
    CHECK(h.state.phase==Phase::Encounter&&h.state.wildFormId==queued&&!h.state.pendingEncounter.formId);
}
void storageFaults(){
    for(unsigned fault=0;fault<3;++fault){
        Harness h;h.runtime.imu_.sample={5,motion::StepStatus::Tracking,29999};CHECK(h.runtime.pollUsage(29999));
        h.usage.fail=fault==0;h.game.fail=fault==1;h.game.failAfter=fault==2;
        h.idle(30000);CHECK(!h.runtime.idle_.blanked()&&!fake::blanks&&h.runtime.walkingFault_&&h.state.explorationSteps==0);
        const auto gw=h.game.writes,uw=h.usage.writes;h.idle(60000);CHECK(h.game.writes==gw&&h.usage.writes==uw);
        if(fault==2){storage::SaveStore restored(h.game);State disk;CHECK(restored.restore(disk)==storage::BootStatus::Loaded&&disk.explorationSteps==5);}
    }
}
void blockers(){
    const std::function<void(Harness&)> cases[]{
        [](auto&h){h.runtime.ui_.selected=deviceui::Screen::Battle;},
        [](auto&h){h.runtime.ui_.selected=deviceui::Screen::Capture;},
        [](auto&h){h.runtime.ui_.selected=deviceui::Screen::EvolutionReview;},
        [](auto&h){h.runtime.ui_.pending_=true;},[](auto&h){h.runtime.forcePressedForTest();},
        [](auto&h){h.runtime.battle_.locked_=true;},[](auto&h){h.runtime.nearby=true;},
        [](auto&h){h.runtime.setup_.active_=true;},[](auto&h){h.runtime.interfacePaused_=true;},
        [](auto&h){h.runtime.frozen=true;},[](auto&h){h.runtime.power_.s.phase=power::Phase::Holding;},
        [](auto&h){h.runtime.usbTransferLease_=true;},[](auto&h){h.runtime.usbTransfer_.active_=true;},
        [](auto&h){h.runtime.usbTransfer_.quiet=false;},[](auto&h){h.runtime.network_.leased=true;},
        [](auto&h){h.runtime.network_.scan.busy=true;},[](auto&h){h.runtime.network_.s.state=net::State::Joining;},
        [](auto&h){h.runtime.network_.s.state=net::State::Backoff;},
        [](auto&h){h.runtime.network_.s.probePending=true;},[](auto&h){h.runtime.assets_.s.busy=true;},
        [](auto&h){h.runtime.art_.quiet=false;},[](auto&h){h.runtime.partnerArt_.quiet=false;},
        [](auto&h){h.runtime.audio_.quiet=false;},[](auto&){fake::dmaQuiet=false;},
        [](auto&){fake::displayReady=false;},[](auto&){fake::touchReady=false;},
        [](auto&h){h.runtime.imu_.available=false;},[](auto&h){h.runtime.walkingFault_=true;},
        [](auto&h){h.runtime.idleSettings_.writable_=false;},
        [](auto&h){h.runtime.practice_.allowed=false;}
    };
    for(const auto& block:cases){Harness h;block(h);CHECK(h.runtime.idleBlocked());h.idle(30000);CHECK(!h.runtime.idle_.blanked()&&!fake::blanks&&!h.game.writes&&!h.usage.writes);}
}
void wakeContact(){
    Harness h;h.idle(30000);CHECK(h.runtime.touch_.awaitingRelease());
    h.input(30020,{true,200,320,true});CHECK(!h.runtime.idle_.blanked()&&fake::wakes==1);
    CHECK(h.runtime.touch_.awaitingRelease()&&!h.runtime.touch_.pressed()&&!h.runtime.intents&&!h.runtime.ui_.downs&&!h.runtime.ui_.ups);
    h.input(30040,{true,200,320,true});CHECK(!h.runtime.intents&&!h.runtime.ui_.downs);
    h.input(30060,{false,200,320,false});CHECK(h.runtime.touch_.awaitingRelease()&&!h.runtime.intents);
    h.input(30080,{false,200,320,true});CHECK(!h.runtime.touch_.awaitingRelease()&&!h.runtime.intents&&!h.runtime.ui_.ups);
    h.input(30100,{true,200,320,true});h.input(30120,{false,200,320,true});h.input(30140,{false,200,320,false}); // confirmed release
    CHECK(h.runtime.ui_.downs==1&&h.runtime.ui_.ups==1&&h.runtime.intents==1);
    CHECK(!h.game.writes&&!h.usage.writes);
}
void wakeFaultAndMotion(){
    Harness h;h.idle(30000);fake::touchError=ESP_FAIL;
    h.input(30020,{true,200,300,true});CHECK(h.runtime.idle_.blanked()&&!fake::wakes&&!h.runtime.intents);
    fake::touchError=ESP_OK;fake::blankError=ESP_FAIL;
    h.input(30040,{true,200,300,true});CHECK(h.runtime.idle_.blanked()&&fake::wakes==1&&!h.runtime.intents);
    h.input(30060);CHECK(fake::wakes==1&&h.runtime.touch_.awaitingRelease());
    fake::blankError=ESP_OK;h.input(31040,{true,200,300,true});CHECK(!h.runtime.idle_.blanked()&&fake::wakes==2&&!h.runtime.intents);
    Harness m;m.idle(30000);auto& r=m.runtime.imu_.reading_;r.valid=r.calibrated=true;r.accelerationG={0,0,1};r.observedAtMs=30020;
    m.input(30020);r.accelerationG.x=0.2f;r.observedAtMs=30040;m.input(30040);CHECK(m.runtime.idle_.blanked());
    r.observedAtMs=30060;m.input(30060,{true,200,300,true});CHECK(!m.runtime.idle_.blanked()&&!m.runtime.intents&&m.runtime.touch_.awaitingRelease());
}
void strongerSuspension(){
    for(bool usb:{false,true}){
        Harness h;h.idle(30000);fake::now=30001;h.runtime.pauseInterface(true);
        h.runtime.usbTransferLease_=usb;h.runtime.frozen=!usb;
        CHECK(h.runtime.imu_.paused&&fake::panel.suspended&&fake::panel.idleBlanked);
        h.input(31000,{true,200,300,true});h.idle(31000);CHECK(!fake::wakes&&h.runtime.idle_.blanked());
        h.runtime.usbTransferLease_=h.runtime.frozen=false;fake::now=32000;h.runtime.pauseInterface(false);
        CHECK(!h.runtime.imu_.paused&&!fake::panel.suspended&&fake::panel.idleBlanked);
        h.input(32020,{true,200,300,true});CHECK(!h.runtime.idle_.blanked()&&fake::wakes==1&&!h.runtime.intents&&h.runtime.touch_.awaitingRelease());
    }
}
void musicIdleAndWake(){
    for(const auto screen:{deviceui::Screen::Home,deviceui::Screen::Settings,deviceui::Screen::Sound}){
        Harness h;h.runtime.ui_.selected=screen;h.runtime.audio_.musicActive=true;
        // Full audio quiescence remains false during music. Idle must use the
        // independent SFX barrier, then request Quiet without pausing sensors.
        CHECK(!h.runtime.audio_.quiescent()&&h.runtime.audio_.effectsQuiescent());
        CHECK(!h.runtime.idleBlocked());h.input(30000);
        CHECK(h.runtime.idle_.blanked()&&h.runtime.audio_.scene==device::MusicScene::Quiet);
        CHECK(!h.runtime.audio_.paused&&!h.runtime.imu_.paused&&!h.runtime.intents);
        // Scene selection resumes in the same owner poll that wakes the display.
        // The wake contact is consumed; the first subsequent tap remains usable.
        h.input(30020,{true,200,300,true});
        CHECK(!h.runtime.idle_.blanked()&&h.runtime.audio_.scene==device::MusicScene::Home);
        CHECK(h.runtime.touch_.awaitingRelease()&&!h.runtime.intents);
        h.input(30040,{false,200,300,true});
        h.input(30060,{true,200,300,true});h.input(30080,{false,200,300,true});h.input(30100,{false,200,300,false}); // confirmed release
        CHECK(h.runtime.intents==1&&h.runtime.ui_.downs==1&&h.runtime.ui_.ups==1);
        CHECK(!h.game.writes&&!h.usage.writes);
    }
    Harness effect;effect.runtime.audio_.musicActive=true;effect.runtime.audio_.quiet=false;
    CHECK(effect.runtime.idleBlocked());effect.input(30000);CHECK(!effect.runtime.idle_.blanked());
}
void musicScenePolicy(){
    Harness h;h.runtime.updateMusicScene();CHECK(h.runtime.audio_.scene==device::MusicScene::Home);
    h.runtime.battle_.locked_=true;h.runtime.updateMusicScene();CHECK(h.runtime.audio_.scene==device::MusicScene::Battle);
    h.runtime.setup_.active_=true;h.runtime.updateMusicScene();CHECK(h.runtime.audio_.scene==device::MusicScene::Quiet);
    h.runtime.setup_.active_=false;h.runtime.battle_.locked_=false;h.runtime.encounterRecoveryRequired_=true;
    h.runtime.updateMusicScene();CHECK(h.runtime.audio_.scene==device::MusicScene::Quiet);
    h.runtime.encounterRecoveryRequired_=false;h.runtime.pauseInterface(true);
    CHECK(h.runtime.audio_.paused&&h.runtime.audio_.scene==device::MusicScene::Quiet);
    h.runtime.updateMusicScene();CHECK(h.runtime.audio_.scene==device::MusicScene::Quiet);
    h.runtime.pauseInterface(false);h.runtime.updateMusicScene();
    CHECK(!h.runtime.audio_.paused&&h.runtime.audio_.scene==device::MusicScene::Home);
    h.runtime.frozen=true;h.runtime.updateMusicScene();CHECK(h.runtime.audio_.scene==device::MusicScene::Quiet);
}
int main(){quietAndWrites();resultIdle();pendingBeforeBlank();flushStartsEncounter();homeWalkingWhileBlanked();menuIdleQueuesEncounter();storageFaults();blockers();wakeContact();wakeFaultAndMotion();strongerSuspension();musicIdleAndWake();musicScenePolicy();std::printf("Idle runtime: %u checks passed; actual idle/usage methods + exact touch dispatch/pause body, hardware/UI publication doubles\n",checks);}
