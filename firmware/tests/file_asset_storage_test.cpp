#include "file_asset_storage.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {
using namespace digivice::assets;
unsigned checks = 0, failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (false)

enum class Op { None, Size, Read, Write, Sync };
struct Event { Op op; std::size_t offset, length; };
// Host model only: sync copies volatile writes into the reboot image. Faults
// may write a prefix, or persist a whole operation before reporting failure.
class File final : public FileIo {
public:
    std::vector<std::uint8_t> bytes, durable;
    std::vector<Event> events;
    std::size_t capacity = kFileStorageBytes, maximumIo = 0;
    bool mounted = true, readOnly = false, uncertainSyncPersists = false;
    void fault(Op op, std::size_t skip = 0, std::size_t prefix = 0) {
        fault_ = op; skip_ = skip; prefix_ = prefix;
    }
    void reboot() { bytes = durable; fault_ = Op::None; mounted = true; }
    void resetTrace() { events.clear(); }
    std::size_t count(Op op) const {
        return std::count_if(events.begin(), events.end(), [op](const Event& e) { return e.op == op; });
    }
    bool size(std::size_t& length) override {
        events.push_back({Op::Size, 0, 0});
        if (!mounted || hit(Op::Size)) return false;
        length = bytes.size(); return true;
    }
    bool read(std::size_t offset, void* out, std::size_t length) override {
        events.push_back({Op::Read, offset, length});
        maximumIo = std::max(maximumIo, length);
        if (!mounted || offset > bytes.size() || length > bytes.size() - offset) return false;
        const bool failed = hit(Op::Read);
        if (length) std::memcpy(out, bytes.data() + offset, failed ? std::min(prefix_, length) : length);
        return !failed;
    }
    bool write(std::size_t offset, const void* source, std::size_t length) override {
        events.push_back({Op::Write, offset, length});
        maximumIo = std::max(maximumIo, length);
        if (!mounted || readOnly || offset > capacity) return false;
        const bool failed = hit(Op::Write);
        const auto n = std::min({length, capacity - offset, failed ? prefix_ : length});
        if (offset + n > bytes.size()) bytes.resize(offset + n, 0);
        if (n) std::memcpy(bytes.data() + offset, source, n);
        return !failed && n == length;
    }
    bool sync() override {
        events.push_back({Op::Sync, 0, 0});
        if (!mounted || readOnly) return false;
        const bool failed = hit(Op::Sync);
        if (!failed || uncertainSyncPersists) durable = bytes;
        return !failed;
    }
private:
    Op fault_ = Op::None;
    std::size_t skip_ = 0, prefix_ = 0;
    bool hit(Op op) {
        if (fault_ != op) return false;
        if (skip_) { --skip_; return false; }
        fault_ = Op::None; return true;
    }
};

bool allErased(const std::vector<std::uint8_t>& bytes, std::size_t offset = 0) {
    return std::all_of(bytes.begin() + offset, bytes.end(), [](std::uint8_t b) { return b == 255; });
}
Spec spec(std::size_t bytes, const char* hash) {
    Spec value{}; std::strcpy(value.id, "sprite-file-test-v1");
    value.version = 1; value.bytes = std::uint32_t(bytes); value.width = value.height = 32;
    for (unsigned i = 0; i < 32; ++i) {
        unsigned n = 0; CHECK(std::sscanf(hash + i * 2, "%2x", &n) == 1);
        value.sha256[i] = std::uint8_t(n);
    }
    return value;
}
Spec abcSpec() { return spec(3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"); }
Spec longSpec() { return spec(8199, "3c5ccc8289a8a12e4b207f74966aee00a72e983c44ecf5802a37d65e5734e32e"); }
std::vector<std::uint8_t> pattern() {
    std::vector<std::uint8_t> bytes(8199);
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = std::uint8_t(i * 37 + 11);
    return bytes;
}

void creationAndOwnership() {
    File file; FileAssetStorage storage(file);
    CHECK(!storage.ready()); CHECK(kFileStorageBytes == 696832);
    CHECK(storage.begin(true)); CHECK(storage.ready());
    CHECK(file.bytes.size() == kFileStorageBytes && file.durable == file.bytes);
    CHECK(allErased(file.bytes, kFileHeaderBytes));
    CHECK(std::memcmp(file.bytes.data(), "DVASSET1", 8) == 0);
    CHECK(file.maximumIo <= 512 && file.count(Op::Sync) == 2);
    auto firstSync = std::find_if(file.events.begin(), file.events.end(), [](Event e) { return e.op == Op::Sync; });
    auto lastWrite = std::find_if(file.events.rbegin(), file.events.rend(), [](Event e) { return e.op == Op::Write; });
    CHECK(firstSync != file.events.end() && lastWrite != file.events.rend());
    CHECK(lastWrite->offset == 0 && lastWrite->length == kFileHeaderBytes);
    CHECK(std::size_t(firstSync - file.events.begin()) < file.events.size() - 1 - std::size_t(lastWrite - file.events.rbegin()));
    const auto saved = file.bytes;
    file.resetTrace(); CHECK(storage.begin(false));
    CHECK(file.count(Op::Write) == 0 && file.count(Op::Sync) == 0);
    CHECK(!storage.begin(true)); CHECK(file.bytes == saved);
    CHECK(!storage.ready()); CHECK(storage.begin(false));
    std::uint8_t read[1025]{}; CHECK(storage.read(0, read, sizeof(read)));
    CHECK(std::all_of(std::begin(read), std::end(read), [](std::uint8_t b) { return b == 255; }));
    storage.end(); const auto calls = file.events.size();
    CHECK(!storage.read(0, read, 1)); CHECK(file.events.size() == calls);

    for (const auto size : {std::size_t(0), std::size_t(1), kFileStorageBytes - 1, kFileStorageBytes + 1}) {
        File wrong; wrong.bytes.resize(size, 0x42); const auto before = wrong.bytes;
        FileAssetStorage rejected(wrong); CHECK(!rejected.begin(false));
        CHECK(wrong.bytes == before && wrong.count(Op::Write) == 0);
    }
    for (const auto changed : {std::size_t(0), std::size_t(8), std::size_t(16), std::size_t(511)}) {
        File wrong; wrong.bytes = saved; wrong.bytes[changed] ^= 1; const auto before = wrong.bytes;
        FileAssetStorage rejected(wrong); CHECK(!rejected.begin(false));
        CHECK(wrong.bytes == before && wrong.count(Op::Write) == 0);
    }
    File foreign; foreign.bytes.resize(kFileStorageBytes, 0);
    FileAssetStorage rejected(foreign); CHECK(!rejected.begin(false)); CHECK(foreign.count(Op::Write) == 0);
}

void interruptedCreation() {
    for (const auto capacity : {std::size_t(0), std::size_t(8197)}) {
        File file; file.capacity = capacity; FileAssetStorage storage(file);
        CHECK(!storage.begin(true)); CHECK(!storage.ready()); CHECK(file.bytes.size() == capacity);
        const auto before = file.bytes; file.resetTrace(); file.capacity = kFileStorageBytes;
        CHECK(!storage.begin(false)); CHECK(file.bytes == before && file.count(Op::Write) == 0);
    }
    // Interruption during FF fill, first sync, ownership write, or final sync.
    for (unsigned failure = 0; failure < 4; ++failure) {
        File file; FileAssetStorage storage(file);
        if (failure == 0) file.fault(Op::Write, 12, 97);
        if (failure == 1) file.fault(Op::Sync);
        if (failure == 2) file.fault(Op::Write, kFileStorageBytes / kFileIoBytes, 17);
        if (failure == 3) { file.fault(Op::Sync, 1); file.uncertainSyncPersists = true; }
        CHECK(!storage.begin(true)); CHECK(!storage.ready());
        const auto calls = file.events.size(); std::uint8_t byte = 0;
        CHECK(!storage.program(0, &byte, 1)); CHECK(file.events.size() == calls);
        const auto before = file.bytes; file.resetTrace();
        CHECK(storage.begin(false) == (failure == 3));
        CHECK(file.bytes == before && file.count(Op::Write) == 0);
        if (failure == 3) { file.reboot(); CHECK(storage.begin(false)); }
    }
    File absent; absent.mounted = false; FileAssetStorage missing(absent);
    CHECK(!missing.begin(true)); CHECK(absent.bytes.empty());
    File ro; ro.readOnly = true; FileAssetStorage readonly(ro);
    CHECK(!readonly.begin(true)); CHECK(ro.bytes.empty());
}

void boundsAndNor() {
    File file; FileAssetStorage storage(file); CHECK(storage.begin(true));
    const std::vector<std::uint8_t> header(file.bytes.begin(), file.bytes.begin() + kFileHeaderBytes);
    std::vector<std::uint8_t> bytes(1024, 0xf0), read(1024);
    CHECK(storage.program(0, bytes.data(), bytes.size()));
    CHECK(storage.read(0, read.data(), read.size()) && read == bytes);
    file.resetTrace(); bytes[512] = 255;
    const auto before = file.bytes;
    CHECK(!storage.program(0, bytes.data(), bytes.size()));
    CHECK(storage.ready()); CHECK(file.bytes == before && file.count(Op::Write) == 0);
    bytes.assign(1024, 0x80); CHECK(storage.program(0, bytes.data(), bytes.size()));
    CHECK(storage.erase(0, kChunkBytes)); CHECK(allErased(file.bytes, kFileHeaderBytes));
    CHECK(std::equal(header.begin(), header.end(), file.bytes.begin()));
    file.resetTrace();
    CHECK(!storage.erase(1, kChunkBytes)); CHECK(!storage.erase(0, 1)); CHECK(!storage.erase(0, 0));
    CHECK(!storage.erase(kStorageBytes, kChunkBytes));
    CHECK(!storage.read(kStorageBytes, read.data(), 1));
    CHECK(!storage.program(std::numeric_limits<std::size_t>::max(), bytes.data(), 1));
    CHECK(!storage.program(0, bytes.data(), std::numeric_limits<std::size_t>::max()));
    CHECK(!storage.read(0, nullptr, 1)); CHECK(!storage.program(0, nullptr, 1));
    CHECK(storage.read(kStorageBytes, nullptr, 0)); CHECK(storage.program(kStorageBytes, nullptr, 0));
    CHECK(file.count(Op::Write) == 0 && file.count(Op::Sync) == 0 && storage.ready());
    CHECK(file.maximumIo <= kFileIoBytes);
}

void runtimeFailuresLatch() {
    for (const auto failure : {Op::Read, Op::Write, Op::Sync, Op::Size}) {
        File file; FileAssetStorage storage(file); CHECK(storage.begin(true));
        std::uint8_t bytes[1024]; std::memset(bytes, 0x22, sizeof(bytes));
        file.fault(failure, failure == Op::Write ? 1 : 0, 3);
        CHECK(!storage.program(0, bytes, sizeof(bytes))); CHECK(!storage.ready());
        const auto calls = file.events.size();
        CHECK(!storage.erase(0, kChunkBytes)); CHECK(!storage.read(0, bytes, 1));
        CHECK(!storage.program(0, bytes, 1)); CHECK(file.events.size() == calls);
        file.reboot(); CHECK(storage.begin(false));
        CHECK(allErased(file.bytes, kFileHeaderBytes));
    }
    for (unsigned failure = 0; failure < 3; ++failure) {
        File file; FileAssetStorage storage(file); CHECK(storage.begin(true));
        if (failure == 0) file.mounted = false;
        if (failure == 1) file.bytes.resize(kFileStorageBytes - 1);
        if (failure == 2) file.readOnly = true;
        const auto before = file.bytes; const std::uint8_t byte = 0;
        CHECK(!storage.program(0, &byte, 1)); CHECK(!storage.ready()); CHECK(file.bytes == before);
    }
}

void journalOrderingAndResume() {
    File file; FileAssetStorage storage(file); CHECK(storage.begin(true));
    Cache cache(storage); CHECK(cache.boot() == Result::Ok);
    const auto value = longSpec(); const auto bytes = pattern();
    std::uint32_t token = 0; CHECK(cache.begin(value, token) == Result::Ok);
    CHECK(cache.append(token, 0, bytes.data(), kChunkBytes) == Result::Ok);
    // Second chunk: erase is durable, but only part of its payload write lands.
    file.fault(Op::Write, kChunkBytes / kFileIoBytes + 1, 7);
    CHECK(cache.append(token, kChunkBytes, bytes.data() + kChunkBytes, kChunkBytes) == Result::Io);
    CHECK(!storage.ready()); CHECK(cache.recoveryRequired());
    file.reboot(); CHECK(storage.begin(false)); Cache resumed(storage);
    CHECK(resumed.boot() == Result::Ok); CHECK(resumed.transfer().received == kChunkBytes);
    CHECK(!resumed.contains(value)); CHECK(resumed.begin(value, token) == Result::Ok);
    for (auto offset = resumed.transfer().received; offset < value.bytes;) {
        const auto n = std::min(std::size_t(kChunkBytes), std::size_t(value.bytes - offset));
        CHECK(resumed.append(token, offset, bytes.data() + offset, n) == Result::Ok);
        offset += std::uint32_t(n);
    }
    CHECK(resumed.finish(token) == Result::Ok); CHECK(resumed.contains(value));
    file.reboot(); CHECK(storage.begin(false)); Cache complete(storage);
    CHECK(complete.boot() == Result::Ok && complete.contains(value));
    std::vector<std::uint8_t> loaded(bytes.size());
    CHECK(complete.read(value, 0, loaded.data(), loaded.size()) == Result::Ok && loaded == bytes);

    // Independent trace asserts data sync precedes journal writes, then journal
    // sync precedes activation. Host ordering is not physical card power proof.
    File small; FileAssetStorage smallStorage(small); CHECK(smallStorage.begin(true));
    Cache smallCache(smallStorage); CHECK(smallCache.boot() == Result::Ok);
    const auto abc = abcSpec(); CHECK(smallCache.begin(abc, token) == Result::Ok);
    small.resetTrace();
    const std::uint8_t content[]{'a','b','c'};
    CHECK(smallCache.append(token, 0, content, 3) == Result::Ok);
    CHECK(smallCache.finish(token) == Result::Ok);
    std::vector<Event> mutations;
    for (const auto& event : small.events) if (event.op == Op::Write || event.op == Op::Sync) mutations.push_back(event);
    CHECK(mutations.size() == 15); // 8 erase writes + sync + data/sync + journal/sync + commit/sync.
    if (mutations.size() == 15) {
        CHECK(mutations[8].op == Op::Sync);
        CHECK(mutations[9].offset == kFileHeaderBytes + 2 * kChunkBytes && mutations[9].length == 3);
        CHECK(mutations[10].op == Op::Sync);
        CHECK(mutations[11].offset == kFileHeaderBytes + kChunkBytes && mutations[11].length == 16);
        CHECK(mutations[12].op == Op::Sync);
        CHECK(mutations[13].offset == kFileHeaderBytes + 124 && mutations[13].length == 4);
        CHECK(mutations[14].op == Op::Sync);
    }
    // A completed commit whose sync reports failure can be discovered only by
    // explicit reopen and Cache::boot; never activate optimistically in memory.
    File uncertain; FileAssetStorage uncertainStorage(uncertain); CHECK(uncertainStorage.begin(true));
    Cache uncertainCache(uncertainStorage); CHECK(uncertainCache.boot() == Result::Ok);
    CHECK(uncertainCache.begin(abc, token) == Result::Ok);
    CHECK(uncertainCache.append(token, 0, content, 3) == Result::Ok);
    uncertain.fault(Op::Sync); uncertain.uncertainSyncPersists = true;
    CHECK(uncertainCache.finish(token) == Result::Io && !uncertainCache.contains(abc));
    uncertain.reboot(); CHECK(uncertainStorage.begin(false)); Cache recovered(uncertainStorage);
    CHECK(recovered.boot() == Result::Ok && recovered.contains(abc));
}
}

int main() {
    creationAndOwnership(); interruptedCreation(); boundsAndNor();
    runtimeFailuresLatch(); journalOrderingAndResume();
    std::printf("FileAssetStorage: %u checks, %u failures; state=%zu bytes, logical=%zu, physical=%zu. Host file/sync model only.\n",
                checks, failures, sizeof(digivice::assets::FileAssetStorage),
                digivice::assets::kStorageBytes, digivice::assets::kFileStorageBytes);
    return failures ? 1 : 0;
}
