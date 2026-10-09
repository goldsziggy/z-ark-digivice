#include "nvs_backend.hpp"
#include "nvs_flash.h"

namespace digivice::storage {
namespace {
constexpr const char* kKeys[2] = {"save_a", "save_b"};
}

NvsBackend::~NvsBackend() {
    if (ready_) nvs_close(handle_);
}

esp_err_t NvsBackend::initialize() {
    // Never call nvs_flash_erase on NO_FREE_PAGES / NEW_VERSION_FOUND.
    // Those are recovery conditions, not permission to delete a player's save.
    auto err = nvs_flash_init();
    if (err != ESP_OK) return err;
    err = nvs_open("digivice", NVS_READWRITE, &handle_);
    ready_ = err == ESP_OK;
    return err;
}

ReadStatus NvsBackend::readSlot(unsigned index, Slot& slot) {
    if (!ready_ || index > 1) return ReadStatus::Unreadable;
    std::size_t length = 0;
    auto err = nvs_get_blob(handle_, kKeys[index], nullptr, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ReadStatus::Missing;
    // Future larger schemas are preserved without allocating arbitrary lengths.
    if (err != ESP_OK || length > sizeof(slot.bytes) || length == 0) return ReadStatus::Unreadable;
    slot.length = length;
    err = nvs_get_blob(handle_, kKeys[index], slot.bytes, &slot.length);
    return err == ESP_OK ? ReadStatus::Present : ReadStatus::Unreadable;
}

bool NvsBackend::writeSlot(unsigned index, const Snapshot& snapshot) {
    if (!ready_ || index > 1) return false;
    return nvs_set_blob(handle_, kKeys[index], snapshot.bytes, sizeof(snapshot.bytes)) == ESP_OK &&
           nvs_commit(handle_) == ESP_OK;
}

} // namespace digivice::storage
