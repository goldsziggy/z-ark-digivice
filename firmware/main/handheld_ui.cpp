#include "handheld_runtime.hpp"

#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace digivice {
namespace {
device::AudioCue cueFor(Message message) {
    using C = device::AudioCue;
    switch (message) {
    case Message::Hatched: return C::Hatch;
    case Message::Fed: return C::Feed;
    case Message::Played: case Message::Trained: return C::Play;
    case Message::Rested: return C::Rest;
    case Message::Encounter: return C::Encounter;
    case Message::Attacked: return C::Attack;
    case Message::Won: return C::Win;
    case Message::Captured: return C::CaptureSuccess;
    case Message::CaptureMissed: return C::CaptureFail;
    case Message::Retreated: return C::Retreat;
    case Message::Evolved: return C::Evolution;
    default: return C::Navigate;
    }
}
}

deviceui::Model HandheldRuntime::interfaceModel() const {
    deviceui::Model model;
    model.starterStage = starter_.stage(); model.selectedId = starter_.selectedId();
    model.starterCount = starter_.choiceCount();
    model.starterFormId = starterForm(state_, starter_.selectedId());
    model.sleepTimeoutSeconds = static_cast<std::uint16_t>(idle_.timeoutSeconds());
    model.writable = !tradeSession_.blocksForeground() && saves_.writable() && (state_.onboardingComplete || state_.starterOfferSeed);
    model.partyEditable = allowsCareAction(Action::PartyAdd) && allowsCareAction(Action::PartyRemove);
    model.encounterRecoveryRequired = encounterRecoveryRequired_ || !tradeSession_.healthy();
    model.inputEnabled = !powerFrozen() && !interfacePaused_ && !setup_.active() && !encounterRecoveryRequired_;
    model.motionAvailable = imu_.ready(); model.gyroEnabled = gyroEnabled_;
    const auto now = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
    model.stepsAvailable = physicalStepsReady(now);
    model.stepsRecovering = imu_.recovering();
    model.lifetimeSteps = usage_.total(); model.sessionSteps = usage_.session();
    model.stepStatus = usage_.writable() && !walkingFault_ ? motion::stepStatusText(imu_.stepReading().status) : "STEP SAVE RECOVERY";
    model.encounterReady = model.stepsAvailable && state_.phase == Phase::Home &&
        state_.encounterRate != EncounterRate::Off && !battle_.locked() && !setup_.active() && !nearbyBusy();
    model.battle = battle_.locked() ? &battle_.view() : nullptr;
    model.trade = &tradeWire_.view();
    model.tradeWritable = tradeSession_.healthy() && saves_.writable();
    model.tradeStatus = tradeSession_.blocksForeground() ? tradeSession_.diagnostic() : tradeStatus_;
    model.nearby = &nearby_.view(); model.nearbyLocalFighter = nearbyFighter_; model.nearbyStatus = nearbyStatus_;
    if (nearbyPhase_ == NearbyPhase::Active && nearbyShownSequence_ &&
        nearbyShownSession_ == nearby_.view().session && nearbyShownSequence_ == nearby_.view().match.sequence)
        model.nearbyTurnElapsedMs = static_cast<std::uint32_t>(std::min<std::uint64_t>(UINT32_MAX - 1, now - nearbyTurnAt_));
    model.muted = audio_.muted();
    model.volumePercent = static_cast<std::uint8_t>(audio_.volume());
    model.musicEnabled = audio_.musicEnabled();
    model.audioPreferencesWritable = audio_.preferencesWritable();
    model.audioAvailable = audio_.ready();
    const auto& sample = imu_.reading();
    if (gyroEnabled_ && sample.valid && sample.calibrated) {
        model.tiltX = static_cast<std::int16_t>(std::clamp(sample.tiltX, -1.0F, 1.0F) * 8);
        model.tiltY = static_cast<std::int16_t>(std::clamp(sample.tiltY, -1.0F, 1.0F) * 8);
    }
    return model;
}

void HandheldRuntime::beginInterface() {
    // Disposable drawing memory never shares the core's save or identity storage.
    frame_ = static_cast<std::uint16_t*>(heap_caps_malloc(deviceui::kPixels * sizeof(*frame_),
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    const auto panel = frame_ ? display::initialize() : display::Status{};
    audio_.setMusicScene(device::MusicScene::Quiet);
    const auto sound = audio_.begin();
    const auto usageNvs = usageBackend_.initialize();
    const auto now = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
    const auto idleSettings = idleSettings_.begin();
    idle_.configure(idleSettings_.seconds(), now);
    std::printf("Screen idle preference: %lu seconds (%s); no hardware sleep.\n",
        static_cast<unsigned long>(idle_.timeoutSeconds()), esp_err_to_name(idleSettings));
    const bool usageReady = usage_.restore(now); lastWalkingSaveMs_ = now;
    std::printf("Lifetime steps: %s (%s); %s\n", usageReady ? "ready" : "recovery", esp_err_to_name(usageNvs), usage_.diagnostic());
    const auto motion = imu_.begin();
    // Completed captures already belong to the collection. Reboot returns Home
    // without reopening their acknowledgement screen; the receipt stays saved.
    // An unfinished encounter can still reveal its last committed throw.
    if (state_.phase == Phase::Encounter && !encounterRecoveryRequired_ && !tradeSession_.blocksForeground())
        (void)battle_.startSavedCapture(state_, now); // Read-only reveal; never reapply a throw.
    uiSequence_ = state_.sequence;
    ui_.update(state_, interfaceModel());
    if (!saves_.writable()) ui_.notice("SAVE RECOVERY - USB STATUS");
    std::printf("Device init: framebuffer=%u B LCD=%s touch=%s sound=%s IMU=%s revision=0x%02x\n",
        frame_ ? static_cast<unsigned>(deviceui::kPixels * sizeof(*frame_)) : 0,
        esp_err_to_name(panel.display), esp_err_to_name(panel.touch), esp_err_to_name(sound),
        esp_err_to_name(motion), imu_.revision());
    if (sound == ESP_OK) audio_.play(device::AudioCue::Boot);
    printInterface();
}

void HandheldRuntime::interfaceIntent(deviceui::Intent intent) {
    using K = deviceui::IntentKind;
    if (!intent) return;
    interfaceActivity(static_cast<std::uint64_t>(esp_timer_get_time() / 1000));
    const auto previousScreen = ui_.screen();
    const char* notice = nullptr;
    if (powerFrozen() || interfacePaused_) { ui_.resolve("POWER TRANSITION"); return; }
    switch (intent.kind) {
    case K::GameAction: {
        // Touch proposals use exactly the same copy -> apply -> verified save ->
        // publish order as USB. Never publish a write whose durability is uncertain.
        if (tradeSession_.blocksForeground()) { notice = "RECONNECT TRADE PEER TO FINISH"; break; }
        if (battle_.locked()) { notice = "WAIT FOR THE TURN"; break; }
        if (nearbyBusy()) { notice = "CLOSE NEARBY FIRST"; break; }
        if (!saves_.writable()) { notice = "SAVE RECOVERY - USB STATUS"; break; }
        if (state_.onboardingComplete && !state_.worldSeed) { notice = "WAIT FOR WORLD SAVE"; break; }
        if (!practice_.allowsCareAction(intent.action)) { notice = "FINISH ACTIVE PRACTICE FIRST"; break; }
        if (intent.action == Action::Hatch) { notice = "USE STARTER CONFIRMATION"; break; }
        if (intent.action == Action::EncounterRate &&
            !pollUsage(static_cast<std::uint64_t>(esp_timer_get_time() / 1000), true)) {
            notice = "STEP SAVE RECOVERY"; break;
        }
        State candidate = state_;
        const bool automatic = intent.action == Action::Auto || intent.action == Action::AutoFight || intent.action == Action::AutoResume || intent.action == Action::Focus;
        const auto error = intent.action == Action::Auto ? applyAuto(candidate, &battleTrace_) :
            intent.action == Action::AutoFight ? applyAutoFight(candidate, &battleTrace_) :
            intent.action == Action::AutoResume ? applyAutoResume(candidate, &battleTrace_) :
            intent.action == Action::Focus ? applyFocus(candidate, intent.value, &battleTrace_) : apply(candidate, intent.action, intent.value);
        if (error != Error::None) { notice = errorText(error); audio_.play(device::AudioCue::Error); break; }
        if (!saves_.checkpoint(candidate)) { notice = "SAVE UNCERTAIN - REBOOT TO RECOVER"; audio_.play(device::AudioCue::Error); break; }
        const auto now = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
        const bool playback = automatic ? battle_.startAuto(battleTrace_, now) :
            battle_.startTactical(state_, candidate, intent.action, intent.value, now);
        state_ = candidate;
        if (playback) uiSequence_ = state_.sequence; // Sequencer owns readable, ordered cues.
        if (intent.action == Action::EncounterRate) walkingPending_ = 0;
        std::printf("Touch action saved: sequence=%lu phase=%u\n", static_cast<unsigned long>(state_.sequence), static_cast<unsigned>(state_.phase));
        break;
    }
    case K::StarterNext: starterInput(onboarding::Input::Next); audio_.play(device::AudioCue::Navigate); break;
    case K::StarterPrevious:
        starterInput(onboarding::Input::Previous);
        audio_.play(device::AudioCue::Navigate); break;
    case K::StarterConfirm: starterInput(onboarding::Input::Confirm); audio_.play(device::AudioCue::Navigate); break;
    case K::StarterBack: starterInput(onboarding::Input::HoldBack); audio_.play(device::AudioCue::Back); break;
    case K::ToggleMute:
        if (audio_.setMuted(!audio_.muted()) != ESP_OK) notice = "SETTINGS NOT SAVED";
        if (!audio_.muted()) audio_.play(device::AudioCue::Navigate);
        break;
    case K::Volume:
        if (intent.value > 100 || audio_.setVolume(static_cast<std::uint8_t>(intent.value)) != ESP_OK)
            notice = "SETTINGS NOT SAVED";
        else audio_.play(device::AudioCue::Navigate);
        break;
    case K::ToggleMusic:
        if (audio_.setMusicEnabled(!audio_.musicEnabled()) != ESP_OK) notice = "SETTINGS NOT SAVED";
        break;
    case K::ToggleGyro:
        gyroEnabled_ = !gyroEnabled_ && imu_.ready();
        if (gyroEnabled_) { imu_.recenter(); notice = "HOLD STILL TO CENTER TILT"; }
        audio_.play(device::AudioCue::Navigate); break;
    case K::Navigation:
        audio_.play(previousScreen == deviceui::Screen::Capture ? device::AudioCue::CaptureArm : device::AudioCue::Navigate); break;
    case K::SleepTimeout:
        if (idleSettings_.setSeconds(intent.value) == ESP_OK) {
            idle_.configure(intent.value, static_cast<std::uint64_t>(esp_timer_get_time() / 1000));
            audio_.play(device::AudioCue::Navigate);
        } else notice = "SLEEP SETTING SAVE FAILED";
        break;
    case K::OpenSetup:
        if (nearbyBusy()) { notice = "CLOSE NEARBY FIRST"; break; }
        setup_.open(); requireTouchRelease(); ui_.cancelTouch();
        audio_.play(device::AudioCue::Navigate); break;
    case K::OpenNearby: case K::CloseNearby: case K::NearbyChallenge:
    case K::NearbyAccept: case K::NearbyChoose: case K::NearbyCancel:
        nearbyIntent(intent); break;
    case K::TradeInvite: case K::TradeOffer: case K::TradeConfirm:
    case K::TradeCancel: case K::TradeClose:
        tradeIntent(intent); break;
    case K::None: break;
    }
    ui_.resolve(notice);
    ui_.update(state_, interfaceModel());
    if (notice) ui_.notice(notice);
    interfaceDirty_ = true;
}

void HandheldRuntime::pollCareAndAuto(std::uint64_t now) {
    if (powerFrozen() || interfacePaused_ || setup_.active()) {
        careAwakeMs_ = now;
        return;
    }
    auto model = interfaceModel();
    // Rules 18 focus prompt: one ring cycle plus a short grace, then Auto plays
    // the exchange untapped. Never a punishment for looking away.
    const bool focusOpen = state_.phase == Phase::Encounter &&
        (state_.autoCapture == AutoCapture::FocusStrike || state_.autoCapture == AutoCapture::FocusBlock);
    if (!focusOpen) focusSinceMs_ = 0;
    else if (!focusSinceMs_ || focusSequence_ != state_.sequence) { focusSinceMs_ = now; focusSequence_ = state_.sequence; }
    else if (!touchPressed() && !ui_.pending() && !battle_.locked() && now - focusSinceMs_ >= kFocusTimeoutMs) {
        focusSinceMs_ = now;
        deviceui::Intent intent;
        intent.kind = deviceui::IntentKind::GameAction;
        intent.action = Action::Focus;
        intent.value = kFocusNoTap;
        interfaceIntent(intent);
        model = interfaceModel();
    }
    // Touch is already applied this frame, so Run Away wins over the next chunk.
    if (!touchPressed() && !ui_.pending() && state_.sequence != autoStartSequence_ &&
        deviceui::autoFightReady(state_, model) && allowsCareAction(Action::AutoFight)) {
        autoStartSequence_ = state_.sequence;
        deviceui::Intent intent;
        intent.kind = deviceui::IntentKind::GameAction;
        intent.action = Action::AutoFight;
        interfaceIntent(intent);
        model = interfaceModel();
    }
    if (touchPressed() || ui_.pending() || state_.phase != Phase::Home || !state_.onboardingComplete ||
        !saves_.writable() || battle_.locked() || nearbyBusy() || !allowsCareAction(Action::CareMinute)) {
        if (state_.phase != Phase::Home || interfacePaused_) careAwakeMs_ = now;
        return;
    }
    if (!careAwakeMs_ || now < careAwakeMs_) { careAwakeMs_ = now; return; }
    if (now - careAwakeMs_ < 60000) return;
    careAwakeMs_ = now; // One awake minute, even if the clock jumped.
    if (state_.careMinute == UINT32_MAX) return;
    deviceui::Intent intent;
    intent.kind = deviceui::IntentKind::GameAction;
    intent.action = Action::CareMinute;
    intent.value = state_.careMinute + 1;
    interfaceIntent(intent);
}

// Touch pipeline (docs/TOUCH_SPRINT.md): touchstream::Stream turns SPD2010 polls
// into Down/Move/Up with a confirmed release, roll-off filtering and a quiet
// rearm of the release latch. Bus failure and idle wake cancel, never confirm.
void HandheldRuntime::handleTouchSample(const touchstream::Sample& sample, const deviceui::Model& model) {
    const auto step = touch_.feed(sample);
    if (step.activity) interfaceActivity(sample.atMs);
    if (step.cancel) { ui_.cancelTouch(); setup_.cancelTouch(); }
    // An observed release, or a quiet glass after a latch: clears the capture
    // contact latch without proposing an action.
    if (step.released) ui_.acknowledgeContactReleased();
    if (!step.hasEvent) return;
    if (setup_.active()) {
        setup_.touch(step.event);
        if (!setup_.active()) { ui_.cancelTouch(); requireTouchRelease(); }
        interfaceDirty_ = true;
        return;
    }
    const bool wasIdle = ui_.interactionIdle();
    interfaceIntent(ui_.touch(state_, model, step.event));
    // Held Moves no longer force a full redraw (a 75-141 ms frame blinds touch
    // polling); Down, Up and a press cancelled by its Move still redraw. Real
    // intents already mark dirty. Capture keeps its partial-frame path.
    if (!captureFrameActive_ && (step.event.kind != deviceui::TouchKind::Move || wasIdle != ui_.interactionIdle()))
        interfaceDirty_ = true;
}

void HandheldRuntime::sampleTouchDuringFlush(void* context) {
    auto& self = *static_cast<HandheldRuntime*>(context);
    const auto now = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
    // Same 20 ms cadence as the loop: the UI rejects taps shorter than 20 ms.
    if (self.flushTouchCount_ >= kFlushTouchQueue || !display::touchReady() || now < self.lastTouchMs_ + 20) return;
    self.lastTouchMs_ = now;
    display::TouchPoint point;
    const auto read = display::pollTouch(point);
    self.flushTouch_[self.flushTouchCount_++] = {read == ESP_OK, point.pressed, point.fresh, static_cast<std::int16_t>(point.x),
                                                 static_cast<std::int16_t>(point.y), now, self.idle_.blanked()};
}

void HandheldRuntime::requireTouchRelease() {
    touch_.requireRelease(static_cast<std::uint64_t>(esp_timer_get_time() / 1000));
    flushTouchCount_ = 0; // queued samples predate the cancellation
}

void HandheldRuntime::pollInterface(std::uint64_t now) {
    if (powerFrozen() || interfacePaused_) return;
    setup_.poll();
    (void)imu_.poll(now);
    (void)idle_.observeMotion(imu_.reading(), now);
    pollBattlePresentation(now);
    auto model = interfaceModel();
    ui_.update(state_, model);
    if (state_.sequence != uiSequence_) {
        interfaceActivity(now);
        uiSequence_ = state_.sequence; interfaceDirty_ = true;
        audio_.play(cueFor(state_.message));
    }
    // Samples read between the previous frame's DMA stripes, in order, then this pass's sample.
    for (std::size_t i = 0; i < flushTouchCount_; ++i) handleTouchSample(flushTouch_[i], model);
    flushTouchCount_ = 0;
    if (frame_ && display::displayReady() && display::touchReady() && now - lastTouchMs_ >= (captureFrameActive_ ? 5u : 20u)) {
        lastTouchMs_ = now;
        display::TouchPoint point;
        const auto read = display::pollTouch(point);
        handleTouchSample({read == ESP_OK, point.pressed, point.fresh, static_cast<std::int16_t>(point.x),
                           static_cast<std::int16_t>(point.y), now, idle_.blanked()}, model);
    }
    pollCareAndAuto(now);
    pollIdle(now);
    updateMusicScene();
    // Full frames remain the fallback for entry, state changes and other screens.
    // Capture redraws only the bounded creature/ring region at a 33 ms start-to-
    // start cadence. The same framebuffer and existing rotated DMA stripe are
    // reused; there is no second frame or saved-background allocation.
    model = interfaceModel(); ui_.update(state_, model);
    const bool artReady = assetStorageReady_ && !assetStorageFailed_ && sd_.mounted();
    const bool canDraw = !idle_.blanked() && frame_ && display::displayReady();
    captureFrameActive_ = canDraw && !setup_.active() && ui_.captureAnimating(state_, model);
    const bool captureInvalid = !captureFrameValid_ || lastFrameSequence_ != state_.sequence ||
                                lastArtStorageReady_ != artReady || interfaceDirty_;
    const bool captureDue = captureFrameActive_ && (captureInvalid || now < captureFrameStartedMs_ ||
                                                   now - captureFrameStartedMs_ >= 33);
    const bool ordinaryDue = !captureFrameActive_ && (captureFrameValid_ ||
        (now >= lastFrameMs_ && now - lastFrameMs_ >= 80 &&
         (interfaceDirty_ || now - lastFrameMs_ >= 160)));
    if (canDraw && (captureDue || ordinaryDue)) {
        const auto started = esp_timer_get_time();
        bool partial = captureFrameActive_ && !captureInvalid;
        if (captureFrameActive_) captureFrameStartedMs_ = static_cast<std::uint64_t>(started / 1000);
        if (!setup_.active()) {
            // prepare returns cached animation-frame views for a stable request.
            // Entry/form/scene/storage changes may load artwork once, and always
            // use a full frame. Never retain Artwork views across another prepare.
            model.artwork = art_.prepare(ui_.artRequest(state_, model, now), artReady);
            const auto partnerRequest = ui_.partnerArtRequest(state_, model, now);
            if (partnerRequest.formId) model.partnerArtwork = partnerArt_.prepare(partnerRequest, artReady).sprite;
            for (std::size_t tile = 0; tile < deviceui::kTiles; ++tile) {
                const auto tileRequest = ui_.tileArtRequest(state_, model, tile, now);
                if (tileRequest.formId) model.tileArtwork[tile] = tileArt_[tile].prepare(tileRequest, artReady).sprite;
            }
        }
        bool drawnFrame = setup_.active() ? setup_.render(frame_, deviceui::kPixels) :
            partial ? ui_.renderCaptureRegion(state_, model, frame_, deviceui::kPixels, now) :
                      ui_.render(state_, model, frame_, deviceui::kPixels, now);
        if (!drawnFrame && partial) {
            // A short-lived overlay or controller guard may need pixels beyond
            // the region. Reconstruct the full frame before transferring it.
            partial = false;
            drawnFrame = ui_.render(state_, model, frame_, deviceui::kPixels, now);
        }
        bool completed = false;
        if (drawnFrame) {
            const auto drawn = esp_timer_get_time();
            const auto renderUs = static_cast<std::uint32_t>(drawn - started);
            maxRenderUs_ = std::max(maxRenderUs_, renderUs);
            const int x = partial ? deviceui::Controller::kCaptureX : 0;
            const int y = partial ? deviceui::Controller::kCaptureY : 0;
            const int width = partial ? deviceui::Controller::kCaptureWidth : deviceui::kSize;
            const int height = partial ? deviceui::Controller::kCaptureHeight : deviceui::kSize;
            const auto result = display::flushRgb565(x, y, width, height,
                frame_ + y * deviceui::kSize + x, deviceui::kSize, &HandheldRuntime::sampleTouchDuringFlush, this);
            const auto flushUs = static_cast<std::uint32_t>(esp_timer_get_time() - drawn);
            maxFlushUs_ = std::max(maxFlushUs_, flushUs);
            if (result == ESP_OK) {
                ++renderedFrames_; completed = true;
                if (partial) {
                    ++captureFrames_;
                    maxCaptureRenderUs_ = std::max(maxCaptureRenderUs_, renderUs);
                    maxCaptureFlushUs_ = std::max(maxCaptureFlushUs_, flushUs);
                }
            } else {
                ui_.cancelTouch(); setup_.cancelTouch(); requireTouchRelease();
                std::printf("Display flush stopped: %s; USB/save remain available.\n", esp_err_to_name(result));
            }
        }
        const auto elapsed = static_cast<std::uint32_t>(esp_timer_get_time() - started);
        maxFrameUs_ = std::max(maxFrameUs_, elapsed);
        lastFrameMs_ = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
        captureFrameValid_ = completed && captureFrameActive_;
        lastFrameSequence_ = state_.sequence; lastArtStorageReady_ = artReady;
        interfaceDirty_ = false;
        // Touch read between this frame's stripes is handled now, not a pass later.
        if (flushTouchCount_) {
            const auto after = interfaceModel();
            for (std::size_t i = 0; i < flushTouchCount_; ++i) handleTouchSample(flushTouch_[i], after);
            flushTouchCount_ = 0;
        }
    } else if (!canDraw) captureFrameValid_ = false;

}

void HandheldRuntime::pauseInterface(bool paused) {
    // Power-off, sleep, and USB pauses are not care time.
    careAwakeMs_ = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
    interfaceActivity(careAwakeMs_);
    interfacePaused_ = paused;
    if (paused) { setup_.suspend(); battle_.cancel(); }
    captureFrameActive_ = captureFrameValid_ = false;
    ui_.cancelTouch(); requireTouchRelease();
    art_.pause(paused); partnerArt_.pause(paused);
    for (auto& tile : tileArt_) tile.pause(paused);
    if (paused) audio_.setMusicScene(device::MusicScene::Quiet);
    audio_.pause(paused);
    const auto motion = imu_.pause(paused);
    const auto panel = display::setSuspended(paused);
    if (motion != ESP_OK || (panel != ESP_OK && display::displayReady()))
        std::printf("Peripheral power transition: LCD=%s IMU=%s\n", esp_err_to_name(panel), esp_err_to_name(motion));
    interfaceDirty_ = true;
}

bool HandheldRuntime::interfaceQuiescent() const {
    // A failed flush may retain an outstanding DMA transfer. Require its real
    // completion as well as the audio worker and sensor suspension barrier.
    for (const auto& tile : tileArt_) if (!tile.quiescent()) return false;
    return display::quiescent() && audio_.quiescent() && art_.quiescent() && partnerArt_.quiescent() && (!interfacePaused_ || imu_.quiescent());
}

void HandheldRuntime::printInterface() const {
    const auto& panel = display::status(); const auto& sample = imu_.reading();
    std::printf("device screen=%s framebuffer=%u lcd=%s touch=%s frames=%lu maxFrameUs=%lu paused=%d\n",
        setup_.active() ? "wifi-setup" : deviceui::screenName(ui_.screen()), frame_ ? static_cast<unsigned>(deviceui::kPixels * sizeof(*frame_)) : 0,
        esp_err_to_name(panel.display), esp_err_to_name(panel.touch), static_cast<unsigned long>(renderedFrames_),
        static_cast<unsigned long>(maxFrameUs_), interfacePaused_);
    std::printf("device timing maxRenderUs=%lu maxFlushUs=%lu\n",
        static_cast<unsigned long>(maxRenderUs_), static_cast<unsigned long>(maxFlushUs_));
    std::printf("device capture partialFrames=%lu region=%ux%u cadenceMs=33 maxRenderUs=%lu maxFlushUs=%lu; cadence is requested, not measured FPS\n",
        static_cast<unsigned long>(captureFrames_), static_cast<unsigned>(deviceui::Controller::kCaptureWidth), static_cast<unsigned>(deviceui::Controller::kCaptureHeight),
        static_cast<unsigned long>(maxCaptureRenderUs_), static_cast<unsigned long>(maxCaptureFlushUs_));
    std::printf("device idle blanked=%d backlightOff=%d timeoutSeconds=%lu settingWritable=%d; mode=screen-only\n",
        idle_.blanked(), panel.idleBlanked, static_cast<unsigned long>(idle_.timeoutSeconds()), idleSettings_.writable());
    std::printf("device artwork allocated=%u sprite=%s scene=%s\n",
        static_cast<unsigned>(art_.allocatedBytes()), art_.spriteDiagnostic(), art_.backgroundDiagnostic());
    std::printf("device steps total=%llu session=%lu pending=%lu status=%s writable=%d; nearby phase=%u sequence=%lu %s\n",
        static_cast<unsigned long long>(usage_.total()), static_cast<unsigned long>(usage_.session()),
        static_cast<unsigned long>(walkingPending_), motion::stepStatusText(imu_.stepReading().status),
        usage_.writable() && !walkingFault_, static_cast<unsigned>(nearbyPhase_),
        static_cast<unsigned long>(nearby_.view().match.sequence), nearbyStatus_);
    std::printf("device touch samples=%lu errors=%lu lockMisses=%lu lastError=%s presses=%lu releases=%lu chatter=%lu rearms=%lu rolledOff=%lu xy=%d,%d\n",
        static_cast<unsigned long>(panel.touchSamples), static_cast<unsigned long>(panel.touchErrors),
        static_cast<unsigned long>(panel.touchLockMisses),
        esp_err_to_name(panel.lastTouchError), static_cast<unsigned long>(touch_.presses()), static_cast<unsigned long>(touch_.releases()),
        static_cast<unsigned long>(touch_.chatter()), static_cast<unsigned long>(touch_.rearms()), static_cast<unsigned long>(touch_.rolledOff()), touch_.x(), touch_.y());
    std::printf("device audio ready=%d muted=%d volume=%u error=%s; imu ready=%d revision=0x%02x valid=%d calibrated=%d tilt=%d error=%s\n",
        audio_.ready(), audio_.muted(), audio_.volume(), esp_err_to_name(audio_.lastError()),
        imu_.ready(), imu_.revision(), sample.valid, sample.calibrated, gyroEnabled_, esp_err_to_name(imu_.lastError()));
    std::printf("device sound music=%d settingsWritable=%d settingsError=%s\n", audio_.musicEnabled(),
        audio_.preferencesWritable(), esp_err_to_name(audio_.preferencesError()));
    std::printf("device accelG=%.3f,%.3f,%.3f gyroDps=%.2f,%.2f,%.2f sampleMs=%llu\n",
        static_cast<double>(sample.accelerationG.x), static_cast<double>(sample.accelerationG.y), static_cast<double>(sample.accelerationG.z),
        static_cast<double>(sample.gyroDps.x), static_cast<double>(sample.gyroDps.y), static_cast<double>(sample.gyroDps.z),
        static_cast<unsigned long long>(sample.observedAtMs));
    std::printf("device heap internalFree=%u internalLargest=%u psramFree=%u psramLargest=%u\n",
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
    std::printf("device mainStackMinimumFreeBytes=%u\n",
        static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
}

bool HandheldRuntime::interfaceCommand(const char* line) {
    if (!std::strcmp(line, "device status")) { printInterface(); return true; }
    if (std::strncmp(line, "device ", 7)) return false;
    if (powerFrozen() || interfacePaused_) { std::puts("Device controls paused for power transition."); return true; }
    if (!std::strcmp(line, "device wake")) { interfaceActivity(static_cast<std::uint64_t>(esp_timer_get_time() / 1000)); pollIdle(static_cast<std::uint64_t>(esp_timer_get_time() / 1000)); }
    else if (!std::strcmp(line, "device sound")) audio_.play(device::AudioCue::Boot);
    else if (!std::strcmp(line, "device setup")) { setup_.open(); ui_.cancelTouch(); requireTouchRelease(); }
    else if (!std::strcmp(line, "device mute")) { if (audio_.setMuted(true) != ESP_OK) std::puts("Sound changed; SETTINGS NOT SAVED."); }
    else if (!std::strcmp(line, "device unmute")) { if (audio_.setMuted(false) != ESP_OK) std::puts("Sound changed; SETTINGS NOT SAVED."); }
    else if (!std::strcmp(line, "device recenter")) { imu_.recenter(); ui_.notice("HOLD STILL TO CENTER TILT"); }
    else if (!std::strcmp(line, "device art-retry")) art_.retry();
    else std::puts("device status | wake | sound | mute | unmute | recenter | art-retry | setup");
    interfaceDirty_ = true;
    return true;
}
} // namespace digivice
#endif
