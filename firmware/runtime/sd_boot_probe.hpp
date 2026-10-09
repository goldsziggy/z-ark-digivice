#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Bounded, read-only partition/boot-header inspection, not a filesystem driver.
// Sector contents, labels, GUIDs and serial numbers are never reported. Caller
// supplies one 512-byte buffer and serializes ownership of its raw block reader.
namespace digivice::sdprobe {
constexpr unsigned kMaximumReads = 24, kMaximumGptEntries = 16;
enum class Kind { Unknown, Fat12, Fat16, Fat32, Exfat, Ntfs };
enum class Event { Filesystem, NoBootSignature, Partition, InvalidPartition,
                   ExtendedSkipped, GptHeader, InvalidGpt, GptLimited,
                   ReadFailed, ReadLimit, Summary };
using Read = bool (*)(void*, std::uint64_t lba, std::uint8_t* sector);
using Report = void (*)(void*, Event, std::uint64_t lba, std::uint64_t value, Kind);
inline std::uint16_t get16(const std::uint8_t* p) { return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8); }
inline std::uint32_t get32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
inline std::uint64_t get64(const std::uint8_t* p) { return get32(p) | (std::uint64_t(get32(p + 4)) << 32); }
inline bool signature(const std::uint8_t* b) { return b[510] == 0x55 && b[511] == 0xaa; }
inline Kind identify(const std::uint8_t* b, std::uint64_t availableSectors) {
    if (!signature(b)) return Kind::Unknown;
    if (!std::memcmp(b + 3, "EXFAT   ", 8)) {
        bool reservedZero = true;
        for (unsigned i = 11; i < 64; ++i) reservedZero &= b[i] == 0;
        const auto length = get64(b + 72);
        return reservedZero && b[108] == 9 && b[109] <= 16 &&
               (b[110] == 1 || b[110] == 2) && length && length <= availableSectors
            ? Kind::Exfat : Kind::Unknown;
    }
    if (!std::memcmp(b + 3, "NTFS    ", 8) && get16(b + 11) == 512) return Kind::Ntfs;
    const auto sectorsPerCluster = b[13];
    const auto reserved = get16(b + 14), rootEntries = get16(b + 17);
    const auto total = get16(b + 19) ? get16(b + 19) : get32(b + 32);
    const auto fat16 = get16(b + 22);
    const auto fatSectors = fat16 ? fat16 : get32(b + 36);
    if (get16(b + 11) != 512 || !sectorsPerCluster || sectorsPerCluster > 128 ||
        (sectorsPerCluster & (sectorsPerCluster - 1)) || !reserved ||
        (b[16] != 1 && b[16] != 2) || !total || total > availableSectors || !fatSectors) return Kind::Unknown;
    const auto rootSectors = (std::uint64_t(rootEntries) * 32 + 511) / 512;
    const auto overhead = reserved + std::uint64_t(b[16]) * fatSectors + rootSectors;
    if (overhead >= total) return Kind::Unknown;
    const auto clusters = (total - overhead) / sectorsPerCluster;
    if (clusters < 4085 && fat16 && rootEntries) return Kind::Fat12;
    if (clusters < 65525 && fat16 && rootEntries) return Kind::Fat16;
    if (clusters >= 65525 && !fat16 && !rootEntries && get16(b + 42) == 0 && get32(b + 44) >= 2) return Kind::Fat32;
    return Kind::Unknown;
}
class Probe {
public:
    Probe(Read read, Report report, void* context, std::uint64_t sectors, std::uint8_t* buffer)
        : read_(read), report_(report), context_(context), sectors_(sectors), buffer_(buffer) {}
    void run() {
        if (!read_ || !report_ || !buffer_ || !sectors_) return;
        if (!load(0)) { finish(); return; }
        const auto rootKind = identify(buffer_, sectors_);
        emit(Event::Filesystem, 0, sectors_, rootKind);
        if (rootKind != Kind::Unknown) { finish(); return; }
        if (!signature(buffer_)) { emit(Event::NoBootSignature, 0); finish(); return; }
        struct Partition { std::uint64_t first = 0, length = 0; std::uint8_t type = 0; } parts[4];
        for (unsigned i = 0; i < 4; ++i) {
            const auto* entry = buffer_ + 446 + i * 16;
            parts[i] = {get32(entry + 8), get32(entry + 12), entry[4]};
        }
        bool gpt = false;
        for (const auto& p : parts) {
            if (!p.type) continue;
            if (!p.first || !p.length || p.first >= sectors_ || p.length > sectors_ - p.first) {
                emit(Event::InvalidPartition, p.first, p.length); continue;
            }
            emit(Event::Partition, p.first, p.length);
            if (p.type == 0xee) { gpt = true; continue; }
            if (p.type == 0x05 || p.type == 0x0f || p.type == 0x85) {
                emit(Event::ExtendedSkipped, p.first, p.length); continue;
            }
            boot(p.first, p.length);
        }
        if (gpt) inspectGpt();
        finish();
    }
private:
    bool load(std::uint64_t lba) {
        if (lba >= sectors_) { emit(Event::InvalidPartition, lba); return false; }
        if (reads_ >= kMaximumReads) { emit(Event::ReadLimit, lba, reads_); return false; }
        ++reads_;
        if (!read_(context_, lba, buffer_)) { emit(Event::ReadFailed, lba); return false; }
        return true;
    }
    void boot(std::uint64_t lba, std::uint64_t length) {
        if (load(lba)) emit(Event::Filesystem, lba, length, identify(buffer_, length));
    }
    void inspectGpt() {
        if (!load(1)) return;
        const auto headerSize = get32(buffer_ + 12);
        const auto firstUsable = get64(buffer_ + 40), lastUsable = get64(buffer_ + 48);
        const auto table = get64(buffer_ + 72);
        const auto count = get32(buffer_ + 80), entrySize = get32(buffer_ + 84);
        if (std::memcmp(buffer_, "EFI PART", 8) || get32(buffer_ + 8) != 0x00010000 ||
            headerSize < 92 || headerSize > 512 || get64(buffer_ + 24) != 1 ||
            firstUsable < 2 || firstUsable > lastUsable || lastUsable >= sectors_ ||
            table < 2 || table >= firstUsable || !count ||
            (entrySize != 128 && entrySize != 256 && entrySize != 512) ||
            (std::uint64_t(count) * entrySize + 511) / 512 > firstUsable - table) {
            emit(Event::InvalidGpt, 1); return;
        }
        emit(Event::GptHeader, table, count);
        const auto checked = count < kMaximumGptEntries ? count : kMaximumGptEntries;
        unsigned partitions = 0;
        for (unsigned i = 0; i < checked && partitions < 4; ++i) {
            const auto byteOffset = std::uint64_t(i) * entrySize;
            if (!load(table + byteOffset / 512)) break;
            const auto* entry = buffer_ + byteOffset % 512;
            bool populated = false;
            for (unsigned j = 0; j < 16; ++j) populated |= entry[j] != 0;
            if (!populated) continue;
            ++partitions;
            const auto first = get64(entry + 32), last = get64(entry + 40);
            if (first < firstUsable || last < first || last > lastUsable) {
                emit(Event::InvalidPartition, first, last); continue;
            }
            emit(Event::Partition, first, last - first + 1);
            boot(first, last - first + 1);
        }
        if (count > checked || partitions == 4) emit(Event::GptLimited, table, checked);
    }
    void emit(Event event, std::uint64_t lba, std::uint64_t value = 0, Kind kind = Kind::Unknown) {
        report_(context_, event, lba, value, kind);
    }
    void finish() { emit(Event::Summary, 0, reads_); }
    Read read_; Report report_; void* context_; std::uint64_t sectors_; std::uint8_t* buffer_; unsigned reads_ = 0;
};
}
