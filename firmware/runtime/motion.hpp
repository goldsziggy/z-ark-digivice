#pragma once

#include <cstdint>

namespace digivice::motion {
constexpr std::uint32_t kCounterMask = 0x00ffffffu;
constexpr std::uint16_t kMaximumBatchSteps = 1000;

enum class Status : std::uint8_t {
    AwaitingAnchor, Tracking, Paused, Stale, InvalidConfig, InvalidSample,
    TimeReversed, CounterReset, ImplausibleCount, PendingOverflow,
    RecoveryRequired, BatchIdExhausted
};
enum class AckStatus : std::uint8_t { Applied, AlreadyApplied, InvalidBatch };
struct Config {
    std::uint32_t staleAfterMs = 5000;
    std::uint32_t maximumPendingSteps = 4096;
    std::uint8_t maximumStepsPerSecond = 4;
    // Hardware ped_sig_count can report several accumulated steps at once.
    // This is a plausibility allowance, not calibration or proof of actual gait.
    std::uint8_t reportBurstSteps = 8;
};
struct Batch {
    std::uint32_t sessionId = 0;
    std::uint32_t id = 0;
    std::uint16_t steps = 0;
};
struct Update {
    Status status = Status::AwaitingAnchor;
    std::uint32_t newSteps = 0;
    std::uint32_t pendingSteps = 0;
    std::uint64_t totalSteps = 0;
};

bool validConfig(const Config& config);
const char* statusText(Status status);

// Portable adapter for a cumulative 24-bit hardware count, not an accelerometer
// classifier. The HAL must supply a coherent register read; torn I2C reads cannot
// be repaired here. Serialize calls on one task; this object is not thread-safe.
// No SDK, pins, hardware configuration, heap, clock, or game rules.
//
// The caller supplies a fresh nonzero boot/session generation. Batches are RAM
// transactions: apply to the game durably before acknowledging. This alone does
// not make game-save + acknowledgement atomic across power loss. Never replay a
// previous boot's hardware total; first/reacquired samples establish an anchor.
// Long sleep/stale gaps deliberately discard unconfirmed sensor delta. A driver
// must signal known sensor resets via recover(), including resets near rollover.
// A reset very near 0xffffff cannot be distinguished from a plausible real wrap
// using counter values alone. Physical step accuracy has not been verified.
class MotionCounter {
public:
    explicit MotionCounter(std::uint32_t bootSessionId, Config config = {});
    Update observe(std::uint32_t counter24, std::uint64_t nowMs);
    Update poll(std::uint64_t nowMs);
    Update setPaused(bool paused, std::uint64_t nowMs);
    // After a diagnosed reset/jump/overflow, deliberately drop continuity while
    // retaining confirmed pending steps and any outstanding batch. The next
    // observation becomes an anchor; counts during the gap are not reconstructed.
    Update recover(std::uint64_t nowMs);
    // Repeated peeks return the same batch even if new steps arrive. No mutation
    // of confirmed/pending counts occurs until a matching acknowledgement.
    Batch peekBatch(std::uint16_t maximumSteps = kMaximumBatchSteps);
    AckStatus acknowledgeBatch(const Batch& batch);

private:
    Update update(std::uint32_t added = 0) const;
    bool acceptTime(std::uint64_t nowMs);
    void fault(Status status);

    Config config_{};
    std::uint32_t sessionId_ = 0;
    std::uint32_t nextBatchId_ = 1;
    std::uint32_t lastCounter_ = 0;
    std::uint32_t pending_ = 0;
    std::uint64_t total_ = 0;
    std::uint64_t lastNowMs_ = 0;
    std::uint64_t lastSampleMs_ = 0;
    std::uint32_t allowanceMilliSteps_ = 0;
    Batch activeBatch_{}, acknowledgedBatch_{};
    Status status_ = Status::AwaitingAnchor;
    bool configured_ = false;
    bool clockSeen_ = false;
    bool anchored_ = false;
    bool paused_ = false;
    bool recovery_ = false;
};
} // namespace digivice::motion
