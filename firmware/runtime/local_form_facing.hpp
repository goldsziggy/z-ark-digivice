#pragma once

#include "device_ui.hpp"

namespace digivice::sprite {

// Facing of the locally audited DSF artwork, not a species/anatomy assumption.
// The 2026-10-08 SD manifest contains 241 private forms: 235 left-facing,
// two right-facing and four frontal. All battle clips reuse each form's idle
// poses. DVA1 has no facing field; unreviewed form IDs remain Unknown.
// A replacement pack with changed poses must update this mapping with its audit.
// Provenance and the seven previously unspecified directions were checked
// against decoded pixels before adding this bounded mapping. No asset edits.
inline deviceui::SpriteFacing localFormFacing(std::uint32_t formId) {
    using Facing = deviceui::SpriteFacing;
    switch (formId) {
        case 73: case 91: return Facing::Right; // Koromon, Gaomon
        case 87: case 104: case 114: case 123: return Facing::Front;
        default: break;
    }
    struct Range { std::uint16_t first, last; };
    constexpr Range left[]{
        {11,12}, {14,22}, {25,29}, {31,36}, {39,49}, {52,56},
        {58,63}, {66,72}, {74,86}, {88,90}, {92,103}, {105,113},
        {115,122}, {124,167}, {169,212}, {214,214}, {216,216},
        {221,226}, {228,230}, {232,234}, {236,240}, {242,247},
        {249,249}, {251,266}, {268,276},
    };
    for (const auto& range : left)
        if (formId >= range.first && formId <= range.last) return Facing::Left;
    return Facing::Unknown;
}

} // namespace digivice::sprite
