#include "idle.hpp"
#include <cmath>

namespace digivice::idle {
namespace {
constexpr std::uint64_t kFreshMs = 200, kWakeRetryMs = 1000;
bool finite(motion::Vector3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
float squared(motion::Vector3 v) { return v.x*v.x + v.y*v.y + v.z*v.z; }
}
bool validTimeout(std::uint32_t seconds) {
    return seconds == 0 || seconds == 30 || seconds == 60 || seconds == 120 || seconds == 300;
}
void Controller::resetMotion() {
    haveSample_ = motionCandidate_ = false;
    lastSampleMs_ = motionCandidateMs_ = 0;
    anchor_ = {};
}
bool Controller::configure(std::uint32_t seconds, std::uint64_t now) {
    if (!validTimeout(seconds)) return false;
    timeoutSeconds_ = seconds;
    activity(now);
    return true;
}
void Controller::activity(std::uint64_t now) {
    if (initialized_ && now < lastTickMs_) resetMotion();
    initialized_ = true;
    lastActivityMs_ = lastTickMs_ = now;
    preparePending_ = false;
    if (blanked_) wakeDue_ = true;
}
bool Controller::observeMotion(const motion::ImuReading& reading, std::uint64_t now) {
    const auto sample = reading.observedAtMs;
    if (!reading.valid || !finite(reading.accelerationG) || !finite(reading.gyroDps) ||
        sample > now || now - sample > kFreshMs || squared(reading.accelerationG) > 20.0f ||
        squared(reading.gyroDps) > 3.0f*2048.0f*2048.0f) {
        resetMotion(); return false;
    }
    if (haveSample_ && sample == lastSampleMs_) return false;
    if (!haveSample_ || sample < lastSampleMs_ || sample-lastSampleMs_ > kFreshMs) {
        anchor_ = reading.accelerationG; lastSampleMs_ = sample;
        haveSample_ = true; motionCandidate_ = false; return false;
    }
    lastSampleMs_ = sample;
    const motion::Vector3 delta{reading.accelerationG.x-anchor_.x,
        reading.accelerationG.y-anchor_.y, reading.accelerationG.z-anchor_.z};
    // Orientation independent change from the recent gravity baseline, plus
    // bias-corrected rotation if calibrated. One bump never qualifies alone.
    const bool moving = squared(delta) >= 0.12f*0.12f ||
        (reading.calibrated && squared(reading.gyroDps) >= 25.0f*25.0f);
    if (!moving) {
        motionCandidate_ = false;
        anchor_.x += delta.x*0.125f; anchor_.y += delta.y*0.125f; anchor_.z += delta.z*0.125f;
        return false;
    }
    if (!motionCandidate_ || sample-motionCandidateMs_ > kFreshMs) {
        motionCandidate_ = true; motionCandidateMs_ = sample; return false;
    }
    if (sample-motionCandidateMs_ < 10) return false;
    anchor_ = reading.accelerationG; motionCandidate_ = false;
    activity(now); return true;
}
Request Controller::tick(std::uint64_t now, bool blocked) {
    if (!initialized_) activity(now);
    if (now < lastTickMs_) {
        activity(now); retryWakeMs_ = 0; wakePending_ = false;
    }
    lastTickMs_ = now;
    if (blocked) activity(now);
    if (blanked_) {
        if ((wakeDue_ || !timeoutSeconds_) && !wakePending_ && now >= retryWakeMs_) {
            wakePending_ = true; return Request::Wake;
        }
        return Request::None;
    }
    if (!blocked && timeoutSeconds_ && !preparePending_ && now-lastActivityMs_ >= std::uint64_t(timeoutSeconds_)*1000) {
        preparePending_ = true; return Request::Prepare;
    }
    return Request::None;
}
void Controller::completePrepare(bool success, std::uint64_t now) {
    if (!preparePending_) return;
    preparePending_ = false;
    lastActivityMs_ = lastTickMs_ = now;
    blanked_ = success;
    wakePending_ = wakeDue_ = false;
    retryWakeMs_ = 0;
    resetMotion();
}
void Controller::completeWake(bool success, std::uint64_t now) {
    if (!wakePending_) return;
    wakePending_ = false;
    if (!success) {
        retryWakeMs_ = now > UINT64_MAX-kWakeRetryMs ? UINT64_MAX : now+kWakeRetryMs;
        return;
    }
    blanked_ = wakeDue_ = false;
    lastActivityMs_ = lastTickMs_ = now;
    retryWakeMs_ = 0;
    resetMotion();
}
} // namespace digivice::idle
