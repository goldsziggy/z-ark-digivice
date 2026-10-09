// Frozen rules-3 combat. Historical replay must remain byte-for-byte compatible.
#pragma once
#include <cstddef>
#include <cstdint>

namespace digivice::legacy_v3::combat {
constexpr std::size_t kProfileJsonCapacity = 512;
constexpr std::size_t kCatalogJsonCapacity = 8192; // 7,708-byte catalog; host output, never a device frame buffer.
constexpr std::size_t kStarterJsonCapacity = 4096;
constexpr std::uint32_t kStarterCount = 8;
constexpr std::uint32_t kSpeciesCount = 12;
// Species IDs match the game enum. Original IDs 1–4 remain immutable; starters
// occupy 5–12. Rookie is a stage, independent of numeric training level 1–3.
struct Stats { std::uint32_t maxHp, attack, defense, magic, resistance; };
struct Profile {
    const char* name;
    const char* type;
    const char* physicalSkill;
    const char* heavySkill;
    const char* magicSkill;
    Stats stats;
};
enum class Move : std::uint8_t { Physical, Heavy, Magic };
enum class Defense : std::uint8_t { None, Brace, Counter, Ward };
struct Hit { std::uint32_t damage; bool reflected; std::uint32_t typePercent; };
bool validProfile(std::uint32_t species, std::uint32_t level);
const Profile& profile(std::uint32_t species, std::uint32_t level);
const char* speciesName(std::uint32_t species);
bool isStarterSpecies(std::uint32_t species);
std::uint32_t starterSpecies(std::uint32_t starterId); // Invalid IDs return 0.
const char* starterName(std::uint32_t starterId); // Invalid IDs return nullptr.
const char* stageName(std::uint32_t species); // Original roster / absent: nullptr.
std::uint32_t typePercent(std::uint32_t attackerSpecies, std::uint32_t defenderSpecies);
// Cards remain stateful callers' responsibility: Spark +5 after this resolver,
// only on ordinary outgoing damage; Shelter absorbs received damage afterward.
Hit resolve(std::uint32_t attackerSpecies, std::uint32_t attackerLevel,
            std::uint32_t defenderSpecies, std::uint32_t defenderLevel,
            Move move, Defense defense);
std::size_t writeProfileJson(std::uint32_t species, std::uint32_t level, char* output, std::size_t capacity);
std::size_t writeCatalogJson(char* output, std::size_t capacity);
std::size_t writeStarterJson(char* output, std::size_t capacity);
} // namespace digivice::legacy_v3::combat
