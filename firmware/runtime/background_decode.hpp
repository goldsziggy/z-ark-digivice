#pragma once
#include <cstddef>
#include <cstdint>

namespace digivice::assets {
constexpr unsigned kBackgroundSide = 412;
constexpr std::size_t kBackgroundPixels = kBackgroundSide*kBackgroundSide;
constexpr std::size_t kMaximumJpegBytes = 128*1024;
struct JpegInfo { unsigned width = 0, height = 0; };
// Restricted baseline RGB/YCbCr JPEG used by the local scene fixtures. Validate
// marker lengths/dimensions/tables before exposing bytes to the S3 ROM decoder.
bool inspectBackgroundJpeg(const std::uint8_t* data, std::size_t bytes, JpegInfo& info);
// ROM decoder RGB888 MCU -> nearest-neighbor 412px RGB565, pre-dimmed to60%.
// Inclusive source rectangle; rejects unexpected geometry without writing.
bool writeBackgroundBlock(const JpegInfo& info, unsigned left, unsigned top,
    unsigned right, unsigned bottom, const std::uint8_t* rgb, std::size_t rgbBytes,
    std::uint16_t* target, std::size_t capacity, std::size_t& pixelsWritten);
} // namespace digivice::assets
