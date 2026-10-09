#include "usage_nvs.hpp"

namespace digivice::usage {
namespace { constexpr const char* keys[]{"steps_a", "steps_b"}; }
NvsBackend::~NvsBackend() { if (ready_) nvs_close(handle_); }
esp_err_t NvsBackend::initialize() {
    // Global NVS initialization belongs to the existing game save backend. No erase.
    const auto result = nvs_open("digi_usage", NVS_READWRITE, &handle_);
    ready_ = result == ESP_OK;
    return result;
}
Read NvsBackend::read(unsigned slot, Record& record) {
    if (!ready_ || slot > 1) return Read::Failed;
    std::size_t length = 0;
    auto result = nvs_get_blob(handle_, keys[slot], nullptr, &length);
    if (result == ESP_ERR_NVS_NOT_FOUND) return Read::Missing;
    if (result != ESP_OK || length != kRecordBytes) return Read::Failed;
    result = nvs_get_blob(handle_, keys[slot], record.bytes, &length);
    return result == ESP_OK && length == kRecordBytes ? Read::Present : Read::Failed;
}
bool NvsBackend::write(unsigned slot, const Record& record) {
    return ready_ && slot < 2 && nvs_set_blob(handle_, keys[slot], record.bytes, kRecordBytes) == ESP_OK && nvs_commit(handle_) == ESP_OK;
}
}
