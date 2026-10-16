#pragma once
#include "esp_err.h"
#include <cstdint>
using gpio_num_t = int;
enum gpio_mode_t { GPIO_MODE_INPUT = 1, GPIO_MODE_OUTPUT = 2 };
enum gpio_pullup_t { GPIO_PULLUP_DISABLE = 0, GPIO_PULLUP_ENABLE = 1 };
enum gpio_pulldown_t { GPIO_PULLDOWN_DISABLE = 0, GPIO_PULLDOWN_ENABLE = 1 };
enum gpio_int_type_t { GPIO_INTR_DISABLE = 0 };
struct gpio_config_t {
    std::uint64_t pin_bit_mask = 0;
    gpio_mode_t mode = GPIO_MODE_INPUT;
    gpio_pullup_t pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_pulldown_t pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_int_type_t intr_type = GPIO_INTR_DISABLE;
};
esp_err_t gpio_set_level(gpio_num_t pin, std::uint32_t level);
esp_err_t gpio_config(const gpio_config_t* config);
int gpio_get_level(gpio_num_t pin);
