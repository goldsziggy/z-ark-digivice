#include "sd_asset_storage.hpp"

#include "board_hal.hpp"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sdkconfig.h"
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace digivice::assets {
namespace {
constexpr char kMount[] = "/sdcard";
// 8.3 naming works with the default FATFS_LFN_NONE setting. No directory scan,
// arbitrary file paths, unlink, rename, truncate, partitioning or formatting.
constexpr char kCachePath[] = "/sdcard/DVASSET1.CCH";
constexpr unsigned kMaximumInterruptedRetries = 3;
}

bool SdAssetStorage::buildEnabled() {
#if defined(CONFIG_DIGIVICE_BOARD_WAVESHARE_146) && CONFIG_DIGIVICE_BOARD_WAVESHARE_146 && \
    defined(CONFIG_DIGIVICE_SD_ASSETS) && CONFIG_DIGIVICE_SD_ASSETS
    return true;
#else
    return false;
#endif
}
SdAssetStorage::~SdAssetStorage() { end(); }
bool SdAssetStorage::ready() const { return mounted_ && file_.good() && policy_.ready(); }
esp_err_t SdAssetStorage::lastError() const {
    return error_ != ESP_OK ? error_ : (file_.error() ? ESP_FAIL : ESP_OK);
}
const char* SdAssetStorage::diagnostic() const {
    return mounted_ && error_ == ESP_OK ? policy_.diagnostic() : diagnostic_;
}
esp_err_t SdAssetStorage::begin() {
    if (attempted_) return ESP_ERR_INVALID_STATE;
    attempted_ = true;
    if (!buildEnabled()) {
        diagnostic_ = "SD assets disabled for this board; resident artwork available";
        return error_ = ESP_ERR_NOT_SUPPORTED;
    }
    const auto& profile = board::selectedProfile();
    if (profile.id != board::ProfileId::Waveshare146 || !board::powerHoldReady()) {
        diagnostic_ = "verified SD board/power hold unavailable; card untouched";
        return error_ = ESP_ERR_INVALID_STATE;
    }
    error_ = board::prepareSdCardSelect();
    if (error_ != ESP_OK) {
        diagnostic_ = "SD expander select preparation failed; card not mounted";
        return error_;
    }
    // Official vendor uses one-bit SDMMC. EXIO3/D3 is an expander output,
    // never LCD GPIO21 or an MCU D3 pin. Preserve default20MHz host timing.
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = static_cast<gpio_num_t>(profile.sdClockGpio);
    slot.cmd = static_cast<gpio_num_t>(profile.sdCommandGpio);
    slot.d0 = static_cast<gpio_num_t>(profile.sdData0Gpio);
    slot.d1 = slot.d2 = slot.d3 = GPIO_NUM_NC;
    slot.d4 = slot.d5 = slot.d6 = slot.d7 = GPIO_NUM_NC;
    slot.cd = slot.wp = GPIO_NUM_NC;
    // Verified board has10k external pull-ups; this mirrors the vendor's
    // optional internal pull-ups, which alone would not satisfy the SD bus.
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t mount{};
    mount.format_if_mount_failed = false; // Never changed, even after errors.
    // Cache plus one owner-task read-only art/inventory descriptor. Inspection
    // closes each file before the next and never overlaps another local reader.
    mount.max_files = 2;
    mount.allocation_unit_size = 0; // Irrelevant because formatting is disabled.
    mount.disk_status_check_enable = true;
    mount.use_one_fat = false;
    error_ = esp_vfs_fat_sdmmc_mount(kMount, &host, &slot, &mount, &card_);
    if (error_ != ESP_OK) {
        card_ = nullptr;
        diagnostic_ = "SD mount failed or card absent; no format attempted; resident artwork available";
        return error_;
    }
    mounted_ = true;
    int flags = O_RDWR;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW; // FAT has no symlinks; protects host-based HAL tests too.
#endif
    bool created = false;
    int descriptor = ::open(kCachePath, flags);
    if (descriptor < 0 && errno == ENOENT) {
        descriptor = ::open(kCachePath, flags | O_CREAT | O_EXCL, 0600);
        created = descriptor >= 0;
    }
    if (descriptor < 0) {
        error_ = (errno == EACCES || errno == EROFS) ? ESP_ERR_NOT_ALLOWED : ESP_FAIL;
        diagnostic_ = "SD cache cannot be opened writable; existing files preserved";
        end(); return error_;
    }
    file_.adopt(descriptor);
    // The portable policy validates exact physical size and ownership header,
    // initializes only a freshly O_EXCL-created empty file, and provides NOR
    // semantics. Cache::boot remains the sole payload/journal recovery engine.
    if (!policy_.begin(created)) {
        error_ = file_.error() ? ESP_FAIL : ESP_ERR_INVALID_STATE;
        diagnostic_ = policy_.diagnostic(); // Static string survives end().
        end(); return error_;
    }
    diagnostic_ = "SD cache ready";
    return error_ = ESP_OK;
}
void SdAssetStorage::end() {
    policy_.end();
    file_.close();
    if (mounted_ && card_) {
        const auto result = esp_vfs_fat_sdcard_unmount(kMount, card_);
        if (result != ESP_OK && error_ == ESP_OK) error_ = result;
    }
    mounted_ = false; card_ = nullptr;
    // attempted_ deliberately remains true. Recovery/remount needs a reboot;
    // this object cannot silently change media while a Cache references it.
}
esp_err_t SdAssetStorage::preparePowerOff() {
    if (!mounted_) return ESP_OK;
    if (!ready() || !file_.sync()) {
        diagnostic_ = "SD synchronization failed; power retained";
        return error_ = ESP_FAIL;
    }
    return ESP_OK;
}
bool SdAssetStorage::read(std::size_t offset, void* bytes, std::size_t length) {
    return ready() && policy_.read(offset, bytes, length);
}
bool SdAssetStorage::erase(std::size_t offset, std::size_t length) {
    return ready() && policy_.erase(offset, length);
}
bool SdAssetStorage::program(std::size_t offset, const void* bytes, std::size_t length) {
    return ready() && policy_.program(offset, bytes, length);
}

void SdAssetStorage::PosixFile::adopt(int descriptor) { descriptor_ = descriptor; error_ = 0; }
void SdAssetStorage::PosixFile::close() {
    if (descriptor_ >= 0) {
        if (::close(descriptor_) != 0 && !error_) error_ = errno ? errno : EIO;
        descriptor_ = -1;
    }
}
bool SdAssetStorage::PosixFile::fail(int error) { error_ = error ? error : EIO; return false; }
bool SdAssetStorage::PosixFile::size(std::size_t& bytes) {
    if (!good()) return false;
    struct stat value{};
    if (::fstat(descriptor_, &value) != 0) return fail(errno);
    if (!S_ISREG(value.st_mode) || value.st_size < 0 || static_cast<std::uint64_t>(value.st_size) > kFileStorageBytes)
        return fail(EINVAL);
    bytes = static_cast<std::size_t>(value.st_size);
    return true;
}
bool SdAssetStorage::PosixFile::read(std::size_t offset, void* bytes, std::size_t length) {
    if (!good()) return false;
    if (offset > kFileStorageBytes || length > kFileStorageBytes - offset || (!bytes && length)) return fail(EINVAL);
    auto* target = static_cast<std::uint8_t*>(bytes);
    unsigned interruptions = 0;
    while (length) {
        const auto count = ::pread(descriptor_, target, length, static_cast<off_t>(offset));
        if (count < 0 && errno == EINTR && interruptions++ < kMaximumInterruptedRetries) continue;
        if (count <= 0 || static_cast<std::size_t>(count) > length) return fail(count < 0 ? errno : EIO);
        offset += static_cast<std::size_t>(count); target += count; length -= static_cast<std::size_t>(count);
    }
    return true;
}
bool SdAssetStorage::PosixFile::write(std::size_t offset, const void* bytes, std::size_t length) {
    if (!good()) return false;
    if (offset > kFileStorageBytes || length > kFileStorageBytes - offset || (!bytes && length)) return fail(EINVAL);
    const auto* source = static_cast<const std::uint8_t*>(bytes);
    unsigned interruptions = 0;
    // ESP-IDF5.3.6 vfs_fat_pwrite returns ENOSPC without releasing its VFS
    // lock, so a full card can deadlock a subsequent close. Use seek+write on
    // this private descriptor; the cache owner serializes the whole operation.
    // Ordinary vfs_fat_write releases that lock on its ENOSPC path.
    if (length) {
        off_t position;
        do { position = ::lseek(descriptor_, static_cast<off_t>(offset), SEEK_SET); }
        while (position < 0 && errno == EINTR && interruptions++ < kMaximumInterruptedRetries);
        if (position != static_cast<off_t>(offset)) return fail(position < 0 ? errno : EIO);
    }
    while (length) {
        const auto count = ::write(descriptor_, source, length);
        if (count < 0 && errno == EINTR && interruptions++ < kMaximumInterruptedRetries) continue;
        if (count <= 0 || static_cast<std::size_t>(count) > length) return fail(count < 0 ? errno : ENOSPC);
        offset += static_cast<std::size_t>(count); source += count; length -= static_cast<std::size_t>(count);
    }
    return true;
}
bool SdAssetStorage::PosixFile::sync() {
    if (!good()) return false;
    if (::fsync(descriptor_) != 0) return fail(errno);
    // This requests FatFS/card synchronization; it is not proof that an SD
    // controller has physically persisted every sector across sudden power loss.
    return true;
}
} // namespace digivice::assets
