#include "power.hpp"
#include <cstdio>
#include <initializer_list>
#include <limits>

namespace {
using namespace digivice::power;
unsigned checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__, #x); } } while (false)

struct Rig {
    Controller c;
    Input in{};
    Actions last{};
    unsigned starts = 0, saves = 0, cuts = 0, resumes = 0;
    Actions tick(std::uint64_t delta = 0) {
        in.nowMs += delta;
        last = c.tick(in);
        starts += last.beginShutdown; saves += last.save; cuts += last.cutLatch; resumes += last.resume;
        CHECK(!(last.cutLatch && (last.save || last.resume || last.beginShutdown)));
        return last;
    }
    void advance(std::uint64_t duration) {
        while (duration) { const auto delta = duration > 10 ? 10 : duration; tick(delta); duration -= delta; }
    }
    void arm() { tick(); advance(kDebounceMs); CHECK(c.status().phase == Phase::Ready); }
    void press() { in.pressed = true; tick(1); advance(kDebounceMs); }
    void release() { in.pressed = false; tick(1); advance(kDebounceMs); }
    void shutdown() {
        arm(); press(); CHECK(c.status().phase == Phase::Holding);
        advance(kHoldMs); CHECK(c.status().phase == Phase::Draining && starts == 1);
    }
    void saved() {
        shutdown(); in.work = Work::Ready;
        CHECK(tick(1).save && c.status().phase == Phase::Saving);
        in.work = Work::Pending; advance(100);
        in.work = Work::Ready; tick(1); in.work = Work::Pending;
        CHECK(c.status().phase == Phase::AwaitRelease && cuts == 0);
    }
    void standby() {
        saved(); release(); CHECK(last.cutLatch);
        in.latchDropped = true; tick(1); in.latchDropped = false;
        CHECK(c.status().phase == Phase::QuietStandby);
        advance(kDebounceMs);
    }
};

void initialReleaseAndCountdown() {
    Rig r; r.in.pressed = true; r.tick(); r.advance(6000);
    CHECK(r.c.status().phase == Phase::AwaitInitialRelease && r.starts == 0);
    r.release(); CHECK(r.c.status().phase == Phase::Ready);
    r.in.pressed = true; r.tick(1); r.advance(29);
    CHECK(r.c.status().phase == Phase::Ready && !r.c.frozen());
    r.tick(1); CHECK(r.c.status().countdownSeconds == 3);
    r.advance(999); CHECK(r.c.status().countdownSeconds == 3);
    r.tick(1); CHECK(r.c.status().countdownSeconds == 2);
    r.advance(1000); CHECK(r.c.status().countdownSeconds == 1);
    r.advance(999); CHECK(r.starts == 0 && !r.c.frozen());
    CHECK(r.tick(1).beginShutdown && r.c.frozen());
    r.advance(100); CHECK(r.starts == 1 && r.saves == 0 && r.cuts == 0);
    Controller reboot;
    CHECK(!reboot.tick({r.in.nowMs, true}).beginShutdown);
    CHECK(reboot.status().phase == Phase::AwaitInitialRelease);
}

void releaseWinsAndBounce() {
    // Any sampled release before OR exactly on the threshold wins, even 1 ms.
    for (const auto releaseAt : {0U, 1U, 999U, 1000U, 2999U, 3000U}) {
        Rig r; r.arm(); r.press();
        if (releaseAt) r.advance(releaseAt - 1);
        r.in.pressed = false; r.tick(releaseAt ? 1 : 0);
        CHECK(r.c.status().phase == Phase::AwaitInitialRelease && r.starts == 0);
        r.in.pressed = true; r.tick(1); r.advance(4000);
        CHECK(r.starts == 0); // Cannot accumulate separate holds through bounce.
        r.release(); r.press(); r.advance(kHoldMs);
        CHECK(r.starts == 1);
    }
    Rig r; r.arm();
    for (unsigned i = 0; i < 100; ++i) {
        r.in.pressed = true; r.tick(1); r.advance(29);
        r.in.pressed = false; r.tick(1); r.advance(29);
        CHECK(r.c.status().phase == Phase::Ready && r.starts == 0);
    }
}

void pipelineReleaseRaceAndStandby() {
    Rig r; r.shutdown();
    r.release(); r.advance(400); // Early release cannot bypass drain/save.
    CHECK(r.cuts == 0 && r.saves == 0);
    r.in.work = Work::Ready; CHECK(r.tick(1).save);
    r.in.work = Work::Pending; r.advance(100); CHECK(r.saves == 1);
    r.in.work = Work::Ready; r.tick(1); r.in.work = Work::Pending;
    CHECK(r.c.status().phase == Phase::AwaitRelease);
    CHECK(r.tick(1).cutLatch); // Adapter may deny if raw hardware changed.
    r.in.pressed = true; CHECK(!r.tick(1).cutLatch);
    r.advance(50); CHECK(r.c.status().phase == Phase::AwaitRelease);
    r.in.pressed = false; r.tick(1); r.advance(29); CHECK(!r.last.cutLatch);
    CHECK(r.tick(1).cutLatch);
    CHECK(r.tick(1).cutLatch); // Repeat until acknowledged; no repeated save.
    r.in.latchDropped = true; r.in.pressed = true; r.tick(1);
    CHECK(r.c.status().phase == Phase::QuietStandby && r.saves == 1);
    r.in.latchDropped = false; r.advance(3500); CHECK(r.resumes == 0);
    r.release(); CHECK(r.resumes == 0); // Held-at-cut does not count as wake.
    r.press(); CHECK(r.resumes == 0);
    r.release(); CHECK(r.resumes == 1 && r.c.status().phase == Phase::Ready && !r.c.frozen());
    r.advance(100); CHECK(r.resumes == 1 && r.starts == 1);
    r.press(); r.advance(kHoldMs); CHECK(r.starts == 2); // Resume can shut down again.

    Rig held; held.saved(); held.advance(20000);
    CHECK(held.c.status().phase == Phase::AwaitRelease && held.cuts == 0);
    held.release(); CHECK(held.last.cutLatch);

    Rig stale; stale.arm(); stale.in.latchDropped = true; stale.tick(1);
    CHECK(stale.c.status().phase == Phase::Ready && stale.resumes == 0);
}

void failuresAndBusyResume() {
    for (const auto stage : {Phase::Draining, Phase::Saving, Phase::AwaitRelease}) {
        Rig r; r.shutdown();
        if (stage != Phase::Draining) { r.in.work = Work::Ready; r.tick(1); }
        if (stage == Phase::AwaitRelease) r.tick(1);
        r.in.work = Work::Failed; r.tick(1);
        CHECK(r.c.status().phase == Phase::Failed && r.c.frozen() && r.cuts == 0);
        CHECK(r.c.status().failure == (stage == Phase::Draining ? Failure::DrainFailed :
              stage == Phase::Saving ? Failure::SaveFailed : Failure::LatchFailed));
        r.in.work = Work::Ready; r.advance(100);
        CHECK(r.c.status().phase == Phase::Failed && r.cuts == 0);
        r.in.workIdle = false; r.release(); r.press(); r.release();
        CHECK(r.resumes == 0);
        r.in.workIdle = true; r.advance(100); CHECK(r.resumes == 0); // No queued wake.
        r.press(); r.release();
        CHECK(r.resumes == 1 && r.c.status().phase == Phase::Ready && r.c.status().failure == Failure::None);
    }
    for (bool duringSave : {false, true}) {
        Rig r; r.shutdown();
        if (duringSave) { r.in.work = Work::Ready; r.tick(1); r.in.work = Work::Pending; }
        r.advance(kShutdownTimeoutMs - (duringSave ? 1 : 0));
        CHECK(r.c.status().phase == Phase::Failed && r.c.status().failure == Failure::Timeout && r.cuts == 0);
    }
    Rig fault; fault.arm(); fault.in.hardwareFault = true; fault.tick(1);
    CHECK(fault.c.status().failure == Failure::Hardware && fault.c.frozen());
    fault.press(); fault.release(); CHECK(fault.resumes == 0 && fault.cuts == 0);
    fault.in.hardwareFault = false; fault.advance(30); fault.press(); fault.release();
    CHECK(fault.resumes == 1);
}

void clockAndGapSafety() {
    for (const auto delay : {kMaxSampleGapMs + 1, kHoldMs, std::uint64_t{1000000}}) {
        Rig r; r.arm(); r.press(); r.advance(1000); r.tick(delay);
        CHECK(r.c.status().phase == Phase::AwaitInitialRelease && r.starts == 0);
        r.advance(4000); CHECK(r.starts == 0);
    }
    Rig within; within.arm(); within.press();
    for (unsigned i = 0; i < 12; ++i) within.tick(kMaxSampleGapMs);
    CHECK(within.starts == 1); // Exact max allowed sample gap is accepted.

    Rig reversed; reversed.arm(); reversed.press(); reversed.advance(100);
    reversed.in.nowMs = 0; reversed.tick();
    CHECK(reversed.c.status().phase == Phase::AwaitInitialRelease && reversed.starts == 0);
    Rig saveClock; saveClock.shutdown(); saveClock.in.nowMs = 0; saveClock.tick();
    CHECK(saveClock.c.status().failure == Failure::ClockReversed && saveClock.cuts == 0);

    Rig slowSave; slowSave.shutdown(); slowSave.in.work = Work::Ready; slowSave.tick(1);
    slowSave.in.work = Work::Pending; slowSave.tick(1000);
    CHECK(slowSave.c.status().phase == Phase::Saving); // Slow worker isn't a gesture gap failure.
    slowSave.in.work = Work::Ready; slowSave.tick(1); slowSave.in.work = Work::Pending;
    CHECK(slowSave.c.status().phase == Phase::AwaitRelease && slowSave.cuts == 0);

    Rig wakeGap; wakeGap.standby(); wakeGap.press(); wakeGap.tick(1000); wakeGap.release();
    CHECK(wakeGap.resumes == 0); wakeGap.press(); wakeGap.release(); CHECK(wakeGap.resumes == 1);

    Rig overflow; overflow.in.nowMs = std::numeric_limits<std::uint64_t>::max() - 100;
    overflow.arm(); overflow.press(); overflow.in.nowMs = 0; overflow.tick();
    CHECK(overflow.c.status().phase == Phase::AwaitInitialRelease && overflow.starts == 0);
}
} // namespace

int main() {
    initialReleaseAndCountdown(); releaseWinsAndBounce(); pipelineReleaseRaceAndStandby();
    failuresAndBusyResume(); clockAndGapSafety();
    std::printf("Power: %u checks, %u failures; controller=%zu bytes. Simulated PWR/storage only; no bench claim.\n",
                checks, failures, sizeof(Controller));
    return failures ? 1 : 0;
}
