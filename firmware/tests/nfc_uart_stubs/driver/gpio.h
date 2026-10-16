#pragma once
#include "esp_err.h"
using gpio_num_t=int;
constexpr gpio_num_t GPIO_NUM_43=43, GPIO_NUM_44=44;
esp_err_t gpio_reset_pin(gpio_num_t);
