#pragma once
#include "audio_cues.hpp"

namespace digivice::device {
enum class MusicScene : std::uint8_t { Home, Battle, Quiet };

// One original eight-step melody plus sparse bass, two fixed oscillator voices.
// No streams, files, RNG or allocations. Quiet/off fades without affecting SFX.
class MusicSynth {
public:
    float sample(bool enabled, MusicScene scene, bool duck);
    bool active() const { return gain_>0; }
private:
    MusicScene scene_ = MusicScene::Home;
    float gain_ = 0, leadPhase_ = 0, bassPhase_ = .25F;
    float leadFrequency_ = 0, bassFrequency_ = 0;
    std::uint32_t stepFrame_ = 0, bassFrame_ = 0;
    unsigned step_ = 0;
};

// Fixed SFX + two music voices; shared master ramps over at most 20ms.
class AudioMixer {
public:
    void start(AudioCue cue) { cue_.start(cue); }
    bool cueFinished() const { return cue_.finished(); }
    bool needsSamples(bool enabled, MusicScene scene) const {
        return !cue_.finished() || music_.active() || (enabled && scene!=MusicScene::Quiet);
    }
    std::int16_t sample(unsigned volume, bool enabled, MusicScene scene);
private:
    CueSynth cue_;
    MusicSynth music_;
    float gain_ = 0;
};
} // namespace digivice::device
