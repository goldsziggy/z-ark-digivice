#pragma once

#include "save_store.hpp"
#include "esp_err.h"
#include "nvs.h"

namespace digivice::storage {

class NvsBackend final : public Backend {
public:
    ~NvsBackend() override;
    esp_err_t initialize();
    ReadStatus readSlot(unsigned index, Slot& slot) override;
    bool writeSlot(unsigned index, const Snapshot& snapshot) override;
private:
    nvs_handle_t handle_ = 0;
    bool ready_ = false;
};

} // namespace digivice::storage
