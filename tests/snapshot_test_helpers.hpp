#pragma once
#include "game.hpp"
#include <array>
#include <cstring>

// Test-only projection for asserting migration of the frozen eight-slot payload.
// Schema21 inserts52 zero slots after the original eight; everything after that
// moves2288 bytes. This does not rewrite source-era fixtures or their goldens.
namespace snapshot_test {
constexpr std::size_t kExtraRosterBytes = 52 * 44;
constexpr std::size_t currentOffset(std::size_t oldOffset) {
    return oldOffset < 464 ? oldOffset : oldOffset + kExtraRosterBytes;
}
inline std::array<std::uint8_t,664> eightSlotBytes(const std::uint8_t* current) {
    std::array<std::uint8_t,664> old{};
    std::memcpy(old.data(),current,464);
    std::memcpy(old.data()+464,current+currentOffset(464),200);
    return old;
}
inline bool sameOldPayload(const std::uint8_t* old,const std::uint8_t* current,std::size_t oldSize) {
    const auto projected=eightSlotBytes(current);
    return !std::memcmp(old+12,projected.data()+12,oldSize-16);
}
} // namespace snapshot_test
