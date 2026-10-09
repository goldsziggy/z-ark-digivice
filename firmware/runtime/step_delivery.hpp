#pragma once
#include "game.hpp"
#include "motion.hpp"
#include "save_store.hpp"
#include <cstdint>

namespace digivice::motion {
constexpr std::uint16_t kDeliveryBatchSteps = 100;
constexpr std::uint64_t kDeliveryIntervalMs = 30000;
enum class DeliveryResult : std::uint8_t { Idle, Deferred, Blocked, Committed, CoreRejected, SaveRecovery, AckRecovery };
struct Delivery { DeliveryResult result=DeliveryResult::Idle; Error coreError=Error::None; std::uint16_t steps=0; };
// True for continuity/configuration faults that require deliberate recovery.
// A stale sample is different: the next coherent observation anchors normally.
bool countRequiresRecovery(Status status);

// Main-task-only delivery of already-confirmed hardware deltas. No sensor, clock,
// network, heap, or synthetic steps. The caller supplies practice/input guards.
// Home and Encounter use the same native Walk action. Commit before RAM/ack;
// after uncertain save/ack never retry this session automatically.
class StepDelivery {
public:
    explicit StepDelivery(std::uint64_t nowMs=0) : lastSaveMs_(nowMs) {}
    // force bypasses only the batching timer/minimum, for orderly shutdown.
    // Existing permission, phase, onboarding, writable and halted guards remain.
    Delivery pump(MotionCounter& counter, State& state, storage::SaveStore& saves,
                  std::uint64_t nowMs, bool permitted, bool force = false);
    bool reanchor(MotionCounter& counter, const storage::SaveStore& saves, std::uint64_t nowMs);
    bool halted() const { return halted_; }
private:
    std::uint64_t lastSaveMs_=0;
    bool halted_=false;
};
} // namespace digivice::motion
