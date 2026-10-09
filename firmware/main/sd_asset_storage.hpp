#pragma once

#include "../runtime/file_asset_storage.hpp"
#include "esp_err.h"
#include "sdmmc_cmd.h"

namespace digivice::assets {

// One application-lifetime SD owner and one fixed cache file. begin() runs once
// before the asset worker starts. After handing this Storage to Cache, serialize
// every call through the client's cache mutex. Removal/I/O failure latches off;
// it never remounts or substitutes another storage medium under live metadata.
class SdAssetStorage final : public Storage {
public:
    SdAssetStorage() : policy_(file_) {}
    ~SdAssetStorage() override;
    SdAssetStorage(const SdAssetStorage&) = delete;
    SdAssetStorage& operator=(const SdAssetStorage&) = delete;
    static bool buildEnabled();
    esp_err_t begin();
    // Only after ALL users/workers are quiescent (or during failed startup).
    // Never call from a hot-removal callback or a second task.
    void end();
    // Owner only, AFTER asset worker quiescence and suspending other SD users.
    // Requests FatFS synchronization, leaves media mounted for standby resume.
    // Disabled/absent media is okay; mounted failed media must never permit cut.
    // This is a software flush acknowledgement, not measured card durability.
    esp_err_t preparePowerOff();
    bool ready() const;
    // Main-owner diagnostics only. Never mounts, probes or retains a card handle.
    bool mounted() const { return mounted_; }
    const sdmmc_card_t* mountedCard() const { return mounted_ ? card_ : nullptr; }
    esp_err_t lastError() const;
    const char* diagnostic() const;
    bool read(std::size_t offset, void* bytes, std::size_t length) override;
    bool erase(std::size_t offset, std::size_t length) override;
    bool program(std::size_t offset, const void* bytes, std::size_t length) override;
private:
    class PosixFile final : public FileIo {
    public:
        void adopt(int descriptor);
        void close();
        bool good() const { return descriptor_ >= 0 && error_ == 0; }
        int error() const { return error_; }
        bool size(std::size_t& bytes) override;
        bool read(std::size_t offset, void* bytes, std::size_t length) override;
        bool write(std::size_t offset, const void* bytes, std::size_t length) override;
        bool sync() override;
    private:
        bool fail(int error);
        int descriptor_ = -1;
        int error_ = 0;
    };
    PosixFile file_{};
    FileAssetStorage policy_;
    sdmmc_card_t* card_ = nullptr;
    bool attempted_ = false, mounted_ = false;
    esp_err_t error_ = ESP_ERR_INVALID_STATE;
    const char* diagnostic_ = "SD asset storage not initialized";
};
} // namespace digivice::assets
