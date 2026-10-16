#include "handheld_runtime.hpp"

#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
#include <algorithm>
#include <cstdio>
#include <iterator>

namespace digivice {
bool HandheldRuntime::idleDiscovery() const {
    // Backlight-only idle leaves discovery/radio polling alive. Any invitation,
    // match, trade or radio transition must remain visible and can wake us.
    const auto screen = ui_.screen();
    const auto trade = tradeWire_.view().stage;
    return (screen == deviceui::Screen::ExpeditionLobby || screen == deviceui::Screen::Nearby) &&
        nearbyPhase_ == NearbyPhase::Active && nearby_.view().stage == nearby::Stage::Discovering &&
        (trade == tradewire::Stage::Closed || trade == tradewire::Stage::Discovering) &&
        tradeSession_.healthy() && !tradeNegotiating();
}

bool HandheldRuntime::idleForegroundActive() const {
    using S = deviceui::Screen;
    const auto screen = ui_.screen();
    const bool quietScreen = screen == S::Egg || screen == S::Home || screen == S::Result ||
        screen == S::Collection || screen == S::Squad || screen == S::Box || screen == S::Stats ||
        screen == S::Settings || screen == S::Sound || screen == S::NfcTest || screen == S::Care || screen == S::Explore ||
        screen == S::EncounterSettings || screen == S::Expeditions || screen == S::ExpeditionLobby ||
        screen == S::Evolution || screen == S::EvolutionResult || screen == S::EvolutionReview ||
        screen == S::ReleaseReview || screen == S::ModeReview || screen == S::Starter ||
        screen == S::StarterReview || screen == S::Nearby;
    return !quietScreen || state_.phase == Phase::Encounter || ui_.pending() || touchPressed() ||
        battle_.locked() || !practice_.allowsCareAction(Action::Explore) ||
        (nearbyBusy() && !idleDiscovery()) || tradeNegotiating() ||
        setup_.active() || (powerEnabled_ && power_.pressed());
}

bool HandheldRuntime::idleBlocked() const {
    const auto& network = network_.status();
    return nfc_.busy() || idleForegroundActive() ||
        setup_.active() || interfacePaused_ || powerFrozen() ||
        usbTransferLease_ || usbTransfer_.active() || !usbTransfer_.quiescent() ||
        (network_.radioLeased() && !idleDiscovery()) || network_.scanStatus().busy || network.state == net::State::Joining ||
        network.state == net::State::Backoff ||
        network.probePending || assets_.status().busy || !art_.quiescent() || !partnerArt_.quiescent() ||
        std::any_of(std::begin(tileArt_), std::end(tileArt_), [](const auto& tile) { return !tile.quiescent(); }) ||
        !audio_.effectsQuiescent() || !display::quiescent() || !display::displayReady() ||
        !display::touchReady() || !imu_.ready() || !saves_.writable() || !usage_.writable() ||
        walkingFault_ || !idleSettings_.writable();
}

void HandheldRuntime::updateMusicScene() {
    using Scene = device::MusicScene;
    // Keep diagnostics and preferences quiet on the fixed-pitch board.
    if(audio_.buzzerOnly() && ui_.screen()!=deviceui::Screen::Home &&
       ui_.screen()!=deviceui::Screen::Encounter && ui_.screen()!=deviceui::Screen::Battle) {
        audio_.setMusicScene(Scene::Quiet); return;
    }
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
    // Foreground sessions/real contact retain visibility. Temporary work and
    // sensor readiness only defer entry; they never manufacture a wake event.
    if (idleForegroundActive()) interfaceActivity(now);
    const auto request = idle_.tick(now, idleBlocked());
    if (request == idle::Request::Prepare) {
        const bool durable = pollUsage(now, true);
        // A flush can commit a walking encounter. Recheck before blanking so
        // a newly published game transition is never hidden.
        const bool ready = durable && !idleBlocked();
        const auto error = ready ? display::setIdleBlank(true) : ESP_ERR_INVALID_STATE;
        idle_.completePrepare(ready && error == ESP_OK, now);
        if (idle_.blanked()) {
            audio_.setScreenBlanked(true);
            audio_.setMusicScene(device::MusicScene::Quiet);
            ui_.cancelTouch(); setup_.cancelTouch();
            requireTouchRelease();
            std::puts("Screen idle: backlight off; touch and step sampling remain active.");
        }
    } else if (request == idle::Request::Wake) {
        const auto error = display::setIdleBlank(false);
        idle_.completeWake(error == ESP_OK, now);
        if (error == ESP_OK) {
            audio_.setScreenBlanked(false);
            ui_.cancelTouch(); setup_.cancelTouch();
            requireTouchRelease();
            interfaceDirty_ = true;
            std::puts("Screen awake: wake contact consumed; release before choosing an action.");
        }
    }
}
} // namespace digivice
#endif
