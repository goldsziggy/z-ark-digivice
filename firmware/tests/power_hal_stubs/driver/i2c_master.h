#pragma once
// No I2C functions are declared: an accidental enabled dependency fails compile.
struct i2c_master_bus_t;
using i2c_master_bus_handle_t = i2c_master_bus_t*;
