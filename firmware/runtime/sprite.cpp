#include "sprite.hpp"

#include <algorithm>
#include <cstring>

namespace digivice::sprite {
namespace {
constexpr std::size_t kHeaderBytes = 32, kMetadataBytes = 80, kFrameOffset = 112;
std::uint16_t get16(const std::uint8_t* bytes) {
    return std::uint16_t(bytes[0]) | (std::uint16_t(bytes[1]) << 8);
}
std::uint32_t get32(const std::uint8_t* bytes) {
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
           (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}
std::uint32_t updateCrc(std::uint32_t crc, const std::uint8_t* bytes, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}
bool read(const Reader& reader, std::size_t offset, void* output, std::size_t length) {
    return reader.read && output && offset <= reader.bytes && length <= reader.bytes - offset &&
           length <= kReadChunkBytes && reader.read(reader.context, offset, output, length);
}
} // namespace

void Sprite::close() { reader_ = {}; info_ = {}; open_ = false; }

Result Sprite::open(Reader reader) {
    close();
    if (!reader.read) return Result::InvalidReader;
    if (reader.bytes < kHeaderBytes || reader.bytes > kMaximumBlobBytes) return Result::InvalidLength;
    std::uint8_t header[kHeaderBytes];
    if (!read(reader, 0, header, sizeof(header))) return Result::Io;
    if (std::memcmp(header, "DVA1", 4) != 0) return Result::InvalidFormat;
    if (get16(header + 4) != 1) return Result::Unsupported;
    if (get16(header + 6) != kHeaderBytes || header[16] != 16 ||
        header[17] != kAnimationCount || header[18] != 0 || header[19] != 0)
        return Result::InvalidFormat;
    Info candidate{};
    candidate.assetVersion = get32(header + 8);
    candidate.width = get16(header + 12); candidate.height = get16(header + 14);
    candidate.frameCount = get16(header + 20); candidate.packedFrameBytes = get16(header + 22);
    if (!candidate.assetVersion || (candidate.width != 16 && candidate.width != 32) ||
        candidate.height != candidate.width || candidate.frameCount < kAnimationCount ||
        candidate.frameCount > kAnimationCount * 8 ||
        candidate.packedFrameBytes != candidate.width * candidate.height / 2)
        return Result::InvalidFormat;
    const auto payloadBytes = kMetadataBytes + std::size_t(candidate.frameCount) * candidate.packedFrameBytes;
    if (get32(header + 24) != payloadBytes || reader.bytes != kHeaderBytes + payloadBytes)
        return Result::InvalidLength;

    // Parse metadata from the exact bytes included in the streamed CRC. Never
    // reread a header/table to derive an unchecked frame address during decode.
    std::uint8_t chunk[kReadChunkBytes];
    std::uint32_t crc = UINT32_MAX;
    for (std::size_t offset = kHeaderBytes; offset < reader.bytes;) {
        const auto length = std::min(sizeof(chunk), reader.bytes - offset);
        if (!read(reader, offset, chunk, length)) return Result::Io;
        crc = updateCrc(crc, chunk, length);
        if (offset == kHeaderBytes) {
            // All valid files have at least six 16px frames, so this first chunk
            // always contains the complete 80-byte palette and animation table.
            for (unsigned i = 0; i < 16; ++i) candidate.palette[i] = get16(chunk + 2 * i);
            unsigned nextFrame = 0;
            for (std::size_t i = 0; i < kAnimationCount; ++i) {
                const auto* record = chunk + 32 + i * 8;
                auto& clip = candidate.clips[i];
                clip.frames = record[1]; clip.frameMs = get16(record + 2); clip.firstFrame = get16(record + 4);
                if (record[0] != i || get16(record + 6) != 0 || clip.frames < 1 || clip.frames > 8 ||
                    clip.frameMs < 40 || clip.frameMs > 2000 || clip.firstFrame != nextFrame ||
                    unsigned(clip.firstFrame) + clip.frames > candidate.frameCount)
                    return Result::InvalidFormat;
                nextFrame += clip.frames;
            }
            if (nextFrame != candidate.frameCount) return Result::InvalidFormat;
        }
        offset += length;
    }
    if (~crc != get32(header + 28)) return Result::Integrity;
    reader_ = reader; info_ = candidate; open_ = true;
    return Result::Ok;
}

Result Sprite::decode(Animation animation, std::size_t frameIndex,
                      std::uint16_t* pixels, std::size_t pixelCapacity,
                      std::uint8_t* opaqueMask, std::size_t maskBytes) const {
    if (!open_) return Result::NotOpen;
    const auto index = static_cast<std::size_t>(animation);
    if (index >= kAnimationCount || frameIndex >= info_.clips[index].frames) return Result::InvalidFrame;
    const auto pixelCount = std::size_t(info_.width) * info_.height;
    if (!pixels || pixelCapacity < pixelCount || (opaqueMask && maskBytes < pixelCount / 8) ||
        (!opaqueMask && maskBytes != 0)) return Result::SmallBuffer;
    if (opaqueMask) std::memset(opaqueMask, 0, pixelCount / 8);
    const auto frame = std::size_t(info_.clips[index].firstFrame) + frameIndex;
    const auto offset = kFrameOffset + frame * info_.packedFrameBytes;
    std::uint8_t chunk[kReadChunkBytes];
    for (std::size_t consumed = 0; consumed < info_.packedFrameBytes;) {
        const auto length = std::min(sizeof(chunk), std::size_t(info_.packedFrameBytes) - consumed);
        if (!read(reader_, offset + consumed, chunk, length)) return Result::Io;
        for (std::size_t i = 0; i < length; ++i) {
            const auto first = (consumed + i) * 2;
            const std::uint8_t indices[]{std::uint8_t(chunk[i] >> 4), std::uint8_t(chunk[i] & 15)};
            for (unsigned half = 0; half < 2; ++half) {
                const auto pixel = first + half;
                pixels[pixel] = info_.palette[indices[half]];
                if (opaqueMask && indices[half] != info_.transparentIndex)
                    opaqueMask[pixel / 8] |= std::uint8_t(1u << (pixel % 8));
            }
        }
        consumed += length;
    }
    return Result::Ok;
}

const char* resultName(Result result) {
    switch (result) {
        case Result::Ok: return "ok";
        case Result::InvalidReader: return "invalid-reader";
        case Result::InvalidLength: return "invalid-length";
        case Result::Io: return "io";
        case Result::Unsupported: return "unsupported";
        case Result::InvalidFormat: return "invalid-format";
        case Result::Integrity: return "integrity";
        case Result::NotOpen: return "not-open";
        case Result::InvalidFrame: return "invalid-frame";
        case Result::SmallBuffer: return "small-buffer";
    }
    return "unknown";
}
} // namespace digivice::sprite
