#pragma once

#include <cstddef>
#include <cstdint>

// Boss and dungeon scenarios share one catalog. A dungeon key opens one
// three-floor raid. A boss sigil opens one boss. Play is still ahead of this
// catalog: holding a key never starts a fight by itself.
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

} // namespace digivice::expeditions
