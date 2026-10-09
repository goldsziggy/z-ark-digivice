#include "handheld_runtime.hpp"
#include <cstdio>

namespace digivice {
void HandheldRuntime::beginTradeStorage() {
    const auto initialized = tradeBackend_.initialize();
    const bool restored = tradeSession_.restore();
    std::printf("Trade storage: %s (%s); %s\n", restored ? "ready" : "recovery",
        esp_err_to_name(initialized), tradeSession_.diagnostic());
}
}

#if defined(CONFIG_DIGIVICE_DISPLAY_TOUCH) && CONFIG_DIGIVICE_DISPLAY_TOUCH
#include "esp_random.h"
#include "esp_timer.h"
#include <cstring>
namespace digivice {
namespace {
tradewire::Durable durablePhase(trade::Phase phase) {
    switch (phase) {
    case trade::Phase::Prepared: return tradewire::Durable::Prepared;
    case trade::Phase::Committed: return tradewire::Durable::Committed;
    case trade::Phase::Applied: return tradewire::Durable::Applied;
    case trade::Phase::Aborted: return tradewire::Durable::Aborted;
    }
    return tradewire::Durable::None;
}
std::uint32_t nonce() { const auto n = esp_random(); return n ? n : 1; }
bool battleActive(nearby::Stage stage) {
    return stage != nearby::Stage::Closed && stage != nearby::Stage::Discovering &&
        stage != nearby::Stage::Finished && stage != nearby::Stage::Cancelled;
}
}
bool HandheldRuntime::tradeNegotiating() const {
    const auto stage = tradeWire_.view().stage;
    return tradeSession_.blocksForeground() || (stage != tradewire::Stage::Closed &&
        stage != tradewire::Stage::Discovering && stage != tradewire::Stage::Applied &&
        stage != tradewire::Stage::Aborted && stage != tradewire::Stage::Cancelled);
}
bool HandheldRuntime::openTradeRadio(std::uint64_t now) {
    if (!tradeSession_.healthy()) return false;
    trade::Identity local; std::memcpy(local.bytes, nearbyRadio_.identity(), sizeof(local.bytes));
    const auto* receipt = tradeSession_.record();
    if (receipt && !tradePeerTerminal_) {
        // Persisted identities and nonces bind recovery to the same peer. Never
        // construct a replacement offer while this receipt is outstanding.
        if (!tradeWire_.restore(local, receipt->transcript, durablePhase(receipt->phase), now, state_.receivedTrades)) {
            tradeStatus_ = "Trade identity mismatch - recovery required"; return false;
        }
        tradeStatus_ = tradeSession_.diagnostic(); return true;
    }
    const auto* member = activeMember(state_);
    if (!member) return false;
    const bool available = !tradeSession_.blocksForeground() && trade::canOffer(state_, member->id);
    tradeStatus_ = available ? "Choose a nearby partner to trade" : "Need another partner to trade";
    return tradeWire_.open(local, *member, state_.sequence, nonce(), now, available, state_.receivedTrades);
}
void HandheldRuntime::pollTradePersistence(std::uint64_t now) {
    if (!tradeSession_.healthy()) { tradeStatus_ = tradeSession_.diagnostic(); return; }
    // At most Prepare -> Commit -> Apply in a single owner turn. Each packet is
    // authorized only after both durable mirrors and readbacks have completed.
    for (unsigned i = 0; i < 3; ++i) {
        const auto& view = tradeWire_.view();
        const auto request = view.requested;
        if (request == tradewire::Request::None) break;
        bool saved = false;
        tradewire::Durable phase = tradewire::Durable::None;
        switch (request) {
        case tradewire::Request::Prepare:
            saved = tradeSession_.prepare(view.transcript, view.localSide, tradePeerTerminal_);
            phase = tradewire::Durable::Prepared;
            break;
        case tradewire::Request::Commit:
            saved = tradeSession_.commit(); phase = tradewire::Durable::Committed; break;
        case tradewire::Request::Apply:
            saved = tradeSession_.applyCommitted(); phase = tradewire::Durable::Applied; break;
        case tradewire::Request::Abort:
            saved = tradeSession_.abort(view.transcript, view.localSide, tradePeerTerminal_);
            phase = tradewire::Durable::Aborted; break;
        case tradewire::Request::None: break;
        }
        if (!saved) {
            tradeStatus_ = tradeSession_.healthy() ? "Trade could not be saved - reconnect peer" : tradeSession_.diagnostic();
            // No speculative ACK and no retry of uncertain storage in this boot.
            break;
        }
        if (phase == tradewire::Durable::Prepared || phase == tradewire::Durable::Aborted) tradePeerTerminal_ = false;
        if (!tradeWire_.setDurable(phase, now)) {
            // A verified record is authoritative. Restore that exact record;
            // never cancel or downgrade it because an ephemeral view changed.
            const auto* receipt = tradeSession_.record();
            trade::Identity local; std::memcpy(local.bytes, nearbyRadio_.identity(), sizeof(local.bytes));
            if (receipt) (void)tradeWire_.restore(local, receipt->transcript, durablePhase(receipt->phase), now, state_.receivedTrades);
            break;
        }
        tradeStatus_ = tradeSession_.diagnostic(); interfaceDirty_ = true;
    }
    const auto* receipt = tradeSession_.record();
    const auto& view = tradeWire_.view();
    if (receipt && trade::sameTranscript(receipt->transcript, view.transcript) &&
        ((receipt->phase == trade::Phase::Applied && view.peerDurable == tradewire::Durable::Applied) ||
         (receipt->phase == trade::Phase::Aborted && view.peerDurable == tradewire::Durable::Aborted)))
        tradePeerTerminal_ = true;
}
void HandheldRuntime::tradeIntent(deviceui::Intent intent) {
    using K = deviceui::IntentKind;
    const auto now = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
    if (nearbyPhase_ != NearbyPhase::Active || !tradeSession_.healthy()) {
        tradeStatus_ = "Open Nearby to reconnect the trade peer"; return;
    }
    const auto& view = tradeWire_.view();
    if (intent.kind != K::TradeInvite &&
        (intent.tradeSession != view.transcript.session || intent.tradeRevision != view.transcript.revision ||
         intent.tradeFingerprint != trade::fingerprint(view.transcript))) {
        tradeStatus_ = "Offer changed - review again"; return;
    }
    bool accepted = false;
    if (intent.kind == K::TradeClose) {
        // Closing the radio never unlocks an escrow or clears its receipt.
        closeNearby(); return;
    }
    if (intent.kind == K::TradeInvite || intent.kind == K::TradeOffer) {
        const auto* member = findMember(state_, intent.value);
        if (!member || tradeSession_.blocksForeground() || !trade::canOffer(state_, intent.value) ||
            battleActive(nearby_.view().stage) || (tradeSession_.record() && !tradePeerTerminal_)) {
            tradeStatus_ = "Finish pending trade / need another partner"; return;
        }
        accepted = tradeWire_.offer(*member, state_.sequence, now, state_.receivedTrades);
        if (accepted && intent.kind == K::TradeInvite) {
            const auto session = (std::uint64_t(nonce()) << 32) | nonce();
            accepted = tradeWire_.invite(intent.peer, session, now);
        }
    } else if (intent.kind == K::TradeConfirm) accepted = tradeWire_.confirm(view.transcript, now);
    else if (intent.kind == K::TradeCancel) accepted = tradeWire_.cancel(view.transcript, now);
    tradeStatus_ = accepted ? "Trade review updated" : "Offer changed - review again";
    if (accepted) pollTradePersistence(now);
    ui_.cancelTouch(); touchNeedsRelease_ = true; interfaceDirty_ = true;
}
}
#endif
