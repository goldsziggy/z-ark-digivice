#include "device_audio.hpp"
#include "board_hal.hpp"
#include "buzzer_patterns.hpp"
#include "esp_timer.h"
#include "sdkconfig.h"

#if defined(CONFIG_DIGIVICE_BUZZER_21) && CONFIG_DIGIVICE_BUZZER_21
namespace digivice::device {
void Audio::runBuzzer() {
    BuzzerPattern pattern;
    BuzzerBackground background;
    bool known=false,on=false,outputWasEffect=false;
    unsigned priority=0;
    std::uint32_t activeEpoch=epoch_.load();
    std::uint64_t nextAttempt=0,last=0;
    for(;;) {
        const auto now=static_cast<std::uint64_t>(esp_timer_get_time()/1000);
        Request request{};
        const bool received=known && xQueueReceive(queue_,&request,0)==pdTRUE;
        const auto epoch=epoch_.load();
        if(now<last) {pattern.cancel();nextAttempt=0;}
        last=now;
        if(stopRequested() || activeEpoch!=epoch || !ready_.load()) {
            pattern.cancel();activeEpoch=epoch;
        }
        if(received && now>=request.createdMs && now-request.createdMs>350) ++buzzerExpired_;
        if(known && ready_.load() && !stopRequested() && received &&
           request.epoch==epoch && now>=request.createdMs && now-request.createdMs<=350 &&
           (pattern.finished(now) || cuePriority(request.cue)>=priority)) {
            pattern.start(request.cue,now);priority=cuePriority(request.cue);++buzzerStarted_;
        }
        const bool enabled=ready_.load() && !stopRequested();
        if(background.update(scene_.load(),enabled && known && musicEnabled_.load(),
                             !pattern.finished(now) || received,!on,now)) ++backgroundStarted_;
        const bool desired=enabled && (pattern.on(now) || background.on(now));
        if(desired && pattern.on(now)) outputWasEffect=true;
        if((!known || desired!=on) && now>=nextAttempt) {
            const auto result=board::setBuzzer(desired);
            if(result==ESP_OK) {
                known=true;on=desired;outputWasEffect=desired && pattern.on(now);
                if(on) ++buzzerOnWrites_; else ++buzzerOffWrites_;
            }
            else if(result==ESP_ERR_NOT_FINISHED) {
                // The shared bus owner wins. No transaction was attempted.
                ++buzzerBusyRetries_;
                nextAttempt=now+20;
            } else {
                // Even a failed ON may have changed the output before readback
                // failed. Latch the fault and keep attempting OFF, never replay.
                known=false;ready_=false;error_=result;pattern.cancel();
                xQueueReset(queue_);nextAttempt=now+100;
            }
        }
        buzzerOutputKnown_=known;buzzerOutputOn_=on;
        const bool settled=known && !on && pattern.finished(now) && background.finished(now);
        active_=!settled;
        // Background motifs never keep the screen awake. Strong power/USB
        // pause still waits for verified OFF through quiescent().
        effectsActive_=!known || !pattern.finished(now) || (on && outputWasEffect);
        if(settled) settledEpoch_=epoch;
        // One bounded masked I2C group at most per iteration, on this low-priority
        // worker. UI play/mute/pause do no I2C and never wait on the bus mutex.
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
} // namespace digivice::device
#endif
