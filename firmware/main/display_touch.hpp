#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"
#include "display_orientation.hpp"

namespace digivice::display {

inline constexpr int kWidth = kPanelSide;
inline constexpr int kHeight = kPanelSide;
inline constexpr int kStripeRows = kMaxStripeRows;
inline constexpr std::size_t kDmaStripeBytes = kWidth * kStripeRows * 2;

struct Status {
    esp_err_t display = ESP_ERR_NOT_SUPPORTED;
    esp_err_t touch = ESP_ERR_NOT_SUPPORTED;
    bool suspended = false;
    bool idleBlanked = false; // Backlight only; touch and motion remain live.
    std::uint32_t frames = 0; // Successful rectangle flushes, not measured FPS.
    std::uint32_t touchSamples = 0;
    std::uint32_t touchErrors = 0;
    std::uint32_t touchLockMisses = 0; // Subset of errors: shared-bus acquisition timed out.
    esp_err_t lastTouchError = ESP_OK;
};

struct TouchPoint {
    bool pressed = false;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    bool fresh = false; // A decoded new report, not a cached/no-update level.
};

// Single application-task owner. Calls do not alter saves or install legacy I2C.
// Success proves bus transactions completed; visual/touch acceptance is separate.
Status initialize();
const Status& status();
// Runtime orientation controls both pixels and the inverse touch map.
// The build default is retained until a stored installation setting is loaded.
Orientation configuredOrientation();
// Single UI-task owner; cancel held input and redraw fully after success.
// Invalid values or an outstanding DMA transfer leave the orientation unchanged.
esp_err_t setOrientation(Orientation orientation);
bool displayReady();
bool touchReady();
// True only when no color DMA is pending/uncertain. Part of the power barrier.
bool quiescent();

// Host-endian RGB565; exclusive rectangle, packed rows or supplied pixel stride.
// 1.46 rotates into one internal big-endian DMA stripe; 2.1 scales/rotates
// into host-endian RGB double buffers. Returns
// after transfer completion (two VSYNC fences on RGB). Caller may reuse pixels after
// return. On DMA timeout the stripe stays allocated and display is disabled.
// betweenStripes, when set, runs after each completed stripe (no DMA in flight);
// the handheld uses it to keep touch sampling at its 20 ms cadence during long
// flushes. It must not draw, flush or change game state.
using StripeHook = void (*)(void* context);
esp_err_t flushRgb565(int x, int y, int width, int height,
                     const std::uint16_t* pixels, std::size_t stridePixels,
                     StripeHook betweenStripes = nullptr, void* context = nullptr);

// Logical UI 412x412 coordinates after inverse panel rotation; one contact only.
// Multiple contacts cancel input. Cached/no-update points are not remapped.
// Bounded packet sizes/drain loop,
// modern I2C with a 20 ms mutex allowance then a 16 ms transaction-group deadline. On error
// point is INVALID: CANCEL any pending gesture; never interpret it as touch Up.
// After cancellation, rearm only on ESP_OK with fresh=true and pressed=false.
esp_err_t pollTouch(TouchPoint& point);

// Screen-idle brightness only. Does not suspend touch, reset the panel, enter
// ESP sleep or alter GPIO7. Caller stops frame rendering while blanked.
esp_err_t setIdleBlank(bool blanked);

// Backlight and display off; no touch traffic while suspended. Caller quiesces
// its touch gesture before requesting suspension. No reset or save mutation.
// Pending DMA blanks backlight immediately but returns TIMEOUT without issuing
// a panel command. Retry after quiescent(); never treat timeout as a power barrier.
esp_err_t setSuspended(bool suspended);

} // namespace digivice::display
