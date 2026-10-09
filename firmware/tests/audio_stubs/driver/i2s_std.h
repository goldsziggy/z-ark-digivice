#pragma once
#include "esp_err.h"
#include <cstddef>
using i2s_chan_handle_t=void*;
constexpr int I2S_NUM_0=0,I2S_ROLE_MASTER=1,I2S_DATA_BIT_WIDTH_16BIT=16,I2S_SLOT_MODE_STEREO=2,
    I2S_GPIO_UNUSED=-1,GPIO_NUM_48=48,GPIO_NUM_38=38,GPIO_NUM_47=47;
struct i2s_chan_config_t {unsigned dma_desc_num=0,dma_frame_num=0;bool auto_clear=false;};
inline i2s_chan_config_t I2S_CHANNEL_DEFAULT_CONFIG(int,int){return {};}
struct i2s_std_clk_config_t {unsigned sample_rate_hz=0;};
inline i2s_std_clk_config_t I2S_STD_CLK_DEFAULT_CONFIG(unsigned rate){return {rate};}
struct i2s_std_slot_config_t {unsigned data_bit_width=0,slot_mode=0;};
inline i2s_std_slot_config_t I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(unsigned bits,unsigned slots){return {bits,slots};}
struct i2s_std_config_t {
    i2s_std_clk_config_t clk_cfg; i2s_std_slot_config_t slot_cfg;
    struct {int mclk=0,bclk=0,ws=0,dout=0,din=0;} gpio_cfg;
};
esp_err_t i2s_new_channel(const i2s_chan_config_t*,i2s_chan_handle_t*,i2s_chan_handle_t*);
esp_err_t i2s_del_channel(i2s_chan_handle_t);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t,const i2s_std_config_t*);
esp_err_t i2s_channel_preload_data(i2s_chan_handle_t,const void*,std::size_t,std::size_t*);
esp_err_t i2s_channel_enable(i2s_chan_handle_t);
esp_err_t i2s_channel_disable(i2s_chan_handle_t);
esp_err_t i2s_channel_write(i2s_chan_handle_t,const void*,std::size_t,std::size_t*,unsigned);
