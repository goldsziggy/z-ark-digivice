#pragma once
#include "esp_err.h"
struct esp_netif_t {};
struct esp_netif_ip_info_t { struct { unsigned addr; } ip; };
esp_err_t esp_netif_init();
esp_netif_t* esp_netif_create_default_wifi_sta();
esp_err_t esp_netif_get_ip_info(esp_netif_t*,esp_netif_ip_info_t*);
