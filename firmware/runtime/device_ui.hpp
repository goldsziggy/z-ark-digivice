#pragma once
#include "game.hpp"
#include "starter.hpp"
#include "evolution_choice.hpp"
#include "battle_presentation.hpp"
#include "nearby_protocol.hpp"
#include "sprite.hpp"
#include <cstddef>
#include <cstdint>

// Host-testable physical UI. No ESP, allocation, storage or network dependency.
// One main-task owner. Intents are proposals: caller validates, durably saves,
// publishes State, then calls resolve(). RGB565 pixels are host integer order.
namespace digivice::deviceui {
constexpr int kSize = 412;
constexpr std::size_t kPixels = kSize * kSize;
constexpr std::uint32_t kNoNearbyTurn = UINT32_MAX;
constexpr std::uint32_t kNearbyFeedbackMs = 2400;
enum class Screen : std::uint8_t {
    Egg, Starter, StarterReview, Home, Care, Explore,
    Encounter, Battle, Capture, Result, Collection, Stats, ReleaseReview, Evolution, EvolutionReview, EvolutionResult,
    Settings, EncounterSettings, ModeReview, Nearby, NearbyReview
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
    bool encounterRecoveryRequired = false; // A boot repair could not be verified durable; no gameplay.
    bool motionAvailable = false, gyroEnabled = false, muted = false;
    // Raw IMU availability does not imply a verified step counter.
    bool stepsAvailable = false, encounterReady = false;
    std::uint64_t lifetimeSteps = 0;
    std::uint32_t sessionSteps = 0;
    const char* stepStatus = nullptr; // Short caller-owned sensor status; never assumed from gyro.
    std::int16_t tiltX = 0, tiltY = 0;
    Artwork artwork{};
    SpriteFrame partnerArtwork{}; // Exact second actor; unavailable art uses a neutral indicator.
    const battlepresentation::View* battle = nullptr; // Immutable runtime-owned presentation snapshot.
    const nearby::View* nearby = nullptr;
    const char* nearbyStatus = nullptr;
    // Runtime supplies monotonic elapsed time for the current session/sequence.
    // Sentinel means no result to animate. Existing sprite fields hold the
    // local (partnerArtwork) and remote (artwork.sprite) frozen fighters, Idle.
    std::uint32_t nearbyTurnElapsedMs = kNoNearbyTurn;
};
enum class IntentKind : std::uint8_t {
    None, GameAction, StarterNext, StarterPrevious, StarterConfirm,
    StarterBack, ToggleMute, ToggleGyro, Navigation, OpenSetup,
    OpenNearby, NearbyChallenge, NearbyAccept, NearbyChoose, NearbyCancel, CloseNearby, SleepTimeout
};
struct Intent {
    IntentKind kind = IntentKind::None;
    Action action = Action::Feed;
    std::uint32_t value = 0;
    explicit operator bool() const { return kind != IntentKind::None; }
};
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
    ArtRequest artRequest(const State& state, const Model& model, std::uint64_t nowMs) const;
    // Use this same request for loading and rendering the second actor, including
    // the unlocked move picker between turns. Never guess animation in the HAL.
    ArtRequest partnerArtRequest(const State& state, const Model& model, std::uint64_t nowMs) const;
    bool render(const State& state, const Model& model, std::uint16_t* pixels,
                std::size_t pixelCapacity, std::uint64_t nowMs) const;
    Screen screen() const { return screen_; }
    HomePanel homePanel() const { return homePanel_; }
    bool pending() const { return pending_; }
    // Background walking is independent of the current screen. Only revealing
    // its saved encounter waits for a quiet Home panel.
    bool encounterPresentationEligible() const { return !battleLocked_ && screen_ == Screen::Home; }
    bool walkingEligible() const { return encounterPresentationEligible(); } // Compatibility for older callers.
    bool interactionIdle() const { return !down_ && !pending_; }
    static bool inside(int x, int y);
private:
    struct Sample { std::int16_t x = 0, y = 0; std::uint64_t atMs = 0; };
    struct Button { int x, y, w, h; const char* label; int id; bool enabled; };
    std::size_t buttons(const State&, const Model&, Button* out) const;
    Intent activate(int id, const State&, const Model&);
    Intent propose(const State&, const Model&, Action, std::uint32_t value = 0);
    Intent navigate(Screen);
    Intent navigateHorizontal(bool next,const State&,const Model&);
    void append(Touch);
    void resetTouch();
    void cancelEvolution();
    Screen screen_ = Screen::Egg;
    HomePanel homePanel_ = HomePanel::Care;
    Phase phase_ = Phase::Egg;
    onboarding::Stage starterStage_ = onboarding::Stage::Egg;
    std::uint32_t sequence_ = UINT32_MAX, starterForm_ = 0;
    std::uint8_t starterCount_ = 8;
    std::uint8_t selectedId_ = 0, memberIndex_ = 0, proposedMode_ = 255;
    std::uint8_t statsPage_ = 0, evolutionIndex_ = 0, evolutionPage_ = 0, nearbyIndex_ = 0;
    nearby::Stage nearbyStage_ = nearby::Stage::Closed;
    nearby::Mac nearbyPeer_{};
    nearby::Fighter nearbyFighter_{};
    std::uint64_t nearbySession_ = 0;
    std::uint32_t nearbySequence_ = 0;
    bool nearbyPending_ = false, nearbyFeedback_ = false;
    std::uint8_t nearbyRole_ = 255;
    controls::EvolutionChoice evolution_{};
    std::uint32_t evolutionMember_ = 0, evolutionForm_ = 0, evolutionTarget_ = 0, releaseMember_ = 0;
    bool writable_ = false, enabled_ = false, initialized_ = false, battleLocked_ = false;
    bool down_ = false, gesture_ = false, battleGesture_ = false, browseGesture_ = false, cancelled_ = false, pending_ = false, tapMoved_ = false;
    combat::Move battleSelection_ = combat::Move::Physical;
    std::uint8_t defenseSelection_ = 0;
    // Presentation-only first-use clocks. No save writes or gameplay mutation.
    mutable std::uint64_t hintAt_ = UINT64_MAX;
    mutable std::uint8_t hintSeen_ = 0, hintContext_ = 0;
    int downButton_ = 0;
    Sample samples_[16]{};
    std::uint8_t sampleCount_ = 0;
    std::uint64_t downAt_ = 0, lastAt_ = 0, actionAt_ = 0;
    std::uint32_t actionSequence_ = UINT32_MAX, actionValue_ = 0;
    Action lastAction_ = Action::Feed;
    std::int16_t downX_ = 0, downY_ = 0;
    char notice_[48]{};
};
const char* screenName(Screen screen);
} // namespace digivice::deviceui
