#include "capture_ring.hpp"
#include "game.hpp"
#include <cstdio>
#include <cstring>
#include <limits>

int main(int argc, char** argv) {
    using namespace digivice::capturering;
    const bool dump = argc == 2 && std::strcmp(argv[1], "--dump") == 0;
    unsigned checks = 0;
    for (uint32_t form = 0; form < 4; ++form) {
        unsigned hits = 0;
        auto previous = sample(0, form);
        for (uint32_t ms = 0; ms < kCycleMs; ++ms) {
            const auto value = sample(ms, form);
            if (value.radiusQ8 > previous.radiusQ8 || value.targetRadius != kTargetRadii[form]) return 1;
            if (value.hit) ++hits;
            digivice::FlickTrajectory trajectory;
            if (!digivice::decodeFlick(value.flickValue, trajectory) || trajectory.hit != value.hit) return 2;
            const int distance = static_cast<int>(value.radiusQ8) - value.targetRadius * 256;
            const int absolute = distance < 0 ? -distance : distance;
            const auto expected = absolute <= 12 * 256 ? Grade::Green : absolute <= 24 * 256 ? Grade::Orange : Grade::Red;
            if (value.grade != expected || value.hit != (value.grade == Grade::Green)) return 6;
            const auto repeat = sample(ms + uint64_t{kCycleMs} * 1000000, form);
            if (repeat.radiusQ8 != value.radiusQ8 || repeat.hit != value.hit || repeat.grade != value.grade) return 3;
            if (dump) std::printf("%u,%u,%u,%u,%u,%u,%s\n", form, value.phaseMs, value.radiusQ8,
                                  value.targetRadius, value.hit, value.flickValue, gradeName(value.grade));
            previous = value;
            checks += 4;
        }
        if (hits < 720 || hits > 722 || sample(kCycleMs, form).radiusQ8 != kMaxRadius * 256) return 4;
    }
    const auto extreme = sample(std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint32_t>::max());
    if (extreme.phaseMs != std::numeric_limits<uint64_t>::max() % kCycleMs || extreme.targetRadius != kTargetRadii[3]) return 5;
    if (!dump) std::printf("Capture ring: %u checks; four targets, inclusive band, existing Flick hit/miss, restart and uint64 bounds\n", checks);
    return 0;
}
