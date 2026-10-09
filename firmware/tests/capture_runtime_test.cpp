#include "handheld_runtime_double.hpp"
#include "esp_timer.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace {
unsigned checks=0;
void check(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
#define CHECK(x) check((x),#x)
using namespace digivice;
std::array<std::uint16_t,deviceui::kPixels> frame{},background{},expected{},before{},panel{};
std::array<std::uint16_t,1024> spritePixels{};
std::array<std::uint8_t,128> mask{};
struct Flush {std::uint64_t start;int x,y,w,h;};
std::vector<Flush> flushes;
std::vector<std::uint64_t> touches;
bool lcd=true,touch=true,flushFailure=false;
display::TouchPoint point{};
esp_err_t touchError=ESP_OK;
}
namespace capturefake {
std::uint64_t nowUs=1000000;
std::uint32_t fullRenderUs=30619,partialRenderUs=7804;
unsigned fullRenders=0,partialRenders=0,parityChecks=0;
bool forcePartialFailure=false;
digivice::deviceui::Artwork artwork{};
void checkPartial(const deviceui::Controller& ui,const State& state,const deviceui::Model& model,std::uint16_t* pixels,std::size_t capacity,std::uint64_t now){
    std::copy(pixels,pixels+deviceui::kPixels,before.begin());
    CHECK(ui.renderCaptureRegion(state,model,pixels,capacity,now));
    CHECK(ui.render(state,model,expected.data(),expected.size(),now));
    CHECK(!std::memcmp(pixels,expected.data(),deviceui::kPixels*sizeof(std::uint16_t)));
    for(int y=0;y<deviceui::kSize;++y){
        const bool outside=y<deviceui::Controller::kCaptureY||y>=deviceui::Controller::kCaptureY+deviceui::Controller::kCaptureHeight;
        if(outside)CHECK(!std::memcmp(before.data()+y*deviceui::kSize,pixels+y*deviceui::kSize,deviceui::kSize*2));
        else {CHECK(!std::memcmp(before.data()+y*deviceui::kSize,pixels+y*deviceui::kSize,deviceui::Controller::kCaptureX*2));
            constexpr int end=deviceui::Controller::kCaptureX+deviceui::Controller::kCaptureWidth;
            CHECK(!std::memcmp(before.data()+y*deviceui::kSize+end,pixels+y*deviceui::kSize+end,(deviceui::kSize-end)*2));}
    }
    ++parityChecks;
}
}
std::int64_t esp_timer_get_time(){return static_cast<std::int64_t>(capturefake::nowUs);}
namespace digivice::display {
bool displayReady(){return lcd;}bool touchReady(){return touch;}
esp_err_t pollTouch(TouchPoint& out){touches.push_back(capturefake::nowUs);out=point;return touchError;}
esp_err_t flushRgb565(int x,int y,int w,int h,const std::uint16_t* source,std::size_t stride){
    CHECK(stride==deviceui::kSize);CHECK(source==frame.data()+y*deviceui::kSize+x);
    const Rect logical{x,y,w,h};CHECK(validRect(logical));flushes.push_back({capturefake::nowUs,x,y,w,h});
    const auto native=logicalRectToPanel(Orientation::Ccw90,logical);
    std::array<std::uint8_t,kDmaStripeBytes+16> bytes;
    const auto pixels=static_cast<std::size_t>(h-1)*stride+w;
    for(int row=0;row<native.height;row+=kStripeRows){
        const auto rows=std::min(kStripeRows,native.height-row);bytes.fill(0xa5);
        CHECK(packStripe(Orientation::Ccw90,logical,source,pixels,stride,row,rows,bytes.data()+8,kDmaStripeBytes));
        const auto count=static_cast<std::size_t>(native.width)*rows;
        for(unsigned i=0;i<8;++i)CHECK(bytes[i]==0xa5&&bytes[8+count*2+i]==0xa5);
        for(int dy=0;dy<rows;++dy)for(int dx=0;dx<native.width;++dx){
            const auto pos=static_cast<std::size_t>(dy)*native.width+dx;
            const auto value=static_cast<std::uint16_t>((bytes[8+pos*2]<<8)|bytes[9+pos*2]);
            const auto mapped=panelToLogical(Orientation::Ccw90,{native.x+dx,native.y+row+dy});
            CHECK(value==frame[mapped.y*deviceui::kSize+mapped.x]);panel[(native.y+row+dy)*kWidth+native.x+dx]=value;
        }
    }
    // Conservative linear byte-cost MODEL from an earlier full-frame45,757us
    // measurement. Not an ESP partial-frame timing measurement.
    capturefake::nowUs+=45757ull*static_cast<unsigned>(w*h)/deviceui::kPixels;
    return flushFailure?ESP_FAIL:ESP_OK;
}
}
namespace {
State waiting(){
    for(unsigned seed=1;seed<1000;++seed){auto s=newDevice(seed);CHECK(apply(s,Action::Hatch,2)==Error::None);CHECK(apply(s,Action::Mode,1)==Error::None);CHECK(apply(s,Action::Explore,1000)==Error::None);CHECK(applyAutoFight(s)==Error::None);if(s.autoCapture==AutoCapture::Awaiting)return s;}
    throw std::runtime_error("capture fixture unavailable");
}
void reset(){capturefake::nowUs=1000000;capturefake::fullRenders=capturefake::partialRenders=capturefake::parityChecks=0;capturefake::forcePartialFailure=false;flushes.clear();touches.clear();lcd=touch=true;flushFailure=false;point={false,0,0,true};touchError=ESP_OK;}
void setup(HandheldRuntime& runtime){
    runtime.frame_=frame.data();runtime.model.writable=runtime.model.inputEnabled=true;runtime.uiSequence_=runtime.state_.sequence;
    runtime.ui_.update(runtime.state_,runtime.model);CHECK(runtime.ui_.screen()==deviceui::Screen::Capture);
    const auto request=runtime.ui_.artRequest(runtime.state_,runtime.model,capturefake::nowUs/1000);
    capturefake::artwork={};capturefake::artwork.background=background.data();capturefake::artwork.backgroundPixels=background.size();capturefake::artwork.backgroundId=request.sceneId;
    auto& art=capturefake::artwork.sprite;art.formId=request.formId;art.animation=request.animation;art.pixels=spritePixels.data();art.pixelCount=spritePixels.size();art.mask=mask.data();art.maskBytes=mask.size();art.width=art.height=32;
}
void loop(HandheldRuntime& runtime,unsigned milliseconds){
    const auto end=capturefake::nowUs+milliseconds*1000ull;
    while(capturefake::nowUs<end){spritePixels.fill((capturefake::nowUs/200000)%2?0xf81f:0x07e0);runtime.pollInterface(capturefake::nowUs/1000);
        // ESP config uses100Hz. A5ms request would truncate tozero; actual
        // production waits clamp at leastone tick and currently request10ms.
        const auto ticks=std::max(1u,runtime.recommendedPollDelayMs()*100u/1000u);
        CHECK(ticks>=1);capturefake::nowUs+=ticks*10000ull;
    }
}
void restoredCapturePolicy(){
    const auto startChecks=checks;
    auto pending=waiting();
    auto escaped=pending;CHECK(apply(escaped,Action::Flick,0)==Error::None);
    CHECK(escaped.phase==Phase::Encounter&&escaped.lastCapture.result==CaptureResult::Miss);
    auto ended=escaped;CHECK(apply(ended,Action::Flick,0)==Error::None);CHECK(apply(ended,Action::Flick,0)==Error::None);
    CHECK(ended.phase==Phase::Home&&ended.lastCapture.attempt==3);
    State beforeCapture{},captured{};bool found=false;
    for(unsigned seed=1;seed<=100&&!found;++seed){
        beforeCapture=pending;beforeCapture.rngState=seed;captured=beforeCapture;
        CHECK(apply(captured,Action::Flick,41140)==Error::None);
        found=captured.lastCapture.result==CaptureResult::Captured;
    }
    CHECK(found&&captured.phase==Phase::Home);
    for(auto saved:{captured,ended}){
        // The actual production startup block chooses Home on every restart;
        // it must not acknowledge or erase the saved capture receipt to do so.
        Snapshot original;CHECK(encodeSnapshot(saved,original));
        for(unsigned boot=0;boot<2;++boot){
            State restored;CHECK(decodeSnapshot(original.bytes,kSnapshotSize,restored)==SnapshotStatus::Ok);
            HandheldRuntime runtime(restored);runtime.model.writable=true;runtime.useOwnedPlayback=true;
            runtime.restoreCaptureForTest(1000);
            CHECK(!runtime.battle_.locked()&&runtime.ui_.screen()==deviceui::Screen::Home);
            CHECK(runtime.uiSequence_==restored.sequence&&!runtime.intents);
            Snapshot after;CHECK(encodeSnapshot(restored,after));
            CHECK(!std::memcmp(original.bytes,after.bytes,kSnapshotSize));
        }
    }
    // An unfinished capture still replays the committed miss, then restores
    // its manual capture choice. Recovery/trade locks suppress that replay.
    for(unsigned gate=0;gate<3;++gate){
        auto restored=escaped;Snapshot original;CHECK(encodeSnapshot(restored,original));
        HandheldRuntime runtime(restored);runtime.model.writable=true;runtime.useOwnedPlayback=true;
        runtime.encounterRecoveryRequired_=gate==1;runtime.tradeSession_.blocked=gate==2;
        runtime.restoreCaptureForTest(1000);CHECK(runtime.battle_.locked()==(gate==0));
        if(!gate){
            CHECK(runtime.ui_.screen()==deviceui::Screen::Battle);
            for(unsigned now=1000;now<=6000;now+=20){runtime.battle_.poll(now);runtime.ui_.update(restored,runtime.interfaceModel());}
            CHECK(!runtime.battle_.locked()&&runtime.ui_.screen()==deviceui::Screen::Capture);
        }
        Snapshot after;CHECK(encodeSnapshot(restored,after));CHECK(!std::memcmp(original.bytes,after.bytes,kSnapshotSize));
    }
    // Newly committed successful captures still play normally and retain a
    // working HOME button after playback; only boot restoration changed.
    HandheldRuntime fresh(captured);fresh.model.writable=true;fresh.useOwnedPlayback=true;
    CHECK(fresh.battle_.startTactical(beforeCapture,captured,Action::Flick,41140,1000));
    for(unsigned now=1000;now<=6000;now+=20){fresh.battle_.poll(now);fresh.ui_.update(captured,fresh.interfaceModel());}
    CHECK(!fresh.battle_.locked()&&fresh.ui_.screen()==deviceui::Screen::Result);
    Snapshot original;CHECK(encodeSnapshot(captured,original));
    CHECK(!fresh.ui_.touch(captured,fresh.interfaceModel(),{deviceui::TouchKind::Down,206,300,7000}));
    const auto home=fresh.ui_.touch(captured,fresh.interfaceModel(),{deviceui::TouchKind::Up,206,300,7100});
    CHECK(home.kind==deviceui::IntentKind::Navigation&&fresh.ui_.screen()==deviceui::Screen::Home);
    Snapshot after;CHECK(encodeSnapshot(captured,after));CHECK(!std::memcmp(original.bytes,after.bytes,kSnapshotSize));
    std::printf("Restored capture policy: %u checks; actual boot selection, capture sequencer/UI and snapshot identity.\n",checks-startChecks);
}
void releaseAndPlaybackBarrier(){
    reset();auto state=waiting();HandheldRuntime runtime(state);setup(runtime);
    runtime.pollInterface(capturefake::nowUs/1000);
    point={true,206,176,true};loop(runtime,20);CHECK(runtime.intents==1);
    runtime.ui_.resolve("SIMULATED SAVE FAILURE");runtime.interfaceDirty_=true;
    touchError=ESP_FAIL;loop(runtime,20);CHECK(runtime.touchNeedsRelease_&&runtime.intents==1);
    touchError=ESP_OK;loop(runtime,20);CHECK(runtime.touchNeedsRelease_&&runtime.intents==1);
    point={false,206,176,false};loop(runtime,20);CHECK(runtime.touchNeedsRelease_);
    point={false,206,176,true};loop(runtime,20);CHECK(!runtime.touchNeedsRelease_&&runtime.intents==1);
    // Wait past duplicate-contact cooldown, then the first new Down must work.
    loop(runtime,350);point={true,206,176,true};loop(runtime,20);CHECK(runtime.intents==2);

    reset();auto next=waiting();HandheldRuntime held(next);setup(held);
    battlepresentation::View playback{};playback.locked=true;playback.enemyFormId=next.wildFormId;
    held.model.battle=&playback;point={true,206,176,true};loop(held,40);CHECK(held.intents==0&&held.touchPressed_);
    held.model.battle=nullptr;loop(held,100);CHECK(held.intents==0); // HeldlevelneverbecomesnewDown.
    point={false,206,176,true};loop(held,30);CHECK(held.intents==0);
    point={true,206,176,true};loop(held,30);CHECK(held.intents==1);
}
void flushFailureReleaseBarrier(){
    reset();auto state=waiting();HandheldRuntime runtime(state);setup(runtime);
    runtime.pollInterface(capturefake::nowUs/1000);
    // A held header contact is tracked by the hardware owner, but is not a
    // throw. Fail the display transfer while that contact remains pressed.
    point={true,206,40,true};runtime.interfaceDirty_=true;flushFailure=true;
    loop(runtime,30);CHECK(runtime.touchPressed_&&runtime.touchNeedsRelease_&&runtime.intents==0);
    flushFailure=false;loop(runtime,20);CHECK(runtime.touchPressed_&&runtime.touchNeedsRelease_&&runtime.intents==0);
    // A cached/no-update release cannot discharge the barrier.
    point={false,206,40,false};loop(runtime,20);CHECK(runtime.touchPressed_&&runtime.touchNeedsRelease_);
    // The observed release must clear BOTH hardware and UI contact latches.
    point={false,206,40,true};capturefake::nowUs+=30000;runtime.pollInterface(capturefake::nowUs/1000);
    CHECK(!runtime.touchPressed_&&!runtime.touchNeedsRelease_&&runtime.intents==0);
    const auto presses=runtime.touchPresses_;
    point={true,206,176,true};loop(runtime,20);
    CHECK(runtime.touchPresses_==presses+1&&runtime.intents==1);
    CHECK(state.captureAttempts==0); // This I/O harness records intent only.
}
void cadence(){
    reset();auto state=waiting();HandheldRuntime runtime(state);setup(runtime);
    CHECK(std::max(1u,5u*100u/1000u)==1);CHECK(runtime.recommendedPollDelayMs()==20);
    runtime.pollInterface(capturefake::nowUs/1000);CHECK(flushes.size()==1&&flushes[0].w==412&&runtime.recommendedPollDelayMs()==10);
    const auto started=capturefake::nowUs;loop(runtime,2400);
    CHECK(capturefake::fullRenders==1&&capturefake::partialRenders>=55&&capturefake::parityChecks==capturefake::partialRenders);
    CHECK(runtime.captureFrames_==capturefake::partialRenders);
    for(std::size_t i=1;i<flushes.size();++i)CHECK(flushes[i].w==208&&flushes[i].h==208&&flushes[i].x==102&&flushes[i].y==76);
    const auto beforeFull=capturefake::fullRenders;point={true,206,40,true};loop(runtime,1000); // Heldoutsideplayarea.
    CHECK(capturefake::fullRenders==beforeFull&&capturefake::partialRenders>75&&runtime.intents==0);
    point={false,206,40,true};loop(runtime,20);
    std::uint64_t maxGap=0;for(std::size_t i=2;i<touches.size();++i)maxGap=std::max(maxGap,touches[i]-touches[i-1]);
    std::printf("MODEL tick100Hz partialRenderUs=%u partialFlushUs=%u frames=%u modelElapsedMs=%llu maxSteadyTouchGapUs=%llu bytesPerPartial=86528 fullBytes=339488\n",capturefake::partialRenderUs,runtime.maxCaptureFlushUs_,runtime.captureFrames_,static_cast<unsigned long long>((capturefake::nowUs-started)/1000),static_cast<unsigned long long>(maxGap));
    CHECK(runtime.maxCaptureFlushUs_<12000&&runtime.maxCaptureRenderUs_==7804);
    auto full=capturefake::fullRenders;state.sequence++;state.foregroundSequence=state.sequence;loop(runtime,40);CHECK(capturefake::fullRenders==full+1);
    full=capturefake::fullRenders;runtime.sd_.online=false;loop(runtime,40);CHECK(capturefake::fullRenders==full+1);
    full=capturefake::fullRenders;runtime.sd_.online=true;loop(runtime,40);CHECK(capturefake::fullRenders==full+1);
    full=capturefake::fullRenders;capturefake::forcePartialFailure=true;loop(runtime,40);CHECK(capturefake::fullRenders>full);capturefake::forcePartialFailure=false;
    const auto count=flushes.size();runtime.idle_.blank=true;loop(runtime,80);CHECK(flushes.size()==count&&!runtime.captureFrameValid_&&runtime.recommendedPollDelayMs()==20);
    runtime.idle_.blank=false;loop(runtime,40);CHECK(flushes.back().w==412); // Wake invalidatespartialbase.
    runtime.interfacePaused_=true;CHECK(runtime.recommendedPollDelayMs()==20);const auto paused=flushes.size();loop(runtime,100);CHECK(flushes.size()==paused);
}
}
int main(){try{for(unsigned i=0;i<background.size();++i)background[i]=static_cast<std::uint16_t>(0x1000+(i*17)%0x5fff);spritePixels.fill(0xf81f);mask.fill(255);restoredCapturePolicy();cadence();releaseAndPlaybackBarrier();flushFailureReleaseBarrier();std::printf("PASS %u capture runtime/CCW90/stride/parity/scheduling checks; SDKclock/touch/storage/transfertime doubled, nohardwareFPSclaim.\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL capture runtime: %s\n",e.what());return 1;}}
