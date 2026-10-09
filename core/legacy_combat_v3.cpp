// Frozen rules-3 combat. Historical replay must remain byte-for-byte compatible.
#include "legacy_combat_v3.hpp"
#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace digivice::legacy_v3::combat {
namespace {
constexpr Profile roster[4][3] = {
    {{"Mote", "grove", "Twig Tap", "Root Ram", "Seed Spark", {100,18,14,16,16}},
     {"Glint", "grove", "Briar Swipe", "Timber Rush", "Bloom Flash", {116,24,19,23,22}},
     {"Lumen", "grove", "Canopy Claw", "Ancient Crash", "Solar Bloom", {136,32,26,32,30}}},
    {{"Flicker", "neutral", "Quick Peck", "Comet Dive", "Glimmer Pulse", {88,20,12,14,12}},
     {"Flicker", "neutral", "Quick Peck", "Comet Dive", "Glimmer Pulse", {88,20,12,14,12}},
     {"Flicker", "neutral", "Quick Peck", "Comet Dive", "Glimmer Pulse", {88,20,12,14,12}}},
    {{"Rill", "tide", "Fin Slap", "River Rush", "Bubble Burst", {104,14,16,20,18}},
     {"Brine", "tide", "Reef Strike", "Breaker Bash", "Tidal Orb", {120,20,23,29,26}},
     {"Pelagia", "tide", "Trident Sweep", "Maelstrom Ram", "Abyssal Wave", {142,28,31,40,36}}},
    {{"Cinder", "ember", "Coal Claw", "Furnace Charge", "Ember Shot", {96,22,12,18,12}},
     {"Scoria", "ember", "Obsidian Slash", "Magma Crash", "Lava Lance", {112,32,18,25,18}},
     {"Pyrel", "ember", "Inferno Talon", "Volcano Break", "Phoenix Flare", {132,44,25,34,26}}}
};
// Authored prototype balance and physical/heavy skills; these are not official
// Digimon statistics or attributes. Display names stay Rookie through training.
constexpr Profile starterBase[kStarterCount] = {
    {"Impmon", "ember", "Prank Jab", "Imp Rush", "Night of Fire", {92,16,12,24,14}},
    {"Agumon", "ember", "Claw Jab", "Dino Charge", "Pepper Breath", {104,24,14,14,11}},
    {"Gabumon", "tide", "Horn Jab", "Horn Rush", "Blue Blaster", {104,20,18,13,12}},
    {"Patamon", "neutral", "Wing Slap", "Air Tackle", "Air Shot", {96,14,13,18,20}},
    {"Tentomon", "grove", "Shell Tap", "Beetle Bash", "Super Shocker", {108,16,23,12,11}},
    {"Palmon", "grove", "Vine Lash", "Root Slam", "Poison Ivy", {104,13,16,22,12}},
    {"Gomamon", "tide", "Flipper Slap", "Iceberg Rush", "Marching Fishes", {112,16,19,12,14}},
    {"Renamon", "neutral", "Palm Strike", "Fox Rush", "Diamond Storm", {92,23,12,18,13}}
};
constexpr Profile trained(Profile p, unsigned level) {
    if (level == 2) {
        p.stats.maxHp += 16; p.stats.attack += 6; p.stats.defense += 5;
        p.stats.magic += 7; p.stats.resistance += 6;
    } else if (level == 3) {
        p.stats.maxHp += 36; p.stats.attack += 14; p.stats.defense += 12;
        p.stats.magic += 16; p.stats.resistance += 14;
    }
    return p;
}
struct StarterProfiles {
    Profile values[kStarterCount][3]{};
    constexpr StarterProfiles() {
        for (unsigned s = 0; s < kStarterCount; ++s)
            for (unsigned level = 1; level <= 3; ++level)
                values[s][level - 1] = trained(starterBase[s], level);
    }
};
constexpr StarterProfiles starterProfiles;
constexpr const char* species[] = {"none", "mote", "flicker", "rill", "cinder", "impmon",
    "agumon", "gabumon", "patamon", "tentomon", "palmon", "gomamon", "renamon"};
}
bool validProfile(std::uint32_t value, std::uint32_t level) {
    return value >= 1 && value <= kSpeciesCount && level >= 1 && level <= 3 && (value != 2 || level == 1);
}
const Profile& profile(std::uint32_t value, std::uint32_t level) {
    // A bounded fallback is useful to render absent/invalid data; state validators
    // must call validProfile before accepting persisted or externally supplied IDs.
    if (value < 1 || value > kSpeciesCount) value = 1;
    if (level < 1 || level > 3) level = 1;
    if (isStarterSpecies(value)) return starterProfiles.values[value - 5][level - 1];
    return roster[value - 1][level - 1];
}
const char* speciesName(std::uint32_t value) { return value <= kSpeciesCount ? species[value] : species[0]; }
bool isStarterSpecies(std::uint32_t value) { return value >= 5 && value <= kSpeciesCount; }
std::uint32_t starterSpecies(std::uint32_t id) { return id >= 1 && id <= kStarterCount ? id + 4 : 0; }
const char* starterName(std::uint32_t id) { return starterSpecies(id) ? starterBase[id - 1].name : nullptr; }
const char* stageName(std::uint32_t value) { return isStarterSpecies(value) ? "Rookie" : nullptr; }
std::uint32_t typePercent(std::uint32_t attacker, std::uint32_t defender) {
    if (attacker < 1 || attacker > kSpeciesCount || defender < 1 || defender > kSpeciesCount) return 100;
    // Map the new profiles into the existing type cycle without changing any
    // original species pairing, including neutral and same-type matchups.
    const auto type = [](std::uint32_t s) {
        const auto* name = profile(s, 1).type;
        return std::strcmp(name, "grove") == 0 ? 1u : std::strcmp(name, "tide") == 0 ? 2u :
               std::strcmp(name, "ember") == 0 ? 3u : 0u;
    };
    const auto a = type(attacker), d = type(defender);
    if (!a || !d || a == d) return 100;
    if (a % 3 + 1 == d) return 125;
    if (d % 3 + 1 == a) return 80;
    return 100;
}
Hit resolve(std::uint32_t attackerSpecies, std::uint32_t attackerLevel,
            std::uint32_t defenderSpecies, std::uint32_t defenderLevel, Move move, Defense defense) {
    if (!validProfile(attackerSpecies, attackerLevel) || !validProfile(defenderSpecies, defenderLevel) ||
        static_cast<unsigned>(move) > 2 || static_cast<unsigned>(defense) > 3) return {0, false, 100};
    const auto& a = profile(attackerSpecies, attackerLevel).stats;
    const auto& d = profile(defenderSpecies, defenderLevel).stats;
    const auto power = move == Move::Heavy ? 16 : 8;
    const auto offense = move == Move::Magic ? a.magic : a.attack;
    const auto protection = move == Move::Magic ? d.resistance : d.defense;
    const auto difference = static_cast<int>(offense) + power - static_cast<int>(protection);
    const auto raw = static_cast<std::uint32_t>(difference < 4 ? 4 : difference);
    const auto percent = typePercent(attackerSpecies, defenderSpecies);
    auto damage = raw * percent / 100;
    const bool reflected = move == Move::Heavy && defense == Defense::Counter;
    const bool guarded = (move == Move::Physical && defense == Defense::Brace) ||
                         (move == Move::Magic && defense == Defense::Ward);
    if (guarded || reflected) damage /= 2;
    if (damage == 0) damage = 1;
    return {damage, reflected, percent};
}
std::size_t writeProfileJson(std::uint32_t value, std::uint32_t level, char* output, std::size_t capacity) {
    if (!output || !capacity) return 0;
    output[0] = '\0';
    if (!validProfile(value, level)) return 0;
    const auto& p = profile(value, level);
    const int n = std::snprintf(output, capacity,
        "{\"maxHp\":%" PRIu32 ",\"attack\":%" PRIu32 ",\"defense\":%" PRIu32
        ",\"magic\":%" PRIu32 ",\"resistance\":%" PRIu32 ",\"type\":\"%s\","
        "\"skills\":{\"physical\":\"%s\",\"heavy\":\"%s\",\"magic\":\"%s\"}}",
        p.stats.maxHp,p.stats.attack,p.stats.defense,p.stats.magic,p.stats.resistance,p.type,
        p.physicalSkill,p.heavySkill,p.magicSkill);
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) { output[0] = '\0'; return 0; }
    return static_cast<std::size_t>(n);
}
std::size_t writeCatalogJson(char* output, std::size_t capacity) {
    if (!output || !capacity) return 0;
    output[0] = '\0'; std::size_t used = 0;
    const auto append = [&](const char* text) {
        const auto length = std::strlen(text);
        if (length >= capacity - used) return false;
        std::memcpy(output + used, text, length + 1); used += length; return true;
    };
    if (!append("{\"rulesVersion\":3,\"types\":[\"grove\",\"tide\",\"ember\",\"neutral\"],\"typeChart\":["
        "{\"attacker\":\"grove\",\"strongAgainst\":\"tide\",\"weakAgainst\":\"ember\"},"
        "{\"attacker\":\"tide\",\"strongAgainst\":\"ember\",\"weakAgainst\":\"grove\"},"
        "{\"attacker\":\"ember\",\"strongAgainst\":\"grove\",\"weakAgainst\":\"tide\"},"
        "{\"attacker\":\"neutral\",\"strongAgainst\":null,\"weakAgainst\":null}],\"profiles\":[")) return 0;
    bool comma = false;
    for (std::uint32_t s = 1; s <= kSpeciesCount; ++s) for (std::uint32_t level = 1; level <= (s == 2 ? 1u : 3u); ++level) {
        char entry[640], stats[kProfileJsonCapacity];
        if (!writeProfileJson(s,level,stats,sizeof(stats))) return 0;
        const int n = std::snprintf(entry,sizeof(entry),"%s{\"species\":\"%s\",\"level\":%" PRIu32 ",\"name\":\"%s\",\"combat\":%s}",comma ? "," : "",speciesName(s),level,profile(s,level).name,stats);
        if (n < 0 || static_cast<std::size_t>(n) >= sizeof(entry) || !append(entry)) { output[0]='\0'; return 0; }
        comma = true;
    }
    if (!append("]}")) { output[0]='\0'; return 0; }
    return used;
}
std::size_t writeStarterJson(char* output, std::size_t capacity) {
    if (!output || !capacity) return 0;
    output[0] = '\0'; std::size_t used = 0;
    const auto append = [&](const char* text) {
        const auto length = std::strlen(text);
        if (length >= capacity - used) { output[0] = '\0'; return false; }
        std::memcpy(output + used, text, length + 1); used += length; return true;
    };
    if (!append("{\"formatVersion\":1,\"rulesVersion\":3,\"starters\":[")) return 0;
    for (std::uint32_t id = 1; id <= kStarterCount; ++id) {
        char entry[640], stats[kProfileJsonCapacity];
        const auto s = starterSpecies(id);
        if (!writeProfileJson(s, 1, stats, sizeof(stats))) { output[0] = '\0'; return 0; }
        const int n = std::snprintf(entry, sizeof(entry),
            "%s{\"id\":%" PRIu32 ",\"species\":\"%s\",\"name\":\"%s\",\"stage\":\"Rookie\",\"combat\":%s}",
            id == 1 ? "" : ",", id, speciesName(s), starterName(id), stats);
        if (n < 0 || static_cast<std::size_t>(n) >= sizeof(entry) || !append(entry)) { output[0] = '\0'; return 0; }
    }
    if (!append("]}")) return 0;
    return used;
}
} // namespace digivice::legacy_v3::combat
