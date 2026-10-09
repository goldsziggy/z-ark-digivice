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
    bool ready() const { return configured_ && !paused_.load() && error_.load() == ESP_OK; }
    bool quiescent() const {
        return !configured_ || (paused_.load() && settledEpoch_.load() == epoch_.load() && !active_.load());
    }
    esp_err_t lastError() const { return error_.load(); }
    std::uint8_t revision() const { return revision_; }
    const motion::ImuReading& reading() const { return ownerReading_; }
    motion::StepReading stepReading() const;
private:
    static void taskEntry(void* context);
    void run();
    void sample(std::uint64_t nowMs);
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
    std::atomic<bool> paused_{false}, active_{false};
    std::uint8_t revision_ = 0;
    unsigned failures_ = 0;
    bool configured_ = false;
};

} // namespace digivice::device
