#pragma once
#include "save_store.hpp"
#include <cstring>

// Deterministic NVS commit boundary model: an unsuccessful write may have landed.
class ParkSaveBackend final : public digivice::storage::Backend {
public:
    digivice::storage::Slot slots[2]{};
    bool present[2]{};
    bool failBefore=false, failAfter=false, failReadback=false;
    unsigned writes=0;
    digivice::storage::ReadStatus readSlot(unsigned i,digivice::storage::Slot& out) override {
        if(failReadback)return digivice::storage::ReadStatus::Unreadable;
        if(!present[i])return digivice::storage::ReadStatus::Missing;
        out=slots[i];return digivice::storage::ReadStatus::Present;
    }
    bool writeSlot(unsigned i,const digivice::Snapshot& value) override {
        ++writes;if(failBefore)return false;
        std::memcpy(slots[i].bytes,value.bytes,digivice::kSnapshotSize);
        slots[i].length=digivice::kSnapshotSize;present[i]=true;
        return !failAfter;
    }
};
inline bool sameSavedState(const digivice::State& a,const digivice::State& b) {
    digivice::Snapshot left,right;
    return digivice::encodeSnapshot(a,left) && digivice::encodeSnapshot(b,right) &&
        std::memcmp(left.bytes,right.bytes,digivice::kSnapshotSize)==0;
}
