#pragma once

#include <cstdint>

namespace digivice::power {

constexpr std::uint64_t kDebounceMs = 30;
constexpr std::uint64_t kHoldMs = 3000;
constexpr std::uint64_t kMaxSampleGapMs = 250;
constexpr std::uint64_t kShutdownTimeoutMs = 10000;

enum class Phase { AwaitInitialRelease, Ready, Holding, Draining, Saving,
                   AwaitRelease, QuietStandby, Failed };
enum class Failure { None, DrainFailed, SaveFailed, LatchFailed, Hardware, Timeout, ClockReversed };
enum class Work { Pending, Ready, Failed };

struct Input {
    std::uint64_t nowMs = 0;
    bool pressed = false; // Actual PWR sense only; never ACTION/BACK.
    Work work = Work::Pending; // Result for current Draining/Saving phase only.
    bool latchDropped = false; // Acknowledges a previously requested cut.
    bool workIdle = true; // Resume gesture accepted only AFTER pending I/O finishes.
    bool hardwareFault = false; // Invalid sense or failed latch GPIO operation.
};
struct Actions {
    bool beginShutdown = false; // Freeze new work and start draining existing work.
    bool save = false;          // One final durable checkpoint after drain.
    bool cutLatch = false;      // Recheck raw PWR before lowering SYS_EN; acknowledge next tick.
    bool resume = false;        // Assert SYS_EN HIGH successfully BEFORE resuming any work.
};
struct Status {
    Phase phase = Phase::AwaitInitialRelease;
    Failure failure = Failure::None;
    std::uint8_t countdownSeconds = 0;
};

// No heap, SDK, game state, GPIO, blocking calls or device-source assumptions.
// Caller serializes ticks and all effects. Feed monotonic milliseconds regularly.
// A failed shutdown keeps work frozen and SYS_EN high. Fresh PWR press/release
// resumes only once workIdle; a new full hold is required for another attempt.
// If cutLatch fails, report Work::Failed in AwaitRelease. If code survives a
// successful cut, its acknowledgment enters quiet standby (USB is NOT inferred).
class Controller {
public:
    Actions tick(const Input& input);
    const Status& status() const { return status_; }
    bool frozen() const;
private:
    Status status_{};
    bool initialized_ = false;
    bool rawPressed_ = false;
    bool wakeArmed_ = false;
    bool wakePressed_ = false;
    bool cutRequested_ = false;
    std::uint64_t lastMs_ = 0;
    std::uint64_t rawSinceMs_ = 0;
    std::uint64_t holdSinceMs_ = 0;
    std::uint64_t shutdownSinceMs_ = 0;
    void fail(Failure failure, std::uint64_t nowMs);
    void resetWake(std::uint64_t nowMs);
};

const char* phaseName(Phase phase);
const char* failureText(Failure failure);

} // namespace digivice::power
