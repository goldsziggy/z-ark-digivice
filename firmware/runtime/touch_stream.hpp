#pragma once

// Portable per-sample touch pipeline shared by the handheld runtime and host
// tests. It turns SPD2010 poll results into Down/Move/Up for deviceui.
//
// The firmware polls the SPD2010 (every 20 ms, 5 ms during capture) instead of
// waiting for its interrupt as the vendor demo does. What a poll reads between
// controller scans is not benchmarked, so the stream is written to be correct
// for every plausible behaviour (tests/touch_waveform_test.cpp models them):
//
//   latched  - one report waits until the host clears it
//   level    - a read reflects the current contact only
//   chatter  - a read between scans decodes as "running, no data" = fresh
//              release, even while the finger is still down
//
// Two options differ from the old inline handheld loop (zero = old behaviour):
//   releaseConfirmMs - Up is sent only after no pressed report for this long,
//                      so between-scan "release" reads under a held finger
//                      never chatter Up/Down.
//   rollOffMs        - Moves are held back one sample; on release, a last sample
//                      this close to the lift is roll-off (the contact centroid
//                      slides as the finger peels away) and is dropped, so a
//                      clean tap is not cancelled by its own lift.
//   quietRearmMs     - after requireRelease() (wake, setup close, resume, bus
//                      error) the latch also clears once no contact at all has
//                      been seen for this long, so a controller that never sends
//                      an explicit release report cannot eat the next tap. A
//                      finger resting on the glass keeps it latched.
#include "device_ui.hpp"
#include <cstdint>

namespace digivice::touchstream {

struct Sample {
    bool ok = true;          // pollTouch succeeded (false = bus/protocol failure)
    bool pressed = false;
    bool fresh = false;      // decoded new report, not the cached level
    std::int16_t x = 0, y = 0;
    std::uint64_t atMs = 0;
    bool blanked = false;    // screen idle-blanked when sampled
};

struct Options {
    std::uint32_t releaseConfirmMs = 25;  // > one SPD2010 scan at 60 Hz (16.7 ms)
    std::uint32_t quietRearmMs = 150;     // > several scans with no contact
    std::uint32_t rollOffMs = 30;         // one 20 ms poll plus margin
};

struct Step {
    bool cancel = false;     // caller cancels in-progress UI/setup touch
    bool released = false;   // contact latch cleared (deviceui::acknowledgeContactReleased)
    bool inferred = false;   // released by quiet rearm, not an observed release report
    bool activity = false;   // fresh press: keeps the screen awake
    bool hasEvent = false;
    deviceui::Touch event{deviceui::TouchKind::Move, 0, 0, 0};
};

class Stream {
public:
    explicit Stream(Options options = {}) : options_(options) {}
    Step feed(const Sample& sample);
    // Ignore contact until a release is observed or the glass stays quiet.
    void requireRelease(std::uint64_t nowMs);
    bool pressed() const { return pressed_; }
    bool awaitingRelease() const { return needsRelease_; }
    std::int16_t x() const { return x_; }
    std::int16_t y() const { return y_; }
    std::uint32_t presses() const { return presses_; }
    std::uint32_t releases() const { return releases_; }
    std::uint32_t chatter() const { return chatter_; }   // release reads later contradicted by a press
    std::uint32_t rearms() const { return rearms_; }     // latches cleared by quiet rearm
    std::uint32_t rolledOff() const { return rolledOff_; } // lift samples dropped as roll-off

private:
    Options options_;
    bool pressed_ = false, needsRelease_ = true, releasing_ = false;
    std::uint64_t lastPressedMs_ = 0, releaseSeenMs_ = 0, quietSinceMs_ = 0;
    std::int16_t x_ = 0, y_ = 0;
    std::uint32_t presses_ = 0, releases_ = 0, chatter_ = 0, rearms_ = 0, rolledOff_ = 0;
    bool held_ = false;                       // one pressed sample waiting to become a Move
    std::int16_t heldX_ = 0, heldY_ = 0;
    std::uint64_t heldAtMs_ = 0;
};

} // namespace digivice::touchstream
