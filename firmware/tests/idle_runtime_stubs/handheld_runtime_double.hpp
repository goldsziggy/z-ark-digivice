#pragma once
// Actual owner methods are compiled against this declaration. Hardware/UI
// publication is doubled; game, SaveStore, usage Store and idle policy are real.
#define CONFIG_DIGIVICE_DISPLAY_TOUCH 1
#include "game.hpp"
#include "entropy_seed.hpp"
#include "save_store.hpp"
#include "usage_store.hpp"
#include "idle.hpp"
#include "device_ui.hpp"
#include "touch_stream.hpp"
#include "audio_cues.hpp"
#include "music_synth.hpp"
#include "display_touch.hpp"
#include "pedometer.hpp"
#include "network.hpp"
#include "power.hpp"
#include <cstdint>

namespace digivice {
struct IdleUiDouble {
    deviceui::Screen selected=deviceui::Screen::Home;bool pending_=false,walking=true;
    unsigned cancellations=0,downs=0,ups=0;
    deviceui::Screen screen()const{return selected;}bool pending()const{return pending_;}
    bool walkingEligible()const{return walking && selected==deviceui::Screen::Home;}
    bool encounterPresentationEligible()const{return walkingEligible();}
    bool acknowledgeWalking(const State&,const State&,Action,std::uint32_t){return true;}
    bool interactionIdle()const{return !pending_;}void cancelTouch(){++cancellations;}
    void acknowledgeContactReleased(){}
    void update(const State&,const deviceui::Model&){}
    deviceui::Intent touch(const State&,const deviceui::Model&,deviceui::Touch t){
        if(t.kind==deviceui::TouchKind::Down)++downs;
        if(t.kind==deviceui::TouchKind::Up){++ups;return {deviceui::IntentKind::GameAction,Action::Feed,0};}
        return {};
    }
};
struct IdleSetupDouble {bool active_=false;unsigned cancellations=0,events=0;bool active()const{return active_;}void poll(){}void cancelTouch(){++cancellations;}void suspend(){active_=false;}void touch(deviceui::Touch){++events;}};
struct IdleImuDouble {
    bool available=true,paused=false;unsigned pauses=0,polls=0;
    motion::StepReading sample{};motion::ImuReading reading_{};
    bool ready()const{return available&&!paused;}bool quiescent()const{return paused;}
    motion::StepReading stepReading()const{return sample;}
    const motion::ImuReading& poll(std::uint64_t){++polls;return reading_;}
    const motion::ImuReading& reading()const{return reading_;}
    esp_err_t pause(bool value){paused=value;++pauses;return ESP_OK;}
};
struct IdleAudioDouble {bool quiet=true,paused=false,musicActive=false;unsigned cues=0;device::MusicScene scene=device::MusicScene::Home;bool quiescent()const{return quiet&&!musicActive;}bool effectsQuiescent()const{return quiet;}void setMusicScene(device::MusicScene value){scene=value;}void play(device::AudioCue){++cues;}void pause(bool v){paused=v;}};
struct IdleArtDouble {bool quiet=true,paused=false;bool quiescent()const{return quiet;}void pause(bool v){paused=v;}};
struct IdleAssetsDouble {struct Status{bool busy=false;}s;const Status& status()const{return s;}};
struct IdleNetworkDouble {net::Status s;struct Scan{bool busy=false;}scan;bool leased=false;const net::Status& status()const{return s;}bool radioLeased()const{return leased;}const Scan& scanStatus()const{return scan;}};
struct IdleUsbDouble {bool active_=false,quiet=true;bool active()const{return active_;}bool quiescent()const{return quiet;}};
struct IdlePowerDouble {power::Status s{power::Phase::Ready,power::Failure::None,0};const power::Status& status()const{return s;}};
struct IdleBattleDouble {bool locked_=false;bool locked()const{return locked_;}void cancel(){locked_=false;}};
struct IdlePracticeDouble {bool allowed=true;bool allowsCareAction(Action)const{return allowed;}};
struct IdleSettingsDouble {bool writable_=true;bool writable()const{return writable_;}};
// Trade persistence is covered independently; this idle harness exposes only
// the foreground/background gates consumed by the exact pollUsage method.
struct IdleTradeSessionDouble {bool healthy_=true,blocked=false,backgroundAllowed=true;bool healthy()const{return healthy_;}bool blocksForeground()const{return blocked;}bool permitsBackground()const{return backgroundAllowed;}};
inline device::AudioCue cueFor(Message){return device::AudioCue::Navigate;}

class HandheldRuntime {
public:
    HandheldRuntime(State& s,storage::SaveStore& saves,usage::Backend& backend):usage_(backend),state_(s),saves_(saves){}
    bool idleBlocked()const;void interfaceActivity(std::uint64_t);void pollIdle(std::uint64_t);
    void pollInterface(std::uint64_t);void pauseInterface(bool);
    void updateMusicScene();
    bool pollUsage(std::uint64_t,bool force=false);
    bool powerFrozen()const{return frozen;}bool nearbyBusy()const{return nearby;}
    bool interfaceQuiescent()const{return imu_.quiescent() && display::quiescent();}
    deviceui::Model interfaceModel()const{return {};}
    void pollBattlePresentation(std::uint64_t){}
    void interfaceIntent(deviceui::Intent i){if(i)++intents;}
    void pollCareAndAuto(std::uint64_t){}
    // Extracted byte-for-byte from production handheld_ui.cpp by the runner.
    void handleTouchSample(const touchstream::Sample&,const deviceui::Model&);
    static void sampleTouchDuringFlush(void*);
    void requireTouchRelease();
    bool touchPressed()const{return touch_.pressed();}
    void forcePressedForTest(){touch_.feed({true,false,true,0,0,0});touch_.feed({true,true,true,200,200,1});}
    touchstream::Stream touch_{};
    static constexpr std::size_t kFlushTouchQueue=8;
    touchstream::Sample flushTouch_[kFlushTouchQueue]{};
    std::size_t flushTouchCount_=0;
    IdleUiDouble ui_;IdleSetupDouble setup_;IdleImuDouble imu_;IdleAudioDouble audio_;
    IdleArtDouble art_,partnerArt_,tileArt_[deviceui::kTiles];IdleAssetsDouble assets_;IdleNetworkDouble network_;
    IdleUsbDouble usbTransfer_;IdlePowerDouble power_;IdleBattleDouble battle_;
    IdlePracticeDouble practice_;IdleSettingsDouble idleSettings_;
    IdleTradeSessionDouble tradeSession_;
    idle::Controller idle_;usage::Store usage_;
    State& state_;storage::SaveStore& saves_;
    entropy::Seeds startupSeeds_{7,11,0x12345678u,13};
    bool interfacePaused_=false,frozen=false,nearby=false,powerEnabled_=true,usbTransferLease_=false;
    bool encounterRecoveryRequired_=false;
    bool captureFrameActive_=false,captureFrameValid_=false;
    bool walkingFault_=false,interfaceDirty_=false;
    std::uint32_t walkingPending_=0,uiSequence_=0;
    std::uint64_t lastWalkingSaveMs_=0,lastTouchMs_=0,careAwakeMs_=0;
    std::uint16_t pixel=0;std::uint16_t* frame_=&pixel;
    unsigned intents=0;
};
} // namespace digivice
