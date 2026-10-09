#include "audio_cues.hpp"
#include "audio_settings.hpp"

#include <algorithm>
#include <cmath>

namespace digivice::device {
namespace {
enum Wave : std::uint8_t { Sine, Triangle, Square };
struct Note { float pitch, duration, offset, endPitch = 0; };
struct Cue { Wave wave; unsigned count; Note notes[6]; };
constexpr Cue cues[]{
    {Sine, 3, {{60,.10F,0}, {67,.12F,.11F}, {72,.18F,.24F}}}, // Boot
    {Triangle, 4, {{60,.12F,0}, {64,.12F,.12F}, {67,.14F,.24F}, {72,.3F,.4F}}}, // Hatch
    {Sine, 2, {{72,.04F,0}, {79,.065F,.04F}}},
    {Sine, 2, {{72,.04F,0}, {67,.065F,.04F}}},
    {Sine, 3, {{72,.12F,0}, {76,.16F,.09F}, {79,.14F,.2F}}},
    {Triangle, 4, {{67,.09F,0}, {74,.11F,.1F}, {79,.16F,.23F}, {76,.1F,.4F}}},
    {Sine, 3, {{67,.25F,0}, {64,.3F,.22F}, {60,.4F,.47F}}},
    {Square, 4, {{55,.09F,0}, {67,.09F,.1F}, {74,.16F,.21F}, {79,.22F,.38F}}},
    {Square, 2, {{48,.09F,0,79}, {79,.065F,.05F,43}}},
    {Sine, 3, {{67,.14F,0,79}, {74,.14F,.07F,86}, {86,.18F,.16F,81}}},
    {Triangle, 2, {{55,.06F,0,36}, {43,.08F,.04F,31}}},
    {Sine, 2, {{67,.09F,0}, {74,.13F,.1F}}},
    {Triangle, 2, {{60,.17F,0,91}, {84,.12F,.12F,72}}},
    {Triangle, 5, {{72,.12F,0}, {76,.13F,.12F}, {79,.15F,.25F}, {84,.35F,.43F}, {72,.3F,.45F}}},
    {Sine, 3, {{72,.1F,0}, {68,.14F,.12F}, {63,.24F,.28F}}},
    {Triangle, 4, {{67,.1F,0}, {72,.1F,.12F}, {79,.18F,.24F}, {84,.3F,.44F}}},
    {Sine, 3, {{67,.2F,0}, {64,.25F,.2F}, {60,.35F,.43F}}},
    {Triangle, 6, {{60,.12F,0}, {64,.12F,.12F}, {67,.12F,.24F}, {72,.14F,.36F}, {79,.2F,.5F}, {84,.5F,.68F}}},
    {Sine, 2, {{55,.07F,0}, {48,.13F,.09F}}},
};
constexpr const char* names[]{"boot", "hatch", "navigate", "back", "feed", "play", "rest", "encounter",
    "attack", "magic", "hit", "capture-arm", "capture-throw", "capture-success", "capture-fail",
    "win", "retreat", "evolution", "error"};
static_assert(sizeof(cues)/sizeof(cues[0]) == static_cast<unsigned>(AudioCue::Count));
static_assert(sizeof(names)/sizeof(names[0]) == static_cast<unsigned>(AudioCue::Count));
float frequency(float midi) { return 440.0F * std::exp2((midi-69.0F)/12.0F); }
}
unsigned cuePriority(AudioCue cue) {
    switch (cue) {
    case AudioCue::CaptureSuccess: case AudioCue::CaptureFail: case AudioCue::Win:
    case AudioCue::Retreat: case AudioCue::Hatch: case AudioCue::Evolution: return 4;
    case AudioCue::CaptureArm: case AudioCue::CaptureThrow: case AudioCue::Error: return 3;
    case AudioCue::Encounter: case AudioCue::Attack: case AudioCue::Magic: case AudioCue::Hit: return 2;
    default: return 1;
    }
}
const char* cueName(AudioCue cue) {
    const auto index = static_cast<unsigned>(cue);
    return index < static_cast<unsigned>(AudioCue::Count) ? names[index] : "invalid";
}
void CueSynth::start(AudioCue cue) {
    *this = {};
    const auto index = static_cast<unsigned>(cue);
    if (index >= static_cast<unsigned>(AudioCue::Count)) return;
    const auto& source = cues[index];
    count_ = source.count;
    for (unsigned i = 0; i < count_; ++i) {
        const auto& note = source.notes[i];
        auto& voice = voices_[i];
        voice.frequency = frequency(note.pitch);
        voice.start = static_cast<std::uint32_t>(note.offset*kAudioSampleRate);
        voice.duration = static_cast<std::uint32_t>(note.duration*kAudioSampleRate);
        voice.waveform = source.wave;
        voice.slide = note.endPitch == 0 ? 1.0F : std::pow(frequency(note.endPitch)/voice.frequency, 1.0F/voice.duration);
        length_ = std::max(length_, voice.start+voice.duration);
    }
}
std::int16_t CueSynth::sample(unsigned volume) {
    if (finished()) return 0;
    float sum = 0;
    for (unsigned i = 0; i < count_; ++i) {
        auto& voice = voices_[i];
        if (position_ < voice.start || position_-voice.start >= voice.duration) continue;
        const auto frame = position_-voice.start;
        const float attack = std::min(1.0F, static_cast<float>(frame)/(kAudioSampleRate*.006F));
        const float decay = 1.0F-static_cast<float>(frame)/voice.duration;
        float signal = std::sin(voice.phase*6.283185307F);
        if (voice.waveform == Triangle) signal = 1.0F-4.0F*std::fabs(voice.phase-.5F);
        if (voice.waveform == Square) signal = (voice.phase < .5F ? .55F : -.55F);
        sum += signal*attack*decay*.30F;
        voice.phase += voice.frequency/kAudioSampleRate;
        if (voice.phase >= 1.0F) voice.phase -= 1.0F;
        voice.frequency *= voice.slide;
    }
    ++position_;
    const float bounded = std::clamp(sum, -.65F, .65F)*static_cast<float>(std::min(volume, sound::kMaxVolume))/100.0F;
    return static_cast<std::int16_t>(bounded*32767.0F);
}
} // namespace digivice::device
