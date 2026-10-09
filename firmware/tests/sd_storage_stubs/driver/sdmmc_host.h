#pragma once
using gpio_num_t=int;
constexpr gpio_num_t GPIO_NUM_NC=-1;
constexpr int SDMMC_SLOT_FLAG_INTERNAL_PULLUP=1;
struct sdmmc_host_t {};
struct sdmmc_slot_config_t {int width;gpio_num_t clk,cmd,d0,d1,d2,d3,d4,d5,d6,d7,cd,wp;int flags;};
#define SDMMC_HOST_DEFAULT() sdmmc_host_t{}
#define SDMMC_SLOT_CONFIG_DEFAULT() sdmmc_slot_config_t{}
