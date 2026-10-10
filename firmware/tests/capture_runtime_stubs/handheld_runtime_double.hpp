#pragma once
#define CONFIG_DIGIVICE_DISPLAY_TOUCH 1
#include "device_ui.hpp"
#include "touch_stream.hpp"
#include "display_touch.hpp"
#include "audio_cues.hpp"
#include "imu_filter.hpp"
#include <cstdint>
namespace capturefake {
extern std::uint64_t nowUs;
extern std::uint32_t fullRenderUs,partialRenderUs;
extern unsigned fullRenders,partialRenders,parityChecks;
extern bool forcePartialFailure;
extern digivice::deviceui::Artwork artwork;
void checkPartial(const digivice::deviceui::Controller&,const digivice::State&,const digivice::deviceui::Model&,std::uint16_t*,std::size_t,std::uint64_t);
}
namespace digivice {
struct CaptureUi:deviceui::Controller {
    bool render(const State& s,const deviceui::Model& m,std::uint16_t* p,std::size_t n,std::uint64_t now)const {
        ++capturefake::fullRenders;const auto result=deviceui::Controller::render(s,m,p,n,now);capturefake::nowUs+=capturefake::fullRenderUs;return result;
    }
    bool renderCaptureRegion(const State& s,const deviceui::Model& m,std::uint16_t* p,std::size_t n,std::uint64_t now)const {
        if(capturefake::forcePartialFailure)return false;
        ++capturefake::partialRenders;capturefake::checkPartial(*this,s,m,p,n,now);capturefake::nowUs+=capturefake::partialRenderUs;return true;
    }
};
struct CaptureSetup {bool open=false;void poll(){}bool active()const{return open;}void cancelTouch(){}void touch(deviceui::Touch){}bool render(std::uint16_t*,std::size_t){return true;}};
struct CaptureImu {motion::ImuReading sample{};const auto& poll(std::uint64_t){return sample;}const auto& reading()const{return sample;}};
struct CaptureIdle {bool blank=false;bool blanked()const{return blank;}void observeMotion(const motion::ImuReading&,std::uint64_t){}};
struct CaptureAudio {unsigned cues=0;void play(device::AudioCue){++cues;}};
struct CaptureSd {bool online=true;bool mounted()const{return online;}};
struct CaptureArt {unsigned calls=0;deviceui::Artwork prepare(const deviceui::ArtRequest&,bool ready){++calls;return ready?capturefake::artwork:deviceui::Artwork{};}};
struct CaptureTrade {bool blocked=false;bool blocksForeground()const{return blocked;}};
inline device::AudioCue cueFor(Message){return device::AudioCue::Navigate;}
class HandheldRuntime {
public:
    // Production starts latched; these I/O tests start from a seen release.
    explicit HandheldRuntime(State& state):state_(state){touch_.feed({true,false,true,0,0,0});}
    void pollInterface(std::uint64_t now);
    // Exact boot save-to-UI block extracted from beginInterface, excluding I/O.
    void restoreCaptureForTest(std::uint64_t now);
    bool powerFrozen()const{return frozen;}
    deviceui::Model interfaceModel()const{auto view=model;if(useOwnedPlayback)view.battle=battle_.locked()?&battle_.view():nullptr;return view;}
    void pollBattlePresentation(std::uint64_t){}
    void interfaceActivity(std::uint64_t){}
    void pollIdle(std::uint64_t){}
    void pollCareAndAuto(std::uint64_t){}
    void updateMusicScene(){}
    void interfaceIntent(deviceui::Intent intent){if(intent){++intents;interfaceDirty_=true;}}
    // Extracted byte-for-byte from production handheld_ui.cpp by the runner.
    void handleTouchSample(const touchstream::Sample& sample, const deviceui::Model& model);
    static void sampleTouchDuringFlush(void* runtime);
    void requireTouchRelease();
    bool touchPressed()const{return touch_.pressed();}
    touchstream::Stream touch_{};
    static constexpr std::size_t kFlushTouchQueue=8;
    touchstream::Sample flushTouch_[kFlushTouchQueue]{};
    std::size_t flushTouchCount_=0;
    CaptureUi ui_;CaptureSetup setup_;CaptureImu imu_;CaptureIdle idle_;CaptureAudio audio_;CaptureSd sd_;CaptureArt art_,partnerArt_;
    battlepresentation::Sequencer battle_;CaptureTrade tradeSession_;
    bool encounterRecoveryRequired_=false,useOwnedPlayback=false;
    State& state_;deviceui::Model model{};
    bool frozen=false,interfacePaused_=false,assetStorageReady_=true,assetStorageFailed_=false;
    bool interfaceDirty_=true;
    std::uint16_t* frame_=nullptr;
    std::uint64_t lastTouchMs_=0,lastFrameMs_=0,captureFrameStartedMs_=0;
    std::uint32_t uiSequence_=UINT32_MAX;
    std::uint32_t renderedFrames_=0,maxFrameUs_=0,maxRenderUs_=0,maxFlushUs_=0;
    std::uint32_t captureFrames_=0,maxCaptureRenderUs_=0,maxCaptureFlushUs_=0,lastFrameSequence_=UINT32_MAX;
    bool captureFrameActive_=false,captureFrameValid_=false,lastArtStorageReady_=false;
    unsigned intents=0;
    // The implementation below is extracted byte-for-byte from the production
    // header by the runner, including its actual runtime cadence selection.
    std::uint32_t recommendedPollDelayMs()const;
};
}
