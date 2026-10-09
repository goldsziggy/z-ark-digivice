#pragma once
#include "trade_store.hpp"
#include "esp_err.h"
#include "nvs.h"

namespace digivice::devicetrade {
class NvsBackend final : public Backend {
public:
    ~NvsBackend() override;
    esp_err_t initialize();
    Read read(unsigned,Slot&) override;
    bool write(unsigned,const Bytes&) override;
private:
    nvs_handle_t handle_=0;
    bool ready_=false;
};
// Called only after the care backend initializes NVS. Any evidence of trade or
// an unreadable journal prevents app_main from creating/checkpointing a new egg.
bool freshCareStorageAllowed();
} // namespace digivice::devicetrade
