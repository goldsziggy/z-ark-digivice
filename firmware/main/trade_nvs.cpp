#include "trade_nvs.hpp"

namespace digivice::devicetrade {
namespace {constexpr const char* keys[]{"journal_a","journal_b"};}
NvsBackend::~NvsBackend(){if(ready_)nvs_close(handle_);}
esp_err_t NvsBackend::initialize(){if(ready_)return ESP_OK;const auto error=nvs_open("trade",NVS_READWRITE,&handle_);ready_=error==ESP_OK;return error;}
Read NvsBackend::read(unsigned i,Slot& out){
    if(!ready_||i>1)return Read::Unreadable;
    std::size_t n=0;auto error=nvs_get_blob(handle_,keys[i],nullptr,&n);
    if(error==ESP_ERR_NVS_NOT_FOUND)return Read::Missing;
    if(error!=ESP_OK||!supportedJournalSize(n))return Read::Unreadable;
    out.length=n;error=nvs_get_blob(handle_,keys[i],out.bytes.data,&out.length);
    return error==ESP_OK&&out.length==n?Read::Present:Read::Unreadable;
}
bool NvsBackend::write(unsigned i,const Bytes& bytes){return ready_&&i<2&&nvs_set_blob(handle_,keys[i],bytes.data,kJournalBytes)==ESP_OK&&nvs_commit(handle_)==ESP_OK;}
bool freshCareStorageAllowed(){
    // Only absence permits a new game. Any blob, including an unknown future
    // length, is evidence to preserve; no 6 KiB journal buffer is needed here.
    nvs_handle_t handle;
    const auto opened=nvs_open("trade",NVS_READONLY,&handle);
    if(opened==ESP_ERR_NVS_NOT_FOUND)return true;
    if(opened!=ESP_OK)return false;
    std::size_t length=0;
    const auto first=nvs_get_blob(handle,keys[0],nullptr,&length);
    const auto second=nvs_get_blob(handle,keys[1],nullptr,&length);
    nvs_close(handle);
    return first==ESP_ERR_NVS_NOT_FOUND && second==ESP_ERR_NVS_NOT_FOUND;
}
} // namespace digivice::devicetrade
