#pragma once
#include <cstdint>
namespace digivice::encounters {
// Authored encounter frequency, not an official species attribute or combat stat.
enum class Rarity : std::uint8_t { Unknown, Common, Uncommon, Rare };
Rarity rarityForForm(std::uint32_t formId);
const char* rarityName(Rarity rarity); // Null for Unknown/invalid.
// Frozen rules10..12 selection, including original fixture IDs. Replay only.
// Does not touch capture RNG, clocks or care state.
std::uint32_t select(std::uint32_t encounter,std::uint32_t seed,
                     std::uint32_t partnerFormId,std::uint32_t rivalLevel);
// Current released roster only (rules 19+), including the first encounter; no test-foe fallback.
std::uint32_t selectProduction(std::uint32_t encounter,std::uint32_t seed,
                              std::uint32_t partnerFormId,std::uint32_t rivalLevel);
namespace rules18 {
// Frozen rules 13-18 pool: the 266 production forms of the 276-form roster.
std::uint32_t selectProduction(std::uint32_t encounter,std::uint32_t seed,
                              std::uint32_t partnerFormId,std::uint32_t rivalLevel);
}
} // namespace digivice::encounters
