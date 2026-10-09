#pragma once

#include "../runtime/asset_cache.hpp"
#include "esp_err.h"
#include "esp_partition.h"

namespace digivice::assets {
// ESP-only storage boundary. This owns no filesystem and never autoformats.
// Only the cache's first kStorageBytes may be touched; the rest of the asset
// partition is reserved. One task serializes calls together with assets::Cache.
class PartitionStorage final : public Storage {
public:
    esp_err_t begin();
    bool ready() const { return partition_ != nullptr && ready_; }
    esp_err_t lastError() const { return error_; }
    const char* diagnostic() const { return diagnostic_; }
    bool read(std::size_t offset, void* bytes, std::size_t length) override;
    bool erase(std::size_t offset, std::size_t length) override;
    bool program(std::size_t offset, const void* bytes, std::size_t length) override;
private:
    bool range(std::size_t offset, std::size_t length);
    bool io(esp_err_t result);
    const esp_partition_t* partition_ = nullptr;
    esp_err_t error_ = ESP_ERR_INVALID_STATE;
    const char* diagnostic_ = "asset partition not initialized";
    bool ready_ = false;
};
} // namespace digivice::assets
