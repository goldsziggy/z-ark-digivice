#pragma once

#include "sprite.hpp"

namespace digivice::sprite {
// One stable rectangle for every frame of an animation. A completely empty
// clip yields {0,0,0,0}; renderers treat that as the full-frame fallback.
struct OpaqueBounds {
    std::uint8_t x = 0, y = 0, width = 0, height = 0;
};
static_assert(sizeof(OpaqueBounds) == 4);

// Decoded DVA masks: tightly packed frames, row-major pixels, low bit first,
// 1 = opaque. Scan only this clip; no allocation and no pixel-color inference.
// Invalid input clears out and reads no mask bytes. Capacity covers all frames,
// including any earlier/later clips; only the selected clip must be decoded.
inline bool opaqueClipBounds(const std::uint8_t* masks, std::size_t maskBytes,
                             std::uint16_t width, std::uint16_t height,
                             std::uint16_t frameCount, const Clip& clip,
                             OpaqueBounds& out) {
    out = {};
    if (!masks || (width != 16 && width != 32) || height != width ||
        !frameCount || frameCount > kAnimationCount * 8 ||
        !clip.frames || clip.frames > 8 || clip.firstFrame >= frameCount ||
        clip.frames > frameCount - clip.firstFrame) return false;
    const auto pixels = static_cast<std::size_t>(width) * height;
    const auto stride = pixels / 8;
    if (maskBytes < stride * frameCount) return false;
    unsigned left = width, top = height, right = 0, bottom = 0;
    bool found = false;
    for (unsigned frame = 0; frame < clip.frames; ++frame) {
        const auto* mask = masks + (static_cast<std::size_t>(clip.firstFrame) + frame) * stride;
        for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
            const auto index = static_cast<std::size_t>(y) * width + x;
            if (!(mask[index / 8] & (1u << (index % 8)))) continue;
            if (x < left) left = x;
            if (x > right) right = x;
            if (y < top) top = y;
            if (y > bottom) bottom = y;
            found = true;
        }
    }
    if (found) out = {static_cast<std::uint8_t>(left), static_cast<std::uint8_t>(top),
                      static_cast<std::uint8_t>(right - left + 1),
                      static_cast<std::uint8_t>(bottom - top + 1)};
    return true;
}
} // namespace digivice::sprite
