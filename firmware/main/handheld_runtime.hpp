#pragma once
#include "sdkconfig.h"
#include "sd_asset_storage.hpp"
#include "board_hal.hpp"
#include "device_assets_client.hpp"
#include "game.hpp"
#include "../runtime/entropy_seed.hpp"
#include "motion_adapter.hpp"
#include "network_adapter.hpp"
#include "save_store.hpp"
#include "practice_nvs.hpp"
#include "trade_nvs.hpp"
#include "../runtime/trade_session.hpp"
#include "../runtime/trade_protocol.hpp"
#include "../runtime/motion.hpp"
#include "../runtime/power.hpp"
#include "../runtime/step_delivery.hpp"
#include "../runtime/recovery_choice.hpp"
#include "../runtime/prefetch.hpp"
#include "../runtime/starter.hpp"
#include "../runtime/battle_mode_choice.hpp"
#include "../runtime/evolution_choice.hpp"
#include "../runtime/usb_sd_transfer.hpp"

#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
#include "display_touch.hpp"
#include "device_audio.hpp"
#include "device_imu.hpp"
#include "device_art.hpp"
#include "device_setup.hpp"
#include "usage_nvs.hpp"
#include "idle_settings.hpp"
#include "../runtime/idle.hpp"
#include "nearby_radio.hpp"
#include "../runtime/nearby_protocol.hpp"
#include "../runtime/device_ui.hpp"
#endif

namespace digivice {
// One main-task owner. Keep this object in static storage: the optional client
// has bounded ~20 KiB work buffers which do not belong on the main task stack.
class HandheldRuntime {
public:
    HandheldRuntime(State& state, storage::SaveStore& saves, entropy::Seeds startupSeeds = {})
        : state_(state), saves_(saves), startupSeeds_(startupSeeds), cache_(sd_), steps_(1), practice_(practiceBackend_) {}
    void begin();
    void poll();
    // Cooperative console/runtime wait; only a healthy active capture animation
    // needs the shorter wait. This is a cadence request, not measured FPS.
    std::uint32_t recommendedPollDelayMs() const {
#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
        return captureFrameActive_ && !interfacePaused_ && !powerFrozen() ? 10u : 20u;
#else
        return 20u;
#endif
    }
    bool command(char* line);
    // Console discards partial input when this epoch changes (shutdown/resume).
    std::uint32_t inputEpoch() const { return inputEpoch_; }
    bool powerFrozen() const { return powerFrozen_ || power_.frozen(); }
    board::Capabilities capabilities(board::Capabilities base) const;
    bool allowsCareAction(Action action) const {
        return !encounterRecoveryRequired_ && (!state_.onboardingComplete || state_.worldSeed) && !tradeSession_.blocksForeground() && (action != Action::Hatch || state_.starterOfferSeed) &&
            practice_.allowsCareAction(action) && !battlePlaybackLocked() && !nearbyBusy();
    }
private:
    void beginTradeStorage();
    void resolveTestEncounterAtBoot();
    bool ensureWorldSeed();
    bool encounterRecoveryRequired_ = false;
#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
    void beginInterface();
    void pollInterface(std::uint64_t now);
    void pauseInterface(bool paused);
    bool interfaceQuiescent() const;
    bool interfaceCommand(const char* line);
    void printInterface() const;
    deviceui::Model interfaceModel() const;
    void interfaceIntent(deviceui::Intent intent);
    bool pollUsage(std::uint64_t now, bool force = false);
    bool prepareUsageRestart();
    bool physicalStepsReady(std::uint64_t now) const;
    bool battlePlaybackLocked() const { return battle_.locked(); }
    void pollBattlePresentation(std::uint64_t now);
    void pollIdle(std::uint64_t now);
    void updateMusicScene();
    void interfaceActivity(std::uint64_t now);
    bool idleBlocked() const;
    void beginNearby();
    bool openTradeRadio(std::uint64_t now);
    void pollTradePersistence(std::uint64_t now);
    void tradeIntent(deviceui::Intent intent);
    bool tradeNegotiating() const;
    void closeNearby();
    void pollNearby(std::uint64_t now);
    void nearbyIntent(deviceui::Intent intent);
    bool nearbyBusy() const { return nearbyPhase_ != NearbyPhase::Idle; }
    bool nearbyQuiescent() const { return !nearbyBusy() && nearbyRadio_.quiescent(); }
    bool nearbyRecoveryRestartReady() const { return nearbyPhase_ == NearbyPhase::Fault && nearbyRadio_.quiescent(); }
    enum class NearbyPhase : std::uint8_t { Idle, Starting, Active, Stopping, Fault };
    nearby::Radio nearbyRadio_;
    nearby::Protocol nearby_;
    tradewire::Protocol tradeWire_;
    bool tradePeerTerminal_ = false, tradeTxTurn_ = false;
    const char* tradeStatus_ = "Nearby trade is closed";
    nearby::Fighter nearbyFighter_{};
    NearbyPhase nearbyPhase_ = NearbyPhase::Idle;
    std::uint64_t nearbyDeadline_ = 0, nearbyTurnAt_ = 0, nearbyAttackCueAt_ = 0;
    std::uint32_t nearbyTxToken_ = 0, nearbyShownSequence_ = 0;
    std::uint64_t nearbyShownSession_ = 0;
    std::uint8_t nearbyCuePhase_ = 0;
    bool nearbyNetworkPaused_ = false, nearbyAssetsPaused_ = false, nearbyLease_ = false;
    const char* nearbyStatus_ = "Nearby is closed";
    deviceui::Controller ui_;
    device::Audio audio_;
    device::Imu imu_;
    device::Art art_;
    device::Art partnerArt_;
    usage::NvsBackend usageBackend_;
    usage::Store usage_{usageBackend_};
    idle::Controller idle_;
    idle::NvsSettings idleSettings_;
    battlepresentation::Sequencer battle_;
    autobattle::Trace battleTrace_{}; // Static owner storage, never main-task stack.
    std::uint32_t walkingPending_ = 0;
    std::uint64_t lastWalkingSaveMs_ = 0;
    bool walkingFault_ = false;
    std::uint16_t* frame_ = nullptr;
    std::uint64_t lastTouchMs_ = 0, lastFrameMs_ = 0;
    std::uint32_t uiSequence_ = UINT32_MAX, touchPresses_ = 0, touchReleases_ = 0;
    std::uint32_t renderedFrames_ = 0, maxFrameUs_ = 0;
    std::uint32_t maxRenderUs_ = 0, maxFlushUs_ = 0;
    std::uint64_t captureFrameStartedMs_ = 0;
    std::uint32_t captureFrames_ = 0, maxCaptureRenderUs_ = 0, maxCaptureFlushUs_ = 0;
    std::uint32_t lastFrameSequence_ = UINT32_MAX;
    bool captureFrameActive_ = false, captureFrameValid_ = false, lastArtStorageReady_ = false;
    std::int16_t touchX_ = 0, touchY_ = 0;
    bool touchPressed_ = false, touchNeedsRelease_ = true, gyroEnabled_ = false;
    bool interfacePaused_ = false, interfaceDirty_ = true;
#else
    void beginInterface() {}
    void pollInterface(std::uint64_t) {}
    void pauseInterface(bool) {}
    bool interfaceQuiescent() const { return true; }
    bool interfaceCommand(const char*) { return false; }
    bool pollUsage(std::uint64_t, bool = false) { return true; }
    bool prepareUsageRestart() { return true; }
    bool physicalStepsReady(std::uint64_t) const { return false; }
    bool battlePlaybackLocked() const { return false; }
    bool nearbyBusy() const { return false; }
    bool nearbyQuiescent() const { return true; }
    bool nearbyRecoveryRestartReady() const { return false; }
    void interfaceActivity(std::uint64_t) {}
    void closeNearby() {}
    void pollNearby(std::uint64_t) {}
#endif
    void warm(bool force = false);
    void pollPower(std::uint64_t now);
    void printPower() const;
    power::Work drainPower(std::uint64_t now);
    void beginPowerShutdown(std::uint64_t now);
    void resumePower(std::uint64_t now);
    void printNetwork() const;
    void printAssets() const;
    void configureNetwork(char* fields, bool allowPrivateHttp);
    void printMotion() const;
    void recoverMotion();
    void recoveryCommand(const char* input);
    void printStarter() const;
    void starterInput(onboarding::Input input);
    void printBattleMode() const;
    void proposeBattleMode(controls::BattleChoice choice);
    void confirmBattleMode();
    void printPractice() const;
    void practiceCommand(const char* line);
    void printEvolution() const;
    void evolutionCommand(const char* input);
    void printCollection() const;
    void printJournal() const;
    void releaseCommand(const char* input);
    void previewArt();
    void printArt() const;
    bool usbTransferCommand(const char* line);
    bool stopUsbTransfer(bool restoreInterface);
    void printIdentity() const;
    static bool usbTransferCooperate(void* context);
    assets::UsbSdTransfer usbTransfer_{"/sdcard", usbTransferCooperate, this};
    std::uint64_t lastUsbTransferMs_ = 0;
    unsigned usbTransferYieldBlocks_ = 0;
    bool usbTransferLease_ = false, usbTransferPreviousAssetsPaused_ = false;
    bool usbTransferPreviousInterfacePaused_ = false;
    power::Controller power_;
    power::Work powerWork_ = power::Work::Pending;
    power::Phase reportedPowerPhase_ = power::Phase::AwaitInitialRelease;
    std::uint8_t reportedCountdown_ = 255;
    std::uint32_t inputEpoch_ = 0;
    const char* powerReason_ = "waiting for initial PWR release";
    bool powerEnabled_ = false, powerFrozen_ = false, latchDropped_ = false, powerHardwareFault_ = false;
    bool previousNetworkPaused_ = false, previousAssetsPaused_ = false, sdPowerPrepared_ = false;
    State& state_;
    storage::SaveStore& saves_;
    entropy::Seeds startupSeeds_{};
    devicetrade::NvsBackend tradeBackend_;
    devicetrade::Session tradeSession_{state_, saves_, tradeBackend_};
    assets::SdAssetStorage sd_;
    assets::Cache cache_;
    assets::DeviceAssetsClient assets_;
    net::NetworkAdapter network_;
#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
    DeviceSetup setup_{network_};
#endif
    motion::MotionAdapter sensor_;
    motion::MotionCounter steps_;
    motion::StepDelivery stepDelivery_;
    motion::Update motionUpdate_{};
    controls::RecoveryChoice recoveryChoice_;
    devicepractice::NvsBackend practiceBackend_;
    devicepractice::PracticeSession practice_;
    onboarding::StarterController starter_;
    controls::BattleModeChoice battleModeChoice_;
    controls::EvolutionChoice evolutionChoice_;
    assets::Plan plan_{};
    std::size_t planIndex_ = 0;
    std::uint32_t planSequence_ = UINT32_MAX, planGeneration_ = UINT32_MAX;
    std::uint64_t lastMotionMs_ = 0;
    net::State previousNetwork_ = net::State::Unconfigured;
    bool motionFault_ = false;
    bool assetStorageReady_ = false, assetStorageFailed_ = false;
    std::uint32_t lastArtForm_ = 0;
    bool lastArtWasPrivate_ = false;
};
} // namespace digivice
