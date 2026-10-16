#include "audio_test_nvs.hpp"
#include <cassert>
#include <cstring>
AudioTestNvs audioTestNvs, audioTestRhythmNvs;
namespace {
AudioTestNvs& storage(nvs_handle_t handle) {
    assert(handle==1 || handle==2); return handle==1 ? audioTestNvs : audioTestRhythmNvs;
}
}
esp_err_t nvs_open(const char* name,int mode,nvs_handle_t* handle) {
    assert(mode==NVS_READWRITE);
    assert(!std::strcmp(name,"digi_audio") || !std::strcmp(name,"digi_buzz"));
    *handle=!std::strcmp(name,"digi_audio") ? 1 : 2;
    auto& state=storage(*handle); ++state.opens;
    return state.openError;
}
void nvs_close(nvs_handle_t handle){++storage(handle).closes;}
esp_err_t nvs_get_blob(nvs_handle_t handle,const char* key,void* bytes,std::size_t* size) {
    auto& state=storage(handle); assert(!std::strcmp(key,"config"));++state.reads;
    if(state.readFail)return ESP_FAIL;
    if(!state.present)return ESP_ERR_NVS_NOT_FOUND;
    if(!bytes){*size=state.bytes.size();return ESP_OK;}
    assert(*size>=state.bytes.size());
    std::memcpy(bytes,state.bytes.data(),state.bytes.size());*size=state.bytes.size();return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle,const char* key,const void* bytes,std::size_t size) {
    auto& state=storage(handle); assert(!std::strcmp(key,"config")&&size==16);++state.sets;
    if(state.writeFail)return ESP_FAIL;
    state.bytes.assign(static_cast<const unsigned char*>(bytes),static_cast<const unsigned char*>(bytes)+size);
    state.present=true;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    auto& state=storage(handle); ++state.commits;
    if(state.commitFail)return ESP_FAIL;
    if(state.corruptAfterCommit)state.bytes[8]^=1;
    return ESP_OK;
}
