#pragma once

#include "display_orientation.hpp"

#include <cstddef>
#include <cstdint>

namespace digivice::display {

// SPD2010 status word at 0x2000, in the same order readTouch branches on it.
// Busy wins over a mode bit. BIOS->CPU wins over point-mode start. An empty
// running report is no-contact before a point payload or the leftover ack.
enum class SpdStatus : std::uint8_t {
    Busy,
    BiosToCpu,
    PointModeStart,
    NoContact,
    PointData,
    RunningAck,
    NoUpdate,
};

inline SpdStatus classifySpd2010Status(const std::uint8_t state[4]) {
    const auto length = static_cast<std::size_t>(state[2] | (state[3] << 8));
    if (state[1] & 0x80) return SpdStatus::Busy;
    if (state[1] & 0x40) return SpdStatus::BiosToCpu;
    if (state[1] & 0x20) return SpdStatus::PointModeStart;
    if ((state[1] & 0x08) && length == 0 && !(state[0] & 0x03)) return SpdStatus::NoContact;
    if (state[0] & 0x03) return SpdStatus::PointData;
    if ((state[1] & 0x08) && (state[0] & 0x08)) return SpdStatus::RunningAck;
    return SpdStatus::NoUpdate;
}

// True only for the one in-range contact readTouch publishes. Gesture,
// multicontact and out-of-range payloads stay false (counted as malformed).
inline bool spdDecodedPoint(std::uint8_t state0, const std::uint8_t* packet, std::size_t length,
                            std::uint16_t& x, std::uint16_t& y) {
    x = y = 0;
    if (!packet || (state0 & 0x01) == 0 || length != 10 || packet[4] > 0x0a) return false;
    x = static_cast<std::uint16_t>(((packet[7] & 0xf0) << 4) | packet[5]);
    y = static_cast<std::uint16_t>(((packet[7] & 0x0f) << 8) | packet[6]);
    return x < kPanelSide && y < kPanelSide;
}

inline const char* spdStatusName(SpdStatus kind) {
    switch (kind) {
    case SpdStatus::Busy: return "busy";
    case SpdStatus::BiosToCpu: return "bios-cpu";
    case SpdStatus::PointModeStart: return "point-mode";
    case SpdStatus::NoContact: return "no-contact";
    case SpdStatus::PointData: return "point";
    case SpdStatus::RunningAck: return "ack";
    case SpdStatus::NoUpdate: return "no-update";
    }
    return "unknown";
}

} // namespace digivice::display
