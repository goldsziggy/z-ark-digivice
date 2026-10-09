#include "handheld_runtime.hpp"
#include "sd_inspect.hpp"
#include "../runtime/sprite.hpp"
#include "../runtime/release_confirmation.hpp"
#include "../runtime/local_form_art.hpp"
#include "combat.hpp"
#include "forms.hpp"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/task.h"
#if defined(ESP_PLATFORM)
#include "esp_mac.h"
#include "esp_app_desc.h"
#endif
#include <cstdio>
#include <cstring>

namespace digivice {
namespace {
std::uint64_t nowMs() { return static_cast<std::uint64_t>(esp_timer_get_time() / 1000); }
[[maybe_unused]] void wipe(void* memory, std::size_t length) {
    auto* bytes = static_cast<volatile unsigned char*>(memory);
    while (length--) *bytes++ = 0;
}
}
void HandheldRuntime::resolveTestEncounterAtBoot() {
    if (encounterRecoveryRequired_) return;
    if (!needsTestEncounterResolution(state_)) return;
    // This explicit event preserves the old checkpoint until its replacement
    // is committed and read back. Never redraw, reward, or retry on uncertainty.
    State candidate = state_;
    const auto result = apply(candidate, Action::ResolveTestEncounter, 0);
    if (result == Error::None && saves_.writable() && saves_.checkpoint(candidate)) {
        state_ = candidate;
        std::puts("Old test encounter cleared; care, collection and walking progress preserved in a verified checkpoint.");
        return;
    }
    encounterRecoveryRequired_ = true;
    std::printf("SAVE RECOVERY: old test encounter is blocked; RAM preserved. Core=%s; %s. Restart to check saved state.\n",
        errorText(result), saves_.diagnostic());
}
bool HandheldRuntime::ensureWorldSeed() {
    if (!state_.onboardingComplete || state_.worldSeed) return true;
    if (encounterRecoveryRequired_ || powerFrozen() || tradeSession_.blocksForeground()) return false;
    State candidate = state_;
    const auto result = startupSeeds_.ready() ? apply(candidate, Action::WorldSeed, startupSeeds_.world) : Error::InvalidValue;
    if (result == Error::None && saves_.writable() && saves_.checkpoint(candidate)) {
        state_ = candidate;
        std::puts("Independent encounter world saved; current encounter and walking progress preserved.");
        return true;
    }
    encounterRecoveryRequired_ = true;
    std::printf("SAVE RECOVERY: encounter world seed unavailable or uncommitted; play paused. Core=%s; %s.\n",
        errorText(result), saves_.diagnostic());
    return false;
}
void HandheldRuntime::begin() {
    static_assert(onboarding::kChoices == combat::kStarterCount, "starter roster and navigation must agree");
    beginTradeStorage(); // Journal reconciliation precedes every care mutation.
    if (!tradeSession_.blocksForeground()) resolveTestEncounterAtBoot(); // Before starter, random draws, asset requests or presentation.
    (void)ensureWorldSeed(); // Never select a new foe before its independent seed is durable.
    starter_.reset(state_.onboardingComplete);
    const auto practiceNvs = practiceBackend_.initialize();
    const auto practiceBoot = practice_.restore();
    std::printf("Practice storage: %s (%s); independent from care saves.\n",
        devicepractice::resultText(practiceBoot), esp_err_to_name(practiceNvs));
    const auto session = esp_random();
    steps_ = motion::MotionCounter(session ? session : 1);
    const auto networkResult = network_.begin();
    std::printf("Network adapter: %s; no credentials are printed.\n", esp_err_to_name(networkResult));
    // Offer selection is a saved game event. Never show newly drawn choices
    // until their checkpoint is verified, and never redraw an existing offer.
    if (!tradeSession_.blocksForeground() && !state_.onboardingComplete && !state_.starterOfferSeed && saves_.writable()) {
        const auto seed = startupSeeds_.offers;
        State candidate = state_;
        if (seed && apply(candidate, Action::StarterOfferSeed, seed) == Error::None &&
            saves_.checkpoint(candidate)) {
            state_ = candidate;
            std::puts("Three additional Rookie choices saved for this device.");
        } else std::puts("Starter offer save unavailable; onboarding waits for recovery.");
    }
    starter_.configureChoices(state_.starterOfferSeed ? onboarding::kMaxChoices : onboarding::kChoices);
    if (motion::MotionAdapter::buildEnabled() && board::lockSharedI2c() == ESP_OK) {
        sensor_.begin(board::sharedI2cBus()); // No unsupported configuration attestation.
        board::unlockSharedI2c();
    } else sensor_.begin(nullptr);
    std::printf("Motion adapter: %s\n", motion::adapterStatusText(sensor_.status().status));
    if (assets::SdAssetStorage::buildEnabled()) {
        const auto storageResult = sd_.begin();
        if (storageResult == ESP_OK) {
            const auto boot = cache_.boot();
            std::printf("SD asset cache boot: %s\n", assets::resultName(boot));
            assetStorageReady_ = boot == assets::Result::Ok;
            if (assetStorageReady_ && assets::DeviceAssetsClient::developmentAssetsEnabled())
                std::printf("Development asset client: %s\n", esp_err_to_name(assets_.begin(&cache_)));
        } else std::printf("SD assets unavailable: %s; missing artwork is indicated.\n", sd_.diagnostic());
    }
    std::puts("Artwork: exact-form SD sprites or neutral missing-art indicator; no named test creature fallback.");
    stepDelivery_ = motion::StepDelivery(nowMs());
    powerEnabled_ = board::powerControlAvailable();
    printPower();
    beginInterface();
}
board::Capabilities HandheldRuntime::capabilities(board::Capabilities base) const {
#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
    base.display = frame_ && display::displayReady() && !interfacePaused_;
    base.touch = base.display && display::touchReady();
#endif
    base.network = !powerFrozen() && network_.ready() && !network_.recoveryRequired();
    base.motionSteps = !encounterRecoveryRequired_ && !powerFrozen() && physicalStepsReady(nowMs());
    return base;
}
void HandheldRuntime::warm(bool force) {
    // No creature exists before hatch. Test packs require the explicit
    // development-assets build; production never substitutes a named test form.
    auto candidate = state_.onboardingComplete && state_.worldSeed ? assets::plan(state_, assets::DeviceAssetsClient::developmentAssetsEnabled()) : assets::Plan{};
    if (board::selectedProfile().id == board::ProfileId::HeltecUnverified) {
        std::size_t kept = 0;
        for (std::size_t i = 0; i < candidate.count; ++i) if (std::strncmp(candidate.ids[i], "scene-", 6) != 0) {
            if (i != kept) std::memcpy(candidate.ids[kept], candidate.ids[i], sizeof(candidate.ids[i]));
            ++kept;
        }
        candidate.count = kept;
    }
    bool changed = candidate.count != plan_.count;
    for (std::size_t i = 0; !changed && i < candidate.count; ++i) changed = std::strcmp(candidate.ids[i], plan_.ids[i]) != 0;
    if (changed || force) {
        // Invalidate the old worker before changing protection. It rechecks this
        // epoch under the cache lock before every mutation. Care-only state
        // changes retain a matching plan and do not restart a partial download.
        assets_.cancel();
        plan_ = candidate;
        planIndex_ = 0;
    }
    planSequence_ = state_.sequence; planGeneration_ = network_.status().generation;
}
void HandheldRuntime::poll() {
    if (state_.onboardingComplete && starter_.stage() != onboarding::Stage::Complete)
        starter_.reset(true); // Also observes a successful direct serial hatch.
    network_.tick();
    assets_.tick();
    const auto download = assets_.status();
    if (!assetStorageFailed_ && download.phase == assets::DownloadPhase::Failed &&
        download.error == assets::DownloadError::Cache &&
        (download.cacheResult == assets::Result::Io || download.cacheResult == assets::Result::RecoveryRequired)) {
        // The worker owns SD calls. Observe its result, not unsynchronized HAL
        // fields. Never substitute internal flash under the live cache metadata.
        assets_.cancel();
        assetStorageFailed_ = true;
        assetStorageReady_ = false;
        std::puts("SD cache unavailable; downloads paused until reboot/recovery. Local saves remain available; missing artwork is indicated.");
    }
    const auto now = nowMs();
    pollNearby(now); // Teardown progresses even while power freezes UI/gameplay.
    pollPower(now);
    if (usbTransferLease_ && !powerFrozen() && now - lastUsbTransferMs_ >= 60000) {
        if (stopUsbTransfer(true)) std::puts("SDPUT TIMEOUT partial=retained");
    }
    if (usbTransferLease_) return; // No rendering, touch, steps or prefetch during installation.
    if (encounterRecoveryRequired_ || !tradeSession_.healthy() || !ensureWorldSeed()) {
        pollInterface(now); // Show recovery and retain normal physical power handling.
        return; // No motion delivery, game events or asset downloads while unresolved.
    }
    (void)pollUsage(now);
    pollInterface(now);
    if (powerFrozen() || !ensureWorldSeed()) return; // Includes motion sampling, prefetch, and new asset reads/jobs.
    const auto& network = network_.status();
    if (network.state != previousNetwork_) {
        if (network.state != net::State::Online) assets_.cancel();
        else warm(true);
        std::printf("Network: %s; local gameplay continues.\n", net::stateName(network.state));
        previousNetwork_ = network.state;
    }
    // Physical configuration is intentionally not attested by begin(). When a
    // future verified sampler is enabled, move slow I2C work off this owner task.
    if (!motionFault_ && sensor_.status().status == motion::AdapterStatus::Ready && now - lastMotionMs_ >= 1000) {
        lastMotionMs_ = now;
        motion::CounterReading sample{};
        if (board::lockSharedI2c() == ESP_OK) {
            sample = sensor_.poll(now);
            board::unlockSharedI2c();
        } else sample.status = motion::AdapterStatus::IoError;
        if (!sample.valid) {
            motionFault_ = true;
            std::puts("Motion read failed; sampling paused until reboot/verified recovery.");
        }
        const auto previousCountStatus=motionUpdate_.status;
        motionUpdate_ = sample.valid ? steps_.observe(sample.counter24, now) : steps_.poll(now);
        if (motion::countRequiresRecovery(motionUpdate_.status) && motionUpdate_.status!=previousCountStatus)
            std::printf("Step counting paused: %s; confirmed pending=%lu. Drain durably, then motion recover; gap steps are not reconstructed.\n",
                motion::statusText(motionUpdate_.status), static_cast<unsigned long>(motionUpdate_.pendingSteps));
    }
    // Confirmed deltas can drain even after sensor I/O stops. A storage/ack
    // uncertainty is different: StepDelivery latches and never replays it.
    if (!stepDelivery_.halted() && motionUpdate_.pendingSteps && state_.onboardingComplete) {
        const auto delivered=stepDelivery_.pump(steps_,state_,saves_,now,allowsCareAction(Action::Walk));
        motionUpdate_=steps_.poll(now);
        if (delivered.result==motion::DeliveryResult::SaveRecovery || delivered.result==motion::DeliveryResult::AckRecovery ||
            delivered.result==motion::DeliveryResult::CoreRejected) {
            motionFault_=true;
            std::printf("Step delivery halted; no automatic replay. Save=%s core=%s; reboot/review required.\n",
                saves_.diagnostic(),errorText(delivered.coreError));
        }
    }
    if (!assetStorageReady_ || assetStorageFailed_ || network.state != net::State::Online || network.paused || !assets::DeviceAssetsClient::developmentAssetsEnabled()) return;
    if (planSequence_ != state_.sequence || planGeneration_ != network.generation) warm(planGeneration_ != network.generation);
    if (assets_.status().busy || planIndex_ >= plan_.count) return;
    const char* protectedIds[4]{};
    for (std::size_t i = 0; i < plan_.count; ++i) protectedIds[i] = plan_.ids[i];
    // One attempt per selected ID per plan; failed requests wait for explicit
    // warm/reconnect or a changed game state, rather than retrying every tick.
    const auto* id = plan_.ids[planIndex_++];
    assets_.request(network_.endpoint(), network_.allowsPrivateHttp(), id, protectedIds, plan_.count);
}
void HandheldRuntime::printPower() const {
    if (!powerEnabled_) {
        std::puts("power=disabled; this profile or GPIO setup has no usable onboard PWR control. No power GPIO action requested.");
        return;
    }
    const auto& status = power_.status();
    std::printf("power=%s countdown=%u hold=%d frozen=%d failure=%s; %s\n",
        power::phaseName(status.phase), static_cast<unsigned>(status.countdownSeconds),
        board::powerHoldAsserted(), powerFrozen(), power::failureText(status.failure), powerReason_);
    if (status.phase == power::Phase::QuietStandby)
        std::puts("Quiet standby: CPU still powered, source unknown. No deep sleep or physical LCD shutdown is claimed. Short PWR press/release resumes.");
    if (status.phase == power::Phase::Failed)
        std::puts("Power held on. After I/O settles, release then short-press/release PWR to resume; reboot/recovery is required for uncertain saves.");
}
void HandheldRuntime::beginPowerShutdown(std::uint64_t now) {
    powerFrozen_ = true; ++inputEpoch_;
    previousAssetsPaused_ = usbTransferLease_ ? usbTransferPreviousAssetsPaused_ : assets_.paused();
    previousNetworkPaused_ = network_.requestedPaused();
#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
    if (nearbyBusy()) {
        previousAssetsPaused_ = nearbyAssetsPaused_;
        previousNetworkPaused_ = nearbyNetworkPaused_;
        closeNearby();
    }
#endif
    (void)stopUsbTransfer(false); // Close the logical session; retain its resumable files.
    pauseInterface(true);
    // Scanning pauses the join controller temporarily. Preserve only the
    // explicit owner pause so standby during a scan can reconnect on resume.
    recoveryChoice_.cancel(); evolutionChoice_.cancel(); battleModeChoice_.cancel();
    motionUpdate_ = steps_.setPaused(true, now); // Retains confirmed pending and staged batch.
    assets_.pause(true); network_.pause(true);
    sdPowerPrepared_ = false; latchDropped_ = false; powerWork_ = power::Work::Pending;
    powerReason_ = "stopping new work; draining confirmed steps and waiting for I/O";
}
power::Work HandheldRuntime::drainPower(std::uint64_t now) {
    if (!nearbyQuiescent()) {
        powerReason_ = "waiting for nearby radio teardown"; return power::Work::Pending;
    }
    if (!usbTransfer_.quiescent() || usbTransfer_.active() || usbTransferLease_) {
        powerReason_ = "waiting for USB asset transfer to close"; return power::Work::Pending;
    }
    if (!interfaceQuiescent()) { powerReason_ = "waiting for display/audio/motion shutdown"; return power::Work::Pending; }
    if (!encounterRecoveryRequired_ && !pollUsage(now, true)) { powerReason_ = "lifetime/walking checkpoint requires recovery"; return power::Work::Failed; }
    if (tradeSession_.freezesGameWrites()) { powerReason_ = tradeSession_.diagnostic(); return power::Work::Failed; }
    if (!saves_.writable()) { powerReason_ = saves_.diagnostic(); return power::Work::Failed; }
    // Practice commands already commit/read back synchronously. Uncertain
    // practice storage must be reviewed, never hidden by an intentional cut.
    if (!practice_.writable()) { powerReason_ = practice_.diagnostic(); return power::Work::Failed; }
    motionUpdate_ = steps_.poll(now);
    if (motionUpdate_.pendingSteps) {
        if (!allowsCareAction(Action::Walk)) {
            powerReason_ = "confirmed steps remain; resume and finish practice before powering off";
            return power::Work::Failed;
        }
        const auto delivered = stepDelivery_.pump(steps_, state_, saves_, now, true, true);
        motionUpdate_ = steps_.poll(now);
        if (delivered.result != motion::DeliveryResult::Committed) {
            powerReason_ = "confirmed step delivery blocked or uncertain; no steps discarded";
            return power::Work::Failed;
        }
        if (motionUpdate_.pendingSteps) return power::Work::Pending;
    }
    if (stepDelivery_.halted()) {
        powerReason_ = "step delivery requires recovery before power-off"; return power::Work::Failed;
    }
    const auto network = network_.quiescence();
    if (!assets_.quiescent() || network == ESP_ERR_NOT_FINISHED) return power::Work::Pending;
    if (network != ESP_OK) { powerReason_ = "network shutdown state requires recovery"; return power::Work::Failed; }
    if (!sdPowerPrepared_) {
        // Worker reservation is clear; no renderer or main command can access SD.
        // Keep mounted for surviving-power standby/resume. Never hot-remount.
        if (sd_.preparePowerOff() != ESP_OK) {
            powerReason_ = "SD synchronization failed; keep power on for recovery";
            return power::Work::Failed;
        }
        sdPowerPrepared_ = true;
    }
    powerReason_ = "I/O quiescent; final save required";
    return power::Work::Ready;
}
void HandheldRuntime::resumePower(std::uint64_t now) {
    // The source of surviving power is deliberately unknown. Reassert the
    // battery hold before any SD, radio, gameplay or motion activity resumes.
    if (board::setPowerHold(true) != ESP_OK) {
        powerHardwareFault_ = true;
        powerReason_ = "could not reassert power hold; work remains frozen";
        return;
    }
    latchDropped_ = false; sdPowerPrepared_ = false;
    assets_.pause(previousAssetsPaused_); network_.pause(previousNetworkPaused_);
    motionUpdate_ = steps_.setPaused(false, now); // Next valid sample reanchors; standby gaps grant no steps.
    powerWork_ = power::Work::Pending;
    powerFrozen_ = false; ++inputEpoch_;
    pauseInterface(false);
    powerReason_ = "resumed; confirmed state retained, unverified motion gap not credited";
    warm(true);
}
void HandheldRuntime::pollPower(std::uint64_t now) {
    if (!powerEnabled_) return;
    bool pressed = false;
    const auto read = board::readPowerPressed(pressed);
    const bool hardwareFault = powerHardwareFault_ || read != ESP_OK;
    powerHardwareFault_ = false;
    if (read != ESP_OK) powerReason_ = "PWR input unavailable; work frozen and hold retained";
    const auto phase = power_.status().phase;
    if (phase == power::Phase::Draining && !hardwareFault) powerWork_ = drainPower(now);
    const auto network = powerFrozen_ ? network_.quiescence() : ESP_ERR_NOT_FINISHED;
    // Quiescence reports pending before any terminal failure, so even a failed
    // radio teardown cannot permit resume while its HTTP worker still owns work.
    const bool idle = nearbyQuiescent() && assets_.quiescent() && network != ESP_ERR_NOT_FINISHED && interfaceQuiescent() &&
        usbTransfer_.quiescent() && !usbTransfer_.active() && !usbTransferLease_;
    const auto actions = power_.tick({now, pressed, powerWork_, latchDropped_, idle, hardwareFault});
    if (actions.beginShutdown) beginPowerShutdown(now);
    if (actions.save) {
        // No gameplay callback can run between this checkpoint and latch release.
        powerWork_ = !tradeSession_.freezesGameWrites() && saves_.writable() && practice_.writable() && saves_.checkpoint(state_)
            ? power::Work::Ready : power::Work::Failed;
        powerReason_ = powerWork_ == power::Work::Ready
            ? "save verified; release PWR to finish power-off" : saves_.diagnostic();
    }
    if (actions.cutLatch) {
        // Revalidate the barrier even if the user held PWR long after the save.
        if (!nearbyQuiescent() || !assets_.quiescent() || !interfaceQuiescent() || !usbTransfer_.quiescent() || usbTransfer_.active() || usbTransferLease_ ||
            network_.quiescence() != ESP_OK || !sdPowerPrepared_ ||
            tradeSession_.freezesGameWrites() || !saves_.writable() || !practice_.writable() || steps_.poll(now).pendingSteps) {
            powerWork_ = power::Work::Failed;
            powerReason_ = "shutdown barrier changed after save; power retained";
        } else {
            bool stillPressed = true;
            // A fresh raw read closes the re-press race after the FSM's debounce.
            if (board::readPowerPressed(stillPressed) != ESP_OK) {
                powerWork_ = power::Work::Failed; powerReason_ = "PWR recheck failed; power retained";
            } else if (!stillPressed) {
                if (board::setPowerHold(false) == ESP_OK) {
                    latchDropped_ = true; powerReason_ = "power latch released after verified save and I/O barrier";
                } else { powerWork_ = power::Work::Failed; powerReason_ = "power latch write failed"; }
            }
        }
    }
    if (actions.resume) resumePower(now);
    if (power_.status().phase == power::Phase::Failed) {
        if (!powerFrozen_) {
            const auto* reason = powerReason_;
            beginPowerShutdown(now); powerReason_ = reason;
        }
        if (!board::powerHoldAsserted() && board::setPowerHold(true) != ESP_OK)
            powerReason_ = "cannot verify asserted power hold; work remains frozen";
        if (power_.status().failure == power::Failure::Timeout)
            powerReason_ = "shutdown timed out; waiting safely for outstanding I/O";
    }
    const auto& current = power_.status();
    if (current.phase == power::Phase::Holding)
        powerReason_ = "release before countdown ends to cancel";
    else if (current.phase == power::Phase::Ready && !actions.resume)
        powerReason_ = "onboard PWR armed; hold for 3 seconds to request shutdown";
    else if (current.phase == power::Phase::AwaitInitialRelease)
        powerReason_ = "release PWR before a new hold can begin";
    if (reportedPowerPhase_ != current.phase || reportedCountdown_ != current.countdownSeconds) {
        reportedPowerPhase_ = current.phase; reportedCountdown_ = current.countdownSeconds;
        printPower();
#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
        if (current.phase == power::Phase::Holding) {
            char message[48]; std::snprintf(message, sizeof(message), "POWER OFF IN %u - RELEASE TO CANCEL", current.countdownSeconds);
            ui_.notice(message); interfaceDirty_ = true;
        }
#endif
    }
}
void HandheldRuntime::printMotion() const {
    const auto& value=sensor_.status();
    std::printf("motion=%s valid=%d WHO_AM_I=0x%02x revision=0x%02x fault=%d; physical configuration not attested\n",
        motion::adapterStatusText(value.status),value.valid,value.whoAmI,value.revision,motionFault_);
    std::printf("counter=%s confirmed pending=%lu total=%llu; delivery halted=%d practice permits walk=%d\n",
        motion::statusText(motionUpdate_.status),static_cast<unsigned long>(motionUpdate_.pendingSteps),
        static_cast<unsigned long long>(motionUpdate_.totalSteps),stepDelivery_.halted(),practice_.allowsCareAction(Action::Walk));
}
void HandheldRuntime::recoverMotion() {
    motionUpdate_=steps_.poll(nowMs());
    if(motionFault_ || sensor_.status().status!=motion::AdapterStatus::Ready ||
       !stepDelivery_.reanchor(steps_,saves_,nowMs())) {
        std::puts("Motion reanchor refused: confirmed steps must drain, sensor must be verified ready, and saves/delivery must be healthy. No steps discarded.");
    } else {
        motionUpdate_=steps_.poll(nowMs());
        std::puts("Explicit reanchor requested; next sample anchors only. Steps during the unverified gap are not credited.");
    }
    printMotion();
}
void HandheldRuntime::recoveryCommand(const char* input) {
    if(!std::strcmp(input,"cancel")) { recoveryChoice_.cancel();std::puts("Full recovery cancelled; save unchanged.");return; }
    if(!saves_.writable()) {recoveryChoice_.cancel();std::puts("Full recovery paused: save durability requires recovery.");return;}
    if(!std::strcmp(input,"confirm")) {
        State candidate;const auto count=recoveryChoice_.count();
        if(!recoveryChoice_.confirm(state_,candidate)) {std::puts("No current recovery review; use rest full again. State/partner changes invalidate it.");return;}
        if(!saves_.checkpoint(candidate)) {std::printf("Recovery commit uncertain; RAM preserved; reboot before retry: %s\n",saves_.diagnostic());return;}
        state_=candidate;std::printf("Full recovery saved: %lu existing Rest actions, one verified checkpoint.\n",static_cast<unsigned long>(count));return;
    }
    if(*input && std::strcmp(input,"status")) {std::puts("Use rest full | rest full confirm | rest full cancel | rest full status.");return;}
    if(!*input) {
        if(!recoveryChoice_.propose(state_)) {std::puts("Full recovery unavailable or already complete; no save written. Home only.");return;}
    }
    std::printf("Full recovery review: %lu existing Rest actions for %s; no change until rest full confirm.\n",
        static_cast<unsigned long>(recoveryChoice_.count()),state_.onboardingComplete?creatureName(state_):"unhatched companion");
}
void HandheldRuntime::printNetwork() const {
    const auto& value = network_.status();
    std::printf("network=%s configured=%d ip=%d service=%d paused=%d attempts=%u recovery=%d\n",
        net::stateName(value.state), value.configured, value.hasIp, value.serviceReachable,
        value.paused, static_cast<unsigned>(value.joinAttempts), network_.recoveryRequired());
}
void HandheldRuntime::printAssets() const {
    const auto value = assets_.status();
    std::printf("SD primary ready=%d recovery=%d; no automatic formatting or medium switching.\n", assetStorageReady_, assetStorageFailed_);
    std::printf("assets=%s error=%s cache=%s received=%lu total=%lu busy=%d; named-test-fallback=disabled\n",
        assets::downloadPhaseName(value.phase), assets::downloadErrorName(value.error), assets::resultName(value.cacheResult),
        static_cast<unsigned long>(value.received), static_cast<unsigned long>(value.total), value.busy);
}
void HandheldRuntime::configureNetwork(char* fields, bool allowPrivateHttp) {
#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
    (void)fields; (void)allowPrivateHttp;
    std::puts("Use SETUP on the device to enter Wi-Fi credentials and the server address.");
    return;
#else
    char* first = std::strchr(fields, '|');
    char* second = first ? std::strchr(first + 1, '|') : nullptr;
    if (!first || !second) { std::puts("Use net set origin|SSID|password. Input is not echoed."); return; }
    *first = 0; *second = 0;
    net::Config config{};
    const auto originBytes = std::strlen(fields), ssidBytes = std::strlen(first + 1), passwordBytes = std::strlen(second + 1);
    if (originBytes >= sizeof(config.endpoint) || ssidBytes >= sizeof(config.ssid) || passwordBytes >= sizeof(config.password)) {
        std::puts("Network configuration exceeds field bounds."); return;
    }
    std::memcpy(config.endpoint, fields, originBytes + 1);
    std::memcpy(config.ssid, first + 1, ssidBytes + 1);
    std::memcpy(config.password, second + 1, passwordBytes + 1);
    config.allowPrivateHttp = allowPrivateHttp;
    const auto valid = net::validateConfig(config, net::NetworkAdapter::privateHttpBuildEnabled());
    if (valid != net::ConfigError::None) std::printf("Configuration rejected: %s\n", net::configErrorText(valid));
    else { assets_.cancel(); std::printf("Network configuration: %s\n", esp_err_to_name(network_.configure(config))); }
    wipe(&config, sizeof(config));
#endif
}
void HandheldRuntime::printStarter() const {
    if (state_.onboardingComplete) {
        const auto* form = forms::find(starterForm(state_, state_.starterId));
        const auto* chosen = form ? form->name : nullptr;
        std::printf("Onboarding complete: %s; starter choice cannot replace an existing companion.\n",
                    chosen ? chosen : "restored/existing game");
        return;
    }
    const auto id = starter_.selectedId();
    const auto* form = forms::find(starterForm(state_, id));
    const auto* name = form ? form->name : nullptr;
    std::printf("Starter: %s; choice %u/%u %s. Next cycles, Confirm advances; Back simulates hold-Back.\n",
        onboarding::stageName(starter_.stage()), static_cast<unsigned>(id),
        static_cast<unsigned>(starter_.choiceCount()), name ? name : "unavailable");
    if (!saves_.writable() || !state_.starterOfferSeed)
        std::puts("Save recovery required; starter changes are paused.");
}
void HandheldRuntime::starterInput(onboarding::Input input) {
    if (state_.onboardingComplete) { starter_.reset(true); printStarter(); return; }
    if (!saves_.writable() || !state_.starterOfferSeed) { printStarter(); return; }
    const auto request = starter_.input(input);
    if (request.hatchId) {
        State next = state_;
        const auto error = apply(next, Action::Hatch, request.hatchId);
        if (error != Error::None) {
            starter_.resolve(false);
            std::printf("Starter rejected: %s\n", errorText(error));
        } else if (!saves_.checkpoint(next)) {
            // The NVS write may have landed. Preserve RAM and disable further
            // gameplay via SaveStore; reboot chooses the verified durable state.
            starter_.resolve(false);
            std::printf("Hatch durability uncertain; recovery required: %s\n", saves_.diagnostic());
        } else {
            state_ = next;
            starter_.resolve(true);
            std::printf("Hatched %s; checkpoint verified.\n", creatureName(state_));
        }
    }
    printStarter();
}
void HandheldRuntime::printBattleMode() const {
    std::printf("Wild mode: %s. Tactical uses manual actions; Auto resolves the whole encounter on explicit auto.\n",
                state_.battleMode == BattleMode::Auto ? "Auto" : "Tactical");
    if (battleModeChoice_.pending()) {
        std::printf("Pending choice: %s. Use mode confirm, or mode cancel; gameplay changes invalidate confirmation.\n",
                    battleModeChoice_.selected() == controls::BattleChoice::Auto ? "Auto" : "Tactical");
    }
    std::puts("Mode changes are Home-only. Use practice help for separate serial practice battles.");
}
void HandheldRuntime::proposeBattleMode(controls::BattleChoice choice) {
    battleModeChoice_.cancel();
    if (!saves_.writable()) { std::puts("Mode selection paused: save recovery required."); return; }
    const auto value = choice == controls::BattleChoice::Auto ? 1u : 0u;
    State preview = state_;
    const auto error = apply(preview, Action::Mode, value); // Validate through the core; preview is never saved.
    if (error != Error::None) { std::printf("Mode rejected: %s\n", errorText(error)); return; }
    battleModeChoice_.propose(choice, state_.sequence);
    printBattleMode();
}
void HandheldRuntime::confirmBattleMode() {
    controls::BattleChoice choice;
    if (!battleModeChoice_.confirm(state_.sequence, choice)) {
        std::puts("No current mode choice; choose mode tactical or mode auto again."); return;
    }
    if (!saves_.writable()) { std::puts("Mode confirmation paused: save recovery required."); return; }
    State next = state_;
    const auto error = apply(next, Action::Mode, choice == controls::BattleChoice::Auto ? 1u : 0u);
    if (error != Error::None) { std::printf("Mode rejected: %s\n", errorText(error)); return; }
    if (!saves_.checkpoint(next)) {
        std::printf("Mode durability uncertain; recovery required: %s\n", saves_.diagnostic()); return;
    }
    state_ = next;
    std::puts("Wild mode checkpoint verified.");
    printBattleMode();
}
void HandheldRuntime::printPractice() const {
    std::printf("practice revision=%lu lastCommandId=%lu mode=%s companionId=%lu writable=%d: %s\n",
        static_cast<unsigned long>(practice_.revision()), static_cast<unsigned long>(practice_.lastCommandId()),
        devicepractice::modeName(practice_.mode()), static_cast<unsigned long>(practice_.companionId()),
        practice_.writable(), practice_.diagnostic());
    if (const auto* state = practice_.battle()) {
        char json[practice::kJsonCapacity];
        if (practice::writePublicJson(*state, json, sizeof(json))) std::puts(json);
    }
}
void HandheldRuntime::practiceCommand(const char* line) {
    const auto reply = practice_.command(line, state_, saves_.writable(), esp_random());
    if (reply.result == devicepractice::Result::Help || reply.result == devicepractice::Result::InvalidCommand) {
        std::puts("practice status | practice replay | practice help");
        std::puts("practice start <ID> <REV> tactical|auto (explicit confirmation; no battle runs before start)");
        std::puts("practice act <ID> <REV> physical|heavy|magic|brace|counter|ward|retreat");
        std::puts("practice act <ID> <REV> card 1|2");
        std::puts("IDs are increasing nonzero uint32; REV is current practice revision. Begin with ID 1, REV 0.");
        std::puts("Retry the EXACT last command after a lost reply; never assign it a new ID. Older IDs are rejected.");
        std::puts("Auto saves its whole result once. Tactical saves each action. Practice never changes care HP, bond or rewards.");
        std::puts("Active/recovery practice blocks partner selection, release, evolution and new walks; care remains usable. UINT32 limits require reviewed recovery, never wrap IDs.");
    } else if (reply.result == devicepractice::Result::Replay) {
        autobattle::Trace trace;
        if (!practice_.replay(trace)) std::puts("No verified Auto result available for replay.");
        else {
            std::printf("Saved Auto result: %s, %u exchanges; replay makes no writes.\n",
                autobattle::outcomeName(trace.outcome), static_cast<unsigned>(trace.count));
            for (std::size_t i = 0; i < trace.count; ++i) {
                const auto& step = trace.steps[i];
                std::printf("%u %s %s / %s: HP %lu->%lu, rival %lu->%lu, reflected=%d\n",
                    static_cast<unsigned>(i + 1), step.defending ? "defend" : "attack", autobattle::moveName(step.action),
                    autobattle::moveName(step.opponentAction), static_cast<unsigned long>(step.playerHpBefore),
                    static_cast<unsigned long>(step.playerHpAfter), static_cast<unsigned long>(step.enemyHpBefore),
                    static_cast<unsigned long>(step.enemyHpAfter), step.reflected);
            }
        }
    } else {
        std::printf("Practice: %s\n", devicepractice::resultText(reply.result));
        if (reply.result == devicepractice::Result::CoreRejected) std::printf("Reason: %s\n", practice::errorText(reply.coreError));
    }
    printPractice();
}
void HandheldRuntime::printEvolution() const {
    if (!state_.onboardingComplete || !isValid(state_)) { std::puts("Hatch a companion before choosing a Digivolution."); return; }
    const auto* selected = activeMember(state_);
    if (!selected) return;
    const auto& member = *selected;
    const auto* current = forms::find(member.formId);
    if (!current) return;
    std::printf("%s: level %lu, XP %lu, bond %lu; form %u, pending %lu.\n", current->name,
        static_cast<unsigned long>(member.level), static_cast<unsigned long>(member.xp), static_cast<unsigned long>(member.bond),
        static_cast<unsigned>(current->id), static_cast<unsigned long>(evolutionChoice_.target()));
    for (unsigned index = 0; index < 2; ++index) {
        const auto* edge = forms::outgoing(current->id, index);
        if (!edge) break;
        const auto child = edge->to;
        const auto* form = forms::find(child);
        if (!form) continue;
        State probe = state_;
        const auto available = apply(probe, Action::Evolve, child) == Error::None && saves_.writable() && practice_.allowsCareAction(Action::Evolve);
        const auto previewLevel = member.level > edge->minLevel ? member.level : edge->minLevel;
        const auto stats = combat::formProfile(child, previewLevel).stats;
        std::printf("%u %s [%s]: requires level %u, bond %u, Home, and no active/uncertain practice; %s.\n",
            static_cast<unsigned>(child), form->name, forms::stageName(form->stage), static_cast<unsigned>(edge->minLevel),
            static_cast<unsigned>(edge->minBond), available ? "available" : "locked");
        std::printf("  Preview at level %lu: HP %lu ATK %lu DEF %lu MAG %lu RES %lu; art %s.\n",
            static_cast<unsigned long>(previewLevel), static_cast<unsigned long>(stats.maxHp), static_cast<unsigned long>(stats.attack),
            static_cast<unsigned long>(stats.defense), static_cast<unsigned long>(stats.magic), static_cast<unsigned long>(stats.resistance),
            form->artId ? "catalogued" : "unavailable (name/stats only)");
    }
    if (!forms::outgoing(current->id, 0)) {
        const auto* reason = forms::leafReason(current->id);
        std::puts(reason ? reason : "This form has no further prototype branch.");
    }
    std::puts("evolve <formId> previews an eligible choice; evolve confirm commits it; evolve cancel/status. No change before confirmation.");
}
void HandheldRuntime::evolutionCommand(const char* input) {
    if (!*input || !std::strcmp(input, "status")) { printEvolution(); return; }
    if (!std::strcmp(input, "cancel")) { evolutionChoice_.cancel(); std::puts("Evolution choice cancelled; save unchanged."); return; }
    if (!saves_.writable() || !practice_.allowsCareAction(Action::Evolve)) {
        evolutionChoice_.cancel(); std::puts("Evolution paused: save recovery or active/uncertain practice."); return;
    }
    if (!std::strcmp(input, "confirm")) {
        State candidate;
        if (!evolutionChoice_.confirm(state_, candidate)) { std::puts("No current eligible choice; preview the branch again."); return; }
        if (!saves_.checkpoint(candidate)) {
            std::printf("Evolution commit uncertain; RAM preserved; recovery required: %s\n", saves_.diagnostic()); return;
        }
        state_ = candidate;
        std::printf("Digivolved to %s; checkpoint verified.\n", creatureName(state_));
        printEvolution(); return;
    }
    std::uint32_t target = 0;
    for (const char* p = input; *p; ++p) {
        if (*p < '0' || *p > '9' || target > (UINT32_MAX - static_cast<unsigned>(*p - '0')) / 10u) {
            std::puts("Use evolve <numeric formId>, confirm, cancel or status."); return;
        }
        target = target * 10u + static_cast<unsigned>(*p - '0');
    }
    if (!evolutionChoice_.propose(state_, target)) std::printf("Choice unavailable: %s\n", errorText(evolutionChoice_.error()));
    else std::puts("Eligible choice staged. Use evolve confirm to save it; any intervening care event requires a new preview.");
    printEvolution();
}
void HandheldRuntime::printCollection() const {
    if (!isValid(state_)) { std::puts("Collection unavailable: invalid inspection state; saved bytes preserved."); return; }
    std::printf("Companions: %lu/%zu; IDs are permanent and are not array positions.\n",
        static_cast<unsigned long>(state_.collectionCount), kCollectionCapacity);
    for (std::size_t i = 0; i < state_.collectionCount; ++i) {
        const auto& member = state_.collection[i];
        std::printf("ID %lu: %s, form %lu, level %lu%s\n", static_cast<unsigned long>(member.id),
            memberName(member), static_cast<unsigned long>(member.formId), static_cast<unsigned long>(member.level),
            member.id == state_.activeCreatureId ? " [active; select another before release]" : "");
    }
    std::puts("release <ID> confirm removes that nonactive companion after a verified checkpoint. Home or new encounters; active/older-encounter restrictions apply. Journal entries remain.");
}
void HandheldRuntime::printJournal() const {
    if (!isValid(state_)) { std::puts("Journal unavailable: invalid inspection state."); return; }
    unsigned obtained = 0;
    for (std::uint32_t id = 1; id <= kJournalCapacity; ++id) if (hasObtained(state_, id)) {
        const auto* form = forms::find(id);
        if (form) {
            ++obtained;
            std::printf("%lu %s [%s]\n", static_cast<unsigned long>(id), form->name, forms::stageName(form->stage));
        }
    }
    std::printf("Journal: %u obtained forms; release never clears this history.\n", obtained);
}
void HandheldRuntime::releaseCommand(const char* input) {
    if (!*input || !std::strcmp(input, "status")) { printCollection(); return; }
    std::uint32_t memberId = 0;
    if (!controls::parseReleaseConfirmation(input, memberId)) {
        std::puts("Use release status, or release <stable ID> confirm. No companion was changed."); return;
    }
    if (!saves_.writable() || !practice_.allowsCareAction(Action::Release)) {
        std::puts("Release paused: care recovery or active/uncertain practice. Companions are unchanged."); return;
    }
    State candidate = state_;
    const auto error = apply(candidate, Action::Release, memberId);
    if (error != Error::None) { std::printf("Release rejected: %s\n", errorText(error)); return; }
    if (!saves_.checkpoint(candidate)) {
        std::printf("Release durability uncertain; RAM preserved; recover before continuing: %s\n", saves_.diagnostic()); return;
    }
    state_ = candidate;
    std::printf("Released companion ID %lu; checkpoint verified; journal retained.\n", static_cast<unsigned long>(memberId));
    printCollection();
}
void HandheldRuntime::printArt() const {
    const auto* member = activeMember(state_);
    std::printf("Art inspection: selected form %lu; last inspected %lu (%s). Physical renderer diagnostics: device status.\n",
        static_cast<unsigned long>(member ? member->formId : 0), static_cast<unsigned long>(lastArtForm_),
        lastArtWasPrivate_ ? "exact-form private SD frame" : "unavailable or not yet inspected");
#if defined(CONFIG_DIGIVICE_PRIVATE_SD_ART) && CONFIG_DIGIVICE_PRIVATE_SD_ART
    std::puts("Private SD art enabled: art preview reads only DSFnnnnn.DVA for the selected form; unsigned local files, CRC checked.");
#else
    std::puts("Private SD art disabled in this build; no named test artwork is substituted.");
#endif
}
void HandheldRuntime::previewArt() {
    const auto* member = activeMember(state_);
    if (!isValid(state_) || !member) { std::puts("Hatch a companion before inspecting form art."); return; }
    lastArtForm_ = member->formId; lastArtWasPrivate_ = false;
#if defined(CONFIG_DIGIVICE_PRIVATE_SD_ART) && CONFIG_DIGIVICE_PRIVATE_SD_ART
    std::uint16_t pixels[sprite::kMaximumPixels];
    std::uint8_t mask[sprite::kMaximumMaskBytes];
    if (assetStorageReady_ && !assetStorageFailed_) {
        sprite::LocalFormArt art;
        auto result = art.open(member->formId);
        if (result == sprite::Result::Ok) result = art.decode(sprite::Animation::Idle, 0, pixels, sprite::kMaximumPixels, mask, sizeof(mask));
        if (result == sprite::Result::Ok) {
            lastArtWasPrivate_ = true;
            std::printf("Private exact-form %lu: %ux%u, %u mapped frames; idle frame decoded. File closed after inspection.\n",
                static_cast<unsigned long>(member->formId), static_cast<unsigned>(art.info().width),
                static_cast<unsigned>(art.info().height), static_cast<unsigned>(art.info().frameCount));
        } else std::printf("Private art unavailable: %s (%s); no substitute creature.\n", sprite::resultName(result), art.diagnostic());
    }
#endif
    if (!lastArtWasPrivate_) std::puts("Exact-form artwork unavailable; renderer uses a neutral missing-art indicator.");
    std::puts("Inspection only: no display transfer, no gameplay mutation, no private asset download.");
}
bool HandheldRuntime::usbTransferCooperate(void* context) {
    auto& owner = *static_cast<HandheldRuntime*>(context);
    if (++owner.usbTransferYieldBlocks_ % 16 == 0) vTaskDelay(1);
    return true; // Owner-task operation; no recursive poll or competing SD access.
}
void HandheldRuntime::printIdentity() const {
#if defined(ESP_PLATFORM)
    std::uint8_t mac[6]{};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        std::puts("device identity unavailable"); return;
    }
    const char* profile = board::selectedProfile().id == board::ProfileId::Waveshare146
        ? "waveshare-esp32-s3-touch-lcd-1.46" : "unsupported";
    std::printf("device identity mac=%02x:%02x:%02x:%02x:%02x:%02x board=%s firmware=%s sdput=1\n",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], profile, esp_app_get_description()->version);
#else
    std::puts("device identity unavailable on host");
#endif
}
bool HandheldRuntime::stopUsbTransfer(bool restoreInterface) {
    if (!usbTransfer_.pause() || !usbTransfer_.quiescent()) return false;
    if (!usbTransferLease_) return true;
    usbTransferLease_ = false;
#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
    art_.retry(); // Also refresh completed assets after surviving-power standby.
#endif
    if (restoreInterface && !powerFrozen()) {
        assets_.pause(usbTransferPreviousAssetsPaused_);
        pauseInterface(usbTransferPreviousInterfacePaused_);
        ++inputEpoch_; // Discard any partial or stale user input after installation.
    }
    return true;
}
bool HandheldRuntime::usbTransferCommand(const char* line) {
    if (std::strncmp(line, "sdput", 5) || (line[5] && line[5] != ' ' && line[5] != '\t')) return false;
    const char* argument = line + 5;
    while (*argument == ' ' || *argument == '\t') ++argument;
    if (!std::strncmp(argument, "abort", 5)) {
        const char* tail = argument + 5;
        while (*tail == ' ' || *tail == '\t') ++tail;
        if (!*tail) {
            // Closing the logical lease requires no filesystem access, even
            // after a card failure. Preserve owned partial files for resume.
            std::puts(stopUsbTransfer(true) ? "SDPUT PAUSED" : "SDPUT ERROR code=BUSY");
            return true;
        }
    }
    if (!sd_.mounted() || !sd_.ready()) { std::puts("SDPUT ERROR code=SD_UNAVAILABLE"); return true; }
    if (!usbTransferLease_) {
        if (assets_.status().busy) { std::puts("SDPUT ERROR code=BUSY"); return true; }
        usbTransferPreviousAssetsPaused_ = assets_.paused();
#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
        usbTransferPreviousInterfacePaused_ = interfacePaused_;
#endif
        assets_.pause(true);
        pauseInterface(true);
        // Audio acknowledges its pause on a separate worker (up to one 20 ms
        // queue wait). Give shutdown barriers bounded time to settle before
        // touching SD; immediate rollback would continually revoke that pause.
        for (unsigned wait = 0; wait < 25 && (!assets_.quiescent() || !interfaceQuiescent()); ++wait)
            vTaskDelay(pdMS_TO_TICKS(10));
        if (!assets_.quiescent() || !interfaceQuiescent()) {
            assets_.pause(usbTransferPreviousAssetsPaused_);
            pauseInterface(usbTransferPreviousInterfacePaused_);
            std::puts("SDPUT ERROR code=BUSY"); return true;
        }
        usbTransferLease_ = true;
    }
    lastUsbTransferMs_ = nowMs();
    char response[assets::UsbSdTransfer::kResponseBytes]{};
    const bool handled = usbTransfer_.handle(line, response, sizeof(response));
    lastUsbTransferMs_ = nowMs(); // Hash/flush time is activity, not idle timeout.
    if (handled && !std::strcmp(response, "SDPUT PAUSED") && !stopUsbTransfer(true)) {
        std::puts("SDPUT ERROR code=BUSY"); return true;
    }
    std::puts(handled ? response : "SDPUT ERROR code=SYNTAX");
    return true;
}
bool HandheldRuntime::command(char* line) {
    while (*line == ' ' || *line == '\t') ++line;
    if (std::strcmp(line, "device identity") == 0) { printIdentity(); return true; }
    if (std::strcmp(line, "power status") == 0) { printPower(); return true; }
    if (powerFrozen()) {
        if (!std::strncmp(line, "sdput", 5)) { std::puts("SDPUT ERROR code=POWER"); return true; }
        std::puts("Commands paused for power transition/standby. Use power status; release then short-press/release PWR to resume when I/O is idle.");
        return true; // Never allow app_main fallback, checkpoint, or reboot through.
    }
    if (tradeSession_.blocksForeground()) {
        const bool readOnly = !std::strcmp(line, "status") || !std::strcmp(line, "snapshot") ||
            !std::strcmp(line, "capabilities") || !std::strcmp(line, "companions") ||
            !std::strcmp(line, "journal") || !std::strcmp(line, "device status") ||
            !std::strcmp(line, "net status") || !std::strcmp(line, "trade status");
        if (!std::strcmp(line, "trade status")) { std::puts(tradeSession_.diagnostic()); return true; }
        if (!std::strcmp(line, "nearby open") && tradeSession_.healthy()) {
#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
            beginNearby();
#endif
            return true;
        }
        if (!std::strcmp(line, "nearby close")) { closeNearby(); return true; }
        if (!std::strcmp(line, "reboot") && nearbyQuiescent()) {
            if (!prepareUsageRestart()) return true;
            std::fflush(stdout); esp_restart(); return true;
        }
        if (!readOnly) { std::puts(tradeSession_.diagnostic()); return true; }
    }
    if (encounterRecoveryRequired_) {
        if (!std::strcmp(line, "reboot")) {
            std::puts("Restarting to restore the verified save slots; no checkpoint or automatic retry in this boot.");
            std::fflush(stdout); esp_restart(); return true;
        }
        if (!std::strcmp(line, "status") || !std::strcmp(line, "snapshot") || !std::strcmp(line, "capabilities")) {
            std::puts("SAVE RECOVERY: test encounter resolution is blocked; restart to check saved state.");
            return false; // Existing app read-only observations remain available.
        }
        if (!std::strcmp(line, "device status")) { (void)interfaceCommand(line); return true; }
        std::puts("SAVE RECOVERY: gameplay and transfers paused. Use status, snapshot, device identity or reboot.");
        return true;
    }
    if (nearbyBusy()) {
        if (!std::strcmp(line, "nearby close")) { closeNearby(); return true; }
        const bool recoveryReboot = !std::strcmp(line, "reboot") && nearbyRecoveryRestartReady();
        if (recoveryReboot)
            std::puts("Recovery reboot: ESP-NOW resources are idle; Wi-Fi lease acknowledgement is uncertain. No lease reuse; flushing local usage before restart.");
        if (!recoveryReboot && std::strcmp(line, "status") && std::strcmp(line, "snapshot") &&
            std::strcmp(line, "capabilities") && std::strcmp(line, "device status") &&
            std::strcmp(line, "net status") && std::strcmp(line, "companions")) {
            std::puts("Nearby owns the radio; use nearby close and wait before changing saves, Wi-Fi, USB assets or rebooting.");
            return true;
        }
    }
    if (usbTransferCommand(line)) return true;
    if (usbTransferLease_) { std::puts("SDPUT ERROR code=BUSY"); return true; }
    if (std::strcmp(line, "reboot") == 0 && !prepareUsageRestart()) return true;
    if (battlePlaybackLocked() && std::strcmp(line, "status") && std::strcmp(line, "snapshot") &&
        std::strcmp(line, "capabilities") && std::strcmp(line, "device status") && std::strcmp(line, "reboot")) {
        std::puts("Battle playback in progress; saved result is retained. Wait for the result screen."); return true;
    }
    const bool observation = !std::strcmp(line, "status") || !std::strcmp(line, "snapshot") ||
        !std::strcmp(line, "capabilities") || !std::strcmp(line, "device status") ||
        !std::strcmp(line, "net status") || !std::strcmp(line, "assets status") ||
        !std::strcmp(line, "motion status") || !std::strcmp(line, "starter status") ||
        !std::strcmp(line, "art status") || !std::strcmp(line, "companions") || !std::strcmp(line, "journal");
    if (!observation) interfaceActivity(nowMs());
    if (interfaceCommand(line)) return true;
    if (std::strncmp(line, "power", 5) == 0 && (line[5] == 0 || line[5] == ' ' || line[5] == '\t')) {
        printPower();
        if (powerEnabled_) std::puts("Use onboard PWR: hold 3 seconds, release early to cancel. Serial power status is read-only.");
        return true;
    }
    if (std::strncmp(line, "practice", 8) == 0 && (line[8] == 0 || line[8] == ' ' || line[8] == '\t')) practiceCommand(line);
    else if (std::strncmp(line, "evolve", 6) == 0 && (line[6] == 0 || line[6] == ' ' || line[6] == '\t')) {
        const char* input = line + 6; while (*input == ' ' || *input == '\t') ++input; evolutionCommand(input);
    }
    else if (std::strncmp(line, "release", 7) == 0 && (line[7] == 0 || line[7] == ' ' || line[7] == '\t')) {
        const char* input = line + 7; while (*input == ' ' || *input == '\t') ++input; releaseCommand(input);
    }
    else if (std::strncmp(line,"rest full",9)==0 && (line[9]==0 || line[9]==' ' || line[9]=='\t')) {
        const char* input=line+9;while(*input==' ' || *input=='\t')++input;recoveryCommand(input);
    }
    else if (std::strcmp(line, "companions") == 0) printCollection();
    else if (std::strcmp(line, "journal") == 0) printJournal();
    else if (std::strcmp(line, "art status") == 0) printArt();
    else if (std::strcmp(line, "art preview") == 0) previewArt();
    else if (std::strcmp(line, "art inventory") == 0 || std::strcmp(line, "art probe") == 0) {
        bool localArtIdle = true;
#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
        localArtIdle = art_.quiescent();
#endif
        // quiescent() is a shutdown barrier: it requires an explicit pause,
        // even when the development client is disabled. Refuse active work
        // first, then obtain that barrier only for this synchronous owner-task
        // diagnostic. No renderer/prefetch poll can run until this returns.
        if (assets_.status().busy || !localArtIdle) std::puts("SD inspection waits for active asset work to finish.");
        else {
            const bool previouslyPaused = assets_.paused();
            assets_.pause(true);
            if (!assets_.quiescent()) std::puts("SD inspection could not acquire the asset I/O barrier; retry when idle.");
            else if (std::strcmp(line, "art probe") == 0) assets::printSdBootProbe(sd_);
            else {
                const auto* member = activeMember(state_);
                assets::printSdInventory(sd_, member ? member->formId : 0);
            }
            assets_.pause(previouslyPaused);
        }
    }
    else if (std::strcmp(line, "starter status") == 0) printStarter();
    else if (std::strcmp(line, "starter next") == 0) starterInput(onboarding::Input::Next);
    else if (std::strcmp(line, "starter confirm") == 0) starterInput(onboarding::Input::Confirm);
    else if (std::strcmp(line, "starter back") == 0) starterInput(onboarding::Input::HoldBack);
    else if (std::strcmp(line, "mode status") == 0) printBattleMode();
    else if (std::strcmp(line, "mode tactical") == 0) proposeBattleMode(controls::BattleChoice::Tactical);
    else if (std::strcmp(line, "mode auto") == 0) proposeBattleMode(controls::BattleChoice::Auto);
    else if (std::strcmp(line, "mode confirm") == 0) confirmBattleMode();
    else if (std::strcmp(line, "mode cancel") == 0) { battleModeChoice_.cancel(); std::puts("Pending mode choice cancelled; saved mode unchanged."); }
    else if (std::strcmp(line, "net status") == 0) printNetwork();
    else if (std::strncmp(line, "net set-lan ", 12) == 0) configureNetwork(line + 12, true);
    else if (std::strncmp(line, "net set ", 8) == 0) configureNetwork(line + 8, false);
    else if (std::strcmp(line, "net retry") == 0) { assets_.cancel(); network_.retry(); }
    else if (std::strcmp(line, "net pause") == 0) { assets_.cancel(); network_.pause(true); }
    else if (std::strcmp(line, "net resume") == 0) network_.pause(false);
    else if (std::strcmp(line, "net forget") == 0) { assets_.cancel(); std::printf("Network forget: %s\n", esp_err_to_name(network_.forget())); }
    else if (std::strcmp(line, "assets status") == 0) printAssets();
    else if (std::strcmp(line, "assets warm") == 0) {
        if (!assetStorageReady_ || assetStorageFailed_) std::puts("SD cache unavailable; missing artwork is indicated. Reboot after reviewed recovery.");
        else if (!assets::DeviceAssetsClient::developmentAssetsEnabled()) std::puts("Asset downloads disabled in this build; exact-form local SD artwork remains independent.");
        else { warm(true); std::puts("Selected prefetch plan queued for service availability."); }
    }
    else if (std::strncmp(line, "assets fetch ", 13) == 0) {
        const char* protectedIds[4]{};
        for (std::size_t i = 0; i < plan_.count; ++i) protectedIds[i] = plan_.ids[i];
        // Explicit extra fetch preserves up to three current IDs plus itself.
        const auto count = plan_.count > 3 ? 3 : plan_.count;
        const bool accepted = assetStorageReady_ && !assetStorageFailed_ && network_.status().state == net::State::Online && assets_.request(network_.endpoint(), network_.allowsPrivateHttp(), line + 13, protectedIds, count);
        std::puts(accepted ? "Asset request queued." : "Asset request refused: offline, disabled, busy, invalid or recovery required.");
    } else if (std::strcmp(line, "motion status") == 0) printMotion();
    else if (std::strcmp(line, "motion recover") == 0) recoverMotion();
    else return false;
    return true;
}
} // namespace digivice
