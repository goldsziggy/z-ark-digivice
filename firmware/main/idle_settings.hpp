#pragma once
#include "../runtime/idle_settings.hpp"
#include "esp_err.h"
#include "nvs.h"

namespace digivice::idle {
// Separate preference only. Shared NVS is already initialized by the care store.
// Never initializes or erases NVS; malformed/uncertain records block writes.
class NvsSettings {
public:
    ~NvsSettings();
    esp_err_t begin();
    esp_err_t setSeconds(std::uint32_t seconds);
    std::uint32_t seconds() const { return seconds_; }
    bool writable() const { return opened_ && writable_; }
private:
    esp_err_t read(SettingsRecord&);
    nvs_handle_t handle_ = 0;
    std::uint32_t seconds_ = kDefaultSeconds;
    bool opened_ = false, writable_ = false, attempted_ = false;
    esp_err_t error_ = ESP_ERR_INVALID_STATE;
};
} // namespace digivice::idle
