#pragma once

#include "driver/i2c_master.h"
#include <cstdint>

namespace digivice::motion {
enum class AdapterStatus : std::uint8_t {
    Disabled, NotStarted, BusUnavailable, IoError, WrongDevice,
    RevisionUnverified, RevisionMismatch, ExternalConfigurationRequired,
    IncoherentRead, TimeReversed, Ready
};
struct CounterReading {
    AdapterStatus status = AdapterStatus::NotStarted;
    bool valid = false;
    bool externalConfigurationRequired = true;
    std::uint8_t whoAmI = 0;
    std::uint8_t revision = 0;
    std::uint32_t counter24 = 0;
    std::uint64_t observedAtMs = 0;
};

// Read-only bring-up source, not a completed physical pedometer driver. Uses a
// caller-owned shared bus and adds only this device; it never creates/deletes the
// bus, changes pins, enables/resets the sensor, or issues CTRL9 configuration.
//
// Build gates default off/unverified. QST Rev A gives conflicting revision reset
// values (0x68 in register map, 0x7c in table 21); a nonzero expected revision must
// come from the actual verified board, not the WHO_AM_I family identifier alone.
// externalConfigurationVerified additionally attests that the actual revision's
// pedometer parameters were independently configured and verified. CTRL7/CTRL8
// bits alone cannot prove correct calibration/parameters. Default begin() never
// presents the step counter as enabled. All calls belong to one owner task.
class MotionAdapter {
public:
    MotionAdapter() = default;
    ~MotionAdapter();
    MotionAdapter(const MotionAdapter&) = delete;
    MotionAdapter& operator=(const MotionAdapter&) = delete;
    AdapterStatus begin(i2c_master_bus_handle_t sharedBus, bool externalConfigurationVerified = false);
    CounterReading poll(std::uint64_t nowMs);
    void end();
    static bool buildEnabled();
    const CounterReading& status() const { return reading_; }
private:
    bool readByte(std::uint8_t address, std::uint8_t& value);
    bool configured();
    bool stableCount(std::uint32_t& value);
    i2c_master_dev_handle_t device_ = nullptr;
    CounterReading reading_{};
    bool externalVerified_ = false;
    bool clockSeen_ = false;
    std::uint64_t lastNowMs_ = 0;
};
const char* adapterStatusText(AdapterStatus status);
} // namespace digivice::motion
