#pragma once

#include <cstddef>
#include <cstdint>
#include "combat.hpp"

// Native, immutable game content. Official species names/stages are sourced in
// docs/evidence/digivolution-roster-research.json. Edges, types, numeric balance
// and new skill names are authored prototype rules, not official franchise data.
namespace digivice::forms {

constexpr std::uint32_t kPreservedFormCount = 66;
constexpr std::uint32_t kFormCount = 276; // Stable ID space includes decode-only original fixtures.
constexpr std::uint32_t kFirstProductionFormId = 11;
constexpr std::uint32_t kProductionFormCount = 266;
constexpr std::uint32_t kCatalogVersion = 6;
constexpr std::uint32_t kMaxRpgLevel = 50;
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
// Rules 16 display and eligibility. Stored edge gates remain the historical floor.
struct EvolutionNeed { std::uint8_t level, bond, care; };
// Stricter than the stored edge. Source entry level/bond and zero care are never enough.
constexpr EvolutionNeed evolutionNeedFor(std::uint8_t srcLevel, std::uint8_t srcBond, Stage dest,
    std::uint8_t destLevel, std::uint8_t destBond, std::uint8_t edgeLevel, std::uint8_t edgeBond) {
    std::uint32_t levelFloor = 8, bondFloor = 24, care = 16;
    switch (dest) {
    case Stage::Fresh: levelFloor = 4; bondFloor = 16; care = 12; break;
    case Stage::InTraining: levelFloor = 8; bondFloor = 28; care = 20; break;
    case Stage::Rookie: levelFloor = 12; bondFloor = 40; care = 28; break;
    case Stage::Champion: levelFloor = 18; bondFloor = 64; care = 40; break;
    case Stage::Ultimate: levelFloor = 28; bondFloor = 96; care = 56; break;
    case Stage::Mega: levelFloor = 40; bondFloor = 140; care = 72; break;
    case Stage::Armor: levelFloor = 22; bondFloor = 72; care = 44; break;
    default: break;
    }
    std::uint32_t level = edgeLevel;
    if (level < levelFloor) level = levelFloor;
    if (level < destLevel) level = destLevel;
    if (level < static_cast<std::uint32_t>(srcLevel) + 1) level = static_cast<std::uint32_t>(srcLevel) + 1;
    if (level > kMaxRpgLevel) level = kMaxRpgLevel;
    std::uint32_t bond = edgeBond;
    if (bond < bondFloor) bond = bondFloor;
    if (bond < destBond) bond = destBond;
    if (srcBond < 200 && bond < static_cast<std::uint32_t>(srcBond) + 1) bond = static_cast<std::uint32_t>(srcBond) + 1;
    if (bond > 200) bond = 200;
    if (care < 12) care = 12;
    if (care > 100) care = 100;
    return {static_cast<std::uint8_t>(level), static_cast<std::uint8_t>(bond), static_cast<std::uint8_t>(care)};
}
inline EvolutionNeed evolutionNeed(const EvolutionEdge& edge) {
    const auto* dest = find(edge.to);
    const auto* src = find(edge.from);
    if (!dest || !src) return {static_cast<std::uint8_t>(kMaxRpgLevel), 200, 100};
    return evolutionNeedFor(src->minLevel, src->minBond, dest->stage, dest->minLevel, dest->minBond, edge.minLevel, edge.minBond);
}
// IDs1..10 remain for old saves and frozen replays, never new production pools.
bool productionForm(std::uint32_t formId);
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

// Zero stats for invalid ID/level, level below the form's entry, or level > 50.
// Levels 1–20 keep the previous curve; 21–50 continue that same per-level growth.
// Preserved Rookie stats interpolate exact old bonuses at levels 1/5/10, then
// use growth above 10. Other forms grow after their minLevel anchor.
// Eligibility also needs the current parent, bond and game phase: caller-owned.
combat::Stats stats(std::uint32_t formId, std::uint32_t rpgLevel);

} // namespace digivice::forms
