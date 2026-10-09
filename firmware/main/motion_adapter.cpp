#include "motion_adapter.hpp"

#include "sdkconfig.h"
#include "esp_err.h"

// References: QST QMI8658C Rev A (2022), table 21, CTRL7/CTRL8 table 22,
// sections 6.2 and 11.5. The actual board vendor uses address 0x6b. Board HAL
// supplies its verified shared bus; no GPIO ownership is inferred here.
namespace digivice::motion {
namespace {
constexpr std::uint8_t kAddress = 0x6b;
constexpr std::uint8_t kWhoAmI = 0x00, kRevision = 0x01;
constexpr std::uint8_t kCtrl7 = 0x08, kCtrl8 = 0x09;
constexpr std::uint8_t kCountLow = 0x5a, kCountMid = 0x5b, kCountHigh = 0x5c;
constexpr int kTransactionTimeoutMs = 10;
#if defined(CONFIG_DIGIVICE_QMI_EXPECTED_REVISION)
constexpr unsigned kExpectedRevision = CONFIG_DIGIVICE_QMI_EXPECTED_REVISION;
#else
constexpr unsigned kExpectedRevision = 0;
#endif
static_assert(kExpectedRevision <= 255, "QMI revision must fit one register");
}
bool MotionAdapter::buildEnabled() {
#if defined(CONFIG_DIGIVICE_QMI_PEDOMETER) && CONFIG_DIGIVICE_QMI_PEDOMETER
    return true;
#else
    return false;
#endif
}
MotionAdapter::~MotionAdapter() { end(); }
void MotionAdapter::end() {
    if (device_) i2c_master_bus_rm_device(device_);
    device_ = nullptr;
    externalVerified_ = false; clockSeen_ = false; lastNowMs_ = 0;
    reading_ = {};
}
bool MotionAdapter::readByte(std::uint8_t address, std::uint8_t& value) {
    if (!device_) return false;
    // One explicitly addressed register per bounded transaction. Neither CTRL1
    // address auto-increment nor data-endian defaults are assumed.
    return i2c_master_transmit_receive(device_, &address, 1, &value, 1, kTransactionTimeoutMs) == ESP_OK;
}
bool MotionAdapter::configured() {
    std::uint8_t ctrl7 = 0, ctrl8 = 0;
    if (!readByte(kCtrl7, ctrl7) || !readByte(kCtrl8, ctrl8)) {
        reading_.status = AdapterStatus::IoError; return false;
    }
    // Pedometer requires non-SyncSample, enabled accelerometer, and Pedo_EN.
    // aEN is CTRL7 bit0 per the register table; section 11.6 has a bit1 typo.
    if (!externalVerified_ || (ctrl7 & 0x80) || !(ctrl7 & 0x01) || !(ctrl8 & 0x10)) {
        reading_.status = AdapterStatus::ExternalConfigurationRequired;
        reading_.externalConfigurationRequired = true;
        return false;
    }
    reading_.externalConfigurationRequired = false;
    return true;
}
AdapterStatus MotionAdapter::begin(i2c_master_bus_handle_t sharedBus, bool externalConfigurationVerified) {
    end();
    if (!buildEnabled()) return reading_.status = AdapterStatus::Disabled;
    if (!sharedBus) return reading_.status = AdapterStatus::BusUnavailable;
    i2c_device_config_t deviceConfig{};
    deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    deviceConfig.device_address = kAddress;
    deviceConfig.scl_speed_hz = 100000;
    if (i2c_master_bus_add_device(sharedBus, &deviceConfig, &device_) != ESP_OK) {
        device_ = nullptr; return reading_.status = AdapterStatus::IoError;
    }
    if (!readByte(kWhoAmI, reading_.whoAmI) || !readByte(kRevision, reading_.revision))
        return reading_.status = AdapterStatus::IoError;
    if (reading_.whoAmI != 0x05) return reading_.status = AdapterStatus::WrongDevice;
    if (kExpectedRevision == 0) return reading_.status = AdapterStatus::RevisionUnverified;
    if (reading_.revision != kExpectedRevision) return reading_.status = AdapterStatus::RevisionMismatch;
    externalVerified_ = externalConfigurationVerified;
    if (!configured()) return reading_.status;
    return reading_.status = AdapterStatus::Ready;
}
bool MotionAdapter::stableCount(std::uint32_t& value) {
    // The datasheet does not promise a latched atomic 3-byte step read. Require
    // two identical fully addressed samples; retry at most twice if an update
    // races the read. Never expose a differing/torn candidate to MotionCounter.
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        std::uint8_t a[3]{}, b[3]{};
        const std::uint8_t addresses[] = {kCountLow, kCountMid, kCountHigh};
        for (unsigned i = 0; i < 3; ++i) if (!readByte(addresses[i], a[i])) {
            reading_.status = AdapterStatus::IoError; return false;
        }
        for (unsigned i = 0; i < 3; ++i) if (!readByte(addresses[i], b[i])) {
            reading_.status = AdapterStatus::IoError; return false;
        }
        if (a[0] == b[0] && a[1] == b[1] && a[2] == b[2]) {
            value = static_cast<std::uint32_t>(a[0]) | (static_cast<std::uint32_t>(a[1]) << 8) |
                    (static_cast<std::uint32_t>(a[2]) << 16);
            return true;
        }
    }
    reading_.status = AdapterStatus::IncoherentRead;
    return false;
}
CounterReading MotionAdapter::poll(std::uint64_t nowMs) {
    reading_.valid = false; reading_.counter24 = 0;
    if (clockSeen_ && nowMs < lastNowMs_) { reading_.status = AdapterStatus::TimeReversed; return reading_; }
    clockSeen_ = true; lastNowMs_ = nowMs; reading_.observedAtMs = nowMs;
    if (!buildEnabled()) { reading_.status = AdapterStatus::Disabled; return reading_; }
    if (!device_ || reading_.whoAmI != 0x05 || kExpectedRevision == 0 ||
        reading_.revision != kExpectedRevision) return reading_;
    if (!configured()) return reading_;
    std::uint32_t count;
    if (!stableCount(count)) return reading_;
    reading_.counter24 = count; reading_.valid = true; reading_.status = AdapterStatus::Ready;
    return reading_;
}
const char* adapterStatusText(AdapterStatus status) {
    switch (status) {
    case AdapterStatus::Disabled: return "QMI pedometer reader disabled at build time";
    case AdapterStatus::NotStarted: return "QMI reader not started";
    case AdapterStatus::BusUnavailable: return "shared I2C bus unavailable";
    case AdapterStatus::IoError: return "QMI I2C transaction failed or timed out";
    case AdapterStatus::WrongDevice: return "QMI WHO_AM_I mismatch";
    case AdapterStatus::RevisionUnverified: return "QMI revision needs bench verification";
    case AdapterStatus::RevisionMismatch: return "QMI revision differs from verified build setting";
    case AdapterStatus::ExternalConfigurationRequired: return "QMI pedometer requires externally verified configuration";
    case AdapterStatus::IncoherentRead: return "QMI step counter changed during bounded read";
    case AdapterStatus::TimeReversed: return "QMI poll timestamp reversed";
    case AdapterStatus::Ready: return "QMI counter reader ready; physical accuracy unverified";
    }
    return "unknown QMI reader status";
}
} // namespace digivice::motion
