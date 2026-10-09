#pragma once
#include <cstddef>
#include <cstdint>

namespace digivice::sound {
constexpr unsigned kDefaultVolume = 15, kMaxVolume = 100;
struct Preferences {
    std::uint8_t volume = kDefaultVolume;
    bool muted = false;
    bool music = false;
};
constexpr std::size_t kSettingsBytes = 16;
struct SettingsRecord { std::uint8_t bytes[kSettingsBytes]{}; };
bool samePreferences(const Preferences&, const Preferences&);
bool encodeSettings(const Preferences&, SettingsRecord&);
bool decodeSettings(const SettingsRecord&, Preferences&);
} // namespace digivice::sound
