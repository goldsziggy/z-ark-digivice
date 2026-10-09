#pragma once

#include <cstddef>
#include <cstdint>

namespace digivice::sprite {
constexpr std::size_t kMaximumBlobBytes = 128 * 1024;
constexpr std::size_t kReadChunkBytes = 128;
constexpr std::size_t kMaximumPixels = 32 * 32;
constexpr std::size_t kMaximumFrameBytes = kMaximumPixels * sizeof(std::uint16_t);
constexpr std::size_t kMaximumMaskBytes = kMaximumPixels / 8;
constexpr std::size_t kAnimationCount = 6;

// A successful callback fills exactly length bytes. Source bytes and context
// must remain valid and immutable until close()/open(); serialize with eviction.
// A cache adapter should read a protected, verified Spec. Reader has no ownership.
struct Reader {
    void* context = nullptr;
    bool (*read)(void* context, std::size_t offset, void* output, std::size_t length) = nullptr;
    std::size_t bytes = 0;
};

enum class Animation : std::uint8_t { Idle, Attack, Hurt, Sleep, Care, Celebrate };
struct Clip {
    std::uint16_t frameMs = 0;
    std::uint16_t firstFrame = 0;
    std::uint8_t frames = 0;
};
struct Info {
    std::uint32_t assetVersion = 0;
    std::uint16_t width = 0, height = 0, frameCount = 0, packedFrameBytes = 0;
    std::uint8_t transparentIndex = 0;
    std::uint16_t palette[16]{};
    Clip clips[kAnimationCount]{};
};
enum class Result : std::uint8_t {
    Ok, InvalidReader, InvalidLength, Io, Unsupported, InvalidFormat,
    Integrity, NotOpen, InvalidFrame, SmallBuffer
};
const char* resultName(Result result);

// No heap allocation, full-blob buffer, filesystem, clock or display dependency.
// open() streams the payload CRC and validates the entire fixed DVA1 frame map.
// CRC is corruption detection, not publisher authenticity: downloaded assets
// still require the cache's signed-catalog trust and complete-blob SHA checks.
class Sprite {
public:
    // Any failure closes this object; no stale previously opened sprite remains.
    Result open(Reader reader);
    void close();
    bool isOpen() const { return open_; }
    const Info& info() const { return info_; }

    // pixelCapacity is in uint16_t pixels, not bytes. Decode writes width*height
    // RGB565 values in host integer order; the display adapter controls byte order.
    // Optional opacity mask: one bit per row-major pixel, low bit first in each
    // byte, 1=opaque and 0=transparent. It needs width*height/8 bytes (32 or128).
    // Transparent pixels still contain palette[0]; colors alone cannot determine
    // transparency, as an opaque palette entry can contain the same RGB565 value.
    // Omitted mask requires nullptr and maskBytes=0. The two output buffers must
    // not overlap each other or the Reader's source. Larger buffers' tails remain
    // untouched. I/O failure may leave partial output: render only Result::Ok.
    Result decode(Animation animation, std::size_t frameIndex,
                  std::uint16_t* pixels, std::size_t pixelCapacity,
                  std::uint8_t* opaqueMask = nullptr, std::size_t maskBytes = 0) const;

private:
    Reader reader_{};
    Info info_{};
    bool open_ = false;
};
} // namespace digivice::sprite
