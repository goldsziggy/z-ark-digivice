#pragma once
#include <cstdint>

namespace digivice::controls {
// Parse only the arguments after "release": "<stable member ID> confirm".
// No implicit confirmation, partial number, trailing token, or ID zero.
// Output stays unchanged on error. No game/storage/UI dependencies.
bool parseReleaseConfirmation(const char* input, std::uint32_t& memberId);
} // namespace digivice::controls
