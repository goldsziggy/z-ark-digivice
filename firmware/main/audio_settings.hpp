#pragma once
#include "../runtime/audio_settings.hpp"
#include "esp_err.h"
#include "nvs.h"

namespace digivice::sound {
// Shared NVS is initialized by the save owner. Never initializes/erases it.
// Uncertain writes or malformed/future records remain untouched until reboot.
class NvsSettings {
public:
    ~NvsSettings();
    esp_err_t begin();
    esp_err_t save(const Preferences&);
    const Preferences& preferences() const { return preferences_; }
    bool writable() const { return opened_ && writable_; }
    esp_err_t lastError() const { return error_; }
private:
    esp_err_t read(SettingsRecord&);
    nvs_handle_t handle_ = 0;
    Preferences preferences_{};
    bool opened_ = false, writable_ = false, attempted_ = false;
    esp_err_t error_ = ESP_ERR_INVALID_STATE;
};
} // namespace digivice::sound
