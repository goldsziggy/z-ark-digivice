#pragma once
#include "esp_err.h"
#include <cstdint>
#include <cstddef>
using i2c_master_bus_handle_t=void*; using i2c_master_dev_handle_t=void*;
constexpr int I2C_ADDR_BIT_LEN_7=0;
struct i2c_device_config_t { int dev_addr_length; std::uint16_t device_address; std::uint32_t scl_speed_hz; };
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t,const i2c_device_config_t*,i2c_master_dev_handle_t*);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t,const std::uint8_t*,std::size_t,std::uint8_t*,std::size_t,int);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t,const std::uint8_t*,std::size_t,int);
