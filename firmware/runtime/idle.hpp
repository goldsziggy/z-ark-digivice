#pragma once

#include "imu_filter.hpp"
#include <cstdint>

namespace digivice::idle {

constexpr std::uint32_t kDefaultSeconds = 60;
bool validTimeout(std::uint32_t seconds); // Off, 30, 60, 120, 300.
enum class Request : std::uint8_t { None, Prepare, Wake };

// Screen-idle policy only: no task, hardware sleep, save, or game mutation.
// One UI/main-task owner; live touch and the existing IMU worker keep running.
class Controller {
public:
    bool configure(std::uint32_t seconds, std::uint64_t nowMs);
    void activity(std::uint64_t nowMs);
    // Two distinct fresh meaningful-motion samples qualify, never raw gravity.
    // Returns true and records activity when qualified. No sample is mutated.
    bool observeMotion(const motion::ImuReading&, std::uint64_t nowMs);
    Request tick(std::uint64_t nowMs, bool blocked);
    // Caller acknowledges only after its synchronous checkpoint + HAL operation.
    // Failed preparation leaves the display on and restarts the timeout window.
    void completePrepare(bool success, std::uint64_t nowMs);
    void completeWake(bool success, std::uint64_t nowMs);
    bool blanked() const { return blanked_; }
    std::uint32_t timeoutSeconds() const { return timeoutSeconds_; }
private:
    void resetMotion();
    std::uint32_t timeoutSeconds_ = kDefaultSeconds;
    std::uint64_t lastActivityMs_ = 0, lastTickMs_ = 0, retryWakeMs_ = 0;
    std::uint64_t lastSampleMs_ = 0, motionCandidateMs_ = 0;
    motion::Vector3 anchor_{};
    bool initialized_ = false, blanked_ = false, preparePending_ = false;
    bool wakePending_ = false, wakeDue_ = false, haveSample_ = false, motionCandidate_ = false;
};
} // namespace digivice::idle
