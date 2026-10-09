#include "imu_filter.hpp"

#include <algorithm>
#include <cmath>

namespace digivice::motion {
namespace {
float norm(Vector3 v) { return std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z); }
bool finite(Vector3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
void add(Vector3& to, Vector3 v) { to.x += v.x; to.y += v.y; to.z += v.z; }
Vector3 subtract(Vector3 a, Vector3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
float angleX(Vector3 a) { return std::atan2(a.x, std::sqrt(a.y*a.y+a.z*a.z)); }
float angleY(Vector3 a) { return std::atan2(a.y, std::sqrt(a.x*a.x+a.z*a.z)); }
}
void ImuFilter::reset() { *this = {}; }
void ImuFilter::invalidate() {
    reading_.valid = false;
    reading_.tiltX = reading_.tiltY = 0;
    stillSamples_ = 0;
}
const ImuReading& ImuFilter::observe(Vector3 a, Vector3 g, std::uint64_t now) {
    // Duplicate timestamps cannot advance calibration. A gap or time reversal
    // requires a new neutral pose instead of integrating unknown movement.
    if (seen_ && now == reading_.observedAtMs) return reading_;
    if (seen_ && (now < reading_.observedAtMs || now-reading_.observedAtMs > 200)) reset();
    seen_ = true;
    reading_.observedAtMs = now;
    if (!finite(a) || !finite(g) || norm(a) > 7.0F || norm(g) > 900.0F) {
        invalidate(); return reading_;
    }
    reading_.accelerationG = a;
    reading_.gyroDps = subtract(g, bias_);
    reading_.valid = true;
    if (!reading_.calibrated) {
        const bool still = std::fabs(norm(a)-1.0F) <= 0.12F && norm(g) <= 5.0F &&
            (!stillSamples_ || norm(subtract(a, anchorAccel_)) <= 0.06F);
        if (!still) { stillSamples_ = 0; return reading_; }
        if (!stillSamples_) { firstStillMs_ = now; sumGyro_ = {}; sumAccel_ = {}; anchorAccel_ = a; }
        add(sumGyro_, g); add(sumAccel_, a); ++stillSamples_;
        if (stillSamples_ < 40 || now-firstStillMs_ < 1000) return reading_;
        const float n = static_cast<float>(stillSamples_);
        bias_ = {sumGyro_.x/n, sumGyro_.y/n, sumGyro_.z/n};
        neutralX_ = angleX(sumAccel_); neutralY_ = angleY(sumAccel_);
        reading_.gyroDps = subtract(g, bias_);
        reading_.calibrated = true;
    }
    // Acceleration outside the gravity band must not masquerade as a tilt.
    // 30 degrees maps to the cosmetic limit; no navigation/capture is emitted.
    constexpr float range = 0.523598776F;
    if (std::fabs(norm(a)-1.0F) < 0.25F) {
        const float x = std::clamp((angleX(a)-neutralX_)/range, -1.0F, 1.0F);
        const float y = std::clamp((angleY(a)-neutralY_)/range, -1.0F, 1.0F);
        reading_.tiltX += 0.22F*(x-reading_.tiltX);
        reading_.tiltY += 0.22F*(y-reading_.tiltY);
    }
    return reading_;
}
} // namespace digivice::motion
