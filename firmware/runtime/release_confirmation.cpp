#include "release_confirmation.hpp"
#include <cstddef>
#include <cstring>

namespace digivice::controls {
bool parseReleaseConfirmation(const char* input, std::uint32_t& memberId) {
    if (!input) return false;
    std::size_t length = 0;
    while (length < 48 && input[length]) ++length;
    if (!length || length == 48) return false;
    const char* p = input;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p < '0' || *p > '9') return false;
    std::uint32_t value = 0;
    while (*p >= '0' && *p <= '9') {
        const auto digit = static_cast<unsigned>(*p++ - '0');
        if (value > (UINT32_MAX - digit) / 10u) return false;
        value = value * 10u + digit;
    }
    if (!value || (*p != ' ' && *p != '\t')) return false;
    while (*p == ' ' || *p == '\t') ++p;
    if (std::strncmp(p, "confirm", 7)) return false;
    p += 7;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p) return false;
    memberId = value;
    return true;
}
} // namespace digivice::controls
