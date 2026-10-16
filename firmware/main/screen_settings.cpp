#include "screen_settings.hpp"
#include <cstddef>
#include <cstring>

namespace digivice::screen {
namespace {
constexpr std::size_t kRecordBytes = 16;
constexpr std::uint8_t kMagic[] = {'D', 'S', 'C', 'R'};

void put32(std::uint8_t* out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<std::uint8_t>(value >> (8 * i));
}
std::uint32_t get32(const std::uint8_t* in) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(in[i]) << (8 * i);
    return value;
}
std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t value = ~0u;
    for (std::size_t i = 0; i < size; ++i) {
        value ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
    }
    return ~value;
}
void encode(std::uint16_t degrees, std::uint8_t* record) {
    std::memcpy(record, kMagic, sizeof kMagic);
    put32(record + 4, 1);
    put32(record + 8, degrees);
    put32(record + 12, crc32(record, 12));
}
bool decode(const std::uint8_t* record, std::uint16_t& degrees) {
    if (std::memcmp(record, kMagic, sizeof kMagic) || get32(record + 4) != 1 ||
        get32(record + 12) != crc32(record, 12)) return false;
    const auto value = get32(record + 8);
    if (value != 0 && value != 90 && value != 180 && value != 270) return false;
    degrees = static_cast<std::uint16_t>(value);
    return true;
}
} // namespace

NvsSettings::~NvsSettings() { if (opened_) nvs_close(handle_); }

esp_err_t NvsSettings::read(std::uint8_t* record) {
    std::size_t bytes = 0;
    auto error = nvs_get_blob(handle_, "config", nullptr, &bytes);
    if (error != ESP_OK) return error;
    if (bytes != kRecordBytes) return ESP_ERR_INVALID_SIZE;
    error = nvs_get_blob(handle_, "config", record, &bytes);
    return error != ESP_OK ? error : bytes == kRecordBytes ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t NvsSettings::begin(std::uint16_t defaultDegrees) {
    if (attempted_) return error_;
    attempted_ = true;
    if (!validDegrees(defaultDegrees)) {
        error_ = ESP_ERR_INVALID_ARG;
        return error_;
    }
    degrees_ = defaultDegrees;
    error_ = nvs_open("digi_screen", NVS_READWRITE, &handle_);
    if (error_ != ESP_OK) return error_;
    opened_ = true;
    std::uint8_t record[kRecordBytes]{};
    error_ = read(record);
    if (error_ == ESP_ERR_NVS_NOT_FOUND) {
        writable_ = true;
        error_ = ESP_OK;
        return ESP_OK;
    }
    if (error_ != ESP_OK) return error_;
    if (!decode(record, degrees_)) {
        error_ = ESP_ERR_INVALID_RESPONSE;
        return error_;
    }
    writable_ = true;
    return ESP_OK;
}

esp_err_t NvsSettings::setDegrees(std::uint16_t degrees) {
    if (!writable()) return ESP_ERR_INVALID_STATE;
    if (!validDegrees(degrees)) return ESP_ERR_INVALID_ARG;
    if (degrees == degrees_) return ESP_OK;
    std::uint8_t record[kRecordBytes]{};
    encode(degrees, record);
    auto error = nvs_set_blob(handle_, "config", record, kRecordBytes);
    if (error == ESP_OK) error = nvs_commit(handle_);
    std::uint8_t checked[kRecordBytes]{};
    if (error == ESP_OK) error = read(checked);
    if (error == ESP_OK && std::memcmp(record, checked, kRecordBytes)) error = ESP_ERR_INVALID_RESPONSE;
    if (error != ESP_OK) {
        writable_ = false;
        error_ = error;
        return error;
    }
    degrees_ = degrees;
    return ESP_OK;
}
} // namespace digivice::screen
