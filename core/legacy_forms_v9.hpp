// Frozen rules9/schema12 from e6f3523 before two-profile correction.
#pragma once

#include <cstddef>
#include <cstdint>
#include "legacy_combat_v9.hpp"

// Native, immutable game content. Official species names/stages are sourced in
// docs/evidence/digivolution-roster-research.json. Edges, types, numeric balance
// and new skill names are authored prototype rules, not official franchise data.
namespace digivice::legacy_v9::forms {

constexpr std::uint32_t kPreservedFormCount = 66;
constexpr std::uint32_t kFormCount = 276; // Validated against the generated catalog.
constexpr std::uint32_t kCatalogVersion = 5;
constexpr std::uint32_t kMaxRpgLevel = 20;
enum class Stage : std::uint8_t { Original, Rookie, Champion, Ultimate, Mega, Fresh, InTraining, Armor, NoLevel };
enum class CombatTier : std::uint8_t { Fresh, InTraining, Rookie, Champion, Ultimate, Mega, Unknown = 255 };

struct Form {
    std::uint16_t id;
    std::uint16_t lineage; // Stable profile family identity; never re-rooted by graph changes.
    Stage stage;
    std::uint16_t parent;
    std::uint16_t children[2]; // Historical family metadata; use outgoing() for legal routes.
    std::uint8_t minLevel;
    std::uint8_t minBond;
    const char* name;
    const char* type;
    const char* physicalSkill;
    const char* heavySkill;
    const char* magicSkill;
    const char* artId; // Original sprite ID, or nullptr: artwork is unavailable.
    combat::Stats baseStats; // At minLevel, except preserved Rookie interpolation below.
    combat::Stats growth;
    CombatTier authoredTier = CombatTier::Unknown; // Old66 derive from their preserved stage/entry.
};

// Preserved IDs: originals 1..10; eight seven-form starter blocks 11..66. Each
// block is Rookie, Champion A, Ultimate A, Mega A, Champion B, Ultimate B, Mega B.
// Generated DS additions occupy 67..276; the ID ledger is append-only.
// No allocation, mutable cache, hardware or filesystem dependencies.
const Form* find(std::uint32_t formId);
struct EvolutionEdge { std::uint16_t from, to; std::uint8_t minLevel, minBond; };
// Curated routes are independent of profile families and stat anchors. A form
// can have several parents, with at most two outgoing choices. No heap use.
std::size_t edgeCount();
const EvolutionEdge* edgeAt(std::size_t index);
const EvolutionEdge* outgoing(std::uint32_t formId, std::uint32_t index);
bool canReach(std::uint32_t from, std::uint32_t to); // Includes a valid form reaching itself.
bool validForLineage(std::uint32_t formId, std::uint32_t lineage);
std::uint32_t initialForm(std::uint32_t lineage); // Zero for an unknown lineage.
std::uint32_t migrateLegacyForm(std::uint32_t lineage, std::uint32_t appearanceLevel);
const char* stageName(Stage stage); // Null for an invalid enum value.
CombatTier combatTier(std::uint32_t formId);
const char* combatTierName(CombatTier tier);
const char* lineageSlug(std::uint32_t lineage); // Null for unknown lineage.
std::uint32_t lineageFromSlug(const char* slug); // Zero for unknown slug.
struct CatalogEntry { std::uint16_t formId; const char* entryKey; const char* displayName; const char* role; };
const CatalogEntry* catalogEntry(std::uint32_t formId); // Null for old forms absent from DS inventory.
bool encounterObtainable(std::uint32_t formId);
const char* leafReason(std::uint32_t formId); // Null while an outgoing route exists.
const char* evolutionStatus(std::uint32_t formId); // progression, terminal, independent; null if invalid.

// Zero stats for invalid ID/level, level below the form's entry, or level > 20.
// Preserved Rookie stats interpolate exact old bonuses at levels 1/5/10, then
// use growth above 10. Other forms grow after their minLevel anchor.
// Eligibility also needs the current parent, bond and game phase: caller-owned.
combat::Stats stats(std::uint32_t formId, std::uint32_t rpgLevel);

} // namespace digivice::legacy_v9::forms
