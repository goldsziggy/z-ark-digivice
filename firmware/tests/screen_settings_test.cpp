#include "screen_settings.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <vector>

namespace {
unsigned checks = 0;
void check(bool condition, const char* expression, int line) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "line %d: %s\n", line, expression);
        std::abort();
    }
}
#define CHECK(x) check((x), #x, __LINE__)
using Record = std::array<std::uint8_t, 16>;
constexpr std::uint16_t angles[] = {0, 90, 180, 270};
// Independent fixtures: struct.pack('<II', 1, degrees), Python zlib.crc32.
constexpr Record golden[] = {
    {0x44,0x53,0x43,0x52,0x01,0,0,0,0,0,0,0,0x08,0x24,0x6f,0x4c},
    {0x44,0x53,0x43,0x52,0x01,0,0,0,0x5a,0,0,0,0xce,0xcb,0xdf,0xe8},
    {0x44,0x53,0x43,0x52,0x01,0,0,0,0xb4,0,0,0,0xc5,0xfd,0x7f,0xde},
    {0x44,0x53,0x43,0x52,0x01,0,0,0,0x0e,0x01,0,0,0x0c,0x39,0x72,0xad}
};
constexpr Record invalidWithValidCrc[] = {
    {0x46,0x41,0x49,0x4c,0x01,0,0,0,0x5a,0,0,0,0x71,0x21,0x5e,0x81}, // magic
    {0x44,0x53,0x43,0x52,0x02,0,0,0,0x5a,0,0,0,0x2d,0xcc,0x50,0x66}, // version
    {0x44,0x53,0x43,0x52,0x01,0,0,0,0x2d,0,0,0,0xeb,0x53,0x37,0x1e}, // 45 degrees
    {0x44,0x53,0x43,0x52,0x01,0,0,0,0,0,0x01,0,0x49,0x15,0x74,0x55} // no narrowing 65536 to 0
};
enum class Fault {
    None, Open, Query, Blob, ShortBlob, Set, CommitBefore, CommitAfter,
    VerifyQuery, VerifyBlob, VerifyShort, VerifyLong, VerifyWrong, VerifyMissing
};
struct Fake {
    bool present = false, live = false;
    std::vector<std::uint8_t> durable, staged;
    Fault fault = Fault::None;
    unsigned opens = 0, closes = 0, queries = 0, reads = 0, sets = 0, commits = 0;
} fake;
void reset() { CHECK(!fake.live); fake = {}; }
void seed(const Record& bytes) {
    fake.present = true;
    fake.durable.assign(bytes.begin(), bytes.end());
}
void handle(nvs_handle_t value) { CHECK(fake.live); CHECK(value == 7); }
void key(const char* value) { CHECK(std::strcmp(value, "config") == 0); }
using digivice::screen::NvsSettings;
static_assert(!std::is_copy_constructible<NvsSettings>::value, "NVS handle must have one owner");
static_assert(!std::is_copy_assignable<NvsSettings>::value, "NVS handle must have one owner");
} // namespace

esp_err_t nvs_open(const char* name, int mode, nvs_handle_t* out) {
    CHECK(std::strcmp(name, "digi_screen") == 0);
    CHECK(mode == NVS_READWRITE);
    CHECK(out != nullptr);
    CHECK(!fake.live);
    ++fake.opens;
    if (fake.fault == Fault::Open) return ESP_FAIL;
    fake.live = true;
    *out = 7;
    return ESP_OK;
}
void nvs_close(nvs_handle_t value) {
    handle(value);
    ++fake.closes;
    fake.live = false;
    fake.staged.clear();
}
esp_err_t nvs_get_blob(nvs_handle_t value, const char* name, void* out, std::size_t* size) {
    handle(value); key(name); CHECK(size != nullptr);
    const bool verify = fake.commits != 0;
    if (out == nullptr) {
        ++fake.queries;
        if (fake.fault == Fault::Query || (verify && fake.fault == Fault::VerifyQuery)) return ESP_FAIL;
        if (verify && fake.fault == Fault::VerifyMissing) return ESP_ERR_NVS_NOT_FOUND;
        if (!fake.present) return ESP_ERR_NVS_NOT_FOUND;
        *size = fake.durable.size();
        return ESP_OK;
    }
    ++fake.reads;
    if (fake.fault == Fault::Blob || (verify && fake.fault == Fault::VerifyBlob)) return ESP_FAIL;
    if (*size < fake.durable.size()) return ESP_ERR_INVALID_SIZE;
    std::memcpy(out, fake.durable.data(), fake.durable.size());
    *size = fake.durable.size();
    if (fake.fault == Fault::ShortBlob || (verify && fake.fault == Fault::VerifyShort)) --*size;
    if (verify && fake.fault == Fault::VerifyLong) ++*size;
    // A different, well-formed rotation must fail exact readback too.
    if (verify && fake.fault == Fault::VerifyWrong) std::memcpy(out, golden[3].data(), 16);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t value, const char* name, const void* in, std::size_t size) {
    handle(value); key(name); CHECK(in != nullptr); CHECK(size == 16);
    ++fake.sets;
    if (fake.fault == Fault::Set) return ESP_FAIL;
    const auto* bytes = static_cast<const std::uint8_t*>(in);
    fake.staged.assign(bytes, bytes + size);
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t value) {
    handle(value);
    ++fake.commits;
    if (fake.fault == Fault::CommitBefore) return ESP_FAIL;
    fake.durable = fake.staged;
    fake.staged.clear();
    fake.present = true;
    return fake.fault == Fault::CommitAfter ? ESP_FAIL : ESP_OK;
}

namespace {
void missingAndRoundTrips() {
    for (unsigned initial = 0; initial < 4; ++initial) {
        reset();
        {
            NvsSettings settings;
            CHECK(settings.degrees() == 0);
            CHECK(!settings.writable());
            CHECK(settings.setDegrees(90) == ESP_ERR_INVALID_STATE);
            CHECK(fake.opens == 0 && fake.sets == 0);
            CHECK(settings.begin(angles[initial]) == ESP_OK);
            CHECK(settings.degrees() == angles[initial]);
            CHECK(settings.writable());
            CHECK(fake.queries == 1 && fake.reads == 0 && fake.sets == 0 && fake.commits == 0);
            CHECK(settings.begin(angles[(initial + 1) % 4]) == ESP_OK);
            CHECK(settings.degrees() == angles[initial] && fake.opens == 1);
            CHECK(settings.setDegrees(angles[initial]) == ESP_OK);
            CHECK(fake.sets == 0);
            for (unsigned step = 1; step <= 4; ++step) {
                const auto index = (initial + step) % 4;
                CHECK(settings.setDegrees(angles[index]) == ESP_OK);
                CHECK(settings.degrees() == angles[index] && settings.writable());
                CHECK(fake.durable == std::vector<std::uint8_t>(golden[index].begin(), golden[index].end()));
                CHECK(fake.sets == step && fake.commits == step);
                CHECK(fake.queries == step + 1 && fake.reads == step);
                CHECK(settings.setDegrees(angles[index]) == ESP_OK);
                CHECK(fake.sets == step && fake.commits == step);
            }
        }
        CHECK(fake.closes == 1);
        {
            NvsSettings restarted;
            CHECK(restarted.begin(angles[(initial + 1) % 4]) == ESP_OK);
            CHECK(restarted.degrees() == angles[initial] && restarted.writable());
            CHECK(fake.sets == 4 && fake.commits == 4);
        }
        CHECK(fake.closes == 2);
    }
    // Each on-disk angle overrides every compile-time default without writing.
    for (unsigned stored = 0; stored < 4; ++stored) for (const auto def : angles) {
        reset(); seed(golden[stored]);
        NvsSettings settings;
        CHECK(settings.begin(def) == ESP_OK);
        CHECK(settings.degrees() == angles[stored] && settings.writable());
        CHECK(fake.sets == 0 && fake.commits == 0);
    }
}
void invalidArguments() {
    constexpr std::uint16_t invalid[] = {1, 89, 91, 179, 181, 269, 271, 360, 65535};
    for (const auto value : invalid) {
        reset();
        {
            NvsSettings settings;
            CHECK(!digivice::screen::validDegrees(value));
            CHECK(settings.begin(value) == ESP_ERR_INVALID_ARG);
            CHECK(settings.degrees() == 0 && !settings.writable());
            CHECK(settings.begin(90) == ESP_ERR_INVALID_ARG);
            CHECK(settings.setDegrees(90) == ESP_ERR_INVALID_STATE);
            CHECK(fake.opens == 0 && fake.sets == 0);
        }
        CHECK(fake.closes == 0);
        {
            NvsSettings settings;
            CHECK(settings.begin(180) == ESP_OK);
            CHECK(settings.setDegrees(value) == ESP_ERR_INVALID_ARG);
            CHECK(settings.degrees() == 180 && settings.writable());
            CHECK(fake.sets == 0 && fake.commits == 0);
        }
    }
}
void rejectedRecord(const Record& record) {
    for (const auto def : angles) {
        reset(); seed(record);
        {
            NvsSettings settings;
            CHECK(settings.begin(def) == ESP_ERR_INVALID_RESPONSE);
            CHECK(settings.degrees() == def && !settings.writable());
            CHECK(settings.setDegrees(90) == ESP_ERR_INVALID_STATE);
            CHECK(settings.begin(90) == ESP_ERR_INVALID_RESPONSE);
            CHECK(fake.opens == 1 && fake.sets == 0 && fake.commits == 0);
            CHECK(fake.durable == std::vector<std::uint8_t>(record.begin(), record.end()));
        }
        CHECK(fake.closes == 1);
    }
}
void corruptionAndSizes() {
    for (const auto& record : golden) for (unsigned bit = 0; bit < 128; ++bit) {
        auto corrupt = record;
        corrupt[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
        rejectedRecord(corrupt);
    }
    for (const auto& record : invalidWithValidCrc) rejectedRecord(record);
    for (const auto bytes : {0u, 1u, 15u, 17u, 4096u}) {
        reset(); fake.present = true; fake.durable.resize(bytes, 0);
        NvsSettings settings;
        CHECK(settings.begin(270) == ESP_ERR_INVALID_SIZE);
        CHECK(settings.degrees() == 270 && !settings.writable());
        CHECK(fake.reads == 0 && fake.sets == 0 && fake.commits == 0);
    }
}
void readAndOpenFailures() {
    for (const auto fault : {Fault::Open, Fault::Query, Fault::Blob, Fault::ShortBlob}) {
        reset(); seed(golden[1]); fake.fault = fault;
        const auto expected = fault == Fault::ShortBlob ? ESP_ERR_INVALID_SIZE : ESP_FAIL;
        {
            NvsSettings settings;
            CHECK(settings.begin(180) == expected);
            CHECK(settings.degrees() == 180 && !settings.writable());
            CHECK(settings.begin(0) == expected && fake.opens == 1);
            CHECK(settings.setDegrees(270) == ESP_ERR_INVALID_STATE);
            CHECK(fake.sets == 0 && fake.commits == 0);
        }
        CHECK(fake.closes == (fault == Fault::Open ? 0u : 1u));
    }
}
void uncertainSaves() {
    constexpr Fault failures[] = {
        Fault::Set, Fault::CommitBefore, Fault::CommitAfter,
        Fault::VerifyQuery, Fault::VerifyBlob, Fault::VerifyShort,
        Fault::VerifyLong, Fault::VerifyWrong, Fault::VerifyMissing
    };
    for (const auto fault : failures) {
        reset(); seed(golden[0]);
        esp_err_t expected = ESP_FAIL;
        if (fault == Fault::VerifyShort || fault == Fault::VerifyLong) expected = ESP_ERR_INVALID_SIZE;
        if (fault == Fault::VerifyWrong) expected = ESP_ERR_INVALID_RESPONSE;
        if (fault == Fault::VerifyMissing) expected = ESP_ERR_NVS_NOT_FOUND;
        {
            NvsSettings settings;
            CHECK(settings.begin(270) == ESP_OK);
            fake.fault = fault;
            CHECK(settings.setDegrees(90) == expected);
            CHECK(settings.degrees() == 0 && !settings.writable());
            CHECK(fake.sets == 1 && fake.commits == (fault == Fault::Set ? 0u : 1u));
            const auto queries = fake.queries, reads = fake.reads;
            fake.fault = Fault::None; // A recovered backend cannot silently unblock this object.
            CHECK(settings.setDegrees(180) == ESP_ERR_INVALID_STATE);
            CHECK(settings.setDegrees(0) == ESP_ERR_INVALID_STATE);
            CHECK(settings.begin(270) == expected);
            CHECK(settings.degrees() == 0 && !settings.writable());
            CHECK(fake.sets == 1 && fake.queries == queries && fake.reads == reads);
        }
        CHECK(fake.closes == 1);
        // A new instance can recover the durable result, including a lost commit ACK.
        {
            NvsSettings restarted;
            CHECK(restarted.begin(270) == ESP_OK);
            const auto landed = fault != Fault::Set && fault != Fault::CommitBefore;
            CHECK(restarted.degrees() == (landed ? 90 : 0) && restarted.writable());
        }
        CHECK(fake.closes == 2);
    }
    // On a missing preference, uncertainty still preserves its compile-time default.
    reset();
    NvsSettings settings;
    CHECK(settings.begin(180) == ESP_OK);
    fake.fault = Fault::CommitAfter;
    CHECK(settings.setDegrees(270) == ESP_FAIL);
    CHECK(settings.degrees() == 180 && !settings.writable());
}
} // namespace

int main() {
    missingAndRoundTrips();
    invalidArguments();
    corruptionAndSizes();
    readAndOpenFailures();
    uncertainSaves();
    CHECK(!fake.live);
    std::printf("screen settings: %u checks passed (ASan/UBSan)\n", checks);
}
