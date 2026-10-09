#pragma once

#include "game.hpp"
#include <cstddef>
#include <cstdint>

namespace digivice::storage {

enum class ReadStatus { Missing, Present, Unreadable };
struct Slot {
    std::uint8_t bytes[kSnapshotSize]{};
    std::size_t length = 0;
};

// writeSlot must durably commit one blob; caller then verifies readback.
// An uncertain write failure disables further writes until reboot/recovery.
class Backend {
public:
    virtual ~Backend() = default;
    virtual ReadStatus readSlot(unsigned index, Slot& slot) = 0;
    virtual bool writeSlot(unsigned index, const Snapshot& snapshot) = 0;
};

enum class BootStatus { Empty, Loaded, Migrated, RecoveryRequired };

class SaveStore {
public:
    explicit SaveStore(Backend& backend) : backend_(backend) {}
    BootStatus restore(State& state);
    bool checkpoint(const State& state);
    // Ownership transfers must replace both rollback slots before an external
    // Applied acknowledgement or retirement of the durable trade receipt.
    // Failure never grants that acknowledgement; the trade journal stays locked.
    bool checkpointMirrored(const State& state);
    void requireRecovery(const char* reason) { writable_ = false; diagnostic_ = reason; }
    bool writable() const { return writable_; }
    const char* diagnostic() const { return diagnostic_; }
private:
    Backend& backend_;
    // Main-task owner keeps SaveStore in app-lifetime storage. Sixty-member
    // snapshots and canonical comparisons must not consume its call stack.
    Slot slot_{};
    State decoded_{};
    Snapshot canonical_[2]{};
    bool writable_ = false;
    int activeSlot_ = -1;
    std::uint32_t sequence_ = 0;
    const char* diagnostic_ = "restore not attempted";
};

} // namespace digivice::storage
