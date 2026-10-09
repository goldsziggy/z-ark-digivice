#pragma once

#include "game.hpp"
#include <cstddef>

namespace digivice::assets {
struct Plan {
    char ids[4][48]{};
    std::size_t count = 0;
};
// Priority: active companion, current wild (if any), next wild, fixed cosmetic
// forest. Duplicate IDs are removed. Invalid game state produces an empty plan.
// IDs target the native s3-146-v1 catalog. Low-memory/OLED callers may omit the
// scene; this function allocates no asset pixels and makes no network requests.
// Named original test sprites are opt-in development fixtures. Production
// plans never request those packs for a current or formerly saved test form.
Plan plan(const State& state, bool developmentTestAssets = false);
} // namespace digivice::assets
