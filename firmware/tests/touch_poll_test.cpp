// Compiles the exact production pollTouch function body (and installed1d5552e
// baseline) into controlled bus/clock doubles. No copied implementation under
// test, physical I2C simulation or claimed walking/touch acceptance.
#include "display_touch.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <initializer_list>
namespace {
unsigned checks=0;
void check(bool value,const char* text,int line){++checks;if(!value){std::fprintf(stderr,"%d: %s\n",line,text);std::exit(1);}}
#define CHECK(x) check((x),#x,__LINE__)
std::uint32_t nowMs=0,releaseAtMs=0,lastLockMs=0,lastLockTicks=0,readAtMs=0;
unsigned reads=0,unlocks=0;
bool locked=false,ready=true,updateReport=true;
esp_err_t forcedLock=ESP_OK,readResult=ESP_OK;
digivice::display::TouchPoint report{};
}
namespace digivice::board {
esp_err_t lockSharedI2c(std::uint32_t timeoutMs){
    CHECK(!locked);lastLockMs=timeoutMs;
    // Pinned IDF5.3.6 projdefs.h: pdMS_TO_TICKS truncates to integral ticks.
    lastLockTicks=timeoutMs*100/1000;
    if(forcedLock!=ESP_OK)return forcedLock;
    const auto deadline=nowMs+lastLockTicks*10;
    if(releaseAtMs>deadline){nowMs=deadline;return ESP_ERR_TIMEOUT;}
    if(releaseAtMs>nowMs)nowMs=releaseAtMs;
    locked=true;return ESP_OK;
}
void unlockSharedI2c(){CHECK(locked);locked=false;++unlocks;}
}
namespace digivice::display {
Status current{};TouchPoint previousTouch{};
bool touchReady(){return ready;}
esp_err_t readTouch(TouchPoint& point){
    CHECK(locked);++reads;readAtMs=nowMs;
    // The real readTouch constructs its16ms TouchBus here, after acquisition.
    // No elapsed lock wait is charged against that newly started group budget.
    if(updateReport)point=report;
    return readResult;
}
#define DIGIVICE_PANEL_ENABLED 1
#include "legacy_poll.inc"
#include "current_poll.inc"
}
namespace {
void reset(){using namespace digivice::display;current={};previousTouch={};nowMs=releaseAtMs=lastLockMs=lastLockTicks=readAtMs=0;reads=unlocks=0;locked=false;ready=updateReport=true;forcedLock=readResult=ESP_OK;report={true,42,53,true};}
}
int main(){using namespace digivice::display;
    // Reproduce old behavior: priority3 IMU is still completing a short pair of
    // reads when main wakes.2ms becomes try-lock and cancels despite healthy I2C.
    reset();previousTouch={true,42,53,true};releaseAtMs=15;TouchPoint point{true,42,53,true};
    CHECK(legacyPollTouch(point)==ESP_ERR_TIMEOUT);CHECK(lastLockTicks==0&&nowMs==0);CHECK(reads==0&&unlocks==0);CHECK(current.touchErrors==1);CHECK(!point.fresh&&!previousTouch.pressed);
    // Same contention now waits two ticks and publishes an actual fresh report.
    reset();previousTouch={true,42,53,true};releaseAtMs=15;
    CHECK(pollTouch(point)==ESP_OK);CHECK(lastLockMs==20&&lastLockTicks==2);CHECK(nowMs==15&&readAtMs==15);CHECK(reads==1&&unlocks==1&&!locked);CHECK(point.fresh&&point.pressed&&point.x==42);CHECK(current.touchSamples==1&&current.touchErrors==0&&current.touchLockMisses==0);
    // Every release time through the configured20ms limit succeeds; this clock
    // double aligns tick boundaries and is not a promise of exact wall timing.
    for(unsigned release=0;release<=20;++release){reset();releaseAtMs=release;CHECK(pollTouch(point)==ESP_OK);CHECK(readAtMs==release&&current.touchErrors==0);}
    // Contention beyond the finite allowance remains cancellation, never Up.
    reset();previousTouch={true,42,53,true};point=previousTouch;releaseAtMs=21;
    CHECK(pollTouch(point)==ESP_ERR_TIMEOUT);CHECK(nowMs==20&&reads==0&&unlocks==0);CHECK(current.touchLockMisses==1&&current.touchErrors==1&&current.touchSamples==0);CHECK(!point.fresh&&point.pressed&&!previousTouch.pressed);CHECK(current.lastTouchError==ESP_ERR_TIMEOUT);
    // Subsequent real neutral report may rearm; success doesn't erase counters.
    releaseAtMs=nowMs;report={false,42,53,true};CHECK(pollTouch(point)==ESP_OK);CHECK(point.fresh&&!point.pressed);CHECK(current.touchErrors==1&&current.touchLockMisses==1&&current.lastTouchError==ESP_OK);
    // Real transfer/protocol failures are counted separately and still cancel.
    for(const auto failure:{ESP_ERR_TIMEOUT,ESP_FAIL,ESP_ERR_INVALID_RESPONSE}){
        reset();previousTouch={true,42,53,true};point=previousTouch;readResult=failure;report={false,42,53,true};
        CHECK(pollTouch(point)==failure);CHECK(reads==1&&unlocks==1&&!locked);CHECK(current.touchErrors==1&&current.touchLockMisses==0);CHECK(current.lastTouchError==failure&&!point.fresh&&point.pressed&&!previousTouch.pressed);
    }
    reset();forcedLock=ESP_ERR_INVALID_STATE;CHECK(pollTouch(point)==ESP_ERR_INVALID_STATE);CHECK(current.touchErrors==1&&current.touchLockMisses==0&&reads==0);
    reset();ready=false;CHECK(pollTouch(point)==ESP_ERR_INVALID_STATE);CHECK(current.touchErrors==0&&current.touchLockMisses==0&&reads==0);
    // A successful busy/no-new-report read preserves level but supplies no
    // fresh release. The adapter never invents Up to disguise a missed poll.
    reset();previousTouch={true,42,53,true};updateReport=false;CHECK(pollTouch(point)==ESP_OK);CHECK(point.pressed&&!point.fresh&&current.touchSamples==1);
    std::printf("touch poll: %u checks passed; actual1d5552e zero-tick contention reproduced;20ms fix bounded; SDK doubles only\n",checks);
}
