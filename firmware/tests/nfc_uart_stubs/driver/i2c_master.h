#pragma once
#include "gpio.h"
#include "esp_err.h"
#include <cstddef>
struct i2c_master_bus_t; using i2c_master_bus_handle_t=i2c_master_bus_t*;
struct i2c_master_dev_t; using i2c_master_dev_handle_t=i2c_master_dev_t*;
constexpr int I2C_CLK_SRC_DEFAULT=0,I2C_ADDR_BIT_LEN_7=7;
struct i2c_master_bus_config_t {int i2c_port=0;gpio_num_t scl_io_num{},sda_io_num{};int clk_source=0,glitch_ignore_cnt=0;struct{bool enable_internal_pullup=false;}flags;};
struct i2c_device_config_t {int dev_addr_length=7,device_address=0,scl_speed_hz=0;};
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t*,i2c_master_bus_handle_t*);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t,const i2c_device_config_t*,i2c_master_dev_handle_t*);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t,const std::uint8_t*,std::size_t,std::uint8_t*,std::size_t,int);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t,const std::uint8_t*,std::size_t,int);
