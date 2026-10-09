#include "idle_settings.hpp"
#include <cstring>

namespace digivice::idle {
NvsSettings::~NvsSettings() { if (opened_) nvs_close(handle_); }
esp_err_t NvsSettings::read(SettingsRecord& record) {
    std::size_t bytes=0;
    auto error=nvs_get_blob(handle_,"config",nullptr,&bytes);
    if(error!=ESP_OK)return error;
    if(bytes!=kSettingsBytes)return ESP_ERR_INVALID_SIZE;
    error=nvs_get_blob(handle_,"config",record.bytes,&bytes);
    return error!=ESP_OK?error:bytes==kSettingsBytes?ESP_OK:ESP_ERR_INVALID_SIZE;
}
esp_err_t NvsSettings::begin() {
    if(attempted_)return error_;
    attempted_=true;
    error_=nvs_open("digi_idle",NVS_READWRITE,&handle_);
    if(error_!=ESP_OK)return error_;
    opened_=true;
    SettingsRecord record;error_=read(record);
    if(error_==ESP_ERR_NVS_NOT_FOUND){writable_=true;error_=ESP_OK;return error_;}
    if(error_!=ESP_OK)return error_;
    if(!decodeSettings(record,seconds_)){error_=ESP_ERR_INVALID_RESPONSE;return error_;}
    writable_=true;return ESP_OK;
}
esp_err_t NvsSettings::setSeconds(std::uint32_t seconds) {
    if(!writable())return ESP_ERR_INVALID_STATE;
    SettingsRecord record;if(!encodeSettings(seconds,record))return ESP_ERR_INVALID_ARG;
    if(seconds==seconds_)return ESP_OK;
    auto error=nvs_set_blob(handle_,"config",record.bytes,kSettingsBytes);
    if(error==ESP_OK)error=nvs_commit(handle_);
    SettingsRecord checked;
    if(error==ESP_OK)error=read(checked);
    if(error==ESP_OK&&std::memcmp(record.bytes,checked.bytes,kSettingsBytes))error=ESP_ERR_INVALID_RESPONSE;
    if(error!=ESP_OK){writable_=false;error_=error;return error;}
    seconds_=seconds;return ESP_OK;
}
} // namespace digivice::idle
