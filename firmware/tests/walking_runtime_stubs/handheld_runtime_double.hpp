#pragma once
// Header substitution ONLY for the actual handheld_walking.cpp methods. Real
// core, SaveStore, lifetime Store and playback sequencer remain linked. These
// declarations double hardware publication, UI ownership and main-task timing;
// they do not prove the ESP class layout, power GPIO or worker implementation.
#define CONFIG_DIGIVICE_DISPLAY_TOUCH 1
#include "game.hpp"
#include "entropy_seed.hpp"
#include "save_store.hpp"
#include "usage_store.hpp"
#include "pedometer.hpp"
#include "battle_presentation.hpp"
#include "audio_cues.hpp"
#include "device_ui.hpp"
#include "nearby_protocol.hpp"
#include "trade_protocol.hpp"
#include "esp_err.h"
#include <cstdint>
#include <cstring>

namespace digivice {
struct WalkingImuDouble {
    mutable unsigned reads=0;
    motion::StepReading sample{};
    bool available=true;
    motion::StepReading stepReading() const { ++reads;return sample; }
    bool ready() const { return available; }
};
struct WalkingSetupDouble { bool open=false;bool active() const {return open;}void suspend(){open=false;} };
struct WalkingUiDouble {
    bool eligible=true,idle=true;
    deviceui::Controller* actual=nullptr;
    bool walkingEligible() const {return eligible && (!actual || actual->walkingEligible());}
    bool encounterPresentationEligible() const {return walkingEligible();}
    bool interactionIdle() const {return idle && (!actual || actual->interactionIdle());}
    bool acknowledgeWalking(const State& before,const State& after,Action action,std::uint32_t value) {
        return !actual || actual->acknowledgeWalking(before,after,action,value);
    }
    void cancelTouch(){}
};
struct WalkingPracticeDouble { bool allowed=true;bool allowsCareAction(Action) const {return allowed;} };
// Only trade ownership gates are doubled here. The real trade Session/store
// and packet-loss recovery have independent tests; this suite owns walking and
// friendly-battle lifecycle while no trade radio session is being opened.
struct WalkingTradeSessionDouble {
    bool healthy_=true,foregroundBlocked=false,backgroundAllowed=true;
    const trade::Record* current=nullptr;
    bool healthy()const{return healthy_;}
    bool blocksForeground()const{return foregroundBlocked;}
    bool permitsBackground()const{return backgroundAllowed;}
    const trade::Record* record()const{return current;}
};
struct WalkingAudioDouble { unsigned cues=0;void play(device::AudioCue) {++cues;} };
struct WalkingAssetsDouble {
    bool paused_=false,idle=true;unsigned pauses=0;
    bool paused()const{return paused_;}void pause(bool value){paused_=value;++pauses;}
    bool quiescent()const{return paused_&&idle;}
};
struct WalkingNetworkDouble {
    bool ready_=true,recovery=false,paused_=false,lease=false;
    unsigned leaseBegins=0,leaseReleases=0,pauses=0;
    bool releasedStarted=false;
    esp_err_t quiet=ESP_OK,beginResult=ESP_OK,releaseResult=ESP_OK;
    bool ready()const{return ready_;}bool recoveryRequired()const{return recovery;}
    bool requestedPaused()const{return paused_;}void pause(bool value){paused_=value;++pauses;}
    esp_err_t quiescence()const{return !paused_?ESP_ERR_INVALID_STATE:lease?ESP_ERR_NOT_FINISHED:quiet;}
    esp_err_t beginRadioLease(){++leaseBegins;if(beginResult==ESP_OK)lease=true;return beginResult;}
    esp_err_t releaseRadioLease(bool started){++leaseReleases;releasedStarted=started;if(releaseResult==ESP_OK)lease=false;return releaseResult;}
};
namespace nearby {
// Only the fields the production owner reads. Radio/NetworkAdapter have their
// own actual-SDK-double suites; this harness tests ownership ordering.
struct RadioPacket {std::uint8_t source[6]{},bytes[240]{};std::uint16_t size=0;};
struct RadioSendResult {};
}
struct WalkingRadioDouble {
    struct Status {bool recoveryRequired=false,sending=false;};
    Status status_{};bool active=false,started=false,idle=true;
    esp_err_t beginResult=ESP_OK,endResult=ESP_OK;
    unsigned begins=0,ends=0,sends=0;
    std::uint8_t lastSent[240]{};std::size_t lastSize=0;
    std::uint8_t mac[6]{2,4,6,8,10,12};
    esp_err_t begin(){++begins;if(beginResult==ESP_OK){active=started=true;idle=false;}return beginResult;}
    esp_err_t end(){++ends;if(endResult==ESP_OK){active=false;idle=true;}return endResult;}
    bool quiescent()const{return idle;}bool startedThisLease()const{return started;}
    const std::uint8_t* identity()const{return mac;}Status status()const{return status_;}
    void tick(){}bool receive(nearby::RadioPacket&){return false;}bool sendResult(nearby::RadioSendResult&){return false;}
    esp_err_t sendBroadcast(const void* bytes,std::size_t size,std::uint32_t){++sends;lastSize=size;std::memcpy(lastSent,bytes,size);return ESP_OK;}
    esp_err_t selectPeer(const std::uint8_t*){return ESP_OK;}
    esp_err_t sendPeer(const void*,std::size_t,std::uint32_t){++sends;return ESP_OK;}
};
class HandheldRuntime {
public:
    HandheldRuntime(State& state,storage::SaveStore& saves,usage::Backend& usage)
        :usage_(usage),state_(state),saves_(saves) {}
    bool physicalStepsReady(std::uint64_t now) const;
    bool pollUsage(std::uint64_t now,bool force=false);
    bool prepareUsageRestart();
    void pollBattlePresentation(std::uint64_t now);
    void beginNearby();void closeNearby();void pollNearby(std::uint64_t now);void nearbyIntent(deviceui::Intent);
    bool openTradeRadio(std::uint64_t){return false;}
    void pollTradePersistence(std::uint64_t){}
    bool tradeNegotiating()const{return tradeNegotiating_;}
    enum class NearbyPhase:std::uint8_t {Idle,Starting,Active,Stopping,Fault};
    bool nearbyBusy()const{return nearbyPhase_!=NearbyPhase::Idle;}
    bool nearbyQuiescent()const{return !nearbyBusy()&&nearbyRadio_.quiescent();}
    WalkingAssetsDouble assets_;
    WalkingNetworkDouble network_;
    WalkingRadioDouble nearbyRadio_;
    nearby::Protocol nearby_;
    WalkingTradeSessionDouble tradeSession_;
    tradewire::Protocol tradeWire_;
    bool tradePeerTerminal_=false,tradeTxTurn_=false,tradeNegotiating_=false;
    const char* tradeStatus_="Trade disabled in walking lifecycle harness";
    nearby::Fighter nearbyFighter_{};
    NearbyPhase nearbyPhase_=NearbyPhase::Idle;
    std::uint64_t nearbyDeadline_=0,nearbyTurnAt_=0,nearbyShownSession_=0,nearbyAttackCueAt_=0;
    std::uint32_t nearbyTxToken_=0,nearbyShownSequence_=0;
    std::uint8_t nearbyCuePhase_=0;
    bool nearbyNetworkPaused_=false,nearbyAssetsPaused_=false,nearbyLease_=false,touchNeedsRelease_=false;
    const char* nearbyStatus_="Nearby is closed";
    bool powerFrozen() const {return frozen;}
    void pauseInterface(bool paused) {interfacePaused_=paused;++pauseRequests;if(paused)battle_.cancel();}
    bool interfaceQuiescent() const {return quiescent;}
    WalkingImuDouble imu_;
    WalkingSetupDouble setup_;
    WalkingUiDouble ui_;
    WalkingPracticeDouble practice_;
    WalkingAudioDouble audio_;
    usage::Store usage_;
    battlepresentation::Sequencer battle_;
    State& state_;
    storage::SaveStore& saves_;
    entropy::Seeds startupSeeds_{7,11,0x12345678u,13};
    bool interfacePaused_=false,interfaceDirty_=false,walkingFault_=false,touchPressed_=false;
    bool frozen=false,quiescent=true;
    unsigned pauseRequests=0;
    std::uint32_t walkingPending_=0;
    std::uint32_t uiSequence_=UINT32_MAX;
    std::uint64_t lastWalkingSaveMs_=0;
};
}
