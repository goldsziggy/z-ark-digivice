#pragma once
#include "idle.hpp"
#include <cstddef>
#include <cstdint>

namespace digivice::idle {
constexpr std::size_t kSettingsBytes = 16;
struct SettingsRecord { std::uint8_t bytes[kSettingsBytes]{}; };
bool encodeSettings(std::uint32_t seconds, SettingsRecord&);
bool decodeSettings(const SettingsRecord&, std::uint32_t& seconds);
} // namespace digivice::idle
