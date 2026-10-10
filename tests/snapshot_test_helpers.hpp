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
// Schema 23 stores a 12th care word on every member and three care-time words
// before the checksum. Strip those to recover the schema 22 image. The
// eight-slot view then uses the same offsets it used for that image.
inline void rules15Image(const std::uint8_t* current, std::uint8_t* out) {
    std::memcpy(out, current, 112);
    for (unsigned i = 0; i < 60; ++i)
        std::memcpy(out + 112 + i * 44, current + 112 + i * 48, 44);
    std::memcpy(out + 2752, current + 2992, 208);
}
inline std::array<std::uint8_t,664> eightSlotBytes(const std::uint8_t* current) {
    std::uint8_t schema22[2964]{};
    rules15Image(current, schema22);
    std::array<std::uint8_t,664> old{};
    std::memcpy(old.data(), schema22, 464);
    std::memcpy(old.data()+464, schema22+2752, 200);
    return old;
}
inline bool sameOldPayload(const std::uint8_t* old,const std::uint8_t* current,std::size_t oldSize) {
    const auto projected=eightSlotBytes(current);
    return !std::memcmp(old+12,projected.data()+12,oldSize-16);
}
} // namespace snapshot_test
