#pragma once
#include "game.hpp"
#include "starter.hpp"
#include "evolution_choice.hpp"
#include "battle_presentation.hpp"
#include "nearby_protocol.hpp"
#include "trade_protocol.hpp"
#include "sprite.hpp"
#include <cstddef>
#include <cstdint>

// Host-testable physical UI. No ESP, allocation, storage or network dependency.
// One main-task owner. Intents are proposals: caller validates, durably saves,
// publishes State, then calls resolve(). RGB565 pixels are host integer order.
namespace digivice::deviceui {
constexpr int kSize = 412;
constexpr std::size_t kPixels = kSize * kSize;
static_assert(kCollectionCapacity<=UINT8_MAX,"Collection navigation stores a bounded slot index");
constexpr std::uint32_t kNoNearbyTurn = UINT32_MAX;
constexpr std::uint32_t kNearbyFeedbackMs = 2400;
enum class Screen : std::uint8_t {
    Egg, Starter, StarterReview, Home, Care, Explore,
    Encounter, Battle, Capture, Result, Collection, Stats, ReleaseReview, Evolution, EvolutionReview, EvolutionResult,
    Settings, EncounterSettings, ModeReview, Nearby, NearbyReview, Sound, TradeChoose, TradeReview,
    Squad, Box // Partners: 2x2 squad landing and 2x2 pages of every member; Collection is one member.
};
enum class TouchKind : std::uint8_t { Down, Move, Up, Cancel };
enum class HomePanel : std::uint8_t { Care, Partners, Settings, Nearby };
enum class SpriteFacing : std::uint8_t { Unknown, Left, Right, Front };
struct Touch { TouchKind kind; std::int16_t x, y; std::uint64_t atMs; };
// Views borrow immutable, caller-owned decoded buffers until render returns.
// Keep background/sprite memory separate from the output framebuffer. SD/cache
// validation and frame decoding belong to the runtime, never to this renderer.
struct SpriteFrame {
    std::uint32_t formId = 0;
    sprite::Animation animation = sprite::Animation::Idle;
    const std::uint16_t* pixels = nullptr;
    std::size_t pixelCount = 0;
    const std::uint8_t* mask = nullptr;
    std::size_t maskBytes = 0;
    std::uint16_t width = 0, height = 0;
    std::uint8_t contentX=0, contentY=0, contentWidth=0, contentHeight=0; // Stable opaque union for the whole clip; zero means full frame.
    SpriteFacing nativeFacing = SpriteFacing::Unknown; // Verified by the asset adapter, never inferred from animation/turn.
};
struct Artwork {
    const std::uint16_t* background = nullptr; // Predimmed412x412 RGB565.
    std::size_t backgroundPixels = 0;
    const char* backgroundId = nullptr; // Exact native catalog ID, immutable.
    SpriteFrame sprite{};
};
struct ArtRequest {
    std::uint32_t formId = 0; // Zero means this screen has no creature artwork.
    sprite::Animation animation = sprite::Animation::Idle;
    std::uint64_t elapsedMs = 0;
    const char* sceneId = nullptr; // Full native catalog key, e.g. scene-meadow-412-v1.
};
struct Model {
    onboarding::Stage starterStage = onboarding::Stage::Egg;
    std::uint8_t selectedId = 1, starterCount = 8;
    std::uint16_t sleepTimeoutSeconds = 60;
    std::uint32_t starterFormId = 0; // Exact persisted offer; zero retains fixed starter lookup.
    bool writable = false, inputEnabled = true;
    bool partyEditable = true; // Runtime practice/Nearby locks supplement core Home legality.
    bool encounterRecoveryRequired = false; // A boot repair could not be verified durable; no gameplay.
    bool motionAvailable = false, gyroEnabled = false, muted = false;
    std::uint8_t volumePercent = 15;
    bool musicEnabled = false, audioAvailable = false, audioPreferencesWritable = true;
    // Raw IMU availability does not imply a verified step counter.
    bool stepsAvailable = false, stepsRecovering = false, encounterReady = false;
    std::uint64_t lifetimeSteps = 0;
    std::uint32_t sessionSteps = 0;
    const char* stepStatus = nullptr; // Short caller-owned sensor status; never assumed from gyro.
    std::int16_t tiltX = 0, tiltY = 0;
    Artwork artwork{};
    SpriteFrame partnerArtwork{}; // Exact second actor; unavailable art uses a neutral indicator.
    SpriteFrame tileArtwork[4]{}; // Squad/Box tiles, matched to tileArtRequest(); missing art shows a neutral mark.
    const battlepresentation::View* battle = nullptr; // Immutable runtime-owned presentation snapshot.
    const nearby::View* nearby = nullptr;
    nearby::Fighter nearbyLocalFighter{}; // Frozen when this live Nearby session opens.
    const char* nearbyStatus = nullptr;
    const tradewire::View* trade = nullptr;
    bool tradeWritable = false;
    const char* tradeStatus = nullptr;
    // Runtime supplies monotonic elapsed time for the current session/sequence.
    // Sentinel means no result to animate. Existing sprite fields hold the
    // local (partnerArtwork) and remote (artwork.sprite) frozen fighters, Idle.
    std::uint32_t nearbyTurnElapsedMs = kNoNearbyTurn;
};
enum class IntentKind : std::uint8_t {
    None, GameAction, StarterNext, StarterPrevious, StarterConfirm,
    StarterBack, ToggleMute, ToggleGyro, Navigation, OpenSetup,
    OpenNearby, NearbyChallenge, NearbyAccept, NearbyChoose, NearbyCancel, CloseNearby, SleepTimeout,
    Volume, ToggleMusic, TradeInvite, TradeOffer, TradeConfirm, TradeCancel, TradeClose
};
struct Intent {
    IntentKind kind = IntentKind::None;
    Action action = Action::Feed;
    std::uint32_t value = 0;
    trade::Identity peer{}; // Nearby and trade intents bind the exact radio peer.
    std::uint64_t tradeSession = 0;
    std::uint32_t tradeRevision = 0, tradeFingerprint = 0;
    // Explicit, ephemeral friendly-duel consent. Never inherited from wild mode.
    nearby::Mode nearbyMode = nearby::Mode::Tactical;
    nearby::Fighter nearbyFighters[2]{};
    std::uint64_t nearbySession = 0;
    std::uint32_t nearbyOpenNonce = 0;
    explicit operator bool() const { return kind != IntentKind::None; }
};
// Auto encounters continue themselves. Capture pauses, playback, and a same-frame
// retreat stay in the caller's hands; this never spends a throw.
inline bool autoFightReady(const State& state, const Model& model) {
    if (!model.writable || !model.inputEnabled || model.encounterRecoveryRequired) return false;
    if (model.battle && model.battle->locked) return false;
    if (state.phase != Phase::Encounter || state.battleMode != BattleMode::Auto) return false;
    if (state.autoCapture == AutoCapture::Awaiting) return false;
    if (model.trade && model.trade->stage != tradewire::Stage::Closed &&
        model.trade->stage != tradewire::Stage::Discovering) return false;
    if (model.nearby && model.nearby->stage != nearby::Stage::Closed) return false;
    State copy = state;
    return apply(copy, Action::AutoFight) == Error::None;
}
// One bottom spot for BACK/LEAVE/DONE/skip, fully inside the 204 px touch circle.
inline constexpr int kNavX=116, kNavY=332, kNavW=180, kNavH=48, kNavPad=12;
// Release margin for every other button once pressed (roll-off drift), never a Down target.
inline constexpr int kHoldPad=8, kHoldSlop=44; // 44 px = 4 mm of drift; a longer drag is a swipe
// Partners 2x2 grid: 128x92 tiles (11.5 x 8.3 mm), 8 px gaps, entirely inside the touch circle.
inline constexpr std::size_t kTiles=4;
inline constexpr int kTileW=128, kTileH=92, kTileX[2]{74,210}, kTileY[2]{84,184};

class Controller {
public:
    void update(const State& state, const Model& model);
    // Call only after a verified background checkpoint, before update().
    // Rejects any unrelated gameplay change; never writes authoritative State.
    bool acknowledgeWalking(const State& before, const State& after, Action action, std::uint32_t value);
    Intent touch(const State& state, const Model& model, Touch event);
    void resolve(const char* message = nullptr);
    void notice(const char* message);
    void cancelTouch();
    // Call only after a fresh successful hardware sample reports released.
    void acknowledgeContactReleased();
    ArtRequest artRequest(const State& state, const Model& model, std::uint64_t nowMs) const;
    // Use this same request for loading and rendering the second actor, including
    // the unlocked move picker between turns. Never guess animation in the HAL.
    ArtRequest partnerArtRequest(const State& state, const Model& model, std::uint64_t nowMs) const;
    // Squad/Box tile i (0..kTiles-1); zero formId when that tile shows no member.
    ArtRequest tileArtRequest(const State& state, const Model& model, std::size_t tile, std::uint64_t nowMs) const;
    // Member shown on tile i: Squad is the active partner then the XP companions; Box pages every member.
    const CreatureMember* tileMember(const State& state, std::size_t tile) const;
    std::uint8_t boxPage() const { return boxPage_; }
    static constexpr int kCaptureX=102, kCaptureY=76, kCaptureWidth=208, kCaptureHeight=208;
    bool captureAnimating(const State&, const Model&) const;
    // Reconstruct only this fixed region in an already rendered framebuffer.
    bool renderCaptureRegion(const State&, const Model&, std::uint16_t*, std::size_t, std::uint64_t nowMs) const;
    bool render(const State& state, const Model& model, std::uint16_t* pixels,
                std::size_t pixelCapacity, std::uint64_t nowMs) const;
    Screen screen() const { return screen_; }
    HomePanel homePanel() const { return homePanel_; }
    std::uint32_t selectedMemberId() const { return memberId_; }
    bool pending() const { return pending_; }
    // Background walking is independent of the current screen. Only revealing
    // its saved encounter waits for a quiet Home panel.
    bool encounterPresentationEligible() const { return !battleLocked_ && screen_ == Screen::Home; }
    bool walkingEligible() const { return encounterPresentationEligible(); } // Compatibility for older callers.
    bool interactionIdle() const { return !down_ && !captureContactBlocked_ && !pending_; }
    static bool inside(int x, int y);
    // BACK, LEAVE, DONE and the Capture skip share one bottom spot, fully inside the
    // touch circle, with a padded hit area that also forgives release drift.
    struct Button { int x, y, w, h; const char* label; int id; bool enabled; bool padded=false; };
    // Read-only layout of the current screen's tap targets (tests and tap audits).
    std::size_t layout(const State& state, const Model& model, Button* out, std::size_t capacity) const {
        Button all[kMaxButtons]; const auto n=buttons(state,model,all);
        std::size_t i=0; for(;i<n && i<capacity;++i) out[i]=all[i];
        return i;
    }
    static constexpr std::size_t kMaxButtons=8;
private:
    static bool hits(const Button& b, int x, int y, int pad=0) {
        return x>=b.x-pad && x<b.x+b.w+pad && y>=b.y-pad && y<b.y+b.h+pad;
    }
    // Exact hit on any button wins; otherwise the padded bottom button; -1 for none.
    static int hitIndex(const Button* choices, std::size_t n, int x, int y);
    std::size_t buttons(const State&, const Model&, Button* out) const;
    Intent activate(int id, const State&, const Model&);
    Intent propose(const State&, const Model&, Action, std::uint32_t value = 0);
    Intent navigate(Screen);
    Intent navigateHorizontal(bool next,const State&,const Model&);
    Intent proposeTrade(IntentKind,const Model&,std::uint32_t memberId=0);
    void resetTouch();
    std::uint64_t captureElapsed(std::uint64_t now) const;
    void cancelEvolution();
    const CreatureMember* selectedMember(const State& state) const { return findMember(state,memberId_); }
    Screen screen_ = Screen::Egg;
    HomePanel homePanel_ = HomePanel::Care;
    Phase phase_ = Phase::Egg;
    onboarding::Stage starterStage_ = onboarding::Stage::Egg;
    std::uint32_t sequence_ = UINT32_MAX, starterForm_ = 0, memberId_ = 0;
    std::uint8_t starterCount_ = 8;
    std::uint8_t selectedId_ = 0, memberIndex_ = 0, proposedMode_ = 255;
    std::uint8_t statsPage_ = 0, evolutionIndex_ = 0, evolutionPage_ = 0, nearbyIndex_ = 0, boxPage_ = 0;
    Screen memberReturn_ = Screen::Squad; // Where BACK from one member goes: Squad or Box.
    trade::Identity tradePeer_{};
    std::uint32_t tradeMemberId_ = 0, tradeFingerprint_ = 0, tradeContext_ = 0, tradePeerNonce_ = 0;
    std::uint8_t tradePage_ = 0;
    nearby::Stage nearbyStage_ = nearby::Stage::Closed;
    nearby::Mac nearbyPeer_{};
    nearby::Fighter nearbyFighter_{};
    nearby::Fighter nearbyOffered_[2]{};
    nearby::Mac nearbyOpponent_{};
    nearby::Mode nearbyMode_ = nearby::Mode::Tactical, nearbyOfferedMode_ = nearby::Mode::Tactical;
    std::uint32_t nearbyPeerNonce_ = 0;
    std::uint64_t nearbySession_ = 0;
    std::uint32_t nearbySequence_ = 0;
    bool nearbyPending_ = false, nearbyFeedback_ = false;
    std::uint8_t nearbyRole_ = 255;
    controls::EvolutionChoice evolution_{};
    std::uint32_t evolutionMember_ = 0, evolutionForm_ = 0, evolutionTarget_ = 0, releaseMember_ = 0;
    bool writable_ = false, enabled_ = false, partyEditable_ = true, initialized_ = false, battleLocked_ = false;
    bool down_ = false, battleGesture_ = false, browseGesture_ = false, cancelled_ = false, pending_ = false, tapMoved_ = false;
    combat::Move battleSelection_ = combat::Move::Physical;
    std::uint8_t defenseSelection_ = 0;
    // Presentation-only first-use clocks. No save writes or gameplay mutation.
    mutable std::uint64_t hintAt_ = UINT64_MAX;
    mutable std::uint8_t hintSeen_ = 0, hintContext_ = 0;
    int downButton_ = 0;
    std::uint32_t captureEpochForm_ = 0;
    // Survives resolve/context changes until an observed physical release.
    bool captureContactBlocked_ = false;
    std::uint64_t captureAcceptedAt_ = UINT64_MAX;
    // Set once per eligible entry/retry, never advanced by frame count.
    mutable std::uint64_t captureEpoch_ = UINT64_MAX;
    bool captureEligible_ = false;
    std::uint64_t downAt_ = 0, lastAt_ = 0, actionAt_ = 0;
    std::uint32_t actionSequence_ = UINT32_MAX, actionValue_ = 0;
    Action lastAction_ = Action::Feed;
    std::int16_t downX_ = 0, downY_ = 0;
    char notice_[48]{};
};
const char* screenName(Screen screen);
} // namespace digivice::deviceui
