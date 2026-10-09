#include "handheld_runtime.hpp"
#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstdio>

namespace digivice {
bool HandheldRuntime::physicalStepsReady(std::uint64_t now) const {
    const auto sample = imu_.stepReading();
    return usage_.writable() && !walkingFault_ && imu_.ready() &&
        (sample.status == motion::StepStatus::Priming || sample.status == motion::StepStatus::Tracking) &&
        now >= sample.observedAtMs && now - sample.observedAtMs <= 500;
}
bool HandheldRuntime::pollUsage(std::uint64_t now, bool force) {
    // Let the orderly forced drain own the final accepted sampler delta.
    // A routine paused poll must not consume it before quiescence is reached.
    if (interfacePaused_ && !force) return true;
    const auto sample = imu_.stepReading();
    // The worker can publish after the caller captured now; use the same
    // coherent sample for counting and eligibility, never a second read.
    now = std::max(now, sample.observedAtMs);
    const auto added = usage_.observe(sample.acceptedSteps);
    if (!usage_.writable() || walkingFault_) return false;
    // Every fresh post-starter step counts, including menus and active matches.
    // Invalid/stale sensor, paused and power-off gaps remain lifetime-only.
    const bool fresh = now - sample.observedAtMs <= 500;
    const bool finalAccepted = force && interfacePaused_ && sample.status == motion::StepStatus::Paused &&
        fresh && interfaceQuiescent(); // Worker published its final accepted pre-pause count.
    const bool eligible = state_.onboardingComplete && saves_.writable() && fresh &&
        (finalAccepted || (!powerFrozen() && !interfacePaused_ && imu_.ready() &&
        (sample.status == motion::StepStatus::Priming || sample.status == motion::StepStatus::Tracking)));
    if (!state_.onboardingComplete || state_.encounterRate == EncounterRate::Off) walkingPending_ = 0;
    if (eligible && state_.encounterRate != EncounterRate::Off)
        walkingPending_ = std::min<std::uint32_t>(1000, walkingPending_ + std::min<std::uint32_t>(added, 1000));
    if (!usage_.checkpoint(now, force)) { walkingFault_ = true; return false; }
    // Only presentation waits for Home. Background checkpoints preserve the
    // current fight, held touch and menu; one persisted slot cannot pile up.
    const bool quietHome = !powerFrozen() && !interfacePaused_ &&
        state_.phase == Phase::Home && state_.onboardingComplete &&
        ui_.encounterPresentationEligible() && ui_.interactionIdle() && !touchPressed_ &&
        !setup_.active() && !battle_.locked() && !nearbyBusy() &&
        practice_.allowsCareAction(Action::Explore);
    auto publishBackground = [&](const State& candidate, Action action, std::uint32_t value) {
        if (!saves_.checkpoint(candidate)) { walkingFault_ = true; return false; }
        const bool acknowledged = ui_.acknowledgeWalking(state_, candidate, action, value);
        // Do not swallow a foreground cue that pollInterface has not seen yet.
        if (acknowledged && uiSequence_ == state_.sequence) uiSequence_ = candidate.sequence;
        state_ = candidate; interfaceDirty_ = true;
        return true;
    };
    // Flush accepted tails during orderly shutdown too. No presentation is
    // allowed while paused/frozen, but the queued encounter is durable.
    if (walkingPending_ && state_.onboardingComplete && saves_.writable() &&
        (eligible || force || (quietHome && state_.pendingEncounter.formId))) {
        // Device entropy is committed once and never rerolled on a retry.
        if (!state_.encounterRng) {
            State seeded = state_;
            auto seed = esp_random(); if (!seed) seed = 1;
            if (apply(seeded, Action::EncounterSeed, seed) != Error::None ||
                !publishBackground(seeded, Action::EncounterSeed, seed)) {
                walkingFault_ = true; return false;
            }
        }
        State candidate = state_;
        if (apply(candidate, Action::AccrueSteps, walkingPending_) != Error::None) {
            walkingFault_ = true; return false;
        }
        const bool earned = !state_.pendingEncounter.formId && candidate.pendingEncounter.formId;
        // Drain steps taken while the slot was full before consuming it; they
        // must not be credited retroactively toward a second encounter.
        const bool due = force || earned || (quietHome && state_.pendingEncounter.formId) || walkingPending_ >= 64 ||
            now < lastWalkingSaveMs_ || now - lastWalkingSaveMs_ >= 30000;
        if (due) {
            if (!usage_.checkpoint(now, true) || !publishBackground(candidate, Action::AccrueSteps, walkingPending_)) {
                walkingFault_ = true; return false;
            }
            walkingPending_ = 0; lastWalkingSaveMs_ = now;
        }
    }
    if (!quietHome || !state_.pendingEncounter.formId || walkingPending_ || !saves_.writable()) return true;
    State candidate = state_;
    if (apply(candidate, Action::PresentEncounter) != Error::None) { walkingFault_ = true; return false; }
    if (!usage_.checkpoint(now, true) || !saves_.checkpoint(candidate)) { walkingFault_ = true; return false; }
    state_ = candidate; lastWalkingSaveMs_ = now;
    interfaceDirty_ = true;
    return true;
}
bool HandheldRuntime::prepareUsageRestart() {
    pauseInterface(true);
    for (unsigned i = 0; i < 25 && !interfaceQuiescent(); ++i) vTaskDelay(pdMS_TO_TICKS(10));
    const auto now = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
    if (interfaceQuiescent() && (!usage_.writable() || walkingFault_)) {
        std::puts("Step storage recovery: reboot retains existing records; unsaved tail is not claimed durable.");
        return true;
    }
    if (!interfaceQuiescent() || !pollUsage(now, true)) {
        std::printf("Reboot postponed: %s; sensor/save state retained.\n", usage_.diagnostic());
        pauseInterface(false); return false;
    }
    return true;
}
void HandheldRuntime::pollBattlePresentation(std::uint64_t now) {
    const bool wasLocked = battle_.locked();
    battle_.poll(now);
    using C = battlepresentation::Cue;
    switch (battle_.consumeCue()) {
    case C::Attack: audio_.play(device::AudioCue::Attack); break;
    case C::Magic: audio_.play(device::AudioCue::Magic); break;
    case C::Hit: audio_.play(device::AudioCue::Hit); break;
    case C::CaptureThrow: audio_.play(device::AudioCue::CaptureThrow); break;
    case C::CaptureSuccess: audio_.play(device::AudioCue::CaptureSuccess); break;
    case C::CaptureFail: audio_.play(device::AudioCue::CaptureFail); break;
    case C::Win: audio_.play(device::AudioCue::Win); break;
    case C::Retreat: audio_.play(device::AudioCue::Retreat); break;
    case C::None: break;
    }
    if (wasLocked || battle_.locked()) interfaceDirty_ = true;
}
}
#endif
