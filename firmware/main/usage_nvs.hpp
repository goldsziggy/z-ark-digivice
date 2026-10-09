#pragma once
#include "../runtime/usage_store.hpp"
#include "esp_err.h"
#include "nvs.h"

namespace digivice::usage {
class NvsBackend final : public Backend {
public:
    ~NvsBackend() override;
    esp_err_t initialize();
    Read read(unsigned slot, Record& record) override;
    bool write(unsigned slot, const Record& record) override;
private:
    nvs_handle_t handle_ = 0;
    bool ready_ = false;
};
}
