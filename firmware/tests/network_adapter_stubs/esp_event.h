#pragma once
#include "esp_err.h"
#include <cstdint>
using esp_event_base_t=const char*;
using esp_event_handler_instance_t=void*;
using esp_event_handler_t=void(*)(void*,esp_event_base_t,std::int32_t,void*);
inline constexpr char WIFI_EVENT[]="wifi", IP_EVENT[]="ip";
constexpr int ESP_EVENT_ANY_ID=-1, WIFI_EVENT_STA_START=1, WIFI_EVENT_STA_STOP=2, WIFI_EVENT_STA_DISCONNECTED=3, IP_EVENT_STA_GOT_IP=4, IP_EVENT_STA_LOST_IP=5;
constexpr int WIFI_EVENT_SCAN_DONE=6;
esp_err_t esp_event_loop_create_default();
esp_err_t esp_event_handler_instance_register(esp_event_base_t,int,esp_event_handler_t,void*,esp_event_handler_instance_t*);
