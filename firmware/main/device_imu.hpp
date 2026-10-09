#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "imu_filter.hpp"
#include "pedometer.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>

namespace digivice::device {

// One lifetime sampler owns the sensor/filter after begin. The UI owns poll's
// copied tilt view; stepReading is a coherent cross-task snapshot. No NVS here.
// Uses the existing board bus; never installs another bus.
class Imu {
public:
    esp_err_t begin();
    const motion::ImuReading& poll(std::uint64_t nowMs);
    esp_err_t pause(bool paused);
    void recenter() { ++recenterEpoch_; }
    bool ready() const { return configured_ && !paused_.load() && !recovering_.load() && sampleReady_.load() && error_.load() == ESP_OK; }
    bool recovering() const { return recovering_.load(); }
    std::uint32_t recoveryAttempts() const { return recoveryAttempts_.load(); }
    bool quiescent() const {
        return !configured_ ? stoppedWithoutWorker_ :
            (paused_.load() && settledEpoch_.load() == epoch_.load() && !active_.load());
    }
    esp_err_t lastError() const { return error_.load(); }
    std::uint8_t revision() const { return revision_.load(); }
    const motion::ImuReading& reading() const { return ownerReading_; }
    motion::StepReading stepReading() const;
private:
    static void taskEntry(void* context);
    void run();
    void sample(std::uint64_t nowMs);
    esp_err_t configure(std::uint32_t expectedEpoch);
    void beginRecovery(std::uint64_t nowMs);
    void publish();
    esp_err_t read(std::uint8_t reg, std::uint8_t* data, std::size_t size);
    esp_err_t write(std::uint8_t reg, std::uint8_t value);
    i2c_master_dev_handle_t device_ = nullptr;
    motion::ImuFilter filter_{};
    motion::Pedometer pedometer_{};
    motion::ImuReading ownerReading_{}, publishedReading_{};
    motion::StepReading publishedSteps_{};
    mutable portMUX_TYPE publishLock_ = portMUX_INITIALIZER_UNLOCKED;
    TaskHandle_t task_ = nullptr;
    std::atomic<esp_err_t> error_{ESP_ERR_INVALID_STATE};
    std::atomic<std::uint32_t> epoch_{0}, settledEpoch_{0}, recenterEpoch_{0};
    std::atomic<std::uint32_t> recoveryAttempts_{0};
    std::atomic<bool> paused_{false}, active_{false}, recovering_{false}, sampleReady_{false};
    std::atomic<std::uint8_t> revision_{0};
    std::uint64_t nextRecoveryMs_ = 0;
    std::uint32_t recoveryDelayMs_ = 1000;
    unsigned failures_ = 0;
    bool recoveryConfigured_ = false; // Worker-owned; waits for a fresh coherent sample.
    bool sensorTouched_ = false; // Only after WHO_AM_I; failed writes may still land.
    bool stoppedWithoutWorker_ = true; // Startup-only failure path must confirm disable.
    bool configured_ = false;
};

} // namespace digivice::device
