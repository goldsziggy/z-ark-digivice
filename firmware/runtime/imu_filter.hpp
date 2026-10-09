#pragma once

#include <cstdint>

namespace digivice::motion {

struct Vector3 { float x = 0, y = 0, z = 0; };
struct ImuReading {
    Vector3 accelerationG{};
    Vector3 gyroDps{}; // Bias-corrected after still-position calibration.
    float tiltX = 0, tiltY = 0; // Relative sensor-frame tilt, bounded [-1,1].
    std::uint64_t observedAtMs = 0;
    bool valid = false;
    bool calibrated = false;
};

// Cosmetic input only: never counts steps or commits game commands. Sensor axes
// intentionally remain unrotated until the physical screen orientation is tested.
// Recenter asks for 1 second / >=40 still samples; movement restarts that window.
class ImuFilter {
public:
    const ImuReading& observe(Vector3 accelerationG, Vector3 gyroDps, std::uint64_t nowMs);
    const ImuReading& reading() const { return reading_; }
    void reset();
    void invalidate();
private:
    ImuReading reading_{};
    Vector3 bias_{}, sumGyro_{}, sumAccel_{}, anchorAccel_{};
    float neutralX_ = 0, neutralY_ = 0;
    std::uint64_t firstStillMs_ = 0;
    unsigned stillSamples_ = 0;
    bool seen_ = false;
};

} // namespace digivice::motion
