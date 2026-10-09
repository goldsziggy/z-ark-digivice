#pragma once
#include "esp_err.h"
#include <cstdint>
struct wifi_init_config_t {};
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{}
constexpr int WIFI_STORAGE_RAM=0, WIFI_MODE_STA=0, WIFI_AUTH_WPA2_PSK=2;
constexpr int WIFI_AUTH_WPA_WPA2_PSK=3, WIFI_AUTH_WPA2_WPA3_PSK=4, WIFI_SCAN_TYPE_ACTIVE=0;
struct wifi_config_t { struct { std::uint8_t ssid[32],password[64]; struct { int authmode; } threshold; struct { bool capable,required; } pmf_cfg; std::uint8_t channel; } sta; };
struct wifi_ap_record_t { std::uint8_t ssid[33]; std::int8_t rssi; int authmode; };
struct wifi_event_sta_scan_done_t {std::uint32_t status;};
struct wifi_scan_config_t {bool show_hidden;int scan_type;struct {struct {unsigned min,max;} active;} scan_time;};
constexpr int WIFI_IF_STA=0;
esp_err_t esp_wifi_init(const wifi_init_config_t*);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_set_config(int,const wifi_config_t*);
esp_err_t esp_wifi_start();
esp_err_t esp_wifi_stop();
esp_err_t esp_wifi_connect();
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t*);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t*,bool);
esp_err_t esp_wifi_scan_stop();
esp_err_t esp_wifi_clear_ap_list();
esp_err_t esp_wifi_scan_get_ap_records(std::uint16_t*,wifi_ap_record_t*);
