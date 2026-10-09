#include "save_store.hpp"
#include <cstring>

namespace digivice::storage {

BootStatus SaveStore::restore(State& state) {
    writable_ = false;
    activeSlot_ = -1;
    sequence_ = 0;
    bool migrated = false;
    bool damaged = false;
    for (unsigned i = 0; i < 2; ++i) {
        slot_ = {};
        const auto read = backend_.readSlot(i, slot_);
        if (read == ReadStatus::Missing) continue;
        if (read != ReadStatus::Present || slot_.length > kSnapshotSize) {
            damaged = true;
            continue;
        }
        const auto status = decodeSnapshot(slot_.bytes, slot_.length, decoded_);
        if (status != SnapshotStatus::Ok && status != SnapshotStatus::Migrated) {
            damaged = true;
            continue;
        }
        if (activeSlot_ >= 0 && decoded_.sequence == sequence_) {
            encodeSnapshot(state, canonical_[0]);
            encodeSnapshot(decoded_, canonical_[1]);
            damaged |= std::memcmp(canonical_[0].bytes, canonical_[1].bytes, kSnapshotSize) != 0;
        }
        if (activeSlot_ < 0 || decoded_.sequence > sequence_) {
            activeSlot_ = static_cast<int>(i);
            state = decoded_;
            sequence_ = state.sequence;
            migrated = status == SnapshotStatus::Migrated;
        }
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
    if (migrated) {
        diagnostic_ = "older snapshot migrated in RAM; original retained until next checkpoint";
        return BootStatus::Migrated;
    }
    diagnostic_ = "latest verified snapshot restored";
    return BootStatus::Loaded;
}

bool SaveStore::checkpoint(const State& state) {
    if (!writable_) return false;
    auto& snapshot = canonical_[0];
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
    auto& verified = slot_;
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

bool SaveStore::checkpointMirrored(const State& state) {
    return checkpoint(state) && checkpoint(state);
}

} // namespace digivice::storage
