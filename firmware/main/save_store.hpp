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
    bool writable() const { return writable_; }
    const char* diagnostic() const { return diagnostic_; }
private:
    Backend& backend_;
    bool writable_ = false;
    int activeSlot_ = -1;
    std::uint32_t sequence_ = 0;
    const char* diagnostic_ = "restore not attempted";
};

} // namespace digivice::storage
