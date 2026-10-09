#pragma once
#include "game.hpp"
#include <cstdint>

// Disposable playback of an ALREADY COMMITTED result. No RNG, game transitions,
// storage, heap or SDK. The caller owns saving and all hardware/audio effects.
namespace digivice::battlepresentation {
enum class Phase : std::uint8_t { Idle, Player, Opponent, Summary };
enum class Actor : std::uint8_t { None, Player, Opponent };
enum class Cue : std::uint8_t { None, Attack, Magic, Hit, CaptureThrow, CaptureSuccess, CaptureFail, Win, Retreat };
struct View {
    Phase phase = Phase::Idle;
    Actor actor = Actor::None;
    bool locked = false, paused = false, flash = false, reflected = false, captured = false, aimMiss = false;
    // Saved rules-12 capture playback. Chance is revealed only after the throw;
    // a missed aim always reports zero. These fields never decide an outcome.
    bool capturePresentation = false, captureMiss = false, captureCaught = false;
    std::uint8_t captureAttempt = 0, captureRemaining = 0, captureChance = 0;
    std::uint32_t captureElapsedMs = 0;
    std::uint32_t playerFormId = 0, enemyFormId = 0;
    std::uint32_t playerHp = 0, enemyHp = 0, playerMaxHp = 0, enemyMaxHp = 0;
    std::uint32_t damage = 0, progressPermille = 0, turn = 0, turnCount = 0;
    const char* actorName = "";
    const char* moveName = "";
    autobattle::Move move = autobattle::Move::None, guard = autobattle::Move::None;
    autobattle::Outcome outcome = autobattle::Outcome::None;
};
class Sequencer {
public:
    static constexpr std::uint64_t kActorMs = 1200, kImpactMs = 350, kSummaryMs = 1600;
    static constexpr std::uint64_t kCaptureThrowMs = 500, kCaptureWiggleMs = 1800,
        kCaptureResultMs = 1600;
    // Invalid input or an existing locked playback returns false unchanged.
    bool startAuto(const autobattle::Trace& committedTrace, std::uint64_t nowMs);
    bool startTactical(const State& before, const State& committedAfter, Action action,
                       std::uint32_t value, std::uint64_t nowMs);
    // Replay only a valid latest saved capture (record.sequence==state.sequence).
    // Restarting/canceling visuals never changes the state or capture RNG.
    bool startSavedCapture(const State& committedState, std::uint64_t nowMs);
    // At most one transition/cue per poll; late frames never fast-forward actors.
    // A late impact still remains visible for kActorMs-kImpactMs before advancing.
    const View& poll(std::uint64_t nowMs);
    const View& view() const { return view_; }
    bool locked() const { return view_.locked; }
    Cue consumeCue(); // Clears one pending cue; repeated reads return None.
    void pause(bool paused, std::uint64_t nowMs);
    void cancel(); // Skips visuals only. Never rolls back or repeats the saved event.
private:
    bool begin(const autobattle::Trace&, std::uint64_t, bool tactical, bool aimMiss);
    void enter(Phase phase, std::uint64_t nowMs);
    void impact(std::uint64_t nowMs);
    void pollCapture(std::uint64_t nowMs);
    void emit(Cue cue);
    autobattle::Trace trace_{};
    View view_{};
    Cue cue_ = Cue::None;
    std::uint64_t enteredAt_ = 0, impactAt_ = 0, lastAt_ = 0, pausedAt_ = 0;
    std::uint8_t index_ = 0, captureStage_ = 0;
    bool impacted_ = false, aimMiss_ = false;
};
static_assert(sizeof(Sequencer) <= 2048, "Battle presentation must stay under 2 KiB");
} // namespace digivice::battlepresentation
