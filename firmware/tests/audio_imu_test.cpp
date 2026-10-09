#include "audio_cues.hpp"
#include "imu_filter.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {
unsigned checks = 0, failures = 0;
#define CHECK(test) do { ++checks; if (!(test)) { \
    std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); ++failures; } } while (false)

void audioBoundsAndMute() {
    using namespace digivice::device;
    for (unsigned cue = 0; cue < static_cast<unsigned>(AudioCue::Count); ++cue) {
        CueSynth normal, muted, loud, excessive;
        normal.start(static_cast<AudioCue>(cue)); muted.start(static_cast<AudioCue>(cue));
        loud.start(static_cast<AudioCue>(cue)); excessive.start(static_cast<AudioCue>(cue));
        unsigned frames = 0;
        long energy = 0;
        bool silent = true, capped = true, bounded = true;
        while (!normal.finished() && frames < kAudioSampleRate*2) {
            const auto n = normal.sample(15), m = muted.sample(0), l = loud.sample(50), x = excessive.sample(1000);
            energy += std::abs(n); silent = silent && m == 0;
            capped = capped && x == l;
            bounded = bounded && std::abs(n) <= 3195 && std::abs(l) <= 10649;
            ++frames;
        }
        CHECK(frames > 1000 && frames <= static_cast<unsigned>(kAudioSampleRate*1.2F));
        CHECK(energy > 10000 && silent && capped && bounded);
        CHECK(normal.finished() && normal.sample(15) == 0 && normal.sample(50) == 0);
    }
    CueSynth invalid; invalid.start(AudioCue::Count);
    CHECK(invalid.finished() && invalid.sample(50) == 0);
    CHECK(cuePriority(AudioCue::CaptureSuccess) > cuePriority(AudioCue::Navigate));
    CHECK(cuePriority(AudioCue::Win) > cuePriority(AudioCue::Attack));
}

void imuCalibrationAndFaults() {
    using namespace digivice::motion;
    ImuFilter filter;
    const Vector3 gravity{0,0,1}, bias{.5F,-.3F,.1F};
    for (unsigned ms = 0; ms < 1000; ms += 20) CHECK(!filter.observe(gravity, bias, ms).calibrated);
    auto reading = filter.observe(gravity, bias, 1000);
    CHECK(reading.valid && reading.calibrated && std::fabs(reading.gyroDps.x) < .0001F);
    CHECK(std::fabs(reading.tiltX) < .001F && std::fabs(reading.tiltY) < .001F);
    for (unsigned ms = 1020; ms <= 1400; ms += 20) reading = filter.observe({.5F,0,.8660254F}, bias, ms);
    CHECK(reading.tiltX > .9F && reading.tiltX <= 1 && std::fabs(reading.tiltY) < .01F);
    const auto previous = reading.tiltX;
    reading = filter.observe({2,0,1}, bias, 1420);
    CHECK(reading.valid && reading.tiltX == previous); // Linear acceleration isn't tilt.
    filter.invalidate();
    CHECK(!filter.reading().valid && filter.reading().tiltX == 0);
    reading = filter.observe(gravity, bias, 2000);
    CHECK(reading.valid && !reading.calibrated); // Stale gap recenters.
    reading = filter.observe({std::numeric_limits<float>::quiet_NaN(),0,1}, bias, 2020);
    CHECK(!reading.valid && reading.tiltX == 0);
    reading = filter.observe(gravity, {1000,0,0}, 2040);
    CHECK(!reading.valid);
    filter.reset();
    for (unsigned i = 0; i < 100; ++i) reading = filter.observe(gravity, bias, 100);
    CHECK(!reading.calibrated); // Duplicate samples cannot simulate elapsed time.
    for (unsigned ms = 120; ms <= 1000; ms += 20) filter.observe(gravity, bias, ms);
    CHECK(!filter.observe(gravity, {10,0,0}, 1020).calibrated); // Moving restarts calibration.
    for (unsigned ms = 1040; ms < 2040; ms += 20) CHECK(!filter.observe(gravity, bias, ms).calibrated);
    CHECK(filter.observe(gravity, bias, 2040).calibrated);
    CHECK(!filter.observe(gravity, bias, 20).calibrated); // Time reversal resets.
}
}
int main() {
    audioBoundsAndMute(); imuCalibrationAndFaults();
    std::printf("Audio/IMU portable checks: %u checks, %u failures; synth=%zu bytes, filter=%zu bytes. Synthetic inputs only.\n",
        checks, failures, sizeof(digivice::device::CueSynth), sizeof(digivice::motion::ImuFilter));
    return failures ? 1 : 0;
}
