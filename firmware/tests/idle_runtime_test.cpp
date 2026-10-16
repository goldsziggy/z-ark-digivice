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
    Harness h;h.idle(30000);CHECK(h.runtime.idle_.blanked()&&fake::panel.idleBlanked&&h.runtime.audio_.screenBlanked);
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
    CHECK(!h.runtime.idle_.blanked()&&fake::wakes==1&&h.runtime.touch_.awaitingRelease()&&!h.runtime.audio_.screenBlanked);
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
void nfcPolicy(){
    Harness baseline;CHECK(baseline.runtime.nfcAllowed());baseline.runtime.nfc_.active=true;
    CHECK(baseline.runtime.idleBlocked());baseline.idle(30000);CHECK(!baseline.runtime.idle_.blanked());
    baseline.runtime.nfc_.active=false;baseline.idle(30001);CHECK(baseline.runtime.idle_.blanked());CHECK(!baseline.runtime.nfcAllowed());
    const std::function<void(Harness&)> cases[]{
        [](auto&h){h.runtime.interfacePaused_=true;},[](auto&h){h.runtime.setup_.active_=true;},
        [](auto&h){h.runtime.frozen=true;},[](auto&h){h.runtime.usbTransferLease_=true;},
        [](auto&h){h.runtime.encounterRecoveryRequired_=true;},[](auto&h){h.runtime.tradeSession_.healthy_=false;},
        [](auto&h){h.runtime.tradeSession_.blocked=true;},[](auto&h){h.runtime.nearby=true;},
        [](auto&h){h.runtime.battle_.locked_=true;},[](auto&h){h.runtime.practice_.allowed=false;},
        [](auto&h){h.saves.requireRecovery("test");}
    };
    for(const auto& block:cases){Harness h;block(h);CHECK(!h.runtime.nfcAllowed());CHECK(!h.game.writes&&!h.usage.writes);}
}
void blockers(){
    const std::function<void(Harness&)> cases[]{
        [](auto&h){h.runtime.ui_.selected=deviceui::Screen::Battle;},
        [](auto&h){h.runtime.ui_.selected=deviceui::Screen::Capture;},
        [](auto&h){h.runtime.ui_.pending_=true;},[](auto&h){h.runtime.forcePressedForTest();},
        [](auto&h){h.runtime.battle_.locked_=true;},[](auto&h){h.runtime.nearby=true;},
        [](auto&h){h.runtime.setup_.active_=true;},[](auto&h){h.runtime.interfacePaused_=true;},
        [](auto&h){h.runtime.frozen=true;},[](auto&h){h.runtime.power_.tick({1,true});},
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
    { Harness b;b.runtime.audio_.buzzer=true;
      for(const auto screen:{deviceui::Screen::Sound,deviceui::Screen::NfcTest,deviceui::Screen::Settings}) {
        b.runtime.ui_.selected=screen;b.runtime.updateMusicScene();CHECK(b.runtime.audio_.scene==device::MusicScene::Quiet);
      }
      b.runtime.ui_.selected=deviceui::Screen::Home;b.runtime.updateMusicScene();CHECK(b.runtime.audio_.scene==device::MusicScene::Home);
      b.runtime.ui_.selected=deviceui::Screen::Battle;b.runtime.battle_.locked_=true;
      b.runtime.updateMusicScene();CHECK(b.runtime.audio_.scene==device::MusicScene::Battle);
      b.runtime.ui_.selected=deviceui::Screen::Home;b.runtime.battle_.locked_=false;b.runtime.audio_.musicActive=true;
      CHECK(!b.runtime.idleBlocked());b.input(30000);CHECK(b.runtime.idle_.blanked()&&b.runtime.audio_.screenBlanked);
    }

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
void backgroundCareTimeouts(){
    for(const unsigned seconds:{30u,60u,120u,300u})for(const unsigned offset:{0u,980u}){
        Harness h;h.runtime.careAwakeMs_=20;
        CHECK(h.runtime.idle_.configure(seconds,20+offset));
        const auto before=h.state.careMinute;
        for(std::uint64_t now=20+offset;now<=601000;now+=20)h.input(now);
        CHECK(h.state.careMinute==before+10 && h.game.writes==10);
        CHECK(fake::blanks==1 && fake::wakes==0 && h.runtime.idle_.blanked());
        CHECK(!h.runtime.intents && !h.runtime.audio_.cues);
        CHECK(h.runtime.uiSequence_==h.state.sequence);
        State restored;storage::SaveStore reboot(h.game);
        CHECK(reboot.restore(restored)==storage::BootStatus::Loaded && restored.careMinute==h.state.careMinute);
    }
    // A real touch just before expiry extends the window, including across a care tick.
    Harness h;h.runtime.careAwakeMs_=20;CHECK(h.runtime.idle_.configure(60,20));
    h.input(20);h.input(59980,{true,200,200,true});h.input(60000,{false,200,200,true});
    h.input(60020,{false,200,200,false});h.input(119960);
    CHECK(!h.runtime.idle_.blanked());h.input(120000);CHECK(h.runtime.idle_.blanked());
    // A pending foreground publication must still wake and play its own cue.
    Harness pending;pending.idle(30000);pending.runtime.careAwakeMs_=1;
    CHECK(apply(pending.state,Action::Feed)==Error::None);
    const auto unacknowledged=pending.runtime.uiSequence_;
    pending.runtime.pollCareAndAuto(60001);
    CHECK(pending.runtime.uiSequence_==unacknowledged);
    pending.input(60020);CHECK(!pending.runtime.idle_.blanked() && pending.runtime.audio_.cues==1);
    // Failed care checkpoints never publish the candidate or fabricate activity.
    Harness failed;failed.idle(30000);failed.runtime.careAwakeMs_=1;failed.game.fail=true;
    const auto sequence=failed.state.sequence;
    failed.input(60001);CHECK(failed.state.sequence==sequence && !failed.saves.writable());
    CHECK(failed.runtime.idle_.blanked() && !fake::wakes && !failed.runtime.audio_.cues);
}
void nfcQuietGapAndAutoCareIsolation(){
    Harness n;n.runtime.ui_.selected=deviceui::Screen::NfcTest;n.runtime.nfc_.active=true;
    CHECK(!n.runtime.idleForegroundActive());n.idle(30000);CHECK(!n.runtime.idle_.blanked());
    // A completed scan's normal quiet gap permits screen idle; scanning is not activity.
    n.runtime.nfc_.active=false;n.idle(30001);CHECK(n.runtime.idle_.blanked()&&!n.runtime.nfcAllowed());
    CHECK(!n.game.writes&&!n.usage.writes);
    Harness a;CHECK(apply(a.state,Action::Mode,1)==Error::None);
    CHECK(apply(a.state,Action::Explore,1000)==Error::None);CHECK(a.state.phase==Phase::Encounter);
    a.runtime.testModel.writable=true;a.runtime.careAwakeMs_=1;
    const auto before=a.state;a.runtime.pollCareAndAuto(60001);
    CHECK(a.runtime.intents==1&&a.runtime.autoStartSequence_==a.state.sequence);
    CHECK(a.runtime.careAwakeMs_==60001&&a.game.writes==0);
    CHECK(!std::memcmp(&before,&a.state,sizeof(before)));
    a.runtime.pollCareAndAuto(120001);CHECK(a.runtime.intents==1&&a.game.writes==0);
}
void screenPolicy(){
    using S=deviceui::Screen;
    const S passive[]{S::Egg,S::Starter,S::StarterReview,S::Home,S::Care,S::Explore,S::Result,
        S::Collection,S::Stats,S::ReleaseReview,S::Evolution,S::EvolutionReview,S::EvolutionResult,
        S::Settings,S::NfcTest,S::EncounterSettings,S::ModeReview,S::Nearby,S::Sound,S::Squad,S::Box,
        S::Expeditions,S::ExpeditionLobby};
    const S active[]{S::Encounter,S::Battle,S::Capture,S::NearbyReview,S::TradeChoose,S::TradeReview};
    CHECK(std::size(passive)+std::size(active)==static_cast<unsigned>(S::NfcTest)+1);
    for(const auto s:passive){Harness h;h.runtime.ui_.selected=s;CHECK(!h.runtime.idleBlocked());h.idle(30000);CHECK(h.runtime.idle_.blanked());}
    for(const auto s:active){
        Harness h;h.runtime.ui_.selected=s;h.idle(30000);CHECK(!h.runtime.idle_.blanked());
        h.runtime.ui_.selected=S::Home;h.idle(59999);CHECK(!h.runtime.idle_.blanked());
        h.idle(60000);CHECK(h.runtime.idle_.blanked());
        h.runtime.ui_.selected=s;h.idle(60020);CHECK(!h.runtime.idle_.blanked() && fake::wakes==1);
    }
}
void temporaryWork(){
    using Set=std::function<void(Harness&,bool)>;
    const Set cases[]{
        [](auto& h,bool v){h.runtime.audio_.quiet=!v;},
        [](auto& h,bool v){h.runtime.network_.s.probePending=v;},
        [](auto& h,bool v){h.runtime.network_.scan.busy=v;},
        [](auto& h,bool v){h.runtime.network_.s.state=v?net::State::Joining:net::State::Unconfigured;},
        [](auto& h,bool v){h.runtime.network_.s.state=v?net::State::Backoff:net::State::Unconfigured;},
        [](auto& h,bool v){h.runtime.assets_.s.busy=v;},
        [](auto& h,bool v){h.runtime.art_.quiet=!v;},
        [](auto& h,bool v){h.runtime.partnerArt_.quiet=!v;},
        [](auto& h,bool v){h.runtime.tileArt_[0].quiet=!v;},
        [](auto&,bool v){fake::dmaQuiet=!v;},
        [](auto& h,bool v){h.runtime.imu_.available=!v;}
    };
    for(const auto& set:cases){
        Harness h;set(h,true);h.idle(29980);h.idle(30000);CHECK(!h.runtime.idle_.blanked());
        set(h,false);h.idle(30020);CHECK(h.runtime.idle_.blanked());
        for(unsigned n=1;n<=10;++n){set(h,true);h.idle(30020+n*60000);set(h,false);h.idle(30040+n*60000);}
        CHECK(fake::blanks==1 && fake::wakes==0);
    }
}
void discoveryPolicy(){
    using P=HandheldRuntime::NearbyPhase;
    for(const auto screen:{deviceui::Screen::Nearby,deviceui::Screen::ExpeditionLobby}){
        Harness h;h.runtime.ui_.selected=screen;h.runtime.nearbyPhase_=P::Active;
        h.runtime.nearby_.v.stage=nearby::Stage::Discovering;
        h.runtime.tradeWire_.v.stage=tradewire::Stage::Discovering;h.runtime.network_.leased=true;
        CHECK(h.runtime.idleDiscovery());h.idle(30000);CHECK(h.runtime.idle_.blanked());
        const auto sequence=h.state.sequence;
        h.runtime.nearby_.v.stage=nearby::Stage::Incoming;h.idle(30020);
        CHECK(!h.runtime.idle_.blanked() && fake::wakes==1 && h.state.sequence==sequence);
        CHECK(h.runtime.nearby_.v.stage==nearby::Stage::Incoming && h.runtime.network_.leased);
    }
    for(const auto stage:{nearby::Stage::Outgoing,nearby::Stage::Incoming,nearby::Stage::Accepting,
        nearby::Stage::Playing,nearby::Stage::Reconnecting,nearby::Stage::Finished,
        nearby::Stage::Cancelled,nearby::Stage::TimedOut,nearby::Stage::Incompatible}){
        Harness h;h.runtime.ui_.selected=deviceui::Screen::ExpeditionLobby;h.runtime.nearbyPhase_=P::Active;
        h.runtime.nearby_.v.stage=stage;CHECK(h.runtime.idleBlocked());h.idle(30000);CHECK(!fake::blanks);
    }
    for(const auto phase:{P::Starting,P::Stopping,P::Fault}){
        Harness h;h.runtime.nearbyPhase_=phase;h.runtime.nearby_.v.stage=nearby::Stage::Discovering;
        CHECK(h.runtime.idleBlocked());h.idle(30000);CHECK(!fake::blanks);
    }
    for(const auto stage:{tradewire::Stage::Inviting,tradewire::Stage::Reviewing,tradewire::Stage::Preparing,
        tradewire::Stage::Prepared,tradewire::Stage::Committing,tradewire::Stage::Applying,
        tradewire::Stage::Applied,tradewire::Stage::Aborted,tradewire::Stage::Cancelled,tradewire::Stage::Recovering}){
        Harness h;h.runtime.ui_.selected=deviceui::Screen::Nearby;h.runtime.nearbyPhase_=P::Active;
        h.runtime.nearby_.v.stage=nearby::Stage::Discovering;h.runtime.tradeWire_.v.stage=stage;
        CHECK(!h.runtime.idleDiscovery());h.idle(30000);CHECK(!fake::blanks);
    }
    Harness h;h.runtime.negotiating=true;CHECK(h.runtime.idleBlocked());
}
void powerSamplingAndWake(){
    for(const bool enabled:{true,false}){
        Harness h;h.runtime.powerEnabled_=enabled;
        unsigned rearms=0;
        for(std::uint64_t now=20;now<=180000;now+=20){
            if(now%5000==0)now+=300;
            h.runtime.power_.tick({now,false});
            if(h.runtime.power_.status().phase==power::Phase::AwaitInitialRelease)++rearms;
            h.input(now);
        }
        CHECK(rearms>30 && fake::blanks==1 && fake::wakes==0 && h.runtime.idle_.blanked());
        const auto next=fake::now+20;
        h.runtime.power_.tick({next,true});h.idle(next);
        CHECK(h.runtime.idle_.blanked()==!enabled);CHECK(fake::wakes==(enabled?1u:0u));
    }
    // Contact while still release-to-arm wakes, without authorizing a long hold.
    Harness h;h.idle(30000);h.runtime.power_.tick({30020,true});h.idle(30020);
    CHECK(!h.runtime.idle_.blanked() && h.runtime.power_.status().phase==power::Phase::AwaitInitialRelease);
    CHECK(!h.runtime.power_.frozen() && !h.game.writes);
}
int main(){nfcQuietGapAndAutoCareIsolation();nfcPolicy();quietAndWrites();resultIdle();pendingBeforeBlank();flushStartsEncounter();homeWalkingWhileBlanked();menuIdleQueuesEncounter();storageFaults();blockers();wakeContact();wakeFaultAndMotion();strongerSuspension();musicIdleAndWake();musicScenePolicy();backgroundCareTimeouts();screenPolicy();temporaryWork();discoveryPolicy();powerSamplingAndWake();std::printf("Idle runtime: %u checks passed; actual idle/usage/care methods + power controller + exact touch dispatch/pause body, hardware/UI publication doubles\n",checks);}
