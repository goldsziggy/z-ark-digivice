#include "audio_test_nvs.hpp"
#include <cassert>
#include <cstring>
AudioTestNvs audioTestNvs;
esp_err_t nvs_open(const char* name,int mode,nvs_handle_t* handle) {
    assert(!std::strcmp(name,"digi_audio")&&mode==NVS_READWRITE);++audioTestNvs.opens;
    if(audioTestNvs.openError!=ESP_OK)return audioTestNvs.openError;
    *handle=1;return ESP_OK;
}
void nvs_close(nvs_handle_t handle){assert(handle==1);++audioTestNvs.closes;}
esp_err_t nvs_get_blob(nvs_handle_t handle,const char* key,void* bytes,std::size_t* size) {
    assert(handle==1&&!std::strcmp(key,"config"));++audioTestNvs.reads;
    if(audioTestNvs.readFail)return ESP_FAIL;
    if(!audioTestNvs.present)return ESP_ERR_NVS_NOT_FOUND;
    if(!bytes){*size=audioTestNvs.bytes.size();return ESP_OK;}
    assert(*size>=audioTestNvs.bytes.size());
    std::memcpy(bytes,audioTestNvs.bytes.data(),audioTestNvs.bytes.size());*size=audioTestNvs.bytes.size();return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle,const char* key,const void* bytes,std::size_t size) {
    assert(handle==1&&!std::strcmp(key,"config")&&size==16);++audioTestNvs.sets;
    if(audioTestNvs.writeFail)return ESP_FAIL;
    audioTestNvs.bytes.assign(static_cast<const unsigned char*>(bytes),static_cast<const unsigned char*>(bytes)+size);
    audioTestNvs.present=true;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    assert(handle==1);++audioTestNvs.commits;
    if(audioTestNvs.commitFail)return ESP_FAIL;
    if(audioTestNvs.corruptAfterCommit)audioTestNvs.bytes[8]^=1;
    return ESP_OK;
}
