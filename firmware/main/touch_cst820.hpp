#pragma once
#include "display_touch.hpp"
namespace digivice::display {
// One atomic count+first-contact read from register 0x02. Malformed/multi-touch
// reports cancel the gesture; only a fresh neutral report may rearm it.
inline bool decodeCst820(const std::uint8_t (&packet)[7], Orientation orientation, TouchPoint& out) {
    const unsigned count = packet[0] & 0x0f;
    if (!validOrientation(orientation) || count > 1) return false;
    TouchPoint next{}; next.fresh = true;
    if (count) {
        const auto event = packet[1] >> 6;
        if (event == 3) return false;
        const auto p = physicalToLogical(orientation,
            {((packet[1] & 0x0f) << 8) | packet[2], ((packet[3] & 0x0f) << 8) | packet[4]}, 480);
        if (!validPoint(p)) return false;
        next.pressed = event != 1; next.x = p.x; next.y = p.y;
    }
    out = next; return true;
}
}
