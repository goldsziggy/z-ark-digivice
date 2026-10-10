#include "touch_stream.hpp"

namespace digivice::touchstream {

void Stream::requireRelease(std::uint64_t nowMs) {
    needsRelease_ = true; pressed_ = releasing_ = held_ = false; quietSinceMs_ = nowMs;
}

Step Stream::feed(const Sample& sample) {
    Step step;
    if (!sample.ok) {
        // Bus failure is cancellation, never a release/confirmation.
        step.cancel = true; requireRelease(sample.atMs); return step;
    }
    if (sample.blanked) {
        // Waking is not a button press, attack, or confirmation.
        if (sample.fresh && sample.pressed) step.activity = true;
        step.cancel = true; requireRelease(sample.atMs); return step;
    }
    if (needsRelease_) {
        // A fresh pressed report means a finger is on the glass; a stale cached level does not.
        if (sample.pressed && sample.fresh) { quietSinceMs_ = sample.atMs; return step; }
        const bool observed = !sample.pressed && sample.fresh;
        const bool quiet = options_.quietRearmMs && sample.atMs >= quietSinceMs_ &&
                           sample.atMs - quietSinceMs_ >= options_.quietRearmMs;
        if (observed || quiet) {
            step.released = true; step.inferred = !observed;
            if (!observed) ++rearms_;
            needsRelease_ = false; pressed_ = releasing_ = held_ = false;
        }
        return step;
    }
    if (!sample.pressed && !pressed_) return step;
    if (sample.pressed) {
        if (!pressed_ && !sample.fresh) return step; // a Down needs a new report, never a stale cached level
        if (sample.fresh) step.activity = true;
        if (releasing_) { releasing_ = false; ++chatter_; } // between-scan read, finger still down
        lastPressedMs_ = sample.atMs;
        if (!pressed_) {
            x_ = sample.x; y_ = sample.y; held_ = false;
            step.hasEvent = true; step.event = {deviceui::TouchKind::Down, x_, y_, sample.atMs};
            ++presses_; pressed_ = true;
            return step;
        }
        if (!options_.rollOffMs) {   // legacy: every held sample is an immediate Move
            x_ = sample.x; y_ = sample.y;
            step.hasEvent = true; step.event = {deviceui::TouchKind::Move, x_, y_, sample.atMs};
            return step;
        }
        // Emit the previously held sample; hold this one until the next sample shows it is not roll-off.
        if (held_) {
            x_ = heldX_; y_ = heldY_;
            step.hasEvent = true; step.event = {deviceui::TouchKind::Move, x_, y_, heldAtMs_};
        }
        held_ = true; heldX_ = sample.x; heldY_ = sample.y; heldAtMs_ = sample.atMs;
        return step;
    }
    // Not pressed while held: confirm before sending Up (stale cached level reads count too).
    if (!releasing_) { releasing_ = true; releaseSeenMs_ = sample.atMs; }
    if (sample.atMs >= lastPressedMs_ && sample.atMs - lastPressedMs_ < options_.releaseConfirmMs) return step;
    releasing_ = false; pressed_ = false; ++releases_;
    if (held_) {   // the last held sample becomes the lift position unless it is roll-off
        if (releaseSeenMs_ >= heldAtMs_ && releaseSeenMs_ - heldAtMs_ < options_.rollOffMs) ++rolledOff_;
        else { x_ = heldX_; y_ = heldY_; }
        held_ = false;
    }
    step.hasEvent = true;
    step.event = {deviceui::TouchKind::Up, x_, y_, releaseSeenMs_};
    return step;
}

} // namespace digivice::touchstream
