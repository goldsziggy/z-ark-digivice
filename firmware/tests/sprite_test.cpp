#include "sprite.hpp"
#include "fallback_asset.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {
using namespace digivice::sprite;
namespace fallback = digivice::runtime::fallback;
unsigned checks = 0, failures = 0;
#define CHECK(expression) do { ++checks; if (!(expression)) { \
    ++failures; std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); } } while (false)

struct Memory {
    const std::uint8_t* bytes;
    std::size_t length;
    std::size_t calls = 0, largest = 0, invalidRequests = 0;
    std::size_t failAt = std::numeric_limits<std::size_t>::max();
    static bool read(void* context, std::size_t offset, void* output, std::size_t length) {
        auto& self = *static_cast<Memory*>(context);
        ++self.calls; self.largest = std::max(self.largest, length);
        if (offset > self.length || length > self.length - offset || length > kReadChunkBytes) {
            ++self.invalidRequests; return false;
        }
        if (offset >= self.failAt) return false;
        std::memcpy(output, self.bytes + offset, length); return true;
    }
    Reader reader() { return {this, read, length}; }
};

struct File {
    std::FILE* file;
    std::size_t bytes, largest = 0, calls = 0;
    static bool read(void* context, std::size_t offset, void* output, std::size_t length) {
        auto& self = *static_cast<File*>(context);
        ++self.calls; self.largest = std::max(self.largest, length);
        if (offset > self.bytes || length > self.bytes - offset || length > kReadChunkBytes) return false;
        return std::fseek(self.file, static_cast<long>(offset), SEEK_SET) == 0 &&
               std::fread(output, 1, length, self.file) == length;
    }
};

std::uint16_t get16(const std::uint8_t* bytes) {
    return std::uint16_t(bytes[0]) | (std::uint16_t(bytes[1]) << 8);
}
void put16(std::uint8_t* bytes, std::uint16_t value) { bytes[0] = std::uint8_t(value); bytes[1] = std::uint8_t(value >> 8); }
void put32(std::uint8_t* bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = std::uint8_t(value >> (8 * i));
}
void repairCrc(std::vector<std::uint8_t>& bytes) {
    std::uint32_t crc = UINT32_MAX;
    for (std::size_t i = 32; i < bytes.size(); ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    put32(bytes.data() + 28, ~crc);
}

void allFrames(const std::uint8_t* bytes, std::size_t length, unsigned expectedSize, unsigned expectedFrames) {
    Memory source{bytes, length};
    Sprite sprite;
    CHECK(sprite.open(source.reader()) == Result::Ok);
    CHECK(sprite.isOpen());
    const auto& info = sprite.info();
    CHECK(info.width == expectedSize && info.height == expectedSize && info.assetVersion == 1);
    CHECK(info.frameCount == expectedFrames && info.transparentIndex == 0);
    CHECK(source.largest <= kReadChunkBytes && source.calls > 2 && source.invalidRequests == 0);
    const auto pixelCount = expectedSize * expectedSize;
    std::uint16_t pixels[kMaximumPixels + 2];
    std::uint8_t mask[kMaximumMaskBytes + 2];
    for (unsigned animation = 0; animation < kAnimationCount; ++animation) {
        const auto* record = bytes + 64 + animation * 8;
        CHECK(info.clips[animation].frameMs == get16(record + 2));
        for (unsigned frame = 0; frame < info.clips[animation].frames; ++frame) {
            std::fill(std::begin(pixels), std::end(pixels), 0xa55a);
            std::fill(std::begin(mask), std::end(mask), 0x69);
            CHECK(sprite.decode(static_cast<Animation>(animation), frame, pixels, kMaximumPixels + 2,
                                mask, sizeof(mask)) == Result::Ok);
            const auto* packed = bytes + 112 + (get16(record + 4) + frame) * pixelCount / 2;
            bool colorsMatch = true, alphaMatches = true;
            for (unsigned pixel = 0; pixel < pixelCount; ++pixel) {
                const auto paletteIndex = (pixel % 2) ? (packed[pixel / 2] & 15) : (packed[pixel / 2] >> 4);
                colorsMatch &= pixels[pixel] == get16(bytes + 32 + paletteIndex * 2);
                alphaMatches &= ((mask[pixel / 8] >> (pixel % 8)) & 1) == (paletteIndex != 0);
            }
            CHECK(colorsMatch && alphaMatches);
            CHECK(std::all_of(pixels + pixelCount, std::end(pixels), [](auto value) { return value == 0xa55a; }));
            CHECK(std::all_of(mask + pixelCount / 8, std::end(mask), [](auto value) { return value == 0x69; }));
        }
    }
    CHECK(source.invalidRequests == 0 && source.largest <= 128);
    CHECK(sprite.decode(Animation::Idle, 0, pixels, pixelCount) == Result::Ok);
    sprite.close();
    CHECK(!sprite.isOpen() && sprite.info().width == 0);
    CHECK(sprite.decode(Animation::Idle, 0, pixels, pixelCount) == Result::NotOpen);
}

void fallbackGoldenAndTransparency() {
    Memory source{fallback::mote, fallback::mote_bytes};
    Sprite sprite;
    CHECK(sprite.open(source.reader()) == Result::Ok);
    std::uint16_t pixels[256]; std::uint8_t mask[32];
    CHECK(sprite.decode(Animation::Idle, 0, pixels, 256, mask, sizeof(mask)) == Result::Ok);
    unsigned opaque = 0; std::uint32_t checksum = 2166136261u;
    for (unsigned i = 0; i < 256; ++i) {
        opaque += (mask[i / 8] >> (i % 8)) & 1;
        checksum ^= pixels[i]; checksum *= 16777619u;
    }
    // Matches the previously independently inspected resident preview/demo.
    CHECK(opaque == 94 && checksum == 0x5990a02eu);

    std::vector<std::uint8_t> equalColors(fallback::mote, fallback::mote + fallback::mote_bytes);
    put16(equalColors.data() + 32, 0x1234); put16(equalColors.data() + 34, 0x1234);
    equalColors[112] = 0x01; repairCrc(equalColors);
    Memory modified{equalColors.data(), equalColors.size()};
    CHECK(sprite.open(modified.reader()) == Result::Ok);
    CHECK(sprite.decode(Animation::Idle, 0, pixels, 256, mask, sizeof(mask)) == Result::Ok);
    CHECK(pixels[0] == 0x1234 && pixels[1] == 0x1234 && (mask[0] & 3) == 2);
}

void malformed(const std::vector<std::uint8_t>& original) {
    struct Mutation { std::size_t offset; std::uint32_t value; unsigned width; Result expected; };
    const Mutation mutations[]{
        {0, 'X', 1, Result::InvalidFormat}, {4, 2, 2, Result::Unsupported},
        {6, 31, 2, Result::InvalidFormat}, {8, 0, 4, Result::InvalidFormat},
        {12, 24, 2, Result::InvalidFormat}, {14, 16, 2, Result::InvalidFormat},
        {16, 15, 1, Result::InvalidFormat}, {17, 5, 1, Result::InvalidFormat},
        {18, 1, 1, Result::InvalidFormat}, {19, 1, 1, Result::InvalidFormat},
        {20, 0, 2, Result::InvalidFormat}, {20, 49, 2, Result::InvalidFormat},
        {22, 511, 2, Result::InvalidFormat}, {24, UINT32_MAX, 4, Result::InvalidLength},
        {64, 1, 1, Result::InvalidFormat}, {65, 0, 1, Result::InvalidFormat},
        {65, 9, 1, Result::InvalidFormat}, {66, 39, 2, Result::InvalidFormat},
        {66, 2001, 2, Result::InvalidFormat}, {68, 1, 2, Result::InvalidFormat},
        {70, 1, 2, Result::InvalidFormat}, {72, 0, 1, Result::InvalidFormat},
        {76, 3, 2, Result::InvalidFormat}, {105, 3, 1, Result::InvalidFormat},
        {108, 65535, 2, Result::InvalidFormat}
    };
    for (const auto& mutation : mutations) {
        auto damaged = original;
        if (mutation.width == 1) damaged[mutation.offset] = std::uint8_t(mutation.value);
        else if (mutation.width == 2) put16(damaged.data() + mutation.offset, std::uint16_t(mutation.value));
        else put32(damaged.data() + mutation.offset, mutation.value);
        repairCrc(damaged); // Invalid metadata cannot hide behind a checksum error.
        Memory source{damaged.data(), damaged.size()}; Sprite sprite;
        CHECK(sprite.open(source.reader()) == mutation.expected);
        CHECK(!sprite.isOpen() && sprite.info().width == 0 && source.invalidRequests == 0);
    }
    for (const auto offset : {std::size_t(28), std::size_t(40), std::size_t(112), original.size() - 1}) {
        auto damaged = original; damaged[offset] ^= 1;
        Memory source{damaged.data(), damaged.size()}; Sprite sprite;
        CHECK(sprite.open(source.reader()) == Result::Integrity);
    }
    for (const auto length : {std::size_t(0), std::size_t(31), std::size_t(32), std::size_t(63),
                             std::size_t(64), std::size_t(111), std::size_t(112), original.size() - 1}) {
        Memory source{original.data(), length}; Sprite sprite;
        CHECK(sprite.open(source.reader()) == Result::InvalidLength);
        CHECK(source.invalidRequests == 0);
    }
    auto trailing = original; trailing.push_back(0);
    Memory extra{trailing.data(), trailing.size()}; Sprite sprite;
    CHECK(sprite.open(extra.reader()) == Result::InvalidLength);
    Memory oversized{original.data(), original.size()};
    auto reader = oversized.reader(); reader.bytes = kMaximumBlobBytes + 1;
    CHECK(sprite.open(reader) == Result::InvalidLength && oversized.calls == 0);
    reader.bytes = std::numeric_limits<std::size_t>::max();
    CHECK(sprite.open(reader) == Result::InvalidLength && oversized.calls == 0);
    CHECK(sprite.open({}) == Result::InvalidReader);

    auto futureAsset = original; put32(futureAsset.data() + 8, UINT32_MAX);
    Memory future{futureAsset.data(), futureAsset.size()};
    CHECK(sprite.open(future.reader()) == Result::Ok && sprite.info().assetVersion == UINT32_MAX);
    future.failAt = 0;
    CHECK(sprite.open(future.reader()) == Result::Io && !sprite.isOpen() && sprite.info().frameCount == 0);
}

void outputsAndIo(const std::vector<std::uint8_t>& original) {
    Memory source{original.data(), original.size()}; Sprite sprite;
    CHECK(sprite.open(source.reader()) == Result::Ok);
    std::uint16_t pixels[kMaximumPixels + 2]; std::uint8_t mask[kMaximumMaskBytes + 2];
    std::fill(std::begin(pixels), std::end(pixels), 0xabcd);
    std::fill(std::begin(mask), std::end(mask), 0xef);
    const auto before = source.calls;
    CHECK(sprite.decode(static_cast<Animation>(255), 0, pixels, kMaximumPixels) == Result::InvalidFrame);
    CHECK(sprite.decode(Animation::Idle, sprite.info().clips[0].frames, pixels, kMaximumPixels) == Result::InvalidFrame);
    CHECK(sprite.decode(Animation::Attack, std::numeric_limits<std::size_t>::max(), pixels, kMaximumPixels) == Result::InvalidFrame);
    CHECK(sprite.decode(Animation::Idle, 0, nullptr, kMaximumPixels) == Result::SmallBuffer);
    CHECK(sprite.decode(Animation::Idle, 0, pixels, kMaximumPixels - 1) == Result::SmallBuffer);
    CHECK(sprite.decode(Animation::Idle, 0, pixels, kMaximumPixels, mask, kMaximumMaskBytes - 1) == Result::SmallBuffer);
    CHECK(sprite.decode(Animation::Idle, 0, pixels, kMaximumPixels, nullptr, 128) == Result::SmallBuffer);
    CHECK(source.calls == before);
    CHECK(std::all_of(std::begin(pixels), std::end(pixels), [](auto value) { return value == 0xabcd; }));
    CHECK(std::all_of(std::begin(mask), std::end(mask), [](auto value) { return value == 0xef; }));

    source.failAt = 112 + kReadChunkBytes;
    CHECK(sprite.decode(Animation::Idle, 0, pixels, kMaximumPixels, mask, kMaximumMaskBytes) == Result::Io);
    CHECK(pixels[256] == 0xabcd && pixels[kMaximumPixels] == 0xabcd && mask[kMaximumMaskBytes] == 0xef);
    source.failAt = std::numeric_limits<std::size_t>::max();
    CHECK(sprite.decode(Animation::Idle, 0, pixels, kMaximumPixels, mask, kMaximumMaskBytes) == Result::Ok);
    for (const auto failAt : {std::size_t(0), std::size_t(32), std::size_t(160), original.size() - 128}) {
        source.failAt = failAt;
        CHECK(sprite.open(source.reader()) == Result::Io && !sprite.isOpen());
    }
    // Reader advertises complete bytes, but its backing data is truncated.
    Memory truncated{original.data(), original.size() - 1};
    auto reader = truncated.reader(); reader.bytes = original.size();
    CHECK(sprite.open(reader) == Result::Io && !sprite.isOpen());
    CHECK(source.largest <= kReadChunkBytes);
}

void legalClipBoundaries(const std::vector<std::uint8_t>& original) {
    for (const unsigned count : {1u, 8u}) {
        const auto frames = count * kAnimationCount;
        std::vector<std::uint8_t> fixture(112 + frames * 512, 0x12);
        std::copy(original.begin(), original.begin() + 112, fixture.begin());
        put16(fixture.data() + 20, static_cast<std::uint16_t>(frames));
        put32(fixture.data() + 24, static_cast<std::uint32_t>(fixture.size() - 32));
        for (unsigned animation = 0; animation < kAnimationCount; ++animation) {
            auto* record = fixture.data() + 64 + animation * 8;
            record[1] = static_cast<std::uint8_t>(count);
            put16(record + 2, animation % 2 ? 2000 : 40);
            put16(record + 4, static_cast<std::uint16_t>(animation * count));
        }
        repairCrc(fixture);
        Memory source{fixture.data(), fixture.size()}; Sprite sprite;
        CHECK(sprite.open(source.reader()) == Result::Ok && sprite.info().frameCount == frames);
        std::uint16_t pixels[kMaximumPixels]; std::uint8_t mask[kMaximumMaskBytes];
        CHECK(sprite.decode(Animation::Celebrate, count - 1, pixels, kMaximumPixels, mask, sizeof(mask)) == Result::Ok);
        CHECK(pixels[0] == get16(fixture.data() + 34) && pixels[1023] == get16(fixture.data() + 36));
        CHECK(std::all_of(std::begin(mask), std::end(mask), [](auto byte) { return byte == 255; }));
        CHECK(source.invalidRequests == 0 && source.largest <= kReadChunkBytes);
    }
}
} // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "assets/device/sprite-flicker-v1.dva";
    auto* file = std::fopen(path, "rb");
    if (!file) { std::fprintf(stderr, "Cannot read real generated sprite: %s\n", path); return 1; }
    if (std::fseek(file, 0, SEEK_END) != 0) { std::fclose(file); return 1; }
    const auto length = std::ftell(file);
    if (length <= 0 || length > static_cast<long>(kMaximumBlobBytes)) { std::fclose(file); return 1; }
    File source{file, static_cast<std::size_t>(length)};
    Sprite streamed;
    CHECK(streamed.open({&source, File::read, source.bytes}) == Result::Ok);
    std::uint16_t frame[kMaximumPixels]; std::uint8_t mask[kMaximumMaskBytes];
    CHECK(streamed.decode(Animation::Celebrate, 3, frame, kMaximumPixels, mask, sizeof(mask)) == Result::Ok);
    CHECK(source.largest <= kReadChunkBytes && source.calls > 60);
    // Only the host test loads a fixture copy, for controlled corruption cases.
    std::vector<std::uint8_t> original(source.bytes);
    CHECK(std::fseek(file, 0, SEEK_SET) == 0 && std::fread(original.data(), 1, original.size(), file) == original.size());
    streamed.close(); std::fclose(file);

    allFrames(original.data(), original.size(), 32, 17);
    allFrames(fallback::mote, fallback::mote_bytes, 16, 9);
    allFrames(fallback::flicker, fallback::flicker_bytes, 16, 9);
    fallbackGoldenAndTransparency(); malformed(original); outputsAndIo(original); legalClipBoundaries(original);
    std::printf("sprite: %u checks, %u failures; Sprite=%zu B, max RGB565=%zu B, alpha=%zu B, read chunk=%zu B.\n",
                checks, failures, sizeof(Sprite), kMaximumFrameBytes, kMaximumMaskBytes, kReadChunkBytes);
    std::puts("Real 32px generated sprite + both 16px resident fallbacks; streaming, transparency, malformed/truncated bounds. No display hardware.");
    return failures ? 1 : 0;
}
