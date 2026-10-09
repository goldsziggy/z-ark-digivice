#include "music_synth.hpp"
#include "audio_settings.hpp"
#include <algorithm>
#include <cmath>

namespace digivice::device {
namespace {
constexpr unsigned homeMelody[]{72,76,79,74,76,72,67,71};
constexpr unsigned battleMelody[]{69,72,76,74,71,74,79,76};
constexpr unsigned homeBass[]{48,53}, battleBass[]{45,41};
float frequency(unsigned midi) { return 440.F*std::exp2((static_cast<float>(midi)-69.F)/12.F); }
float approach(float value,float target,float speed) {
    return value<target?std::min(target,value+speed):std::max(target,value-speed);
}
}
float MusicSynth::sample(bool enabled,MusicScene scene,bool duck) {
    if(scene!=MusicScene::Home&&scene!=MusicScene::Battle)scene=MusicScene::Quiet;
    const bool playing=enabled&&scene!=MusicScene::Quiet;
    // Finish the fade before changing tempo or oscillator frequencies.
    if(scene!=scene_&&gain_==0) {
        scene_=scene;stepFrame_=bassFrame_=step_=0;leadPhase_=0;bassPhase_=.25F;
    }
    const float target=playing&&scene==scene_?(duck?.22F:1.F):0.F;
    gain_=approach(gain_,target,1.F/(kAudioSampleRate*.04F));
    if(gain_==0) {stepFrame_=bassFrame_=step_=0;leadPhase_=0;bassPhase_=.25F;return 0;}
    const bool battle=scene_==MusicScene::Battle;
    const auto frames=static_cast<unsigned>(kAudioSampleRate*(battle?.36F:.6F));
    if(stepFrame_==0) {
        leadFrequency_=frequency((battle?battleMelody:homeMelody)[step_]);leadPhase_=0;
        if(step_%4==0){bassFrequency_=frequency((battle?battleBass:homeBass)[step_/4]);bassPhase_=.25F;bassFrame_=0;}
    }
    const float leadAge=static_cast<float>(stepFrame_)/frames;
    const float bassAge=static_cast<float>(bassFrame_)/(frames*4);
    const float leadEnvelope=std::min(1.F,static_cast<float>(stepFrame_)/(kAudioSampleRate*.012F))*std::max(0.F,1.F-leadAge/.7F);
    const float bassEnvelope=std::min(1.F,static_cast<float>(bassFrame_)/(kAudioSampleRate*.02F))*std::max(0.F,1.F-bassAge/.85F);
    const float sample=std::sin(leadPhase_*6.283185307F)*leadEnvelope*.04F+
        (1.F-4.F*std::fabs(bassPhase_-.5F))*bassEnvelope*.018F;
    leadPhase_+=leadFrequency_/kAudioSampleRate;if(leadPhase_>=1)leadPhase_-=1;
    bassPhase_+=bassFrequency_/kAudioSampleRate;if(bassPhase_>=1)bassPhase_-=1;
    ++bassFrame_;if(++stepFrame_>=frames){stepFrame_=0;step_=(step_+1)%8;}
    return sample*gain_;
}
std::int16_t AudioMixer::sample(unsigned volume,bool enabled,MusicScene scene) {
    const float target=static_cast<float>(std::min(volume,sound::kMaxVolume))/100.F;
    gain_=approach(gain_,target,1.F/(kAudioSampleRate*.02F));
    const auto music=music_.sample(enabled,scene,!cue_.finished());
    // Keep each existing percentage's steady-state gain: f9 used half-scale
    // sources times volume/50. Full-scale sources times volume/100 extend that
    // same scale to 100 without silently making existing settings louder.
    // Cue <= .65 FS and music <= .058 FS, so the sum remains below .708 FS.
    const float mixed=(cue_.sample(100)+music*32767.F)*gain_;
    return static_cast<std::int16_t>(std::clamp(mixed,-32767.F,32767.F));
}
} // namespace digivice::device
