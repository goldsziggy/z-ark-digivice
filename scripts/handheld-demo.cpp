// Deterministic host integration demo. Sensor/radio/save adapters are fake;
// asset storage is either fake NOR or an owned temporary POSIX file. The game,
// save journal, motion/network policies and cache are shared firmware sources.
// No sockets, physical card access or SDK calls.
#include "game.hpp"
#include "save_store.hpp"
#include "motion.hpp"
#include "network.hpp"
#include "asset_cache.hpp"
#include "fallback_asset.hpp"
#ifdef DIGIVICE_DEMO_FILE_CACHE
#include "file_asset_storage.hpp"
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
namespace assets = digivice::assets;
namespace net = digivice::net;
namespace storage = digivice::storage;

void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void cacheOk(assets::Result result, const char* operation) {
    if (result != assets::Result::Ok) {
        std::fprintf(stderr, "FAIL: %s: %s\n", operation, assets::resultName(result)); std::exit(1);
    }
}

// Retained for object-level simulated reboots in this process only. This does
// not simulate torn writes; dedicated SaveStore/cache tests cover those faults.
class FakeNvs final : public storage::Backend {
public:
    storage::Slot slots[2]{};
    unsigned writes = 0;
    storage::ReadStatus readSlot(unsigned index, storage::Slot& output) override {
        if (index >= 2) return storage::ReadStatus::Unreadable;
        output = slots[index];
        return output.length ? storage::ReadStatus::Present : storage::ReadStatus::Missing;
    }
    bool writeSlot(unsigned index, const digivice::Snapshot& snapshot) override {
        if (index >= 2) return false;
        std::memcpy(slots[index].bytes, snapshot.bytes, sizeof(snapshot.bytes));
        slots[index].length = sizeof(snapshot.bytes); ++writes;
        return true;
    }
};

#ifndef DIGIVICE_DEMO_FILE_CACHE
class DemoStorage final : public assets::Storage {
public:
    std::uint8_t bytes[assets::kStorageBytes];
    std::size_t programmed = 0, erased = 0;
    explicit DemoStorage(const char*) { std::memset(bytes, 255, sizeof(bytes)); }
    void reopen() {} // Object reboot retains this fake flash array.
    void report() const {
        std::printf("FAKE FLASH: host static array=%zu B programmed=%zu B erased=%zu B; not device working RAM.\n",
                    sizeof(bytes), programmed, erased);
    }
    bool read(std::size_t offset, void* output, std::size_t length) override {
        if (!output || !inside(offset, length)) return false;
        std::memcpy(output, bytes + offset, length); return true;
    }
    bool erase(std::size_t offset, std::size_t length) override {
        if (!inside(offset, length) || offset % assets::kChunkBytes || length % assets::kChunkBytes) return false;
        std::memset(bytes + offset, 255, length); erased += length; return true;
    }
    bool program(std::size_t offset, const void* input, std::size_t length) override {
        if (!input || !inside(offset, length)) return false;
        const auto* source = static_cast<const std::uint8_t*>(input);
        for (std::size_t i = 0; i < length; ++i)
            if ((bytes[offset + i] & source[i]) != source[i]) return false;
        for (std::size_t i = 0; i < length; ++i) bytes[offset + i] &= source[i];
        programmed += length; return true;
    }
private:
    static bool inside(std::size_t offset, std::size_t length) {
        return offset <= assets::kStorageBytes && length <= assets::kStorageBytes - offset;
    }
};
#else
// POSIX test HAL, not an ESP/FAT/card driver. The launcher owns a private fresh
// temp directory. O_EXCL creates one new file; reopen verifies the same inode,
// regular-file type and user, and holds a nonblocking exclusive advisory lock.
class PosixFileIo final : public assets::FileIo {
public:
    explicit PosixFileIo(const char* path) : path_(path) { open(true); }
    ~PosixFileIo() override { if (fd_ >= 0) ::close(fd_); }
    void reopen() {
        require(fd_ >= 0 && ::close(fd_) == 0, "close owned POSIX cache file");
        fd_ = -1;
        open(false);
    }
    bool size(std::size_t& bytes) override {
        struct stat info{};
        if (::fstat(fd_, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0) return false;
        bytes = static_cast<std::size_t>(info.st_size); return true;
    }
    bool read(std::size_t offset, void* bytes, std::size_t length) override {
        auto* destination = static_cast<std::uint8_t*>(bytes);
        std::size_t done = 0;
        while (done < length) {
            const auto count = ::pread(fd_, destination + done, length - done, static_cast<off_t>(offset + done));
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) return false;
            done += static_cast<std::size_t>(count);
        }
        return true;
    }
    bool write(std::size_t offset, const void* bytes, std::size_t length) override {
        const auto* source = static_cast<const std::uint8_t*>(bytes);
        std::size_t done = 0;
        while (done < length) {
            const auto count = ::pwrite(fd_, source + done, length - done, static_cast<off_t>(offset + done));
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) return false;
            done += static_cast<std::size_t>(count);
        }
        written += length; return true;
    }
    bool sync() override {
        int result;
        do { result = ::fsync(fd_); } while (result != 0 && errno == EINTR);
        if (result == 0) ++syncs;
        return result == 0;
    }
    std::size_t written = 0, syncs = 0, opens = 0;
private:
    const char* path_;
    int fd_ = -1;
    dev_t device_{}; ino_t inode_{};
    void open(bool create) {
        const int flags = O_RDWR | O_NOFOLLOW | O_CLOEXEC | (create ? O_CREAT | O_EXCL : 0);
        fd_ = ::open(path_, flags, 0600);
        require(fd_ >= 0, create ? "exclusive-create temporary cache file" : "reopen owned cache file");
        struct stat info{};
        require(::fstat(fd_, &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == ::geteuid(), "owned regular cache file");
        require(::flock(fd_, LOCK_EX | LOCK_NB) == 0, "exclusive POSIX file lock");
        if (create) { device_ = info.st_dev; inode_ = info.st_ino; }
        else require(info.st_dev == device_ && info.st_ino == inode_, "reopen preserved file identity");
        ++opens;
    }
};

class DemoStorage final : public assets::Storage {
public:
    explicit DemoStorage(const char* path) : io_(path), file_(io_) {
        require(file_.begin(true), "initialize newly created file through FileAssetStorage");
        std::size_t size = 0;
        require(io_.size(size) && size == assets::kFileStorageBytes, "fixed owned-file size");
        std::printf("POSIX FILE: created %zu B owned cache, including %zu B ownership header; no card formatting.\n",
                    size, assets::kFileHeaderBytes);
    }
    void reopen() {
        file_.end(); io_.reopen();
        require(file_.begin(false), "reopen existing ownership envelope without rewriting");
        std::puts("POSIX FILE: descriptor closed/reopened; existing ownership and size accepted without reinitializing.");
    }
    bool read(std::size_t offset, void* bytes, std::size_t length) override { return file_.read(offset, bytes, length); }
    bool erase(std::size_t offset, std::size_t length) override { return file_.erase(offset, length); }
    bool program(std::size_t offset, const void* bytes, std::size_t length) override { return file_.program(offset, bytes, length); }
    void report() const {
        std::printf("FILE POLICY: physical size=%zu B, logical cache=%zu B, writes=%zu B, fsync=%zu, descriptor opens=%zu.\n",
                    assets::kFileStorageBytes, assets::kStorageBytes, io_.written, io_.syncs, io_.opens);
        std::printf("HOST ABI file policy=%zu B; fixed file I/O scratch=%zu B; filesystem buffers excluded.\n",
                    sizeof(assets::FileAssetStorage), assets::kFileIoBytes);
    }
private:
    PosixFileIo io_;
    assets::FileAssetStorage file_;
};
#endif

// Reads real checked-in bytes as if a transport supplied bounded Range replies.
// No HTTP server, signed catalog verification or TLS is exercised by this demo.
class FileTransport {
public:
    explicit FileTransport(const char* path) : file_(std::fopen(path, "rb")) {
        require(file_ != nullptr, "open actual asset fixture");
    }
    ~FileTransport() { std::fclose(file_); }
    void range(const net::Controller& network, std::uint32_t offset, void* output, std::size_t length) {
        require(network.status().serviceReachable, "fake transport requires reachable service");
        require(length && length <= assets::kChunkBytes, "bounded transport range");
        require(std::fseek(file_, static_cast<long>(offset), SEEK_SET) == 0, "seek asset range");
        require(std::fread(output, 1, length, file_) == length, "read actual asset range");
        downloaded += length; ++requests;
    }
    std::size_t downloaded = 0, requests = 0;
private:
    std::FILE* file_;
};

std::uint32_t number(const char* text) {
    std::uint32_t result = 0;
    const auto end = text + std::strlen(text);
    const auto parsed = std::from_chars(text, end, result);
    require(parsed.ec == std::errc{} && parsed.ptr == end, "fixture unsigned integer");
    return result;
}
unsigned hex(char value) {
    if (value >= '0' && value <= '9') return static_cast<unsigned>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<unsigned>(value - 'a') + 10;
    require(false, "fixture SHA-256 hex"); return 0;
}
assets::Spec fixture(char** args) {
    assets::Spec spec{};
    require(std::strlen(args[2]) < sizeof(spec.id), "fixture identifier bound");
    std::strcpy(spec.id, args[2]); spec.version = number(args[3]); spec.bytes = number(args[4]);
    require(std::strlen(args[5]) == 64, "fixture SHA-256 length");
    for (unsigned i = 0; i < 32; ++i) spec.sha256[i] = static_cast<std::uint8_t>((hex(args[5][i * 2]) << 4) | hex(args[5][i * 2 + 1]));
    const auto width = number(args[6]), height = number(args[7]);
    require(width == 32 && height == 32, "fixture is native original32px sprite");
    spec.width = static_cast<std::uint16_t>(width); spec.height = static_cast<std::uint16_t>(height);
    spec.kind = assets::Kind::Sprite;
    require(assets::validSpec(spec), "valid asset spec"); return spec;
}

void checkpointAction(digivice::State& state, storage::SaveStore& saves,
                      digivice::Action action, std::uint32_t value = 0) {
    auto candidate = state;
    const auto error = digivice::apply(candidate, action, value);
    if (error != digivice::Error::None) {
        std::fprintf(stderr, "FAIL: game action: %s\n", digivice::errorText(error)); std::exit(1);
    }
    require(saves.checkpoint(candidate), "checkpoint before accepting game action");
    state = candidate;
}

std::uint32_t join(net::Controller& network, std::uint64_t now, bool reachable) {
    const auto command = network.tick(now);
    require(command.type == net::CommandType::Connect, "controller requests hotspot join");
    network.gotIp(command.token, now + 1);
    const auto probe = network.tick(now + 1);
    require(probe.type == net::CommandType::ProbeService, "controller probes configured service");
    network.serviceResult(probe.token, reachable, now + 2);
    require(network.status().hasIp && network.status().serviceReachable == reachable, "network result applied");
    return command.token;
}

// Tiny host-only illustration of first resident DVA frame consumption; the
// generated trusted header uses palette at32 and frame data at112. No scene or
// roster is decoded. This is not an ESP LCD driver or general DVA file parser.
void showFallback() {
    const auto* sprite = digivice::runtime::fallback::mote;
    std::uint16_t pixels[16 * 16]{};
    unsigned opaque = 0;
    for (unsigned i = 0; i < 16 * 16; ++i) {
        const auto packed = sprite[112 + i / 2];
        const auto index = (i % 2) ? (packed & 15) : (packed >> 4);
        const auto palette = sprite + 32 + index * 2;
        pixels[i] = static_cast<std::uint16_t>(palette[0] | (palette[1] << 8));
        if (index) ++opaque;
    }
    std::uint32_t checksum = 2166136261u;
    for (auto pixel : pixels) { checksum ^= pixel; checksum *= 16777619u; }
    std::printf("BOOT fallback: original Mote 16px idle decoded into %zu B scratch; opaque=%u checksum=%08x.\n",
                sizeof(pixels), opaque, static_cast<unsigned>(checksum));
}
} // namespace

int main(int argc, char** argv) {
#ifdef DIGIVICE_DEMO_FILE_CACHE
    require(argc == 9, "run scripts/run-handheld-sd-demo.sh (owned temporary file supplied by launcher)");
    const char* cachePath = argv[8];
#else
    require(argc == 8, "run scripts/run-handheld-demo.sh (fixture args supplied from index)");
    const char* cachePath = nullptr;
#endif
    const auto spec = fixture(argv);
    FakeNvs nvs;
    std::uint32_t capturedSequence = 0;
    FileTransport transport(argv[1]);
#ifdef DIGIVICE_DEMO_FILE_CACHE
    std::puts("DIGIVICE FILE-CACHE HOST DEMO — deterministic seed 12345, no physical microSD");
    std::puts("REAL: shared C++ game/policies/cache, FileAssetStorage, POSIX pread/pwrite/fsync and checked-in sprite bytes.");
    std::puts("FAKE: 24-bit hardware-count fixture, monotonic clock, save NVS, hotspot/service events, range transport.");
#else
    std::puts("DIGIVICE HANDHELD HOST DEMO — deterministic seed 12345, no physical device");
    std::puts("REAL: shared C++ game/SaveStore/MotionCounter/network policy/asset cache and checked-in sprite bytes.");
    std::puts("FAKE: 24-bit hardware-count fixture, monotonic clock, NVS/NOR, hotspot/service events, range transport.");
#endif
    static DemoStorage cacheStorage(cachePath); // Static host fake NOR, or tiny POSIX policy wrapper.
    std::puts("Trust: local index SHA checked by launcher; this run does not verify a signed catalog or exercise TLS.");
    showFallback();

    {
        auto state = digivice::newGame();
        storage::SaveStore saves(nvs);
        require(saves.restore(state) == storage::BootStatus::Empty, "empty fake NVS boot");
        require(saves.checkpoint(state), "initial local checkpoint");
        digivice::motion::MotionCounter motion(17);
        require(motion.observe(10000, 0).newSteps == 0, "initial hardware count anchors without credit");
        for (std::uint32_t second = 1; second <= 50; ++second) {
            const auto update = motion.observe(10000 + 2 * second, second * 1000ULL);
            require(update.status == digivice::motion::Status::Tracking && update.newSteps == 2, "plausible counter fixture accepted");
        }
        const auto batch = motion.peekBatch(100);
        require(batch.steps == 100, "bounded100-step motion batch");
        checkpointAction(state, saves, digivice::Action::Walk, batch.steps);
        require(motion.acknowledgeBatch(batch) == digivice::motion::AckStatus::Applied, "ack after checkpoint");
        require(motion.acknowledgeBatch(batch) == digivice::motion::AckStatus::AlreadyApplied, "same-session duplicate ack harmless");
        require(state.phase == digivice::Phase::Encounter && state.steps == 100, "real local encounter begins");
        std::printf("WALK: 50 samples × 2 steps; batch(session=%u, id=%u, steps=%u) checkpointed then acknowledged.\n",
                    batch.sessionId, batch.id, static_cast<unsigned>(batch.steps));
        std::printf("GAME: sequence=%u steps=%u encounter=%s HP=%u/%u; no network required.\n",
                    state.sequence, state.steps, digivice::wildName(state), state.wildHp, state.wildMaxHp);

        net::Controller network;
        network.configure(true, 0, 42);
        auto generation = join(network, 0, false);
        std::printf("NETWORK: hotspot IP acquired, service probe fails -> %s (local game continues).\n", net::stateName(network.status().state));
        network.disconnected(generation, 3);
        const auto retryAt = network.status().deadlineMs;
        generation = join(network, retryAt, true);
        std::printf("NETWORK: bounded backoff -> rejoin at %llu ms -> %s.\n",
                    static_cast<unsigned long long>(retryAt), net::stateName(network.status().state));

        assets::Cache cache(cacheStorage);
        cacheOk(cache.boot(), "cache initial boot");
        require(!cache.contains(spec), "sprite initially unavailable, resident fallback usable");
        std::uint32_t token = 0;
        cacheOk(cache.begin(spec, token), "begin actual sprite");
        std::uint8_t chunk[assets::kChunkBytes];
        transport.range(network, 0, chunk, sizeof(chunk));
        cacheOk(cache.append(token, 0, chunk, sizeof(chunk)), "checkpoint first range");
        require(cache.transfer().received == sizeof(chunk) && !cache.contains(spec), "partial bytes not activated");
        network.disconnected(generation, retryAt + 3);
        std::printf("DOWNLOAD: %s received 4096/%u B; fake link disconnects, incomplete asset stays inactive.\n", spec.id, spec.bytes);
        std::puts("REBOOT: destroy runtime objects; retain saved bytes. No old sensor delta is replayed.");
    }
    cacheStorage.reopen();

    {
        digivice::State state{};
        storage::SaveStore saves(nvs);
        require(saves.restore(state) == storage::BootStatus::Loaded && state.sequence == 1 && state.steps == 100, "game checkpoint restored");
        digivice::motion::MotionCounter motion(18);
        require(motion.observe(10100, 0).newSteps == 0 && motion.peekBatch().steps == 0, "fresh boot anchors count without duplicate steps");
        assets::Cache cache(cacheStorage);
        cacheOk(cache.boot(), "cache reboot");
        require(cache.transfer().received == assets::kChunkBytes && !cache.contains(spec), "actual journal recovers first range");
        const char* protectedIds[]{"sprite-mote-v1", spec.id};
        require(cache.protect(protectedIds, 2), "protect selected partner and current wild");
        std::uint32_t token = 0;
        cacheOk(cache.begin(spec, token), "resume same verified spec");
        const auto resumeAt = cache.transfer().received;
        net::Controller network;
        network.configure(true, 0, 42);
        join(network, 0, true);
        std::uint8_t chunk[assets::kChunkBytes];
        while (cache.transfer().received < spec.bytes) {
            const auto offset = cache.transfer().received;
            const auto length = std::min(sizeof(chunk), static_cast<std::size_t>(spec.bytes - offset));
            transport.range(network, offset, chunk, length);
            cacheOk(cache.append(token, offset, chunk, length), "append resumed range");
        }
        cacheOk(cache.finish(token), "SHA-256 verify and atomically activate");
        require(cache.contains(spec), "complete sprite activated");
        std::uint8_t magic[4];
        cacheOk(cache.read(spec, 0, magic, sizeof(magic)), "read activated sprite");
        require(std::memcmp(magic, "DVA1", sizeof(magic)) == 0, "actual DVA sprite bytes");
        require(transport.downloaded == spec.bytes, "resume does not redownload first chunk");
        std::printf("RESUME: offset=%u, %zu bounded range requests total, %zu B downloaded; SHA-256 PASS, DVA1 active.\n",
                    resumeAt, transport.requests, transport.downloaded);

        network.pause(true, 10);
        require(network.tick(10).type == net::CommandType::Disconnect && !network.status().serviceReachable, "radio pause disconnect command");
        const auto requestsBeforeBattle = transport.requests;
        std::puts("OFFLINE: radio paused; original resident partner + cached wild available; no background decoded.");
        checkpointAction(state, saves, digivice::Action::Card, 1);
        std::puts("CARD: simulated NFC card 1 -> next attack boosted by shared rules.");
        for (unsigned attack = 1; attack <= 8 && state.phase == digivice::Phase::Encounter && state.wildHp > state.wildMaxHp / 2; ++attack) {
            checkpointAction(state, saves, digivice::Action::Attack);
            std::printf("ATTACK %u: partner HP=%u wild HP=%u/%u; local checkpoint sequence=%u.\n",
                        attack, state.hp, state.wildHp, state.wildMaxHp, state.sequence);
        }
        require(digivice::captureChance(state)>0, "native capture eligibility after bounded attacks");
        checkpointAction(state, saves, digivice::Action::Capture);
        capturedSequence=state.sequence;
        require(state.phase == digivice::Phase::Home && state.captures == 1 && state.collectionCount == 2, "real offline capture succeeded");
        require(transport.requests == requestsBeforeBattle, "battle made no fake network requests");
        std::printf("CAPTURE: %s; members=%u steps=%u sequence=%u, new network requests=0.\n",
                    digivice::memberName(state.collection[1]), state.collectionCount, state.steps, state.sequence);
    }
    cacheStorage.reopen();

    digivice::State restored{};
    storage::SaveStore restoredSaves(nvs);
    require(restoredSaves.restore(restored) == storage::BootStatus::Loaded && restored.sequence == capturedSequence && restored.captures == 1, "final capture durable in fake NVS");
    assets::Cache restoredCache(cacheStorage);
    cacheOk(restoredCache.boot(), "final activated cache reboot");
    require(restoredCache.contains(spec), "verified completed sprite survives fake reboot");
    std::printf("RESTORE: sequence=%u collection=%u checkpoints=%u; cached sprite=%u B retained.\n",
                restored.sequence, restored.collectionCount, nvs.writes, spec.bytes);
    std::printf("HOST ABI sizeof: State=%zu SaveStore=%zu MotionCounter=%zu Network=%zu Cache=%zu bytes.\n",
                sizeof(digivice::State), sizeof(storage::SaveStore), sizeof(digivice::motion::MotionCounter), sizeof(net::Controller), sizeof(assets::Cache));
    std::printf("BOUNDS: compiled fallback=%zu B, transfer chunk=%zu B, cache encoded capacity=%zu B, cache with metadata=%zu B.\n",
                digivice::runtime::fallback::resident_bytes, assets::kChunkBytes,
                assets::kMaximumBlobBytes * assets::kSlotCount, assets::kStorageBytes);
    cacheStorage.report();
    std::puts("HEAP: portable core/policies/cache make no explicit dynamic allocations; libc/file buffering excluded.");
    std::puts("LIMITS: object sizes use host ABI; real free heap/DMA/display/JPEG/TLS/step accuracy/radio/power unmeasured.");
    std::puts("LIMITS: motion checkpoint + ack is not cross-reset atomic; this clean checkpoint fixture claims no power-loss exactly-once guarantee.");
#ifdef DIGIVICE_DEMO_FILE_CACHE
    std::puts("LIMITS: POSIX close/reopen and fsync are not FAT/card removal or physical brownout guarantees; no physical card accessed.");
#endif
    std::puts("GPS deferred. No phone app, location sharing, BLE battle or physical NFC implemented by this demo.");
    std::puts("PASS: real-core local walk, fake hotspot recovery, real cache resume/integrity, offline capture and restore.");
}
