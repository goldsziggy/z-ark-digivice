#include "device_imu.hpp"

#include "board_hal.hpp"
#include "sdkconfig.h"
#include "esp_timer.h"

#include <cstring>

namespace digivice::device {
namespace {
// Verified vendor fda89ff2... /main/QMI8658/QMI8658.{h,c}; register/scales
// cross-checked with https://files.waveshare.com/wiki/common/QMI8658C.pdf
// Raw mode only: no CTRL9 handshake, FIFO, interrupt or pedometer programming.
constexpr std::uint8_t whoAmI = 0x00, revisionId = 0x01;
constexpr std::uint8_t ctrl1 = 0x02, ctrl2 = 0x03, ctrl3 = 0x04;
constexpr std::uint8_t ctrl5 = 0x06, ctrl6 = 0x07, ctrl7 = 0x08, axLow = 0x35;
// IDF converts transfer timeouts to whole ticks (10ms in this profile).
// A 5ms timeout becomes zero and can reject every asynchronous completion.
constexpr int timeoutMs = 20;
struct SharedLock {
    explicit SharedLock(std::uint32_t timeout = 2) : status(board::lockSharedI2c(timeout)) {}
    esp_err_t status;
    ~SharedLock() { if (status == ESP_OK) board::unlockSharedI2c(); }
};
float signedWord(const std::uint8_t* p, float scale) {
    const unsigned word = static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8);
    const int value = word >= 32768 ? static_cast<int>(word)-65536 : static_cast<int>(word);
    return static_cast<float>(value)*scale;
}
}
esp_err_t Imu::read(std::uint8_t reg, std::uint8_t* data, std::size_t size) {
    return i2c_master_transmit_receive(device_, &reg, 1, data, size, timeoutMs);
}
esp_err_t Imu::write(std::uint8_t reg, std::uint8_t value) {
    const std::uint8_t bytes[]{reg, value};
    return i2c_master_transmit(device_, bytes, sizeof(bytes), timeoutMs);
}
esp_err_t Imu::begin() {
#if !defined(CONFIG_DIGIVICE_AUDIO_MOTION) || !CONFIG_DIGIVICE_AUDIO_MOTION
    return error_ = ESP_ERR_NOT_SUPPORTED;
#else
    if (configured_) return error_;
    if (board::selectedProfile().id != board::ProfileId::Waveshare146 || !board::sharedI2cBus())
        return error_ = ESP_ERR_NOT_SUPPORTED;
    SharedLock lock(20);
    if (lock.status != ESP_OK) return error_ = lock.status;
    if (!device_) {
        i2c_device_config_t config{};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = 0x6b;
        config.scl_speed_hz = 400000;
        error_ = i2c_master_bus_add_device(board::sharedI2cBus(), &config, &device_);
        if (error_ != ESP_OK) return error_;
    }
    std::uint8_t identity = 0, interface = 0;
    if ((error_ = read(whoAmI, &identity, 1)) != ESP_OK ||
        (error_ = read(revisionId, &revision_, 1)) != ESP_OK) return error_;
    if (identity != 0x05) return error_ = ESP_ERR_NOT_FOUND;
    if ((error_ = read(ctrl1, &interface, 1)) != ESP_OK) return error_;
    // Disable channels during configuration; auto-increment, oscillator on.
    // +/-4g and +/-512dps at 62.5Hz. Software filters avoid vendor LPF masking
    // bugs. Preserve interface reserved bits; SPI endian doesn't affect I2C.
    const std::uint8_t registers[]{ctrl7, ctrl1, ctrl2, ctrl3, ctrl5, ctrl6, ctrl7};
    const std::uint8_t values[]{0x00, static_cast<std::uint8_t>((interface|0x40)&0xfe),
                                0x17, 0x57, 0x00, 0x00, 0x43};
    for (unsigned i = 0; i < sizeof(registers); ++i) {
        if ((error_ = write(registers[i], values[i])) != ESP_OK) break;
        std::uint8_t got = 0;
        if ((error_ = read(registers[i], &got, 1)) != ESP_OK) break;
        if (got != values[i]) { error_ = ESP_ERR_INVALID_RESPONSE; break; }
    }
    if (error_ != ESP_OK) { (void)write(ctrl7, 0x00); return error_; }
    configured_ = true; paused_ = false; failures_ = 0;
    filter_.reset();
    if (xTaskCreate(taskEntry, "digivice-imu", 4096, this, 3, &task_) != pdPASS) {
        configured_ = false;
        (void)write(ctrl7, 0x00);
        return error_ = ESP_ERR_NO_MEM;
    }
    return ESP_OK;
#endif
}
const motion::ImuReading& Imu::poll(std::uint64_t now) {
    portENTER_CRITICAL(&publishLock_);
    ownerReading_ = publishedReading_;
    portEXIT_CRITICAL(&publishLock_);
    if (paused_.load() || now < ownerReading_.observedAtMs || now-ownerReading_.observedAtMs > 200)
        ownerReading_.valid = false;
    return ownerReading_;
}
motion::StepReading Imu::stepReading() const {
    portENTER_CRITICAL(&publishLock_);
    const auto result = publishedSteps_;
    portEXIT_CRITICAL(&publishLock_);
    return result;
}
void Imu::publish() {
    portENTER_CRITICAL(&publishLock_);
    publishedReading_ = filter_.reading();
    publishedSteps_ = pedometer_.reading();
    portEXIT_CRITICAL(&publishLock_);
}
void Imu::sample(std::uint64_t now) {
    std::uint8_t a[12]{}, b[12]{};
    {
        SharedLock lock;
        // A busy shared bus is a skipped sample, not three permanent failures.
        if (lock.status != ESP_OK) { (void)pedometer_.poll(now); return; }
        error_ = read(axLow, a, sizeof(a));
        if (error_ == ESP_OK) error_ = read(axLow, b, sizeof(b));
    }
    if (error_ != ESP_OK) {
        ++failures_; filter_.invalidate(); (void)pedometer_.poll(now);
        if (failures_ >= 3) (void)pedometer_.invalidate();
        return;
    }
    failures_ = 0;
    // Two equal consecutive bursts reject a read crossing the 16ms sensor
    // update boundary. Skip mismatches, without blocking or inventing a sample.
    if (std::memcmp(a, b, sizeof(a))) { filter_.invalidate(); (void)pedometer_.poll(now); return; }
    const motion::Vector3 accel{signedWord(a, 4.0F/32768), signedWord(a+2, 4.0F/32768), signedWord(a+4, 4.0F/32768)};
    const motion::Vector3 gyro{signedWord(a+6, 512.0F/32768), signedWord(a+8, 512.0F/32768), signedWord(a+10, 512.0F/32768)};
    (void)filter_.observe(accel, gyro, now);
    (void)pedometer_.observe(accel, now);
}
esp_err_t Imu::pause(bool paused) {
    if (paused_.exchange(paused) != paused) ++epoch_;
    return ESP_OK; // Request accepted; quiescent() acknowledges actual sensor stop.
}
void Imu::taskEntry(void* context) { static_cast<Imu*>(context)->run(); }
void Imu::run() {
    bool enabled = true;
    std::uint32_t handledEpoch = epoch_.load(), centered = recenterEpoch_.load();
    for (;;) {
        const auto now = static_cast<std::uint64_t>(esp_timer_get_time()/1000);
        const auto epoch = epoch_.load();
        const bool paused = paused_.load();
        if (epoch != handledEpoch) {
            filter_.reset();
            (void)pedometer_.pause(true, now); // Preserve count; discard only an unconfirmed stride.
            if (!paused) (void)pedometer_.pause(false, now);
            failures_ = 0;
            handledEpoch = epoch;
        }
        if (paused == enabled) {
            active_ = true;
            {
                SharedLock lock(20);
                error_ = lock.status == ESP_OK ? write(ctrl7, paused ? 0x00 : 0x43) : lock.status;
            }
            active_ = false;
            if (error_ == ESP_OK) enabled = !paused;
        }
        if (paused) {
            filter_.invalidate(); (void)pedometer_.pause(true, now);
            publish();
            if (!enabled && epoch == epoch_.load() && paused_.load()) settledEpoch_ = epoch;
        } else if (enabled && epoch == epoch_.load() && !paused_.load()) {
            if (centered != recenterEpoch_.load()) { centered = recenterEpoch_.load(); filter_.reset(); }
            active_ = true;
            if (failures_ < 3) sample(now);
            else { filter_.invalidate(); (void)pedometer_.poll(now); }
            active_ = false;
            publish();
        }
        // Never catch up with fabricated samples after lock/transaction latency.
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
} // namespace digivice::device
