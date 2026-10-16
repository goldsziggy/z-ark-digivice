#pragma once
#include "esp_err.h"
#include <cstddef>
struct usb_serial_jtag_driver_config_t {std::size_t rx_buffer_size=0,tx_buffer_size=0;};
inline esp_err_t usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t*){return ESP_OK;}
inline esp_err_t usb_serial_jtag_wait_tx_done(unsigned){return ESP_OK;}
