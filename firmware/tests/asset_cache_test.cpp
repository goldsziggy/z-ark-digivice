#include "asset_cache.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

namespace {
using namespace digivice::assets;
unsigned checks = 0, failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (false)

enum class Op { None, Read, Erase, Program };
// Host-only fake. Real NOR constraints are enforced; a failed operation may
// change a prefix, including the complete operation with an uncertain reply.
class Nor final : public Storage {
public:
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(kStorageBytes, 255);
    std::size_t writes = 0, violations = 0, maximumRead = 0;
    void fail(Op op, std::size_t skip = 0, std::size_t prefix = 0) {
        fault_ = op; skip_ = skip; prefix_ = prefix;
    }
    void clearFault() { fault_ = Op::None; }
    bool read(std::size_t offset, void* out, std::size_t length) override {
        if (!range(offset, length) || (!out && length)) return bad();
        maximumRead = std::max(maximumRead, length);
        const bool failed = trigger(Op::Read);
        std::memcpy(out, bytes.data() + offset, failed ? std::min(prefix_, length) : length);
        return !failed;
    }
    bool erase(std::size_t offset, std::size_t length) override {
        if (!range(offset, length) || offset % kChunkBytes || length % kChunkBytes) return bad();
        ++writes;
        const bool failed = trigger(Op::Erase);
        std::fill_n(bytes.begin() + offset, failed ? std::min(prefix_, length) : length, 255);
        return !failed;
    }
    bool program(std::size_t offset, const void* source, std::size_t length) override {
        if (!range(offset, length) || (!source && length)) return bad();
        const auto* input = static_cast<const std::uint8_t*>(source);
        const bool failed = trigger(Op::Program);
        const auto count = failed ? std::min(prefix_, length) : length;
        for (std::size_t i = 0; i < count; ++i)
            if ((bytes[offset + i] & input[i]) != input[i]) return bad();
        ++writes;
        for (std::size_t i = 0; i < count; ++i) bytes[offset + i] &= input[i];
        return !failed;
    }
    ~Nor() override { CHECK(violations == 0); }
private:
    Op fault_ = Op::None;
    std::size_t skip_ = 0, prefix_ = 0;
    bool range(std::size_t offset, std::size_t length) const {
        return offset <= bytes.size() && length <= bytes.size() - offset;
    }
    bool bad() { ++violations; return false; }
    bool trigger(Op op) {
        if (fault_ != op) return false;
        if (skip_) { --skip_; return false; }
        fault_ = Op::None; return true;
    }
};

constexpr const char* abcHash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
constexpr const char* binaryHash = "3c5ccc8289a8a12e4b207f74966aee00a72e983c44ecf5802a37d65e5734e32e";
std::vector<std::uint8_t> binary(std::size_t count) {
    std::vector<std::uint8_t> value(count);
    for (std::size_t i = 0; i < count; ++i) value[i] = std::uint8_t(i * 37 + 11);
    return value;
}
Spec spec(const char* id, std::size_t size, const char* hash, std::uint32_t version = 1) {
    Spec value{};
    std::snprintf(value.id, sizeof(value.id), "%s", id);
    value.bytes = static_cast<std::uint32_t>(size); value.version = version;
    value.width = value.height = 32;
    for (unsigned i = 0; i < 32; ++i) {
        unsigned byte = 0;
        CHECK(std::sscanf(hash + i * 2, "%2x", &byte) == 1);
        value.sha256[i] = static_cast<std::uint8_t>(byte);
    }
    return value;
}
bool install(Cache& cache, const Spec& value, const std::vector<std::uint8_t>& bytes) {
    std::uint32_t token = 0;
    const auto result = cache.begin(value, token);
    CHECK(result == Result::Ok);
    if (result != Result::Ok) return false;
    if (!token) return cache.contains(value);
    for (auto offset = cache.transfer().received; offset < bytes.size();) {
        const auto count = std::min(kChunkBytes, bytes.size() - offset);
        const auto appended = cache.append(token, offset, bytes.data() + offset, count);
        CHECK(appended == Result::Ok);
        if (appended != Result::Ok) return false;
        offset += static_cast<std::uint32_t>(count);
    }
    const auto finished = cache.finish(token);
    CHECK(finished == Result::Ok);
    return finished == Result::Ok;
}
std::size_t slotOf(const Cache& cache, const Spec& value) {
    for (std::size_t slot = 0; slot < kSlotCount; ++slot)
        if (cache.record(slot).present && sameSpec(cache.record(slot).spec, value)) return slot;
    CHECK(false); return 0;
}
void verifyBytes(Cache& cache, const Spec& value, const std::vector<std::uint8_t>& expected) {
    std::vector<std::uint8_t> read(expected.size());
    CHECK(cache.contains(value));
    CHECK(cache.read(value, 0, read.data(), read.size()) == Result::Ok);
    CHECK(read == expected);
}

void knownDigestsAndBounds() {
    // Independent hashlib/FIPS vectors exercise both SHA padding branches,
    // exact SHA blocks, binary zeros/high bytes and the maximum flash blob.
    struct Vector { std::size_t length; const char* sha; };
    const Vector vectors[]{
        {3, abcHash},
        {55, "2900465fcb533e05a158fd2b3be0e5e3b03740d83060aa3580e0d98a96bf2384"},
        {56, "31454ff48ef36af2f08fd511bdc37d9d5855ac23e992e5ff5445cb6b7674a674"},
        {64, "94eb5de4943613fd048dc93393ab06877405faa39c11f53e9386083339833e7e"},
        {8199, binaryHash},
        {131072, "2809cb8a646aa7d5821261d1b4d00faf3e9da95d268074682785562157ce9b9e"},
    };
    for (const auto& vector : vectors) {
        Nor nor; Cache cache(nor); CHECK(cache.boot() == Result::Ok);
        const auto bytes = vector.length == 3 ? std::vector<std::uint8_t>{'a','b','c'} : binary(vector.length);
        const auto value = spec("sprite-test-v1", bytes.size(), vector.sha);
        CHECK(install(cache, value, bytes));
        CHECK(nor.maximumRead <= kChunkBytes); // Hashing/boot never loads a whole blob.
        Cache rebooted(nor); CHECK(rebooted.boot() == Result::Ok);
        verifyBytes(rebooted, value, bytes);
        std::uint8_t byte = 0;
        CHECK(rebooted.read(value, value.bytes, &byte, 1) == Result::Invalid);
        CHECK(rebooted.read(value, std::numeric_limits<std::size_t>::max(), &byte, 1) == Result::Invalid);
        const auto before = nor.writes;
        std::uint32_t token = 99;
        CHECK(rebooted.begin(value, token) == Result::Ok && token == 0);
        CHECK(nor.writes == before);
        for (unsigned invalid = 0; invalid < 7; ++invalid) {
            auto bad = value;
            switch (invalid) {
            case 0: bad.version = 0; break;
            case 1: bad.bytes = 0; break;
            case 2: bad.bytes = kMaximumBlobBytes + 1; break;
            case 3: bad.width = 480; break;
            case 4: std::memset(bad.id, 'x', sizeof(bad.id)); break;
            case 5: bad.id[0] = '/'; break;
            default: bad.id[47] = 'x'; break;
            }
            CHECK(rebooted.begin(bad, token) == Result::Invalid);
            CHECK(nor.writes == before);
        }
    }
}

void resumeAndTokens() {
    const auto bytes = binary(8199);
    const auto value = spec("sprite-resume-v1", bytes.size(), binaryHash);
    Nor nor; Cache cache(nor); CHECK(cache.boot() == Result::Ok);
    std::uint32_t first = 0; CHECK(cache.begin(value, first) == Result::Ok);
    CHECK(cache.append(first, 0, bytes.data(), kChunkBytes) == Result::Ok);
    CHECK(!cache.contains(value));
    Cache rebooted(nor); CHECK(rebooted.boot() == Result::Ok);
    CHECK(rebooted.transfer().received == kChunkBytes && !rebooted.transfer().active);
    std::uint32_t token = 0; CHECK(rebooted.begin(value, token) == Result::Ok);
    CHECK(rebooted.transfer().received == kChunkBytes);
    auto writes = nor.writes;
    CHECK(rebooted.append(token, 0, bytes.data(), kChunkBytes) == Result::Invalid);
    CHECK(rebooted.append(token, kChunkBytes, bytes.data() + kChunkBytes, 1) == Result::Invalid);
    CHECK(rebooted.append(token, kChunkBytes, nullptr, kChunkBytes) == Result::Invalid);
    CHECK(rebooted.finish(token) == Result::Invalid);
    CHECK(nor.writes == writes && rebooted.transfer().received == kChunkBytes);
    const auto other = spec("sprite-other-v1", 3, abcHash);
    std::uint32_t replacement = 0; CHECK(rebooted.begin(other, replacement) == Result::Ok);
    CHECK(replacement != token);
    writes = nor.writes;
    CHECK(rebooted.append(token, kChunkBytes, bytes.data() + kChunkBytes, kChunkBytes) == Result::WrongTransfer);
    CHECK(rebooted.finish(token) == Result::WrongTransfer && nor.writes == writes);
    CHECK(install(rebooted, other, {'a','b','c'}));
    CHECK(install(rebooted, value, bytes));
    verifyBytes(rebooted, value, bytes);
}

void tornWrites() {
    const auto old = spec("sprite-stable-v1", 3, abcHash);
    const auto bytes = binary(8199), abc = std::vector<std::uint8_t>{'a','b','c'};
    const auto candidate = spec("sprite-candidate-v1", bytes.size(), binaryHash);
    // Power interruption in header, blob, journal, and final commit. Also model
    // an operation whose complete bytes landed before an error was returned.
    enum Fault { HeaderMagic, HeaderBody, Blob, Journal, JournalUncertain, CommitPartial, CommitUncertain, Erase };
    for (const auto fault : {HeaderMagic, HeaderBody, Blob, Journal, JournalUncertain, CommitPartial, CommitUncertain, Erase}) {
        Nor nor; Cache cache(nor); CHECK(cache.boot() == Result::Ok); CHECK(install(cache, old, abc));
        std::uint32_t token = 0;
        if (fault == HeaderMagic || fault == HeaderBody || fault == Erase) {
            nor.fail(fault == Erase ? Op::Erase : Op::Program, 0, fault == HeaderMagic ? 4 : 77);
            CHECK(cache.begin(candidate, token) == Result::Io);
        } else {
            CHECK(cache.begin(candidate, token) == Result::Ok);
            if (fault == Blob || fault == Journal || fault == JournalUncertain) {
                nor.fail(Op::Program, fault == Blob ? 0 : 1, fault == Blob ? 731 : fault == Journal ? 7 : 16);
                CHECK(cache.append(token, 0, bytes.data(), kChunkBytes) == Result::Io);
            } else {
                for (std::uint32_t offset = 0; offset < bytes.size(); offset += kChunkBytes)
                    CHECK(cache.append(token, offset, bytes.data() + offset, std::min(kChunkBytes, bytes.size() - offset)) == Result::Ok);
                nor.fail(Op::Program, 0, fault == CommitPartial ? 2 : 4);
                CHECK(cache.finish(token) == Result::Io);
            }
        }
        CHECK(cache.recoveryRequired());
        CHECK(cache.begin(candidate, token) == Result::RecoveryRequired);
        verifyBytes(cache, old, abc);
        Cache recovered(nor); CHECK(recovered.boot() == Result::Ok);
        verifyBytes(recovered, old, abc);
        if (fault == JournalUncertain) CHECK(recovered.transfer().received == kChunkBytes);
        if (fault == CommitUncertain) CHECK(recovered.contains(candidate));
        else CHECK(!recovered.contains(candidate));
        CHECK(install(recovered, candidate, bytes));
        verifyBytes(recovered, candidate, bytes);
        verifyBytes(recovered, old, abc);
    }
}

void corruptPayloadAndReadFailure() {
    const auto old = spec("sprite-old-v1", 3, abcHash);
    const auto abc = std::vector<std::uint8_t>{'a','b','c'}, bytes = binary(8199);
    const auto candidate = spec("sprite-new-v1", bytes.size(), binaryHash);
    Nor nor; Cache cache(nor); CHECK(cache.boot() == Result::Ok); CHECK(install(cache, old, abc));
    std::uint32_t token = 0; CHECK(cache.begin(candidate, token) == Result::Ok);
    auto corrupted = bytes; corrupted[2] ^= 1;
    for (std::uint32_t offset = 0; offset < bytes.size(); offset += kChunkBytes)
        CHECK(cache.append(token, offset, corrupted.data() + offset, std::min(kChunkBytes, bytes.size() - offset)) == Result::Ok);
    CHECK(cache.finish(token) == Result::Integrity && !cache.contains(candidate));
    verifyBytes(cache, old, abc);
    CHECK(cache.begin(candidate, token) == Result::Ok);
    for (std::uint32_t offset = 0; offset < bytes.size(); offset += kChunkBytes)
        CHECK(cache.append(token, offset, bytes.data() + offset, std::min(kChunkBytes, bytes.size() - offset)) == Result::Ok);
    nor.fail(Op::Read);
    CHECK(cache.finish(token) == Result::Io && cache.recoveryRequired());
    CHECK(cache.boot() == Result::Ok && cache.transfer().received == bytes.size());
    CHECK(install(cache, candidate, bytes));
    const auto candidateSlot = slotOf(cache, candidate);
    nor.bytes[candidateSlot * kSlotBytes + 2 * kChunkBytes + 9] ^= 1;
    Cache corrupt(nor); CHECK(corrupt.boot() == Result::Ok);
    CHECK(!corrupt.contains(candidate)); verifyBytes(corrupt, old, abc);
    CHECK(install(corrupt, candidate, bytes));
    // A transient read failure is not evidence that verified flash may be
    // discarded. It must block mutation until a successful rescan.
    Cache io(nor); nor.fail(Op::Read, 1);
    CHECK(io.boot() == Result::Io);
    CHECK(io.recoveryRequired());
    auto before = nor.writes;
    CHECK(io.begin(candidate, token) == Result::RecoveryRequired && nor.writes == before);
    CHECK(io.boot() == Result::Ok);
    verifyBytes(io, old, abc); verifyBytes(io, candidate, bytes);
    nor.fail(Op::Read);
    std::uint8_t out = 0; CHECK(io.read(old, 0, &out, 1) == Result::Io);
    CHECK(io.read(old, 0, &out, 1) == Result::Ok && out == 'a');
    // A corrupted resume checkpoint is not trusted just because its header is
    // valid. The next attempt starts at zero after clearing staging flash.
    auto pending = spec("sprite-stage-v1", bytes.size(), binaryHash);
    CHECK(io.begin(pending, token) == Result::Ok);
    CHECK(io.append(token, 0, bytes.data(), kChunkBytes) == Result::Ok);
    nor.bytes[slotOf(io, pending) * kSlotBytes + 2 * kChunkBytes] ^= 1;
    Cache resumed(nor); CHECK(resumed.boot() == Result::Ok);
    CHECK(resumed.transfer().received == 0);
    CHECK(install(resumed, pending, bytes));
    verifyBytes(resumed, pending, bytes);
}

void failureAfterDurablePrefix() {
    const auto bytes = binary(8199), abc = std::vector<std::uint8_t>{'a','b','c'};
    const auto old = spec("sprite-prefix-old", 3, abcHash);
    const auto candidate = spec("sprite-prefix-new", bytes.size(), binaryHash);
    for (const auto operation : {Op::Erase, Op::Program}) {
        Nor nor; Cache cache(nor); CHECK(cache.boot() == Result::Ok);
        CHECK(install(cache, old, abc));
        std::uint32_t token = 0; CHECK(cache.begin(candidate, token) == Result::Ok);
        CHECK(cache.append(token, 0, bytes.data(), kChunkBytes) == Result::Ok);
        nor.fail(operation, 0, 731);
        CHECK(cache.append(token, kChunkBytes, bytes.data() + kChunkBytes, kChunkBytes) == Result::Io);
        CHECK(!cache.contains(candidate)); verifyBytes(cache, old, abc);
        Cache rebooted(nor); CHECK(rebooted.boot() == Result::Ok);
        CHECK(rebooted.transfer().received == kChunkBytes);
        CHECK(install(rebooted, candidate, bytes));
        verifyBytes(rebooted, candidate, bytes); verifyBytes(rebooted, old, abc);
    }
}

void protectionAndVersions() {
    const auto abc = std::vector<std::uint8_t>{'a','b','c'};
    Nor nor; Cache cache(nor); CHECK(cache.boot() == Result::Ok);
    Spec values[5];
    for (unsigned i = 0; i < 5; ++i) {
        char id[48]; std::snprintf(id, sizeof(id), "sprite-%u-v1", i);
        values[i] = spec(id, 3, abcHash); CHECK(install(cache, values[i], abc));
    }
    const char* protectedIds[]{values[0].id, values[1].id, values[2].id, values[3].id};
    CHECK(cache.protect(protectedIds, 4));
    CHECK(!cache.protect(protectedIds, 5));
    CHECK(!cache.protect(nullptr, 1));
    const auto sixth = spec("sprite-six-v1", 3, abcHash);
    CHECK(install(cache, sixth, abc));
    CHECK(!cache.contains(values[4]));
    for (unsigned i = 0; i < 4; ++i) verifyBytes(cache, values[i], abc);
    // A protected asset can be upgraded in the spare slot without erasing its
    // last complete version before the replacement hash/commit succeeds.
    auto newer = values[0]; newer.version = 2;
    CHECK(install(cache, newer, abc)); verifyBytes(cache, values[0], abc);
    std::uint32_t token = 99; auto writes = nor.writes;
    auto rollback = newer; rollback.version = 1;
    // Already cached historical bytes remain readable; begin cannot reopen or
    // overwrite them. A changed hash at the same version must be rejected.
    CHECK(cache.begin(rollback, token) == Result::Ok && token == 0 && nor.writes == writes);
    auto conflicting = newer; conflicting.sha256[0] ^= 1;
    CHECK(cache.begin(conflicting, token) == Result::Invalid && nor.writes == writes);
    const auto full = spec("sprite-full-v1", 3, abcHash);
    CHECK(cache.begin(full, token) == Result::Full && nor.writes == writes);
    CHECK(cache.protect(nullptr, 0));
    CHECK(install(cache, full, abc));
    // Once old versions have been evicted, a stale catalog cannot downgrade a
    // still-present newer version of the same ID.
    Cache versions(nor); CHECK(versions.boot() == Result::Ok);
    auto latest = spec("sprite-version-v1", 3, abcHash, 4);
    CHECK(install(versions, latest, abc));
    auto prior = latest; --prior.version; writes = nor.writes;
    CHECK(versions.begin(prior, token) == Result::Invalid && nor.writes == writes);
}

void evictionFailureAndFutureHeader() {
    const auto abc = std::vector<std::uint8_t>{'a','b','c'};
    Nor nor; Cache cache(nor); CHECK(cache.boot() == Result::Ok);
    Spec values[5];
    for (unsigned i = 0; i < 5; ++i) {
        char id[48]; std::snprintf(id, sizeof(id), "sprite-evict-%u", i);
        values[i] = spec(id, 3, abcHash); CHECK(install(cache, values[i], abc));
    }
    // Torn eviction changes old bytes before the replacement header exists.
    // The old in-memory record must stop advertising that slot as verified.
    nor.fail(Op::Erase, 0, 2 * kChunkBytes + 1);
    const auto replacement = spec("sprite-replacement", 3, abcHash);
    std::uint32_t token = 0;
    CHECK(cache.begin(replacement, token) == Result::Io);
    CHECK(!cache.contains(values[0]));
    std::uint8_t byte = 0;
    CHECK(cache.read(values[0], 0, &byte, 1) != Result::Ok);
    for (unsigned i = 1; i < 5; ++i) verifyBytes(cache, values[i], abc);
    Cache rebooted(nor); CHECK(rebooted.boot() == Result::Ok);
    CHECK(install(rebooted, replacement, abc));
    // A fully written, CRC-consistent newer format is distinct from a torn
    // header. Preserve it and require recovery rather than downgrade/erase.
    auto* header = nor.bytes.data() + slotOf(rebooted, replacement) * kSlotBytes;
    header[4] = 2;
    std::uint32_t crc = UINT32_MAX;
    for (unsigned i = 0; i < 120; ++i) {
        crc ^= header[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1)));
    }
    crc = ~crc;
    for (unsigned i = 0; i < 4; ++i) header[120 + i] = std::uint8_t(crc >> (i * 8));
    const auto before = nor.bytes;
    Cache future(nor); CHECK(future.boot() == Result::RecoveryRequired);
    CHECK(future.begin(replacement, token) == Result::RecoveryRequired);
    CHECK(nor.bytes == before);
    verifyBytes(future, values[1], abc);
}

void realGeneratedBlobs() {
    // These original generated fixtures cross the real service/cache boundary.
    // Run from repo root; fixture changes deliberately require fresh evidence.
    struct Fixture { const char* id; const char* ext; std::size_t bytes; const char* sha; Kind kind; };
    const Fixture fixtures[]{
        {"sprite-mote-v1", ".dva", 8816, "592b30ef0b1d13c7a9c7d9504548531b4ea9b770f541ed7725633b47cc3fc59e", Kind::Sprite},
        {"scene-forest-412-v1", ".jpg", 63510, "328725369a829b88e8d0547142c42df3cc1fdbdae2e9ede3c2ee40c717799455", Kind::Background},
    };
    Nor nor; Cache cache(nor); CHECK(cache.boot() == Result::Ok);
    for (const auto& fixture : fixtures) {
        const auto path = std::string("assets/device/") + fixture.id + fixture.ext;
        std::ifstream file(path, std::ios::binary);
        CHECK(file.good());
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
        CHECK(bytes.size() == fixture.bytes);
        auto value = spec(fixture.id, fixture.bytes, fixture.sha);
        value.kind = fixture.kind;
        value.width = value.height = fixture.kind == Kind::Sprite ? 32 : 412;
        CHECK(install(cache, value, bytes));
        Cache rebooted(nor); CHECK(rebooted.boot() == Result::Ok);
        verifyBytes(rebooted, value, bytes);
    }
}
} // namespace

int main() {
    knownDigestsAndBounds(); resumeAndTokens(); tornWrites();
    corruptPayloadAndReadFailure(); failureAfterDurablePrefix();
    protectionAndVersions(); evictionFailureAndFutureHeader(); realGeneratedBlobs();
    std::printf("asset cache: %u checks, %u failures; Cache %zu B, NOR %zu B\n", checks, failures, sizeof(Cache), kStorageBytes);
    return failures ? 1 : 0;
}
