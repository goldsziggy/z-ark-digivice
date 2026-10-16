#include "handheld_runtime.hpp"
#include "expeditions.hpp"
#include "expedition_party.hpp"
#include "nearby_reward.hpp"
#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
#include "esp_random.h"
#include "esp_timer.h"
#include <cstring>

namespace digivice {
namespace {
std::uint64_t clockMs() { return static_cast<std::uint64_t>(esp_timer_get_time() / 1000); }
std::uint32_t randomNonzero() { auto value = esp_random(); return value ? value : 1; }
}
void HandheldRuntime::beginNearby() {
    const bool recovery = tradeSession_.healthy() && tradeSession_.record() && !tradePeerTerminal_;
    if (!tradeSession_.healthy() || nearbyBusy() || (!recovery && (battle_.locked() || state_.phase != Phase::Home)) || !saves_.writable() ||
        !practice_.allowsCareAction(Action::Explore) || !network_.ready() || network_.recoveryRequired()) {
        nearbyStatus_ = "Finish battle / check radio"; return;
    }
    if (!pollUsage(clockMs(), true) || (!recovery && state_.phase != Phase::Home)) {
        nearbyStatus_ = "Finish walking encounter first"; return;
    }
    const auto* member = activeMember(state_);
    if (!member) { nearbyStatus_ = "Choose a partner first"; return; }
    const auto care = memberCare(*member);
    nearbyFighter_ = {member->id, member->formId, member->level, care.offense, care.protection};
    nearbyNetworkPaused_ = network_.requestedPaused(); nearbyAssetsPaused_ = assets_.paused();
    setup_.suspend(); assets_.pause(true); network_.pause(true);
    nearbyPhase_ = NearbyPhase::Starting; nearbyDeadline_ = clockMs() + 15000;
    nearbyStatus_ = "Pausing Wi-Fi for nearby"; nearbyShownSequence_ = 0;
    nearbyShownSession_ = 0; nearbyCuePhase_ = 0;
    ui_.cancelTouch(); requireTouchRelease(); interfaceDirty_ = true;
}
void HandheldRuntime::closeNearby() {
    if (!nearbyBusy() || nearbyPhase_ == NearbyPhase::Stopping) return;
    // Closing an ephemeral session cannot resume or replay its result. Versus
    // has no rewards; a completed co-op claim was checkpointed exactly once.
    nearby_.close(); tradeWire_.close(); nearbyPhase_ = NearbyPhase::Stopping;
    nearbyDeadline_ = clockMs() + 5000;
    nearbyStatus_ = "Closing nearby / restoring Wi-Fi";
    ui_.cancelTouch(); requireTouchRelease(); interfaceDirty_ = true;
}
void HandheldRuntime::pollNearby(std::uint64_t now) {
    if (nearbyPhase_ == NearbyPhase::Idle) return;
    if (nearbyPhase_ == NearbyPhase::Starting) {
        if (now >= nearbyDeadline_) { nearbyStatus_ = "Wi-Fi pause timed out"; closeNearby(); return; }
        if (!assets_.quiescent() || network_.quiescence() == ESP_ERR_NOT_FINISHED) return;
        if (network_.quiescence() != ESP_OK || network_.beginRadioLease() != ESP_OK) {
            nearbyStatus_ = "Radio lease unavailable"; closeNearby(); return;
        }
        nearbyLease_ = true;
        if (nearbyRadio_.begin() != ESP_OK) { nearbyStatus_ = "Nearby radio start failed"; closeNearby(); return; }
        nearby::Mac mac; std::memcpy(mac.bytes, nearbyRadio_.identity(), sizeof(mac.bytes));
        if (!nearby_.open(mac, nearbyFighter_, now, randomNonzero(), nearbyActivity_)) { closeNearby(); return; }
        if (!openTradeRadio(now)) tradeStatus_ = "Trade unavailable / recovery required";
        nearbyPhase_ = NearbyPhase::Active; nearbyStatus_ = "Wi-Fi paused - nearby play";
    }
    if (nearbyPhase_ == NearbyPhase::Stopping || nearbyPhase_ == NearbyPhase::Fault) {
        if (nearbyRadio_.end() != ESP_OK || !nearbyRadio_.quiescent()) {
            if (now >= nearbyDeadline_) { nearbyPhase_ = NearbyPhase::Fault; nearbyStatus_ = "Radio recovery required"; }
            return;
        }
        if (nearbyLease_) {
            const auto result = network_.releaseRadioLease(nearbyRadio_.startedThisLease());
            if (result == ESP_ERR_NOT_FINISHED) return;
            if (result != ESP_OK) { nearbyPhase_ = NearbyPhase::Fault; nearbyStatus_ = "Wi-Fi recovery required"; return; }
            nearbyLease_ = false;
        }
        nearbyPhase_ = NearbyPhase::Idle; nearbyStatus_ = "Nearby closed";
        if (!powerFrozen()) { assets_.pause(nearbyAssetsPaused_); network_.pause(nearbyNetworkPaused_); }
        interfaceDirty_ = true; return;
    }
    nearbyRadio_.tick();
    if (nearbyRadio_.status().recoveryRequired || network_.recoveryRequired()) { closeNearby(); return; }
    nearby::RadioPacket received;
    for (unsigned n = 0; n < 8 && nearbyRadio_.receive(received); ++n) {
        nearby::Mac source; std::memcpy(source.bytes, received.source, sizeof(source.bytes));
        if (received.size >= 4 && !std::memcmp(received.bytes, "DGT1", 4)) {
            const auto stage = nearby_.view().stage;
            const bool duel = stage == nearby::Stage::Outgoing || stage == nearby::Stage::Incoming ||
                stage == nearby::Stage::Accepting || stage == nearby::Stage::Playing || stage == nearby::Stage::Reconnecting;
            if (!duel) {
                trade::Identity peer; std::memcpy(peer.bytes, received.source, sizeof(peer.bytes));
                (void)tradeWire_.receive(peer, received.bytes, received.size, now);
            }
        } else if (!tradeNegotiating() && state_.phase == Phase::Home && !battle_.locked())
            nearby_.receive(source, received.bytes, received.size, now);
        interfaceDirty_ = true;
    }
    nearby_.tick(now); tradeWire_.tick(now);
    pollTradePersistence(now);
    nearby::RadioSendResult receipt;
    (void)nearbyRadio_.sendResult(receipt); // MAC receipt is not a protocol ACK.
    if (!nearbyRadio_.status().sending) {
        nearby::Datagram battlePacket;
        tradewire::Datagram tradePacket;
        const std::uint8_t* destination = nullptr;
        const std::uint8_t* bytes = nullptr;
        std::size_t length = 0;
        bool selected = false;
        // Fair draining prevents periodic discovery packets starving the other
        // protocol. Both protocols retain their own retry-stable decisions.
        for (unsigned pass = 0; pass < 2 && !selected; ++pass) {
            const bool tradeTurn = pass ? !tradeTxTurn_ : tradeTxTurn_;
            if (tradeTurn && tradeWire_.pop(tradePacket)) {
                destination = tradePacket.destination.bytes; bytes = tradePacket.bytes; length = tradePacket.length; selected = true;
            } else if (!tradeTurn && nearby_.pop(battlePacket)) {
                destination = battlePacket.destination.bytes; bytes = battlePacket.bytes; length = battlePacket.length; selected = true;
            }
        }
        if (selected) {
            tradeTxTurn_ = !tradeTxTurn_;
            bool broadcast = true;
            for (unsigned i = 0; i < 6; ++i) broadcast &= destination[i] == 0xff;
            if (++nearbyTxToken_ == 0) ++nearbyTxToken_;
            if (broadcast) (void)nearbyRadio_.sendBroadcast(bytes, length, nearbyTxToken_);
            else if (nearbyRadio_.selectPeer(destination) == ESP_OK)
                (void)nearbyRadio_.sendPeer(bytes, length, nearbyTxToken_);
        }
    }
    const auto& view = nearby_.view();
    if (saves_.writable() && state_.phase == Phase::Home && !battle_.locked()) {
        const auto reward = nearby::checkpointRaidReward(view, state_, claimedRaid_,
            [this](const State& candidate) { return saves_.checkpoint(candidate); });
        if (reward == nearby::RewardResult::Committed) interfaceDirty_ = true;
    }
    if (nearbyShownSession_ != view.session) {
        nearbyShownSession_ = view.session; nearbyShownSequence_ = 0; nearbyCuePhase_ = 0;
    }
    const auto sequence = view.offeredMode == nearby::Mode::Coop ? view.raid.sequence : view.match.sequence;
    const auto lastAttack = view.offeredMode == nearby::Mode::Coop ? view.raid.lastAttack : view.match.lastAttack;
    if (sequence && sequence != nearbyShownSequence_) {
        nearbyShownSequence_ = sequence; nearbyTurnAt_ = now;
        nearbyCuePhase_ = 1;
        audio_.play(device::AudioCue::Navigate); // Guard selection reveal.
        interfaceDirty_ = true;
    } else if (nearbyCuePhase_ == 1 && now - nearbyTurnAt_ >= 600) {
        const auto elapsed = now - nearbyTurnAt_;
        // Drop stale sound phases after a stalled frame; never burst queued
        // attack/hit cues in adjacent polls to catch up with the wall clock.
        nearbyCuePhase_ = elapsed >= 1200 ? 3 : 2;
        if (elapsed < 1200) {
            nearbyAttackCueAt_ = now;
            audio_.play(lastAttack == nearby::Choice::Magic ? device::AudioCue::Magic : device::AudioCue::Attack);
        }
        else if (elapsed < 1800) audio_.play(device::AudioCue::Hit);
        interfaceDirty_ = true;
    } else if (nearbyCuePhase_ == 2 && now - nearbyTurnAt_ >= 1200 &&
               (now - nearbyAttackCueAt_ >= 600 || now - nearbyTurnAt_ >= 1800)) {
        nearbyCuePhase_ = 3;
        if (now - nearbyTurnAt_ < 1800) audio_.play(device::AudioCue::Hit);
        interfaceDirty_ = true;
    }
}
void HandheldRuntime::nearbyIntent(deviceui::Intent intent) {
    using K = deviceui::IntentKind;
    const auto now = clockMs();
    if (intent.kind == K::OpenNearby) { nearbyActivity_ = intent.expeditionActivity; beginNearby(); }
    else if (intent.kind == K::CloseNearby) closeNearby();
    else if (nearbyPhase_ != NearbyPhase::Active) nearbyStatus_ = "Wait for the radio";
    else if (state_.phase != Phase::Home || battle_.locked()) nearbyStatus_ = "Finish the wild battle first";
    else if (tradeNegotiating()) nearbyStatus_ = "Finish pending trade first";
    else if (intent.kind == K::NearbyChallenge) {
        const auto& view=nearby_.view();
        const auto* peer=intent.value<view.peerCount && intent.value<nearby::kMaxPeers ? &view.peers[intent.value] : nullptr;
        const bool coop=intent.nearbyMode==nearby::Mode::Coop;
        const bool reviewed=view.stage==nearby::Stage::Discovering && peer && peer->available && peer->compatible &&
            std::memcmp(peer->mac.bytes,intent.peer.bytes,sizeof(peer->mac.bytes))==0 && peer->openNonce==intent.nearbyOpenNonce &&
            nearby::sameFighter(peer->fighter,intent.nearbyFighters[1]) && nearby::sameFighter(nearbyFighter_,intent.nearbyFighters[0]) &&
            (coop ? nearbyActivity_<=5 && peer->activity==nearbyActivity_ && intent.expeditionActivity==nearbyActivity_ :
                    (intent.nearbyMode==nearby::Mode::Tactical || intent.nearbyMode==nearby::Mode::Auto) && peer->activity>5);
        if (!reviewed) nearbyStatus_ = "Peer changed - review again";
        else if (coop) {
            const bool dungeon=expeditions::kScenarios[nearbyActivity_].kind==expeditions::Kind::Dungeon;
            std::uint32_t form=0, level=0;
            if (dungeon ? !state_.dungeonKeys : !state_.bossSigils) nearbyStatus_ = "Need a key or sigil";
            else if (!nearby::previewCoopExpeditionFoe(nearbyFighter_, peer->fighter, nearbyActivity_, form, level) ||
                     !nearby_.challenge(intent.value, nearby::Mode::Coop, (std::uint64_t(randomNonzero())<<32)|randomNonzero(),
                                        nearby::raidSeed(form, level, nearbyActivity_), now, intent.nearbyCoopAuto))
                nearbyStatus_ = "No shared foe for this lobby";
        } else {
            const auto session = (std::uint64_t(randomNonzero()) << 32) | randomNonzero();
            if (!nearby_.challenge(intent.value,intent.nearbyMode,session,randomNonzero(),now))
                nearbyStatus_ = "Peer changed - choose again";
        }
    } else if (intent.kind == K::NearbyAccept) {
        const auto& view=nearby_.view();
        const bool reviewed=view.stage==nearby::Stage::Incoming && view.session==intent.nearbySession &&
            std::memcmp(view.opponent.bytes,intent.peer.bytes,sizeof(view.opponent.bytes))==0 && view.offeredMode==intent.nearbyMode && view.coopAuto==intent.nearbyCoopAuto &&
            nearby::sameFighter(view.offered[0],intent.nearbyFighters[0]) && nearby::sameFighter(view.offered[1],intent.nearbyFighters[1]);
        const bool coop=view.offeredMode==nearby::Mode::Coop;
        const bool held=!coop || (view.activity<=5 && (expeditions::kScenarios[view.activity].kind==expeditions::Kind::Dungeon ? state_.dungeonKeys : state_.bossSigils));
        if (!held) nearbyStatus_ = "Need a key or sigil";
        else if (!reviewed || !nearby_.accept(now)) nearbyStatus_ = "Offer changed - review again";
    } else if (intent.kind == K::NearbyChoose) {
        if (!nearby_.choose(static_cast<nearby::Choice>(intent.value), now)) nearbyStatus_ = "Waiting for the other player";
    } else if (intent.kind == K::NearbyCancel) nearby_.cancel(now);
    interfaceDirty_ = true;
}
}
#endif
