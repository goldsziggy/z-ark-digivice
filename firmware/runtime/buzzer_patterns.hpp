#pragma once
#include "audio_cues.hpp"
#include "music_synth.hpp"
#include <cstdint>

namespace digivice::device {
// On/off durations only, starting ON and ending OFF. No pitch or PWM claim.
struct BuzzerRhythm {
    std::uint16_t ms[7]{};
    std::uint8_t count=0;
    constexpr unsigned duration() const {
        unsigned total=0;for(unsigned i=0;i<count;++i)total+=ms[i];return total;
    }
};
constexpr BuzzerRhythm buzzerRhythm(AudioCue cue) {
    switch(cue) {
    case AudioCue::DigiCritical: return {{60,60,60,100,180},5};
    case AudioCue::DigiPower: return {{160,80,160},3};
    case AudioCue::DigiGuard: return {{80,80,80,80,80},5};
    case AudioCue::DigiRecovery: return {{40,80,60,80,100},5};
    case AudioCue::DigiElement: return {{40,40,40,40,40,100,140},7};
    case AudioCue::DigiCriticalHit: return {{100},1};
    case AudioCue::Boot: case AudioCue::Hatch: return {{60,80,60},3};
    case AudioCue::Win: case AudioCue::CaptureSuccess: case AudioCue::Evolution:
        return {{60,60,60,60,160},5};
    case AudioCue::Error: case AudioCue::CaptureFail: return {{140,80,60},3};
    case AudioCue::Encounter: case AudioCue::CaptureArm: return {{80,60,80},3};
    case AudioCue::Navigate: case AudioCue::Back: return {{40},1};
    case AudioCue::Feed: case AudioCue::Play: case AudioCue::Rest: return {{60,60,80},3};
    case AudioCue::Attack: case AudioCue::Magic: case AudioCue::Hit:
    case AudioCue::CaptureThrow: case AudioCue::Retreat: return {{60},1};
    case AudioCue::Count: return {};
    }
    return {};
}
class BuzzerPattern {
public:
    void start(AudioCue cue,std::uint64_t now) {rhythm_=buzzerRhythm(cue);start_=now;}
    void start(BuzzerRhythm rhythm,std::uint64_t now) {rhythm_=rhythm;start_=now;}
    void cancel(){rhythm_={};}
    bool finished(std::uint64_t now) const {
        return !rhythm_.count || now<start_ || now-start_>=rhythm_.duration();
    }
    bool on(std::uint64_t now) const {
        if(finished(now))return false;
        auto elapsed=now-start_;
        for(unsigned i=0;i<rhythm_.count;++i) {
            if(elapsed<rhythm_.ms[i])return i%2==0;
            elapsed-=rhythm_.ms[i];
        }
        return false;
    }
private:
    BuzzerRhythm rhythm_{};
    std::uint64_t start_=0;
};
static_assert(buzzerRhythm(AudioCue::DigiCritical).duration()<600,"Bounded cue");

// Four-note excerpts of music_synth.cpp's 600/360 ms Home/Battle beat grids.
// Pitch becomes a short accent duration, not an I2C-generated audio waveform.
constexpr BuzzerRhythm backgroundRhythm(MusicScene scene) {
    return scene==MusicScene::Home ? BuzzerRhythm{{40,560,60,540,80,520,40},7} :
        scene==MusicScene::Battle ? BuzzerRhythm{{40,320,60,300,80,280,60},7} : BuzzerRhythm{};
}
class BuzzerBackground {
public:
    bool update(MusicScene scene,bool enabled,bool foreground,bool canStart,std::uint64_t now) {
        if(now<last_) { pattern_.cancel(); next_=now+30000; }
        last_=now;
        if(scene!=scene_) { pattern_.cancel(); scene_=scene; }
        if(!enabled || foreground || (scene!=MusicScene::Home && scene!=MusicScene::Battle)) {
            pattern_.cancel();
            if(foreground && next_<now+2000) next_=now+2000;
            return false;
        }
        if(canStart && pattern_.finished(now) && now>=next_) {
            pattern_.start(backgroundRhythm(scene),now);
            next_=now+(scene==MusicScene::Home ? 30000 : 20000);
            return true;
        }
        return false;
    }
    bool on(std::uint64_t now) const { return pattern_.on(now); }
    bool finished(std::uint64_t now) const { return pattern_.finished(now); }
private:
    BuzzerPattern pattern_;
    MusicScene scene_=MusicScene::Quiet;
    std::uint64_t next_=0,last_=0;
};
} // namespace digivice::device
