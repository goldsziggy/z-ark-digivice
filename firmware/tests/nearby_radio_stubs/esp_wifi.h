#pragma once
#include "esp_err.h"
#include <cstdint>
enum wifi_mode_t {WIFI_MODE_NULL,WIFI_MODE_STA,WIFI_MODE_AP};
enum wifi_interface_t {WIFI_IF_STA,WIFI_IF_AP};
enum wifi_second_chan_t {WIFI_SECOND_CHAN_NONE,WIFI_SECOND_CHAN_ABOVE};
struct wifi_pkt_rx_ctrl_t {std::int8_t rssi;};
esp_err_t esp_wifi_get_mode(wifi_mode_t*);esp_err_t esp_wifi_get_mac(wifi_interface_t,std::uint8_t*);
esp_err_t esp_wifi_start();esp_err_t esp_wifi_stop();
esp_err_t esp_wifi_set_channel(std::uint8_t,wifi_second_chan_t);esp_err_t esp_wifi_get_channel(std::uint8_t*,wifi_second_chan_t*);
