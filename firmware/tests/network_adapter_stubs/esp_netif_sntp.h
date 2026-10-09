#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <cstddef>
struct esp_sntp_config_t {bool wait_for_sync=false,start=false;std::size_t num_of_servers=0;const char* servers[1]{};};
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t*);
void esp_netif_sntp_deinit();
esp_err_t esp_netif_sntp_sync_wait(TickType_t);
