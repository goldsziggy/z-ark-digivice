#pragma once
#include "imu_filter.hpp"
#include <cstdint>

namespace digivice::motion {
enum class StepStatus : std::uint8_t {
    Unavailable, Priming, Tracking, Paused, Gap, InvalidSample, TimeReversed, CounterExhausted
};
struct StepReading {
    std::uint32_t acceptedSteps = 0; // Cumulative for this object/boot, never a lifetime save.
    StepStatus status = StepStatus::Unavailable;
    std::uint64_t observedAtMs = 0; // Last accepted sensor sample, not last UI poll.
};
const char* stepStatusText(StepStatus status);

// Bounded software estimate from real acceleration in g. No hardware pedometer
// registers, heap, SDK, storage or game rules. One sampler owns this object.
// Magnitude removes orientation; a slow baseline removes gravity. A modest
// hysteretic lobe and three consistent 280..1500ms cycles reject isolated bumps.
// Confirmed first three cycles are credited together, not silently discarded.
// Gaps/invalid samples/pause discard ONLY unconfirmed candidates, retaining count.
// Repetitive shaking can resemble walking; thresholds need real walking trials.
class Pedometer {
public:
    StepReading observe(Vector3 accelerationG, std::uint64_t nowMs);
    StepReading poll(std::uint64_t nowMs);
    StepReading pause(bool paused, std::uint64_t nowMs);
    StepReading invalidate(StepStatus status = StepStatus::InvalidSample);
    StepReading reading() const { return reading_; }
private:
    void resetContinuity(StepStatus status);
    void candidate(std::uint64_t peakMs);
    StepReading reading_{};
    float gravity_ = 1, signal_ = 0, peak_ = 0;
    std::uint64_t warmAtMs_ = 0, lobeAtMs_ = 0, peakAtMs_ = 0, lastPeakMs_ = 0;
    std::uint32_t intervalMs_ = 0;
    std::uint8_t candidates_ = 0;
    bool seen_ = false, paused_ = false, valley_ = false, lobe_ = false;
};
} // namespace digivice::motion
