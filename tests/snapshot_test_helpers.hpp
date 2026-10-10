#pragma once
#include "game.hpp"
#include <array>
#include <cstring>

// Test-only projection for asserting migration of the frozen eight-slot payload.
// Schema21 inserts52 zero slots after the original eight; everything after that
// moves2288 bytes. Schema 27 packs 250 members into 26-byte slots and appends
// the expedition words. This does not rewrite source-era fixtures or their goldens.
namespace snapshot_test {
constexpr std::size_t kExtraRosterBytes = 52 * 44;
constexpr std::size_t currentOffset(std::size_t oldOffset) {
    return oldOffset < 464 ? oldOffset : oldOffset + kExtraRosterBytes;
}
inline void put32(std::uint8_t* out, std::uint32_t value) {
    out[0]=static_cast<std::uint8_t>(value); out[1]=static_cast<std::uint8_t>(value>>8);
    out[2]=static_cast<std::uint8_t>(value>>16); out[3]=static_cast<std::uint8_t>(value>>24);
}
inline std::uint32_t get32(const std::uint8_t* in) {
    return static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1])<<8) |
        (static_cast<std::uint32_t>(in[2])<<16) | (static_cast<std::uint32_t>(in[3])<<24);
}
inline std::uint16_t get16(const std::uint8_t* in) {
    return static_cast<std::uint16_t>(in[0] | (in[1]<<8));
}
// Expand the first 60 packed schema-27 members back into the schema-26 image.
inline void schema26Image(const std::uint8_t* current, std::uint8_t* out) {
    std::memset(out, 0, digivice::kSchema26SnapshotSize);
    std::memcpy(out, current, digivice::kSnapshotLeadBytes);
    for (unsigned i = 0; i < digivice::kRoster60Capacity; ++i) {
        const auto* packed = current + digivice::kSnapshotLeadBytes + i * digivice::kMemberSnapshotBytes;
        auto* words = out + digivice::kSnapshotLeadBytes + i * 48;
        const auto formSpecies = get32(packed + 8), xpHp = get32(packed + 12);
        const std::uint32_t values[]{get32(packed), formSpecies >> 16, xpHp >> 16, packed[17], packed[18], packed[19],
            packed[20], packed[16], get32(packed + 4), xpHp & 0xffffu, formSpecies & 0xffffu, get32(packed + 22)};
        for (unsigned word = 0; word < 12; ++word) put32(words + word * 4, values[word]);
    }
    std::memcpy(out + 2992, current + digivice::kSnapshotLeadBytes + digivice::kCollectionCapacity * digivice::kMemberSnapshotBytes, digivice::kSnapshotTailBytes);
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
    std::uint8_t schema26[digivice::kSchema26SnapshotSize]{};
    schema26Image(current, schema26);
    std::uint8_t schema22[2964]{};
    rules15Image(schema26, schema22);
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
