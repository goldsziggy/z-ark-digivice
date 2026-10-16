#pragma once
#include <cstdint>

namespace digivice::presentation {
// These are separate authored combat profiles of the same GrapLeomon species:
// preserved form30 (Guardian) and Dawn/Dusk sheet form357 (Mystic). Keep their
// saved IDs, stats and evolution routes; the labels are not canonical variants.
inline const char* evolutionProfileLabel(std::uint32_t formId) {
    switch (formId) {
        case 30: return "GUARDIAN FORM";
        case 357: return "MYSTIC FORM";
        default: return nullptr;
    }
}

// A deliberately closed artwork mapping, never a name match or nearest form.
// Provenance: sprite_thread.zip/277_GrappLeomon.png and the official English
// GrapLeomon entry (directory_name=grappuleomon). Prefer exact files and use
// this only on ENOENT, never to mask malformed or unverifiable artwork.
inline std::uint32_t sameSpeciesArtFallback(std::uint32_t formId) {
    return formId == 30 ? 357 : formId == 357 ? 30 : 0;
}
} // namespace digivice::presentation
