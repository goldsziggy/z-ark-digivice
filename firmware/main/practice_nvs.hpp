#pragma once
#include "practice_session.hpp"
#include "esp_err.h"
#include "nvs.h"

namespace digivice::devicepractice {
// Independent namespace and fixed records. Never initializes/erases shared NVS;
// app_main's care backend initializes NVS before this adapter is opened.
class NvsBackend final : public Backend {
public:
    ~NvsBackend() override;
    esp_err_t initialize();
    Read read(unsigned slot, Slot& output) override;
    bool write(unsigned slot, const Bytes& bytes) override;
private:
    nvs_handle_t handle_ = 0;
    bool ready_ = false;
};
} // namespace digivice::devicepractice
