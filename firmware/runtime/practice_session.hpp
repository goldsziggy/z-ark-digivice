#pragma once
#include "game.hpp"
#include "practice_battle.hpp"
#include <cstddef>
#include <cstdint>

namespace digivice::devicepractice {
constexpr std::size_t kLegacyRecordBytes = 288;
constexpr std::size_t kRecordBytes = 304;
struct Bytes { std::uint8_t data[kRecordBytes]{}; };
struct Slot { Bytes bytes; std::size_t length = 0; };
enum class Read { Missing, Present, Unreadable };
class Backend {
public:
    virtual ~Backend() = default;
    virtual Read read(unsigned slot, Slot& output) = 0;
    // Commit one complete record; uncertain failure must return false.
    virtual bool write(unsigned slot, const Bytes& bytes) = 0;
};
enum class Mode : std::uint8_t { Tactical, Auto };
enum class Kind : std::uint8_t { Start = 1, Act = 2 };
enum class Action : std::uint8_t { None, Physical, Heavy, Magic, Brace, Counter, Ward, Card, Retreat };
struct Request {
    std::uint32_t id = 0, expectedRevision = 0;
    Kind kind = Kind::Start;
    Mode mode = Mode::Tactical;
    Action action = Action::None;
    std::uint32_t value = 0;
};
enum class Result : std::uint8_t {
    Ready, Empty, Applied, Duplicate, Status, Help, Replay, InvalidCommand,
    RecoveryRequired, Conflict, Stale, InvalidStart, ActiveBattle, AutoComplete, CoreRejected, Limit
};
struct Reply {
    Result result = Result::InvalidCommand;
    practice::Error coreError = practice::Error::None;
};

// One owner, no heap/network/clock. Only this independent practice record is
// writable. Caller supplies readonly care state and a fresh seed for NEW starts;
// exact retries deliberately ignore changed care/seed after recognizing receipt.
class PracticeSession {
public:
    explicit PracticeSession(Backend& backend) : backend_(backend) {}
    Result restore();
    Reply execute(const Request& request, const State& care, bool careWritable, std::uint32_t seed);
    Reply command(const char* line, const State& care, bool careWritable, std::uint32_t seed);
    bool writable() const { return writable_; }
    bool hasBattle() const { return hasBattle_; }
    const practice::State* battle() const { return hasBattle_ ? &record_.battle : nullptr; }
    std::uint32_t revision() const { return record_.revision; }
    std::uint32_t lastCommandId() const { return record_.last.id; }
    std::uint32_t companionId() const { return record_.companionId; }
    Mode mode() const { return record_.mode; }
    bool blocksPartner() const;
    bool allowsCareAction(digivice::Action action) const;
    // Recompute already-saved Auto presentation. No writes or rewards.
    bool replay(autobattle::Trace& trace) const;
    const char* diagnostic() const { return diagnostic_; }
private:
    struct Record {
        std::uint32_t revision = 0, companionId = 0;
        Mode mode = Mode::Tactical;
        Request last{};
        practice::Snapshot initial{};
        practice::State battle{};
    };
    static bool encode(const Record& record, Bytes& bytes);
    static bool decode(const Slot& slot, Record& record);
    Backend& backend_;
    Record record_{};
    int activeSlot_ = -1;
    bool writable_ = false, hasBattle_ = false;
    const char* diagnostic_ = "practice restore not attempted";
};
const char* resultText(Result result);
const char* modeName(Mode mode);
} // namespace digivice::devicepractice
