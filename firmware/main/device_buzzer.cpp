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
    Request request{};
    bool pending=false, confirmed=false;
    std::uint64_t created=0, onSince=0;
    for(;;) {
        const auto now=static_cast<std::uint64_t>(esp_timer_get_time()/1000);
        if(!pending) pending=xQueueReceive(queue_,&request,0)==pdTRUE;
        const auto epoch=epoch_.load();
        if(now<last) {pattern.cancel();nextAttempt=0;pending=false;confirmed=false;}
        last=now;
        if(stopRequested() || activeEpoch!=epoch || !ready_.load()) {
            pattern.cancel();activeEpoch=epoch;confirmed=false;
        }
        // Expire even while initial OFF is still waiting for the bus.
        if(pending && (now<request.createdMs || now-request.createdMs>350 || request.epoch!=epoch || stopRequested())) {
            ++buzzerExpired_;pending=false;
        }
        const bool received=pending && known;
        if(known && ready_.load() && !stopRequested() && received &&
           request.epoch==epoch && now>=request.createdMs && now-request.createdMs<=350 &&
           (pattern.finished(now) || cuePriority(request.cue)>=priority)) {
            pattern.start(request.cue,now);priority=cuePriority(request.cue);++buzzerStarted_;
            created=request.createdMs;confirmed=false;
        }
        if(received)pending=false;
        const bool enabled=ready_.load() && !stopRequested();
        if(background.update(scene_.load(),enabled && known && musicEnabled_.load(),
                             !pattern.finished(now) || received,!on,now)) ++backgroundStarted_;
        const bool desired=enabled && (pattern.on(now) || background.on(now));
        if(desired && pattern.on(now)) outputWasEffect=true;
        if((!known || desired!=on) && now>=nextAttempt) {
            struct Guard { Audio* audio; BuzzerPattern* pattern; BuzzerBackground* background;
                std::uint32_t epoch; std::uint64_t created; bool confirmed, cancelled; };
            Guard guard{this,&pattern,&background,epoch,created,confirmed,false};
            const auto allowOn=[](void* context) {
                auto& g=*static_cast<Guard*>(context);
                const auto at=static_cast<std::uint64_t>(esp_timer_get_time()/1000);
                const bool effect=g.pattern->on(at);
                const bool fresh=g.confirmed || (at>=g.created && at-g.created<=350);
                const bool background=g.audio->musicEnabled_.load() && g.audio->scene_.load()!=MusicScene::Quiet && g.background->on(at);
                const bool allowed=g.audio->ready_.load() && !g.audio->stopRequested() &&
                    g.audio->epoch_.load()==g.epoch && ((effect && fresh) || background);
                g.cancelled=!allowed;return allowed;
            };
            const auto result=board::setBuzzer(desired,allowOn,&guard);
            const auto after=static_cast<std::uint64_t>(esp_timer_get_time()/1000);
            if(result==ESP_OK) {
                if(desired && !on) {onSince=after;buzzerLastOnMs_=static_cast<std::uint32_t>(after);}
                if(!desired) {
                    buzzerLastOffMs_=static_cast<std::uint32_t>(after);
                    if(on && after>=onSince) {
                        const auto width=static_cast<std::uint32_t>(after-onSince);
                        if(width>buzzerMaxOnMs_.load())buzzerMaxOnMs_=width;
                    }
                }
                known=true;on=desired;outputWasEffect=desired && pattern.on(now);
                if(on && outputWasEffect && !confirmed) {confirmed=true;++buzzerConfirmedCues_;}
                if(on) ++buzzerOnWrites_; else ++buzzerOffWrites_;
            }
            else if(result==ESP_ERR_NOT_FINISHED) {
                if(!guard.cancelled)++buzzerBusyRetries_;
                nextAttempt=after+20;
            } else {
                // Even a failed ON may have changed the output before readback
                // failed. Latch the fault and keep attempting OFF, never replay.
                known=false;ready_=false;error_=result;pattern.cancel();
                xQueueReset(queue_);nextAttempt=now+100;
            }
        }
        if(confirmed && known && !on && pattern.finished(now)) {++buzzerCompletedCues_;confirmed=false;}
        buzzerOutputKnown_=known;buzzerOutputOn_=on;
        const bool settled=known && !on && !pending && pattern.finished(now) && background.finished(now);
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
