#pragma once
#include <cstdint>
#include "esp_err.h"
#include "nvs.h"

namespace digivice::screen {
constexpr bool validDegrees(std::uint16_t degrees) {
    return degrees == 0 || degrees == 90 || degrees == 180 || degrees == 270;
}

// Shared NVS must already be initialized. This preference never initializes or
// erases NVS and never touches the game save. Uncertain storage blocks writes.
class NvsSettings {
public:
    NvsSettings() = default;
    ~NvsSettings();
    NvsSettings(const NvsSettings&) = delete;
    NvsSettings& operator=(const NvsSettings&) = delete;
    esp_err_t begin(std::uint16_t defaultDegrees);
    esp_err_t setDegrees(std::uint16_t degrees);
    std::uint16_t degrees() const { return degrees_; }
    bool writable() const { return opened_ && writable_; }
private:
    esp_err_t read(std::uint8_t* record);
    nvs_handle_t handle_ = 0;
    std::uint16_t degrees_ = 0;
    bool opened_ = false, writable_ = false, attempted_ = false;
    esp_err_t error_ = ESP_ERR_INVALID_STATE;
};
} // namespace digivice::screen
