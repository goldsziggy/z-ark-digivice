#pragma once

#include <cstddef>
#include <cstdint>

// Boss and dungeon scenarios share one catalog. A dungeon key opens one
// three-floor raid. A boss sigil opens one boss. The device menu can open a
// waiting lobby or a solo start. Neither spends a key or sigil, and neither
// begins the raid fight. Holding a key never starts a fight by itself.
// The six names below stay in saves and state JSON. A device visit may show
// another title from that theme's pool.
namespace digivice::expeditions {

enum class Theme : std::uint8_t { Grove, Tide, Ember };
enum class Kind : std::uint8_t { Dungeon, Boss };

struct Scenario {
    const char* name;
    Kind kind;
    Theme theme;
    std::uint8_t floors;
    std::uint8_t minPlayers;
    std::uint8_t maxPlayers;
};

inline constexpr Scenario kScenarios[] = {
    {"Grove Dungeon", Kind::Dungeon, Theme::Grove, 3, 2, 2},
    {"Tide Dungeon", Kind::Dungeon, Theme::Tide, 3, 2, 2},
    {"Ember Dungeon", Kind::Dungeon, Theme::Ember, 3, 2, 2},
    {"Grove Boss", Kind::Boss, Theme::Grove, 1, 1, 2},
    {"Tide Boss", Kind::Boss, Theme::Tide, 1, 1, 2},
    {"Ember Boss", Kind::Boss, Theme::Ember, 1, 1, 2},
};

constexpr std::uint32_t kMaxDungeonKeys = 3;
constexpr std::uint32_t kMaxBossSigils = 1;
constexpr std::uint32_t kDungeonWinsPerKey = 8;
constexpr std::uint32_t kBossStepsPerSigil = 5000;

inline const char* themeName(Theme theme) {
    return theme == Theme::Grove ? "grove" : theme == Theme::Tide ? "tide" : "ember";
}
inline const char* kindName(Kind kind) { return kind == Kind::Dungeon ? "dungeon" : "boss"; }

// Presentation titles. Index 0 is the saved catalog name. The rest are visit variants.
inline void variantList(Kind kind, Theme theme, const char* const*& names, std::uint8_t& count) {
    static constexpr const char* groveDungeon[]{"Grove Dungeon","MOSS HOLLOW","THORN VAULT","FERN CRYPT","ROOT SANCTUM","BRIAR DEPTHS","CANOPY RUIN","VERDANT GALLERY"};
    static constexpr const char* tideDungeon[]{"Tide Dungeon","CORAL VAULT","REEF CRYPT","KELP HOLLOW","BRINE DEPTHS","SHELL SANCTUM","FOAM GALLERY","ABYSS STAIR"};
    static constexpr const char* emberDungeon[]{"Ember Dungeon","CINDER VAULT","ASH CRYPT","MAGMA HOLLOW","COAL DEPTHS","FLAME SANCTUM","SPARK GALLERY","BASALT RUIN"};
    static constexpr const char* groveBoss[]{"Grove Boss","MOSS WARDEN","THORN TYRANT","ROOT COLOSSUS","BRIAR BEAST","CANOPY LORD","FERN GIANT","VERDANT KING"};
    static constexpr const char* tideBoss[]{"Tide Boss","CORAL WARDEN","REEF TYRANT","KELP COLOSSUS","BRINE BEAST","SHELL LORD","FOAM GIANT","ABYSS KING"};
    static constexpr const char* emberBoss[]{"Ember Boss","CINDER WARDEN","ASH TYRANT","MAGMA COLOSSUS","COAL BEAST","FLAME LORD","SPARK GIANT","BASALT KING"};
    const bool dungeon = kind == Kind::Dungeon;
    const auto* list = theme == Theme::Tide ? (dungeon ? tideDungeon : tideBoss) : theme == Theme::Ember ? (dungeon ? emberDungeon : emberBoss) : (dungeon ? groveDungeon : groveBoss);
    names = list;
    count = 8;
}
inline std::uint8_t variantCount(const Scenario& scenario) {
    const char* const* names = nullptr;
    std::uint8_t count = 0;
    variantList(scenario.kind, scenario.theme, names, count);
    return count;
}
inline const char* variantName(const Scenario& scenario, std::uint8_t index) {
    const char* const* names = nullptr;
    std::uint8_t count = 0;
    variantList(scenario.kind, scenario.theme, names, count);
    return count ? names[index % count] : scenario.name;
}

} // namespace digivice::expeditions
