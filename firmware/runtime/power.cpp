#include "power.hpp"

namespace digivice::power {

bool Controller::frozen() const {
    return status_.phase == Phase::Draining || status_.phase == Phase::Saving ||
           status_.phase == Phase::AwaitRelease || status_.phase == Phase::QuietStandby ||
           status_.phase == Phase::Failed;
}

void Controller::resetWake(std::uint64_t nowMs) {
    wakeArmed_ = false;
    wakePressed_ = false;
    rawSinceMs_ = nowMs;
}

void Controller::fail(Failure failure, std::uint64_t nowMs) {
    status_ = {Phase::Failed, failure, 0};
    cutRequested_ = false;
    resetWake(nowMs);
}

Actions Controller::tick(const Input& input) {
    Actions actions{};
    const bool reversed = initialized_ && input.nowMs < lastMs_;
    const bool gap = initialized_ && !reversed && input.nowMs - lastMs_ > kMaxSampleGapMs;
    if (!initialized_ || input.pressed != rawPressed_ || reversed || gap) {
        rawSinceMs_ = input.nowMs;
        rawPressed_ = input.pressed;
    }
    initialized_ = true;
    lastMs_ = input.nowMs;

    if (input.hardwareFault) {
        fail(Failure::Hardware, input.nowMs);
        return actions;
    }
    if (reversed) {
        if (frozen()) fail(Failure::ClockReversed, input.nowMs);
        else status_ = {Phase::AwaitInitialRelease, Failure::None, 0};
        return actions;
    }

    // A prior physical cut can be acknowledged even if PWR has since changed.
    // A spontaneous/stale acknowledgment in any other phase has no authority.
    if (status_.phase == Phase::AwaitRelease && cutRequested_ && input.latchDropped) {
        status_ = {Phase::QuietStandby, Failure::None, 0};
        cutRequested_ = false;
        resetWake(input.nowMs);
        return actions;
    }

    if (gap) {
        if (status_.phase == Phase::Holding || status_.phase == Phase::Ready) {
            status_ = {Phase::AwaitInitialRelease, Failure::None, 0};
        }
        if (status_.phase == Phase::QuietStandby || status_.phase == Phase::Failed)
            resetWake(input.nowMs);
    }
    const bool stable = input.nowMs - rawSinceMs_ >= kDebounceMs;

    // Shutdown writes are bounded separately from gesture sampling. A slow save
    // cannot itself become evidence of a held button or an automatic wake.
    if ((status_.phase == Phase::Draining || status_.phase == Phase::Saving) &&
        input.nowMs - shutdownSinceMs_ >= kShutdownTimeoutMs) {
        fail(Failure::Timeout, input.nowMs);
        return actions;
    }

    switch (status_.phase) {
    case Phase::AwaitInitialRelease:
        if (!input.pressed && stable) status_.phase = Phase::Ready;
        break;
    case Phase::Ready:
        if (input.pressed && stable) {
            status_.phase = Phase::Holding;
            status_.countdownSeconds = 3;
            holdSinceMs_ = input.nowMs;
        }
        break;
    case Phase::Holding:
        // A RAW release wins over the threshold, including on the threshold
        // tick. Never credit a release/debounce window toward a continuous hold.
        if (!input.pressed) {
            status_ = {Phase::AwaitInitialRelease, Failure::None, 0};
        } else if (input.nowMs - holdSinceMs_ >= kHoldMs) {
            status_ = {Phase::Draining, Failure::None, 0};
            shutdownSinceMs_ = input.nowMs;
            actions.beginShutdown = true;
        } else {
            status_.countdownSeconds = static_cast<std::uint8_t>(
                (kHoldMs - (input.nowMs - holdSinceMs_) + 999) / 1000);
        }
        break;
    case Phase::Draining:
        if (input.work == Work::Failed) fail(Failure::DrainFailed, input.nowMs);
        else if (input.work == Work::Ready) {
            status_.phase = Phase::Saving;
            actions.save = true;
        }
        break;
    case Phase::Saving:
        if (input.work == Work::Failed) fail(Failure::SaveFailed, input.nowMs);
        else if (input.work == Work::Ready) status_.phase = Phase::AwaitRelease;
        break;
    case Phase::AwaitRelease:
        if (input.work == Work::Failed) fail(Failure::LatchFailed, input.nowMs);
        else {
            actions.cutLatch = !input.pressed && stable;
            cutRequested_ = actions.cutLatch;
        }
        break;
    case Phase::QuietStandby:
    case Phase::Failed:
        if (!input.workIdle) {
            // Never queue a wake gesture while a timed-out worker still owns I/O.
            resetWake(input.nowMs);
        } else if (!wakeArmed_) {
            if (!input.pressed && stable) wakeArmed_ = true;
        } else if (!wakePressed_) {
            if (input.pressed && stable) wakePressed_ = true;
        } else if (!input.pressed && stable && input.workIdle) {
            status_ = {Phase::Ready, Failure::None, 0};
            wakeArmed_ = false;
            wakePressed_ = false;
            actions.resume = true;
        }
        break;
    }
    return actions;
}

const char* phaseName(Phase phase) {
    switch (phase) {
    case Phase::AwaitInitialRelease: return "release PWR to arm";
    case Phase::Ready: return "ready";
    case Phase::Holding: return "hold PWR to shut down";
    case Phase::Draining: return "finishing pending work";
    case Phase::Saving: return "saving";
    case Phase::AwaitRelease: return "saved; release PWR to power off";
    case Phase::QuietStandby: return "quiet standby; power source unknown";
    case Phase::Failed: return "shutdown blocked; power held on";
    }
    return "unknown";
}

const char* failureText(Failure failure) {
    switch (failure) {
    case Failure::None: return "none";
    case Failure::DrainFailed: return "pending work could not finish";
    case Failure::SaveFailed: return "save failed; press and release PWR to resume for recovery";
    case Failure::LatchFailed: return "power latch operation failed";
    case Failure::Hardware: return "power GPIO operation failed";
    case Failure::Timeout: return "shutdown work timed out";
    case Failure::ClockReversed: return "power clock moved backward";
    }
    return "unknown";
}

} // namespace digivice::power
