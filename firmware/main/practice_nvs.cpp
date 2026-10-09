#include "practice_nvs.hpp"

namespace digivice::devicepractice {
namespace { constexpr const char* kKeys[]{"state_a", "state_b"}; }
NvsBackend::~NvsBackend() { if (ready_) nvs_close(handle_); }
esp_err_t NvsBackend::initialize() {
    if (ready_) return ESP_OK;
    const auto error = nvs_open("practice", NVS_READWRITE, &handle_);
    ready_ = error == ESP_OK;
    return error;
}
Read NvsBackend::read(unsigned slot, Slot& output) {
    if (!ready_ || slot > 1) return Read::Unreadable;
    std::size_t length = 0;
    auto error = nvs_get_blob(handle_, kKeys[slot], nullptr, &length);
    if (error == ESP_ERR_NVS_NOT_FOUND) return Read::Missing;
    if (error != ESP_OK || (length != kRecordBytes && length != kLegacyRecordBytes)) return Read::Unreadable;
    output.length = length;
    error = nvs_get_blob(handle_, kKeys[slot], output.bytes.data, &output.length);
    return error == ESP_OK && output.length == length ? Read::Present : Read::Unreadable;
}
bool NvsBackend::write(unsigned slot, const Bytes& bytes) {
    return ready_ && slot < 2 && nvs_set_blob(handle_, kKeys[slot], bytes.data, kRecordBytes) == ESP_OK && nvs_commit(handle_) == ESP_OK;
}
} // namespace digivice::devicepractice
