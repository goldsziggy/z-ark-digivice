#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace digivice::display {

inline constexpr int kPanelSide = 412;
inline constexpr int kMaxStripeRows = 16;
enum class Orientation { Native, Cw90, Ccw90 };
struct Point { int x; int y; };
struct Rect { int x; int y; int width; int height; };

constexpr bool validOrientation(Orientation value) {
    return value == Orientation::Native || value == Orientation::Cw90 || value == Orientation::Ccw90;
}

constexpr bool validPoint(Point point) {
    return point.x >= 0 && point.y >= 0 && point.x < kPanelSide && point.y < kPanelSide;
}

constexpr bool validRect(Rect rect) {
    return rect.x >= 0 && rect.y >= 0 && rect.x < kPanelSide && rect.y < kPanelSide &&
        rect.width > 0 && rect.height > 0 && rect.width <= kPanelSide - rect.x &&
        rect.height <= kPanelSide - rect.y;
}

// Rotation is relative to the panel's existing native coordinates, not a
// guessed case orientation. Touch uses the exact inverse of the pixel mapping.
constexpr Point logicalToPanel(Orientation orientation, Point point) {
    if (!validOrientation(orientation) || !validPoint(point)) return {-1, -1};
    if (orientation == Orientation::Cw90) return {kPanelSide - 1 - point.y, point.x};
    if (orientation == Orientation::Ccw90) return {point.y, kPanelSide - 1 - point.x};
    return point;
}

constexpr Point panelToLogical(Orientation orientation, Point point) {
    if (!validOrientation(orientation) || !validPoint(point)) return {-1, -1};
    if (orientation == Orientation::Cw90) return {point.y, kPanelSide - 1 - point.x};
    if (orientation == Orientation::Ccw90) return {kPanelSide - 1 - point.y, point.x};
    return point;
}

// Rectangles have exclusive right/bottom bounds, matching esp_lcd draw_bitmap.
constexpr Rect logicalRectToPanel(Orientation orientation, Rect rect) {
    if (!validOrientation(orientation) || !validRect(rect)) return {0, 0, 0, 0};
    if (orientation == Orientation::Cw90)
        return {kPanelSide - rect.y - rect.height, rect.x, rect.height, rect.width};
    if (orientation == Orientation::Ccw90)
        return {rect.y, kPanelSide - rect.x - rect.width, rect.height, rect.width};
    return rect;
}

// Source begins at the logical rectangle's first pixel, with a pixel stride.
// panelRow is relative to the transformed rectangle. Output is packed RGB565
// big-endian in panel row order. No allocation, source mutation or overlap is
// permitted. Every argument is validated before any output byte is written.
inline bool packStripe(Orientation orientation, Rect logical,
                       const std::uint16_t* pixels, std::size_t sourcePixelCount,
                       std::size_t stridePixels, int panelRow, int rows,
                       std::uint8_t* output, std::size_t outputBytes) {
    if (!validOrientation(orientation) || !validRect(logical) || !pixels || !output ||
        stridePixels < static_cast<std::size_t>(logical.width) ||
        stridePixels > static_cast<std::size_t>(kPanelSide) || panelRow < 0 ||
        rows <= 0 || rows > kMaxStripeRows) return false;
    const auto panelRect = logicalRectToPanel(orientation, logical);
    if (panelRow >= panelRect.height || rows > panelRect.height - panelRow) return false;
    const auto sourceNeeded = static_cast<std::size_t>(logical.height - 1) * stridePixels +
                              static_cast<std::size_t>(logical.width);
    const auto outputNeeded = static_cast<std::size_t>(panelRect.width) * static_cast<std::size_t>(rows) * 2;
    if (sourcePixelCount < sourceNeeded || outputBytes < outputNeeded) return false;
    const auto sourceStart = reinterpret_cast<std::uintptr_t>(pixels);
    const auto outputStart = reinterpret_cast<std::uintptr_t>(output);
    const auto sourceBytes = sourceNeeded * sizeof(*pixels);
    constexpr auto maxAddress = std::numeric_limits<std::uintptr_t>::max();
    if (sourceStart > maxAddress - sourceBytes || outputStart > maxAddress - outputNeeded ||
        (sourceStart < outputStart + outputNeeded && outputStart < sourceStart + sourceBytes)) return false;

    for (int dy = 0; dy < rows; ++dy) {
        const int py = panelRow + dy;
        for (int px = 0; px < panelRect.width; ++px) {
            int sx = px, sy = py;
            if (orientation == Orientation::Cw90) { sx = py; sy = logical.height - 1 - px; }
            else if (orientation == Orientation::Ccw90) { sx = logical.width - 1 - py; sy = px; }
            const auto pixel = pixels[static_cast<std::size_t>(sy) * stridePixels + static_cast<std::size_t>(sx)];
            const auto index = (static_cast<std::size_t>(dy) * static_cast<std::size_t>(panelRect.width) +
                                static_cast<std::size_t>(px)) * 2;
            output[index] = static_cast<std::uint8_t>(pixel >> 8);
            output[index + 1] = static_cast<std::uint8_t>(pixel);
        }
    }
    return true;
}

} // namespace digivice::display
