#include "save_store.hpp"
#include <cstring>

namespace digivice::storage {

BootStatus SaveStore::restore(State& state) {
    writable_ = false;
    activeSlot_ = -1;
    sequence_ = 0;
    Slot slots[2];
    State decoded[2];
    SnapshotStatus decodedStatus[2] = {SnapshotStatus::InvalidLength,
                                       SnapshotStatus::InvalidLength};
    bool valid[2] = {false, false};
    bool damaged = false;
    for (unsigned i = 0; i < 2; ++i) {
        const auto read = backend_.readSlot(i, slots[i]);
        if (read == ReadStatus::Missing) continue;
        if (read != ReadStatus::Present || slots[i].length > kSnapshotSize) {
            damaged = true;
            continue;
        }
        decodedStatus[i] = decodeSnapshot(slots[i].bytes, slots[i].length, decoded[i]);
        valid[i] = decodedStatus[i] == SnapshotStatus::Ok ||
                   decodedStatus[i] == SnapshotStatus::Migrated;
        damaged |= !valid[i];
    }
    if (valid[0] && valid[1] && decoded[0].sequence == decoded[1].sequence) {
        Snapshot left, right;
        encodeSnapshot(decoded[0], left);
        encodeSnapshot(decoded[1], right);
        damaged |= std::memcmp(left.bytes, right.bytes, kSnapshotSize) != 0;
    }
    if (valid[0] || valid[1]) {
        activeSlot_ = valid[1] && (!valid[0] || decoded[1].sequence > decoded[0].sequence) ? 1 : 0;
        state = decoded[activeSlot_];
        sequence_ = state.sequence;
    }
    if (damaged) {
        diagnostic_ = "slot unreadable, invalid, conflicting or unsupported; saves preserved; writes disabled";
        return BootStatus::RecoveryRequired;
    }
    writable_ = true;
    if (activeSlot_ < 0) {
        diagnostic_ = "empty storage; new game may be saved";
        return BootStatus::Empty;
    }
    if (decodedStatus[activeSlot_] == SnapshotStatus::Migrated) {
        diagnostic_ = "older snapshot migrated in RAM; original retained until next checkpoint";
        return BootStatus::Migrated;
    }
    diagnostic_ = "latest verified snapshot restored";
    return BootStatus::Loaded;
}

bool SaveStore::checkpoint(const State& state) {
    if (!writable_) return false;
    Snapshot snapshot;
    if (!encodeSnapshot(state, snapshot) || (activeSlot_ >= 0 && state.sequence < sequence_)) {
        diagnostic_ = "invalid state or sequence regression rejected";
        return false;
    }
    const unsigned next = activeSlot_ == 0 ? 1 : 0;
    if (!backend_.writeSlot(next, snapshot)) {
        writable_ = false;
        diagnostic_ = "save commit failed or uncertain; reboot into recovery before continuing";
        return false;
    }
    Slot verified;
    if (backend_.readSlot(next, verified) != ReadStatus::Present ||
        verified.length != kSnapshotSize ||
        std::memcmp(verified.bytes, snapshot.bytes, kSnapshotSize) != 0) {
        writable_ = false;
        diagnostic_ = "save readback failed; previous slot retained; writes disabled";
        return false;
    }
    activeSlot_ = static_cast<int>(next);
    sequence_ = state.sequence;
    diagnostic_ = "checkpoint committed and read back";
    return true;
}

} // namespace digivice::storage
