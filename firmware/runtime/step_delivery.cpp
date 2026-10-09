#include "step_delivery.hpp"

namespace digivice::motion {
bool countRequiresRecovery(Status status) {
    switch(status) {
    case Status::InvalidConfig: case Status::TimeReversed: case Status::CounterReset:
    case Status::ImplausibleCount: case Status::PendingOverflow:
    case Status::RecoveryRequired: case Status::BatchIdExhausted: return true;
    default: return false;
    }
}
Delivery StepDelivery::pump(MotionCounter& counter, State& state, storage::SaveStore& saves,
                            std::uint64_t nowMs, bool permitted, bool force) {
    if(halted_ || !saves.writable()) { halted_=true; return {DeliveryResult::SaveRecovery}; }
    const auto update=counter.poll(nowMs);
    if(!update.pendingSteps) return {};
    if(!permitted || !state.onboardingComplete ||
       (state.phase!=Phase::Home && state.phase!=Phase::Encounter)) return {DeliveryResult::Blocked};
    const bool intervalDue=nowMs>=lastSaveMs_ && nowMs-lastSaveMs_>=kDeliveryIntervalMs;
    // A fault retains already-confirmed pending deltas. Drain them durably so a
    // later explicit reanchor can resume without discarding confirmed steps.
    if(!force && update.pendingSteps<kDeliveryBatchSteps && !intervalDue && !countRequiresRecovery(update.status))
        return {DeliveryResult::Deferred};
    const auto batch=counter.peekBatch(kDeliveryBatchSteps);
    if(!batch.steps) return {};
    State candidate=state;
    const auto error=apply(candidate,Action::Walk,batch.steps);
    if(error!=Error::None) { halted_=true; return {DeliveryResult::CoreRejected,error}; }
    if(!saves.checkpoint(candidate)) { halted_=true; return {DeliveryResult::SaveRecovery}; }
    state=candidate;
    if(counter.acknowledgeBatch(batch)!=AckStatus::Applied) { halted_=true; return {DeliveryResult::AckRecovery}; }
    lastSaveMs_=nowMs;
    return {DeliveryResult::Committed,Error::None,batch.steps};
}
bool StepDelivery::reanchor(MotionCounter& counter,const storage::SaveStore& saves,std::uint64_t nowMs) {
    const auto update=counter.poll(nowMs);
    if(halted_ || !saves.writable() || update.pendingSteps || !countRequiresRecovery(update.status) ||
       update.status==Status::InvalidConfig || update.status==Status::BatchIdExhausted) return false;
    return counter.recover(nowMs).status==Status::AwaitingAnchor;
}
} // namespace digivice::motion
