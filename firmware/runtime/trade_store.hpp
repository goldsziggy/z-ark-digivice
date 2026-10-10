#pragma once
#include "trade.hpp"
#include <cstddef>
#include <cstdint>

namespace digivice::devicetrade {
constexpr std::size_t kJournalBytes = 16 + trade::kRecordBytes + 4;
constexpr std::size_t kV26JournalBytes = 16 + trade::kV26RecordBytes + 4;
constexpr std::size_t kV19JournalBytes = 16 + trade::kV19RecordBytes + 4;
constexpr std::size_t kV20JournalBytes = 16 + trade::kV20RecordBytes + 4;
constexpr std::size_t kV21JournalBytes = 16 + trade::kV21RecordBytes + 4;
constexpr std::size_t kV22JournalBytes = 16 + trade::kV22RecordBytes + 4;
constexpr bool supportedJournalSize(std::size_t size) { return size==kJournalBytes || size==kV26JournalBytes || size==kV19JournalBytes || size==kV20JournalBytes || size==kV21JournalBytes || size==kV22JournalBytes; }
struct Bytes { std::uint8_t data[kJournalBytes]{}; };
struct Slot { Bytes bytes{}; std::size_t length = 0; };
enum class Read { Missing, Present, Unreadable };
class Backend {
public:
    virtual ~Backend() = default;
    virtual Read read(unsigned slot, Slot&) = 0;
    virtual bool write(unsigned slot, const Bytes&) = 0;
};
enum class Boot { Empty, Ready, NeedsMirror, RecoveryRequired };

// One bounded journal, never erased. Every externally visible durable phase is
// mirrored and read back. The last receipt remains until a later transaction.
// Valid old mirrors load without rewriting. An interrupted old/new mirror repair
// must preserve the exact decoded transaction and complete before acknowledgement.
// Runtime must mirror the care result before Applied, and must have observed
// the peer's terminal receipt before replacing this transaction with another.
class Store {
public:
    explicit Store(Backend& backend):backend_(backend) {}
    Boot restore();
    bool checkpoint(const trade::Record&);
    bool repairMirror(); // Boot-only; no packet/unlock until this verifies both slots.
    const trade::Record* record() const { return present_ ? &record_ : nullptr; }
    bool writable() const { return writable_; }
    bool mirrored() const { return mirrored_; }
    const char* diagnostic() const { return diagnostic_; }
private:
    bool writePair(const trade::Record&, std::uint32_t revision);
    Backend& backend_;
    trade::Record record_{};
    trade::Record decoded_[2]{};
    Slot scratch_[2]{}; // App-lifetime workspace, never placed on the ESP owner stack.
    std::uint32_t revision_ = 0;
    bool present_ = false, writable_ = false, mirrored_ = false;
    const char* diagnostic_ = "trade journal not restored";
};
} // namespace digivice::devicetrade
