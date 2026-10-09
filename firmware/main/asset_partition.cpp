#include "asset_partition.hpp"

// API contract pinned to ESP-IDF v5.3.1 components/esp_partition/include/esp_partition.h.
namespace digivice::assets {
esp_err_t PartitionStorage::begin() {
    ready_ = false;
    partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        static_cast<esp_partition_subtype_t>(0x40), "assets");
    if (!partition_) {
        diagnostic_ = "assets data partition missing; flash untouched";
        return error_ = ESP_ERR_NOT_FOUND;
    }
    if (partition_->size < kStorageBytes || !partition_->erase_size ||
        kChunkBytes % partition_->erase_size || kStorageBytes % partition_->erase_size ||
        partition_->address % partition_->erase_size) {
        diagnostic_ = "assets partition too small or erase geometry incompatible; flash untouched";
        return error_ = ESP_ERR_INVALID_SIZE;
    }
    if (partition_->readonly) {
        diagnostic_ = "assets partition is read-only; flash untouched";
        return error_ = ESP_ERR_NOT_ALLOWED;
    }
    // Encrypted writes require 16-byte alignment and cannot implement this cache's
    // four-byte commit marker / direct NOR programming contract. Do not bypass
    // encryption by using esp_partition_write_raw on an encrypted partition.
    if (partition_->encrypted) {
        diagnostic_ = "encrypted assets partition unsupported by current NOR cache; flash untouched";
        return error_ = ESP_ERR_NOT_SUPPORTED;
    }
    ready_ = true;
    diagnostic_ = "assets partition located; no erase or format performed";
    return error_ = ESP_OK;
}
bool PartitionStorage::range(std::size_t offset, std::size_t length) {
    if (!ready()) { error_ = ESP_ERR_INVALID_STATE; return false; }
    if (offset > kStorageBytes || length > kStorageBytes - offset) {
        error_ = ESP_ERR_INVALID_SIZE;
        diagnostic_ = "asset operation exceeds reserved cache window";
        return false;
    }
    return true;
}
bool PartitionStorage::io(esp_err_t result) {
    error_ = result;
    if (result != ESP_OK) {
        // An interrupted write/erase may have changed flash. Preserve evidence
        // and block further operations until explicit reinitialization + boot.
        ready_ = false;
        diagnostic_ = "asset flash I/O failed or uncertain; cache recovery required";
        return false;
    }
    return true;
}
bool PartitionStorage::read(std::size_t offset, void* bytes, std::size_t length) {
    if (!range(offset, length)) return false;
    if (!length) { error_ = ESP_OK; return true; }
    if (!bytes) { error_ = ESP_ERR_INVALID_ARG; return false; }
    return io(esp_partition_read(partition_, offset, bytes, length));
}
bool PartitionStorage::erase(std::size_t offset, std::size_t length) {
    if (!range(offset, length)) return false;
    if (!length || offset % partition_->erase_size || length % partition_->erase_size) {
        error_ = ESP_ERR_INVALID_SIZE;
        diagnostic_ = "asset erase requires whole aligned sectors";
        return false;
    }
    return io(esp_partition_erase_range(partition_, offset, length));
}
bool PartitionStorage::program(std::size_t offset, const void* bytes, std::size_t length) {
    if (!range(offset, length)) return false;
    if (!length) { error_ = ESP_OK; return true; }
    if (!bytes) { error_ = ESP_ERR_INVALID_ARG; return false; }
    const auto* source = static_cast<const std::uint8_t*>(bytes);
    std::uint8_t previous[256];
    // Preflight the entire bounded request before writing, using fixed scratch.
    // Caller supplies erased space or clears more bits in an existing journal.
    for (std::size_t checked = 0; checked < length;) {
        const auto remaining = length - checked;
        const auto count = remaining < sizeof(previous) ? remaining : sizeof(previous);
        if (!io(esp_partition_read(partition_, offset + checked, previous, count))) return false;
        for (std::size_t i = 0; i < count; ++i) {
            if ((previous[i] & source[checked + i]) != source[checked + i]) {
                error_ = ESP_ERR_INVALID_STATE;
                diagnostic_ = "asset program would change a zero bit to one; request rejected without writing";
                return false;
            }
        }
        checked += count;
    }
    return io(esp_partition_write(partition_, offset, bytes, length));
}
} // namespace digivice::assets
