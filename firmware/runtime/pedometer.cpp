#include "pedometer.hpp"
#include <algorithm>
#include <cmath>

namespace digivice::motion {
namespace {
constexpr std::uint64_t maximumGapMs = 200, warmupMs = 600;
constexpr std::uint32_t minimumPeriodMs = 280, maximumPeriodMs = 1500;
constexpr float riseG = 0.06F, fallG = 0.015F, valleyG = -0.025F;
}
const char* stepStatusText(StepStatus status) {
    switch (status) {
    case StepStatus::Unavailable: return "unavailable";
    case StepStatus::Priming: return "priming";
    case StepStatus::Tracking: return "tracking (walking estimate)";
    case StepStatus::Paused: return "paused";
    case StepStatus::Gap: return "sample gap; unconfirmed motion discarded";
    case StepStatus::InvalidSample: return "invalid acceleration";
    case StepStatus::TimeReversed: return "sample time reversed";
    case StepStatus::CounterExhausted: return "step counter exhausted";
    }
    return "unknown";
}
void Pedometer::resetContinuity(StepStatus status) {
    seen_ = valley_ = lobe_ = false;
    candidates_ = 0; intervalMs_ = 0; signal_ = peak_ = 0;
    if (reading_.status != StepStatus::CounterExhausted) reading_.status = status;
}
StepReading Pedometer::invalidate(StepStatus status) {
    resetContinuity(paused_ ? StepStatus::Paused : status);
    return reading_;
}
StepReading Pedometer::pause(bool paused, std::uint64_t now) {
    (void)now;
    if (paused != paused_) {
        paused_ = paused;
        resetContinuity(paused ? StepStatus::Paused : StepStatus::Priming);
    }
    return reading_;
}
StepReading Pedometer::poll(std::uint64_t now) {
    if (paused_ || !seen_) return reading_;
    if (now < reading_.observedAtMs) return invalidate(StepStatus::TimeReversed);
    if (now - reading_.observedAtMs > maximumGapMs) return invalidate(StepStatus::Gap);
    return reading_;
}
void Pedometer::candidate(std::uint64_t peakMs) {
    if (!candidates_ || peakMs - lastPeakMs_ > maximumPeriodMs) {
        candidates_ = 1; intervalMs_ = 0; lastPeakMs_ = peakMs; return;
    }
    const auto period = static_cast<std::uint32_t>(peakMs - lastPeakMs_);
    if (period < minimumPeriodMs) {
        candidates_ = 0; intervalMs_ = 0; return; // Rapid jitter breaks a run.
    }
    if (intervalMs_) {
        const auto difference = period > intervalMs_ ? period - intervalMs_ : intervalMs_ - period;
        if (difference > std::max<std::uint32_t>(100, intervalMs_ * 35u / 100u)) {
            candidates_ = 1; intervalMs_ = 0; lastPeakMs_ = peakMs; return;
        }
    }
    intervalMs_ = period; lastPeakMs_ = peakMs;
    if (candidates_ < 3) ++candidates_;
    if (candidates_ < 3) return;
    const auto added = candidates_ == 3 ? 3u : 1u;
    candidates_ = 4;
    if (reading_.acceptedSteps > UINT32_MAX - added) {
        reading_.acceptedSteps = UINT32_MAX;
        reading_.status = StepStatus::CounterExhausted;
    } else reading_.acceptedSteps += added;
}
StepReading Pedometer::observe(Vector3 a, std::uint64_t now) {
    if (paused_ || reading_.status == StepStatus::CounterExhausted) return reading_;
    if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.z) ||
        std::fabs(a.x) >= 3.95F || std::fabs(a.y) >= 3.95F || std::fabs(a.z) >= 3.95F)
        return invalidate();
    const float magnitude = std::sqrt(a.x*a.x + a.y*a.y + a.z*a.z);
    // Reject a drop/large shock instead of interpreting its ringing as cadence.
    if (magnitude < 0.25F || magnitude > 2.8F) return invalidate();
    if (seen_ && now == reading_.observedAtMs) return reading_;
    if (seen_ && now < reading_.observedAtMs) return invalidate(StepStatus::TimeReversed);
    if (seen_ && now - reading_.observedAtMs > maximumGapMs) resetContinuity(StepStatus::Gap);
    if (!seen_) {
        seen_ = true; gravity_ = magnitude; signal_ = 0; warmAtMs_ = now;
        reading_.observedAtMs = now; reading_.status = StepStatus::Priming;
        return reading_;
    }
    const auto dt = static_cast<float>(now - reading_.observedAtMs);
    reading_.observedAtMs = now;
    gravity_ += dt / (1200.0F + dt) * (magnitude - gravity_);
    signal_ += dt / (40.0F + dt) * ((magnitude - gravity_) - signal_);
    if (now - warmAtMs_ < warmupMs) return reading_;
    reading_.status = StepStatus::Tracking;
    // Cadence is peak-to-peak, evaluated when the lobe closes. Expiring here
    // would discard slow valid steps while their next lobe is still falling.
    if (signal_ <= valleyG) valley_ = true;
    if (!lobe_ && valley_ && signal_ >= riseG) {
        lobe_ = true; valley_ = false; lobeAtMs_ = peakAtMs_ = now; peak_ = signal_;
    }
    if (lobe_) {
        if (signal_ > peak_) { peak_ = signal_; peakAtMs_ = now; }
        const auto width = now - lobeAtMs_;
        if (width > 800) { lobe_ = false; candidates_ = 0; intervalMs_ = 0; }
        else if (signal_ <= fallG) {
            lobe_ = false;
            if (width >= 40) candidate(peakAtMs_);
        }
    }
    return reading_;
}
} // namespace digivice::motion
