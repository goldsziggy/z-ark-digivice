#pragma once

#include <cstddef>
#include <cstdint>

namespace digivice::device {

enum class AudioCue : std::uint8_t {
    Boot, Hatch, Navigate, Back, Feed, Play, Rest, Encounter,
    Attack, Magic, Hit, CaptureArm, CaptureThrow, CaptureSuccess,
    CaptureFail, Win, Retreat, Evolution, Error, Count
};
unsigned cuePriority(AudioCue cue);
const char* cueName(AudioCue cue);
// PCM5101A runs from BCLK on this board (no MCLK). 44.1 kHz / 16-bit
// stereo supplies the documented 1.4112 MHz BCLK PLL reference; 22.05 kHz
// with 32 clocks/frame was below its documented PLL configurations.
constexpr unsigned kAudioSampleRate = 44100;

// Original motifs from web/audio-engine.js; no sampled/downloaded audio. Fixed
// voices and state, no allocations; each call generates one signed mono frame.
class CueSynth {
public:
    void start(AudioCue cue);
    std::int16_t sample(unsigned volumePercent);
    bool finished() const { return position_ >= length_; }
private:
    struct Voice {
        float phase = 0, frequency = 0, slide = 1;
        std::uint32_t start = 0, duration = 0;
        std::uint8_t waveform = 0;
    };
    Voice voices_[6]{};
    std::uint32_t position_ = 0, length_ = 0;
    unsigned count_ = 0;
};
} // namespace digivice::device
