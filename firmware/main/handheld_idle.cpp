#include "handheld_runtime.hpp"

#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
#include <cstdio>

namespace digivice {
bool HandheldRuntime::idleBlocked() const {
    using S = deviceui::Screen;
    const auto screen = ui_.screen();
    const bool quietScreen = screen == S::Egg || screen == S::Home || screen == S::Result ||
        screen == S::Collection || screen == S::Stats || screen == S::Settings || screen == S::Sound;
    const auto& network = network_.status();
    return !quietScreen || state_.phase == Phase::Encounter || ui_.pending() || touchPressed() ||
        battle_.locked() || !practice_.allowsCareAction(Action::Explore) || nearbyBusy() ||
        setup_.active() || interfacePaused_ || powerFrozen() ||
        (powerEnabled_ && power_.status().phase != power::Phase::Ready) ||
        usbTransferLease_ || usbTransfer_.active() || !usbTransfer_.quiescent() ||
        network_.radioLeased() || network_.scanStatus().busy || network.state == net::State::Joining ||
        network.state == net::State::Backoff ||
        network.probePending || assets_.status().busy || !art_.quiescent() || !partnerArt_.quiescent() ||
        !audio_.effectsQuiescent() || !display::quiescent() || !display::displayReady() ||
        !display::touchReady() || !imu_.ready() || !saves_.writable() || !usage_.writable() ||
        walkingFault_ || !idleSettings_.writable();
}

void HandheldRuntime::updateMusicScene() {
    using Scene = device::MusicScene;
    if (interfacePaused_ || powerFrozen() || idle_.blanked() || setup_.active() || encounterRecoveryRequired_ || !tradeSession_.healthy()) {
        audio_.setMusicScene(Scene::Quiet); return;
    }
    const auto model = interfaceModel();
    const bool nearbyBattle = model.nearby && model.nearby->stage == nearby::Stage::Playing;
    audio_.setMusicScene(state_.phase == Phase::Encounter || battle_.locked() || nearbyBattle ? Scene::Battle : Scene::Home);
}

void HandheldRuntime::interfaceActivity(std::uint64_t now) {
    idle_.activity(now);
}

void HandheldRuntime::pollIdle(std::uint64_t now) {
    // Explicit power/USB suspension owns the stronger pause. Screen idle never
    // pauses the IMU, touch, radio, or lifetime counter.
    if (interfacePaused_ || powerFrozen() || usbTransferLease_) return;
    const auto request = idle_.tick(now, idleBlocked());
    if (request == idle::Request::Prepare) {
        const bool durable = pollUsage(now, true);
        // A flush can commit a walking encounter. Recheck before blanking so
        // a newly published game transition is never hidden.
        const bool ready = durable && !idleBlocked();
        const auto error = ready ? display::setIdleBlank(true) : ESP_ERR_INVALID_STATE;
        idle_.completePrepare(ready && error == ESP_OK, now);
        if (idle_.blanked()) {
            audio_.setMusicScene(device::MusicScene::Quiet);
            ui_.cancelTouch(); setup_.cancelTouch();
            requireTouchRelease();
            std::puts("Screen idle: backlight off; touch and step sampling remain active.");
        }
    } else if (request == idle::Request::Wake) {
        const auto error = display::setIdleBlank(false);
        idle_.completeWake(error == ESP_OK, now);
        if (error == ESP_OK) {
            ui_.cancelTouch(); setup_.cancelTouch();
            requireTouchRelease();
            interfaceDirty_ = true;
            std::puts("Screen awake: wake contact consumed; release before choosing an action.");
        }
    }
}
} // namespace digivice
#endif
