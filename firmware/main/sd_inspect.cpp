#include "sd_inspect.hpp"
#include "sd_asset_storage.hpp"
#include "board_hal.hpp"
#include "../runtime/sd_boot_probe.hpp"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "ff.h"
#include "sdkconfig.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace digivice::assets {
namespace {
constexpr char kRoot[] = "/sdcard";
constexpr unsigned kEntries = 512, kDirectories = 16, kDepth = 3, kSamples = 24;
constexpr std::size_t kPath = 128, kHeader = 4096;
struct Directory { char path[kPath]{}; unsigned depth = 0; };
struct Counts {
    unsigned files = 0, directories = 0, dva = 0, dvaHeaders = 0, jpeg = 0, jpeg412 = 0;
    unsigned metadata = 0, errors = 0, skipped = 0, entries = 0, samples = 0;
    bool limited = false;
};
unsigned u16(const unsigned char* p) { return unsigned(p[0]) | (unsigned(p[1]) << 8); }
std::uint32_t u32(const unsigned char* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
unsigned be16(const unsigned char* p) { return (unsigned(p[0]) << 8) | p[1]; }
char upper(char ch) { return ch >= 'a' && ch <= 'z' ? char(ch - 'a' + 'A') : ch; }
bool prefix(const char* name, const char* value) {
    while (*value) if (upper(*name++) != *value++) return false;
    return true;
}
bool extension(const char* name, const char* value) {
    const auto* dot = std::strrchr(name, '.');
    if (!dot) return false;
    ++dot;
    while (*value) if (upper(*dot++) != *value++) return false;
    return !*dot;
}
bool assetDirectory(const char* name) {
    for (const auto* start : {"ASSET", "DEVICE", "SPRITE", "SCENE", "BG", "BACK", "FORM",
                              "PACK", "DIGI", "WORLD", "SD", "PUBLIC", "DATA"})
        if (prefix(name, start)) return true;
    return false;
}
void printable(const char* source, char (&out)[kPath]) {
    std::size_t i = 0;
    for (; source[i] && i + 1 < sizeof(out); ++i) {
        const auto ch = static_cast<unsigned char>(source[i]);
        out[i] = ch >= 32 && ch < 127 ? char(ch) : '?';
    }
    out[i] = 0;
}
bool readHeader(const char* path, unsigned char* output, std::size_t maximum, std::size_t& used) {
    int flags = O_RDONLY;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = ::open(path, flags);
    if (fd < 0) return false;
    used = 0;
    bool ok = true;
    unsigned interrupted = 0;
    while (used < maximum) {
        const auto count = ::read(fd, output + used, maximum - used);
        if (count < 0 && errno == EINTR && interrupted++ < 3) continue;
        if (count < 0) { ok = false; break; }
        if (!count) break;
        used += static_cast<std::size_t>(count);
    }
    if (::close(fd) != 0) ok = false;
    return ok;
}
const char* dvaHeader(const unsigned char* b, std::size_t n, std::uint64_t size) {
    if (n < 32 || std::memcmp(b, "DVA1", 4) || u16(b + 4) != 1 || u16(b + 6) != 32)
        return "DVA header incompatible";
    const auto width = u16(b + 12), frames = u16(b + 20);
    if (!u32(b + 8) || (width != 16 && width != 32) || u16(b + 14) != width ||
        b[16] != 16 || b[17] != 6 || b[18] || b[19] || frames < 6 || frames > 48 ||
        u16(b + 22) != width * width / 2 || u32(b + 24) != 80 + frames * width * width / 2 ||
        size != 32ULL + u32(b + 24)) return "DVA header incompatible";
    return "DVA header compatible; CRC not checked";
}
const char* jpegHeader(const unsigned char* b, std::size_t n, unsigned& width, unsigned& height) {
    width = height = 0;
    if (n < 4 || b[0] != 0xff || b[1] != 0xd8) return "JPEG signature invalid";
    std::size_t at = 2;
    while (at + 1 < n) {
        if (b[at++] != 0xff) return "JPEG marker invalid";
        while (at < n && b[at] == 0xff) ++at;
        if (at == n) break;
        const auto marker = b[at++];
        if (marker == 0xd9 || marker == 0xda) break;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
        if (at + 2 > n) break;
        const auto length = be16(b + at);
        if (length < 2) return "JPEG segment invalid";
        if (length > n - at) break;
        const bool sof = marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc;
        if (sof) {
            if (length < 8 || length != 8U + 3U * b[at + 7]) return "JPEG frame header invalid";
            height = be16(b + at + 3); width = be16(b + at + 5);
            if (marker != 0xc0 || b[at + 2] != 8 || b[at + 7] != 3) return "JPEG unsupported encoding";
            return width == 412 && height == 412 ? "baseline JPEG 412x412 header recognized; pixels not decoded by inventory" : "baseline JPEG dimensions differ from 412x412";
        }
        at += length;
    }
    return "JPEG frame not found within 4096-byte inspection bound";
}
void expected(const char* name) {
    char path[kPath];
    const auto count = std::snprintf(path, sizeof(path), "%s/%s", kRoot, name);
    if (count < 0 || static_cast<std::size_t>(count) >= sizeof(path)) return;
    struct stat info{};
    if (::stat(path, &info) == 0)
        std::printf("SD expected %s: %s (%llu B)\n", path, S_ISREG(info.st_mode) ? "present" : "not a regular file", static_cast<unsigned long long>(info.st_size));
    else std::printf("SD expected %s: %s\n", path, errno == ENOENT ? "MISSING at root path" : "unavailable (I/O or path error)");
}
}

void printSdInventory(const SdAssetStorage& storage, std::uint32_t expectedFormId) {
    std::printf("SD inventory mounted=%d cacheReady=%d status=%s\n", storage.mounted(), storage.ready(), storage.diagnostic());
    if (!storage.mounted()) { std::puts("SD inventory unavailable: no mounted card; this command never mounts or repairs media."); return; }
    const auto* card = storage.mountedCard();
    if (card && card->csd.capacity > 0 && card->csd.sector_size > 0)
        std::printf("SD card capacity=%llu B sector=%u B; mounted FAT, exact subtype not queried.\n",
            static_cast<unsigned long long>(static_cast<std::uint64_t>(card->csd.capacity) * static_cast<unsigned>(card->csd.sector_size)),
            static_cast<unsigned>(card->csd.sector_size));
    std::puts("SD inventory bounds: 512 entries, 16 directories, depth 3, paths 128 B, 24 sample paths; no content dump.");
    expected("DVASSET1.CCH"); expected("INDEX.JSON");
    std::puts("SD INDEX.JSON is optional host metadata; the current loader does not parse it.");
    if (expectedFormId >= 1 && expectedFormId <= 512) {
        char name[16]; std::snprintf(name, sizeof(name), "DSF%05u.DVA", static_cast<unsigned>(expectedFormId)); expected(name);
    } else std::puts("SD expected form file: no selected supported form ID.");
    Directory queue[kDirectories]{};
    std::strcpy(queue[0].path, kRoot);
    unsigned queued = 1;
    Counts total;
    unsigned char header[kHeader];
    for (unsigned index = 0; index < queued && total.entries < kEntries; ++index) {
        const auto& folder = queue[index];
        DIR* dir = ::opendir(folder.path);
        if (!dir) { ++total.errors; continue; }
        ++total.directories;
        char shown[kPath]; printable(folder.path, shown);
        std::printf("SD directory %s\n", shown);
        unsigned samplesHere = 0, filesHere = 0, dvaHere = 0, jpegHere = 0;
        bool jpegShown = false, metadataShown = false;
        for (;;) {
            if (total.entries == kEntries) { total.limited = true; break; }
            errno = 0;
            auto* item = ::readdir(dir);
            if (!item) { if (errno) ++total.errors; break; }
            // Includes skipped/dot entries in the traversal bound.
            ++total.entries;
            if (!*item->d_name || item->d_name[0] == '.') { ++total.skipped; continue; }
            if (std::strchr(item->d_name, '/') || std::strchr(item->d_name, '\\')) { ++total.skipped; continue; }
            char path[kPath];
            const auto count = std::snprintf(path, sizeof(path), "%s/%s", folder.path, item->d_name);
            if (count < 0 || static_cast<std::size_t>(count) >= sizeof(path)) { ++total.skipped; total.limited = true; continue; }
            struct stat info{};
            // ESP FAT has no symlinks and its VFS does not promise lstat.
            // Host inspection must not follow a link outside the card root.
#if defined(ESP_PLATFORM)
            const auto inspected = ::stat(path, &info);
#else
            const auto inspected = ::lstat(path, &info);
#endif
            if (inspected != 0) { ++total.errors; continue; }
            if (S_ISDIR(info.st_mode)) {
                if (folder.depth < kDepth && (folder.depth || assetDirectory(item->d_name)) && queued < kDirectories) {
                    std::strcpy(queue[queued].path, path); queue[queued++].depth = folder.depth + 1;
                } else { ++total.skipped; total.limited = true; }
                continue;
            }
            if (!S_ISREG(info.st_mode) || info.st_size < 0) { ++total.skipped; continue; }
            ++total.files; ++filesHere;
            const bool dva = extension(item->d_name, "DVA");
            const bool jpeg = extension(item->d_name, "JPG") || extension(item->d_name, "JPEG");
            const bool metadata = prefix(item->d_name, "INDEX") || prefix(item->d_name, "MANIFE");
            const char* kind = "other asset/file (not opened)";
            unsigned width = 0, height = 0;
            if (dva || jpeg) {
                if (dva) { ++total.dva; ++dvaHere; } else { ++total.jpeg; ++jpegHere; }
                std::size_t used = 0;
                const auto maximum = std::min<std::uint64_t>(dva ? 32 : kHeader, static_cast<std::uint64_t>(info.st_size));
                if (!readHeader(path, header, static_cast<std::size_t>(maximum), used)) { ++total.errors; kind = "header read failed"; }
                else if (dva) {
                    kind = dvaHeader(header, used, static_cast<std::uint64_t>(info.st_size));
                    if (std::strcmp(kind, "DVA header compatible; CRC not checked") == 0) ++total.dvaHeaders;
                } else {
                    kind = jpegHeader(header, used, width, height);
                    if (std::strcmp(kind, "baseline JPEG 412x412 header recognized; pixels not decoded by inventory") == 0) ++total.jpeg412;
                }
            } else if (metadata) { ++total.metadata; kind = "index/manifest filename; content not read"; }
            if (total.samples < kSamples && (samplesHere < 3 || (jpeg && !jpegShown) || (metadata && !metadataShown))) {
                printable(path, shown);
                std::printf("SD sample %s %llu B: %s", shown, static_cast<unsigned long long>(info.st_size), kind);
                if (jpeg && width && height) std::printf(" (%ux%u)", width, height);
                std::putchar('\n');
                ++samplesHere; ++total.samples; jpegShown |= jpeg; metadataShown |= metadata;
            }
        }
        if (::closedir(dir) != 0) ++total.errors;
        std::printf("SD directory summary files=%u DVA=%u JPEG=%u\n", filesHere, dvaHere, jpegHere);
    }
    std::printf("SD inventory summary entries=%u directories=%u files=%u DVA=%u compatibleDvaHeaders=%u JPEG=%u baseline412Headers=%u metadata=%u errors=%u skipped=%u limited=%d\n",
        total.entries, total.directories, total.files, total.dva, total.dvaHeaders, total.jpeg,
        total.jpeg412, total.metadata, total.errors, total.skipped, total.limited);
    std::puts("SD header compatibility is not CRC/SHA verification. Firmware expects root DSFnnnnn.DVA; metadata alone does not install art. No file was modified.");
}

#if defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146 && \
    defined(CONFIG_DIGIVICE_SD_ASSETS) && CONFIG_DIGIVICE_SD_ASSETS
namespace {
bool rawSector(void* context, std::uint64_t lba, std::uint8_t* output) {
    auto* card = static_cast<sdmmc_card_t*>(context);
    if (!card || card->csd.sector_size != 512 || card->csd.capacity <= 0 ||
        lba >= static_cast<std::uint64_t>(card->csd.capacity)) return false;
    const auto result = sdmmc_read_sectors(card, output, static_cast<std::size_t>(lba), 1);
    if (result != ESP_OK) std::printf("SD probe read error=%s\n", esp_err_to_name(result));
    return result == ESP_OK;
}
void reportProbe(void*, sdprobe::Event event, std::uint64_t lba, std::uint64_t value, sdprobe::Kind kind) {
    const auto sector = static_cast<unsigned long long>(lba), number = static_cast<unsigned long long>(value);
    using E = sdprobe::Event;
    switch (event) {
    case E::Filesystem: {
        const char* name = "unrecognized";
        switch (kind) {
        case sdprobe::Kind::Fat12: name = "FAT12"; break;
        case sdprobe::Kind::Fat16: name = "FAT16"; break;
        case sdprobe::Kind::Fat32: name = "FAT32"; break;
        case sdprobe::Kind::Exfat: name = "exFAT"; break;
        case sdprobe::Kind::Ntfs: name = "NTFS"; break;
        case sdprobe::Kind::Unknown: break;
        }
        std::printf("SD probe filesystem header=%s LBA=%llu extentSectors=%llu (header evidence only)\n", name, sector, number);
        if (kind == sdprobe::Kind::Exfat) std::printf("SD probe exFAT build support=%d; no format or repair attempted.\n", FF_FS_EXFAT);
        break;
    }
    case E::NoBootSignature: std::printf("SD probe no 55AA boot signature at LBA=%llu\n", sector); break;
    case E::Partition: std::printf("SD probe partition LBA=%llu sectors=%llu\n", sector, number); break;
    case E::InvalidPartition: std::printf("SD probe invalid/out-of-range partition LBA=%llu value=%llu; skipped\n", sector, number); break;
    case E::ExtendedSkipped: std::printf("SD probe extended partition LBA=%llu skipped by bounded inspector\n", sector); break;
    case E::GptHeader:
        std::printf("SD probe GPT header tableLBA=%llu entries=%llu; CRC not verified; inspecting first16 entries/four partitions\n", sector, number);
        std::printf("SD probe filesystem build LBA64/GPT support=%d\n", FF_LBA64); break;
    case E::InvalidGpt: std::puts("SD probe GPT header invalid or unsupported; no partition traversal"); break;
    case E::GptLimited: std::printf("SD probe GPT inspection bounded: examined at most %llu entries/four partitions\n", number); break;
    case E::ReadFailed: std::printf("SD probe failed reading LBA=%llu\n", sector); break;
    case E::ReadLimit: std::printf("SD probe read limit reached=%llu; remaining sectors untouched\n", number); break;
    case E::Summary: std::printf("SD probe complete sectorReads=%llu maximum=%u; no block writes or filesystem mount\n", number, sdprobe::kMaximumReads); break;
    }
}
}
#endif

void printSdBootProbe(const SdAssetStorage& storage) {
    if (storage.mounted()) { std::puts("SD probe refused: card is mounted; use art inventory."); return; }
#if defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146 && \
    defined(CONFIG_DIGIVICE_SD_ASSETS) && CONFIG_DIGIVICE_SD_ASSETS
    const auto& profile = board::selectedProfile();
    if (profile.id != board::ProfileId::Waveshare146 || !board::powerHoldReady()) {
        std::puts("SD probe refused: verified board/power hold unavailable."); return;
    }
    // This is the existing board-owned expander API; it preserves LCD/touch bits
    // and does not install a second bus or expander driver.
    auto result = board::prepareSdCardSelect();
    if (result != ESP_OK) { std::printf("SD probe select failed=%s\n", esp_err_to_name(result)); return; }
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = static_cast<gpio_num_t>(profile.sdClockGpio);
    slot.cmd = static_cast<gpio_num_t>(profile.sdCommandGpio);
    slot.d0 = static_cast<gpio_num_t>(profile.sdData0Gpio);
    slot.d1 = slot.d2 = slot.d3 = GPIO_NUM_NC;
    slot.d4 = slot.d5 = slot.d6 = slot.d7 = GPIO_NUM_NC;
    slot.cd = slot.wp = GPIO_NUM_NC;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    result = host.init();
    if (result != ESP_OK) {
        // Never deinitialize a host that this diagnostic did not initialize.
        std::printf("SD probe host init failed=%s; no cleanup of another owner\n", esp_err_to_name(result)); return;
    }
    std::uint8_t* buffer = nullptr;
    do {
        result = sdmmc_host_init_slot(host.slot, &slot);
        if (result != ESP_OK) { std::printf("SD probe slot init failed=%s\n", esp_err_to_name(result)); break; }
        sdmmc_card_t card{};
        result = sdmmc_card_init(&host, &card);
        if (result != ESP_OK) { std::printf("SD probe card init failed=%s\n", esp_err_to_name(result)); break; }
        if (card.csd.sector_size != 512 || card.csd.capacity <= 0) {
            std::puts("SD probe unsupported sector size/capacity; no sectors read."); break;
        }
        std::printf("SD probe card capacity=%llu B sectors=%u sector=512 B; existing one-bit SDMMC pins/timing\n",
            static_cast<unsigned long long>(std::uint64_t(card.csd.capacity) * 512), static_cast<unsigned>(card.csd.capacity));
        buffer = static_cast<std::uint8_t*>(heap_caps_malloc(512, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        if (!buffer) { std::puts("SD probe cannot allocate bounded DMA buffer; no sectors read."); break; }
        sdprobe::Probe probe(rawSector, reportProbe, &card, static_cast<std::uint64_t>(card.csd.capacity), buffer);
        probe.run();
    } while (false);
    // host.init succeeded: clean up our host on every subsequent exit, including
    // allocation/card/read failure. No mount/cache state is published or changed.
    const auto cleanup = sdmmc_host_deinit();
    heap_caps_free(buffer);
    std::printf("SD probe host cleanup=%s; card files and saved game were not modified.\n", esp_err_to_name(cleanup));
#else
    std::puts("SD probe unavailable in this board/build; no pins touched.");
#endif
}
}
