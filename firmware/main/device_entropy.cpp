#include "device_entropy.hpp"
#include "bootloader_random.h"
#include "esp_mac.h"
#include "esp_random.h"

namespace digivice::device {
entropy::Seeds collectStartupEntropy() {
    entropy::Material material;
    if(esp_efuse_mac_get_default(material.identity)!=ESP_OK)return {};
    bool any=false,notBroadcast=false;
    for(auto byte:material.identity){any|=byte!=0;notBroadcast|=byte!=255;}
    if(!any || !notBroadcast || (material.identity[0]&1u))return {};
    // IDF5.3.6 Random Number Generation: with RF off, esp_random alone does
    // not guarantee true entropy. The temporary SAR source is supported in
    // early app startup and MUST be disabled before ADC/RF use begins.
    bootloader_random_enable();
    esp_fill_random(material.words,sizeof(material.words));
    bootloader_random_disable();
    return entropy::seeds(material);
}
}
