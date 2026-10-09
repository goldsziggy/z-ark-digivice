#include "motion.hpp"

#include <limits>

namespace digivice::motion {
namespace {
bool equal(const Batch& a, const Batch& b) {
    return a.sessionId == b.sessionId && a.id == b.id && a.steps == b.steps;
}
}
bool validConfig(const Config& c) {
    return c.staleAfterMs >= 250 && c.staleAfterMs <= 60000 &&
        c.maximumPendingSteps >= 1 && c.maximumPendingSteps <= 1000000 &&
        c.maximumStepsPerSecond >= 1 && c.maximumStepsPerSecond <= 8 &&
        c.reportBurstSteps >= 1 && c.reportBurstSteps <= 32;
}
MotionCounter::MotionCounter(std::uint32_t bootSessionId, Config config)
    : config_(config), sessionId_(bootSessionId), configured_(bootSessionId != 0 && validConfig(config)) {
    if (!configured_) status_ = Status::InvalidConfig;
}
Update MotionCounter::update(std::uint32_t added) const { return {status_, added, pending_, total_}; }
void MotionCounter::fault(Status status) {
    status_ = status; recovery_ = true; anchored_ = false; allowanceMilliSteps_ = 0;
}
bool MotionCounter::acceptTime(std::uint64_t nowMs) {
    if (clockSeen_ && nowMs < lastNowMs_) { fault(Status::TimeReversed); return false; }
    clockSeen_ = true; lastNowMs_ = nowMs; return true;
}
Update MotionCounter::poll(std::uint64_t nowMs) {
    if (!configured_ || !acceptTime(nowMs)) return update();
    if (recovery_) return update();
    if (paused_) { status_ = Status::Paused; return update(); }
    if (anchored_ && nowMs - lastSampleMs_ > config_.staleAfterMs) {
        anchored_ = false; allowanceMilliSteps_ = 0; status_ = Status::Stale;
    }
    return update();
}
Update MotionCounter::observe(std::uint32_t counter24, std::uint64_t nowMs) {
    const auto current = poll(nowMs);
    if (!configured_ || recovery_ || paused_) return current;
    if (counter24 > kCounterMask) { status_ = Status::InvalidSample; return update(); }
    if (!anchored_) {
        lastCounter_ = counter24; lastSampleMs_ = nowMs;
        allowanceMilliSteps_ = static_cast<std::uint32_t>(config_.reportBurstSteps) * 1000;
        anchored_ = true; status_ = Status::Tracking;
        return update(); // Initial or recovered sensor history is never credited.
    }
    const auto elapsed = nowMs - lastSampleMs_;
    // Modulo subtraction admits a real 0xffffff -> 0 rollover. A reset normally
    // produces a huge delta and is rejected by the same bounded cadence budget.
    const auto delta = (counter24 - lastCounter_) & kCounterMask;
    const auto capacity = config_.staleAfterMs * config_.maximumStepsPerSecond +
                          static_cast<std::uint32_t>(config_.reportBurstSteps) * 1000;
    const auto accrued = static_cast<std::uint64_t>(allowanceMilliSteps_) + elapsed * config_.maximumStepsPerSecond;
    const auto allowance = accrued < capacity ? static_cast<std::uint32_t>(accrued) : capacity;
    if (static_cast<std::uint64_t>(delta) * 1000 > allowance) {
        fault(counter24 < lastCounter_ ? Status::CounterReset : Status::ImplausibleCount);
        return update();
    }
    if (delta > config_.maximumPendingSteps - pending_ || total_ > std::numeric_limits<std::uint64_t>::max() - delta) {
        fault(Status::PendingOverflow); return update();
    }
    lastCounter_ = counter24; lastSampleMs_ = nowMs;
    allowanceMilliSteps_ = allowance - delta * 1000;
    pending_ += delta; total_ += delta; status_ = Status::Tracking;
    return update(delta);
}
Update MotionCounter::setPaused(bool paused, std::uint64_t nowMs) {
    if (!configured_ || !acceptTime(nowMs)) return update();
    if (paused_ == paused) return poll(nowMs);
    paused_ = paused; anchored_ = false; allowanceMilliSteps_ = 0;
    if (!recovery_) status_ = paused ? Status::Paused : Status::AwaitingAnchor;
    return update();
}
Update MotionCounter::recover(std::uint64_t nowMs) {
    if (!configured_ || !acceptTime(nowMs)) return update();
    // Batch-id exhaustion cannot be cured by reusing an ID in the same session.
    if (status_ == Status::BatchIdExhausted) return update();
    recovery_ = false; anchored_ = false; allowanceMilliSteps_ = 0;
    status_ = paused_ ? Status::Paused : Status::AwaitingAnchor;
    return update();
}
Batch MotionCounter::peekBatch(std::uint16_t maximumSteps) {
    if (!configured_ || maximumSteps == 0 || maximumSteps > kMaximumBatchSteps) return {};
    if (activeBatch_.steps) return activeBatch_;
    if (!pending_) return {};
    if (!nextBatchId_) { fault(Status::BatchIdExhausted); return {}; }
    const auto count = pending_ < maximumSteps ? pending_ : maximumSteps;
    activeBatch_ = {sessionId_, nextBatchId_, static_cast<std::uint16_t>(count)};
    return activeBatch_;
}
AckStatus MotionCounter::acknowledgeBatch(const Batch& batch) {
    if (!configured_ || batch.sessionId != sessionId_ || !batch.id || !batch.steps || batch.steps > kMaximumBatchSteps)
        return AckStatus::InvalidBatch;
    if (equal(batch, acknowledgedBatch_)) return AckStatus::AlreadyApplied;
    if (!equal(batch, activeBatch_) || batch.steps > pending_) return AckStatus::InvalidBatch;
    pending_ -= batch.steps;
    acknowledgedBatch_ = batch; activeBatch_ = {};
    nextBatchId_ = batch.id == std::numeric_limits<std::uint32_t>::max() ? 0 : batch.id + 1;
    return AckStatus::Applied;
}
const char* statusText(Status status) {
    switch (status) {
    case Status::AwaitingAnchor: return "waiting for current hardware count";
    case Status::Tracking: return "hardware count tracking";
    case Status::Paused: return "motion counting paused";
    case Status::Stale: return "motion sample stale; next sample anchors";
    case Status::InvalidConfig: return "invalid motion counter configuration";
    case Status::InvalidSample: return "hardware count is not 24-bit";
    case Status::TimeReversed: return "monotonic time reversed; recovery required";
    case Status::CounterReset: return "hardware counter reset detected; recovery required";
    case Status::ImplausibleCount: return "implausible step count; recovery required";
    case Status::PendingOverflow: return "pending steps full; drain and recover";
    case Status::RecoveryRequired: return "motion continuity requires recovery";
    case Status::BatchIdExhausted: return "batch IDs exhausted; new session required";
    }
    return "unknown motion status";
}
} // namespace digivice::motion
