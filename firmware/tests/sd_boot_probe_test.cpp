#include "../runtime/sd_boot_probe.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>

namespace {
using namespace digivice::sdprobe;
using Sector = std::array<std::uint8_t, 512>;
struct Note { Event event; std::uint64_t lba, value; Kind kind; };
struct Fixture {
    std::map<std::uint64_t, Sector> sectors;
    std::vector<std::uint64_t> reads;
    std::vector<Note> notes;
    std::uint64_t capacity = 100000;
    std::uint64_t failAt = UINT64_MAX;
    static bool read(void* context, std::uint64_t lba, std::uint8_t* out) {
        auto& f = *static_cast<Fixture*>(context);
        assert(lba < f.capacity); f.reads.push_back(lba);
        if (lba == f.failAt) return false;
        const auto found = f.sectors.find(lba);
        if (found == f.sectors.end()) std::memset(out, 0, 512);
        else std::memcpy(out, found->second.data(), 512);
        return true;
    }
    static void report(void* context, Event event, std::uint64_t lba, std::uint64_t value, Kind kind) {
        static_cast<Fixture*>(context)->notes.push_back({event, lba, value, kind});
    }
    void run() {
        Sector buffer{};
        Probe(read, report, this, capacity, buffer.data()).run();
        assert(reads.size() <= kMaximumReads);
    }
    bool saw(Event event, Kind kind = Kind::Unknown) const {
        for (const auto& n : notes) if (n.event == event && n.kind == kind) return true;
        return false;
    }
};
void put16(Sector& s, unsigned offset, std::uint16_t v) { s[offset] = v & 255; s[offset + 1] = v >> 8; }
void put32(Sector& s, unsigned offset, std::uint32_t v) { for (unsigned i = 0; i < 4; ++i) s[offset + i] = v >> (8 * i); }
void put64(Sector& s, unsigned offset, std::uint64_t v) { for (unsigned i = 0; i < 8; ++i) s[offset + i] = v >> (8 * i); }
void bootSignature(Sector& s) { s[510] = 0x55; s[511] = 0xaa; }
Sector exfat(std::uint64_t length) {
    Sector s{}; bootSignature(s); std::memcpy(s.data() + 3, "EXFAT   ", 8);
    put64(s, 72, length); s[108] = 9; s[109] = 7; s[110] = 1; return s;
}
void mbr(Sector& s, unsigned index, std::uint8_t type, std::uint32_t start, std::uint32_t length) {
    bootSignature(s); const auto at = 446 + index * 16;
    s[at + 4] = type; put32(s, at + 8, start); put32(s, at + 12, length);
}
Sector gpt(std::uint32_t count = 128, std::uint32_t size = 128) {
    Sector s{}; std::memcpy(s.data(), "EFI PART", 8); put32(s, 8, 0x00010000);
    put32(s, 12, 92); put64(s, 24, 1); put64(s, 40, 34); put64(s, 48, 99966);
    put64(s, 72, 2); put32(s, 80, count); put32(s, 84, size); return s;
}
}
int main() {
    {
        Fixture f; f.sectors[0] = exfat(f.capacity); f.run();
        assert(f.saw(Event::Filesystem, Kind::Exfat)); assert(f.reads.size() == 1);
    }
    {
        Fixture f; auto s = exfat(f.capacity); s[50] = 1; f.sectors[0] = s; f.run();
        assert(!f.saw(Event::Filesystem, Kind::Exfat));
    }
    for (auto expected : {Kind::Fat12, Kind::Fat16, Kind::Fat32}) {
        Fixture f; Sector s{}; bootSignature(s); put16(s, 11, 512); s[13] = 1; s[16] = 2;
        put16(s, 14, expected == Kind::Fat32 ? 32 : 1);
        put16(s, 17, expected == Kind::Fat32 ? 0 : 224);
        put32(s, 32, expected == Kind::Fat32 ? 100000 : expected == Kind::Fat16 ? 10000 : 3000);
        put16(s, 22, expected == Kind::Fat32 ? 0 : 40);
        if (expected == Kind::Fat32) { put32(s, 36, 100); put32(s, 44, 2); }
        f.sectors[0] = s; f.run(); assert(f.saw(Event::Filesystem, expected)); assert(f.reads.size() == 1);
    }
    {
        Fixture f; mbr(f.sectors[0], 0, 7, 2048, 50000);
        mbr(f.sectors[0], 1, 7, 0xffffff00, 65535);
        mbr(f.sectors[0], 2, 0x0f, 55000, 1000);
        f.sectors[2048] = exfat(50000); f.run();
        assert(f.saw(Event::Filesystem, Kind::Exfat)); assert(f.saw(Event::InvalidPartition));
        assert(f.saw(Event::ExtendedSkipped)); assert(f.reads.size() == 2);
    }
    {
        Fixture f; mbr(f.sectors[0], 0, 0xee, 1, 99999); f.sectors[1] = gpt();
        auto& entry = f.sectors[2]; entry[0] = 1; put64(entry, 32, 2048); put64(entry, 40, 59999);
        f.sectors[2048] = exfat(57952); f.run();
        assert(f.saw(Event::GptHeader)); assert(f.saw(Event::GptLimited));
        assert(f.saw(Event::Filesystem, Kind::Exfat)); assert(f.reads.size() == 19);
    }
    {
        Fixture f; mbr(f.sectors[0], 0, 0xee, 1, 99999); f.sectors[1] = gpt(0xffffffff, 512);
        f.run(); assert(f.saw(Event::InvalidGpt)); assert(f.reads.size() == 2);
    }
    {
        Fixture f; mbr(f.sectors[0], 0, 0xee, 1, 99999); f.sectors[1] = gpt();
        auto& entry = f.sectors[2]; entry[0] = 1; put64(entry, 32, UINT64_MAX); put64(entry, 40, UINT64_MAX);
        f.run(); assert(f.saw(Event::InvalidPartition));
    }
    {
        Fixture f; f.failAt = 0; f.run(); assert(f.saw(Event::ReadFailed)); assert(f.reads.size() == 1);
    }
    {
        Fixture f; f.run(); assert(f.saw(Event::NoBootSignature)); assert(f.reads.size() == 1);
    }
    // Mutated untrusted partition offsets never become out-of-capacity reads.
    std::uint32_t random = 11;
    for (unsigned i = 0; i < 1000; ++i) {
        random = random * 1664525 + 1013904223;
        Fixture f; mbr(f.sectors[0], 0, 7, random, random ^ 0x91919191); f.run();
    }
    std::puts("SD boot probe: FAT12/16/32, exFAT, MBR/GPT bounds, read failure and 1000 malformed extents passed.");
}
