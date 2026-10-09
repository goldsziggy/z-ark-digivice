#include "motion.hpp"

#include <cstdio>
#include <limits>

namespace {
using namespace digivice::motion;
unsigned checks = 0, failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (false)

void anchorAndBatches() {
    MotionCounter counter(17);
    auto state = counter.observe(12000, 0);
    CHECK(state.newSteps == 0 && state.pendingSteps == 0 && state.totalSteps == 0);
    CHECK(counter.peekBatch().steps == 0);
    state = counter.observe(12003, 1000);
    CHECK(state.newSteps == 3 && state.pendingSteps == 3 && state.totalSteps == 3);
    const auto first = counter.peekBatch(2);
    CHECK(first.sessionId == 17 && first.id == 1 && first.steps == 2);
    CHECK(counter.peekBatch(1000).steps == 2); // Changing requested size cannot change a staged batch.
    state = counter.observe(12006, 2000);
    CHECK(state.newSteps == 3 && state.pendingSteps == 6);
    auto wrong = first; ++wrong.steps;
    CHECK(counter.acknowledgeBatch(wrong) == AckStatus::InvalidBatch);
    wrong = first; ++wrong.sessionId;
    CHECK(counter.acknowledgeBatch(wrong) == AckStatus::InvalidBatch);
    CHECK(counter.poll(2000).pendingSteps == 6);
    CHECK(counter.acknowledgeBatch(first) == AckStatus::Applied);
    CHECK(counter.poll(2000).pendingSteps == 4);
    CHECK(counter.acknowledgeBatch(first) == AckStatus::AlreadyApplied);
    CHECK(counter.poll(2000).pendingSteps == 4);
    const auto second = counter.peekBatch();
    CHECK(second.id == 2 && second.steps == 4);
    CHECK(counter.acknowledgeBatch(first) == AckStatus::AlreadyApplied);
    CHECK(counter.acknowledgeBatch(second) == AckStatus::Applied);
    CHECK(counter.poll(2000).pendingSteps == 0 && counter.poll(2000).totalSteps == 6);
    CHECK(counter.peekBatch(0).steps == 0 && counter.peekBatch(1001).steps == 0);
    MotionCounter rebooted(18);
    rebooted.observe(12006, 0); rebooted.observe(12008, 1000);
    CHECK(rebooted.peekBatch().id == 1);
    CHECK(rebooted.acknowledgeBatch(first) == AckStatus::InvalidBatch);
    CHECK(rebooted.poll(1000).pendingSteps == 2);
}

void rolloverResetAndCadence() {
    MotionCounter counter(1);
    counter.observe(kCounterMask - 2, 0);
    auto state = counter.observe(2, 1000);
    CHECK(state.newSteps == 5 && state.totalSteps == 5 && state.status == Status::Tracking);
    state = counter.observe(2, 1100);
    CHECK(state.newSteps == 0 && state.pendingSteps == 5);
    state = counter.observe(10000, 1200);
    CHECK(state.status == Status::ImplausibleCount && state.newSteps == 0 && state.pendingSteps == 5);
    CHECK(counter.observe(10001, 2200).newSteps == 0); // Fault does not grant a new burst on every input.
    CHECK(counter.poll(2200).status == Status::ImplausibleCount);
    counter.recover(2200);
    CHECK(counter.observe(10001, 2200).newSteps == 0);
    CHECK(counter.observe(10003, 3200).newSteps == 2);
    state = counter.observe(0, 4200);
    CHECK(state.status == Status::CounterReset && state.pendingSteps == 7);
    CHECK(counter.peekBatch().steps == 7); // Previously confirmed work can still be committed.

    Config tight; tight.reportBurstSteps = 2;
    MotionCounter rapid(2, tight);
    rapid.observe(0, 0);
    CHECK(rapid.observe(2, 0).newSteps == 2);
    CHECK(rapid.observe(3, 0).status == Status::ImplausibleCount);
    CHECK(rapid.observe(4, 1000).newSteps == 0);
    CHECK(rapid.poll(1000).totalSteps == 2);

    MotionCounter batched(3);
    batched.observe(0, 0);
    CHECK(batched.observe(8, 1000).newSteps == 8); // Hardware may publish confirmed steps together.
    CHECK(batched.observe(16, 2000).newSteps == 8);
    CHECK(batched.observe(24, 3000).status == Status::ImplausibleCount); // Sustained 8 Hz exceeds 4 Hz policy.
}

void pauseStaleAndClock() {
    MotionCounter counter(4);
    counter.observe(10, 0);
    counter.observe(12, 1000);
    const auto batch = counter.peekBatch();
    CHECK(counter.setPaused(true, 1100).status == Status::Paused);
    CHECK(counter.observe(100, 10000).status == Status::Paused);
    CHECK(counter.setPaused(false, 10000).status == Status::AwaitingAnchor);
    CHECK(counter.observe(105, 10000).newSteps == 0);
    CHECK(counter.observe(107, 11000).newSteps == 2);
    CHECK(counter.acknowledgeBatch(batch) == AckStatus::Applied);
    CHECK(counter.poll(11000).pendingSteps == 2);
    CHECK(counter.poll(16001).status == Status::Stale);
    CHECK(counter.observe(130, 16002).newSteps == 0);
    CHECK(counter.observe(132, 17002).newSteps == 2);
    CHECK(counter.poll(17001).status == Status::TimeReversed);
    CHECK(counter.observe(133, 18000).newSteps == 0);
    CHECK(counter.poll(18000).totalSteps == 6);
    counter.recover(18000);
    CHECK(counter.observe(140, 18000).newSteps == 0);
    CHECK(counter.observe(142, 19000).newSteps == 2);
    CHECK(counter.poll(19000).totalSteps == 8);

    MotionCounter boundary(5);
    const auto last = std::numeric_limits<std::uint64_t>::max();
    boundary.observe(100, last - 1000);
    CHECK(boundary.observe(102, last).newSteps == 2);
    CHECK(boundary.observe(103, 0).status == Status::TimeReversed);
}

void limitsAndOverflow() {
    Config invalid; invalid.maximumStepsPerSecond = 0;
    CHECK(!validConfig(invalid));
    MotionCounter unusable(1, invalid), missingSession(0);
    CHECK(unusable.observe(0, 0).status == Status::InvalidConfig);
    CHECK(missingSession.observe(0, 0).status == Status::InvalidConfig);
    CHECK(unusable.peekBatch().steps == 0);
    MotionCounter counter(6);
    counter.observe(10, 0);
    CHECK(counter.observe(kCounterMask + 1, 100).status == Status::InvalidSample);
    CHECK(counter.observe(12, 1000).newSteps == 2);
    Config small; small.maximumPendingSteps = 5;
    MotionCounter full(7, small);
    full.observe(100, 0);
    CHECK(full.observe(105, 1000).newSteps == 5);
    CHECK(full.observe(106, 2000).status == Status::PendingOverflow);
    CHECK(full.poll(2000).pendingSteps == 5 && full.poll(2000).totalSteps == 5);
    const auto batch = full.peekBatch();
    CHECK(full.acknowledgeBatch(batch) == AckStatus::Applied);
    CHECK(full.observe(107, 3000).newSteps == 0); // Draining alone cannot retroactively repair continuity.
    full.recover(3000);
    CHECK(full.observe(107, 3000).newSteps == 0);
    CHECK(full.observe(109, 4000).newSteps == 2);
}

void longWalkAndBoundedBatches() {
    Config config; config.maximumPendingSteps = 10000;
    MotionCounter counter(8, config);
    counter.observe(90000, 0);
    for (std::uint32_t second = 1; second <= 600; ++second) {
        const auto state = counter.observe(90000 + second * 2, static_cast<std::uint64_t>(second) * 1000);
        CHECK(state.status == Status::Tracking && state.newSteps == 2);
    }
    const auto first = counter.peekBatch();
    CHECK(first.steps == 1000);
    CHECK(counter.acknowledgeBatch(first) == AckStatus::Applied);
    CHECK(counter.peekBatch().steps == 200);
    CHECK(counter.acknowledgeBatch(counter.peekBatch()) == AckStatus::Applied);
    CHECK(counter.poll(600000).pendingSteps == 0 && counter.poll(600000).totalSteps == 1200);
    for (unsigned i = 1; i <= 20; ++i) CHECK(counter.observe(91200, 600000 + i * 1000).newSteps == 0);
}
} // namespace

int main() {
    anchorAndBatches(); rolloverResetAndCadence(); pauseStaleAndClock(); limitsAndOverflow(); longWalkAndBoundedBatches();
    std::printf("MotionCounter: %u checks, %u failures; state=%zu bytes, batch=%zu bytes. Synthetic counter inputs only.\n",
                checks, failures, sizeof(MotionCounter), sizeof(Batch));
    return failures ? 1 : 0;
}
