// Frozen rules8/schema11 from 16feedd before two-profile correction.
#pragma once
#include <cstddef>
#include <cstdint>

namespace digivice::legacy_v8::combat {
constexpr std::size_t kProfileJsonCapacity = 512;
constexpr std::size_t kCatalogJsonCapacity = 8192; // Bounded initial roster / one lineage tree; host output.
constexpr std::size_t kStarterJsonCapacity = 4096;
constexpr std::size_t kCatalogPageJsonCapacity = 6144;
constexpr std::size_t kFormCatalogJsonCapacity = 6144;
constexpr std::size_t kEvolutionGraphJsonCapacity = 16384; // Host-only bounded page, never device state.
constexpr std::uint32_t kStarterCount = 8;
constexpr std::uint32_t kSpeciesCount = 12;
// Species IDs match the game enum. Original IDs 1–4 remain immutable; starters
// occupy 5–12. Lineage and current form are independent of RPG level 1–20.
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
Profile profile(std::uint32_t species, std::uint32_t level);
bool validFormProfile(std::uint32_t formId, std::uint32_t level);
Profile formProfile(std::uint32_t formId, std::uint32_t level);
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
// Optional bounded raw floor is applied before type and guard/reflection.
// The default preserves wild combat and saved practice rules; outside4..32 is invalid.
Hit resolveForms(std::uint32_t attackerForm, std::uint32_t attackerLevel,
                 std::uint32_t defenderForm, std::uint32_t defenderLevel,
                 Move move, Defense defense, std::uint32_t minimumRawDamage = 4);
std::size_t writeFormProfileJson(std::uint32_t formId, std::uint32_t level, char* output, std::size_t capacity);
std::size_t writeEvolutionJson(std::uint32_t species, char* output, std::size_t capacity);
std::size_t writeEvolutionGraphJson(std::uint32_t formId, std::uint32_t offset,
    std::uint32_t limit, char* output, std::size_t capacity);
std::size_t writeProfileJson(std::uint32_t species, std::uint32_t level, char* output, std::size_t capacity);
std::size_t writeCatalogJson(char* output, std::size_t capacity);
std::size_t writeStarterJson(char* output, std::size_t capacity);
// Brief pages, never an unbounded all-roster response. Limit must be1..16.
std::size_t writeCatalogPageJson(std::uint32_t offset, std::uint32_t limit, char* output, std::size_t capacity);
std::size_t writeFormCatalogJson(std::uint32_t formId, char* output, std::size_t capacity);
} // namespace digivice::legacy_v8::combat
