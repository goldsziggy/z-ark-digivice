#pragma once
#include "esp_err.h"
#include "freertos/queue.h"
#include <cstddef>
#include <cstdint>
using uart_port_t=int;
constexpr uart_port_t UART_NUM_1=1;
constexpr int UART_PIN_NO_CHANGE=-1, UART_DATA_8_BITS=3, UART_PARITY_DISABLE=0,
 UART_STOP_BITS_1=1, UART_HW_FLOWCTRL_DISABLE=0, UART_SCLK_DEFAULT=1;
enum {UART_DATA,UART_FIFO_OVF,UART_BUFFER_FULL,UART_FRAME_ERR,UART_PARITY_ERR,UART_BREAK};
struct uart_event_t { int type; };
struct uart_config_t { int baud_rate,data_bits,parity,stop_bits,flow_ctrl,source_clk; };
bool uart_is_driver_installed(uart_port_t);
esp_err_t uart_param_config(uart_port_t,const uart_config_t*);
esp_err_t uart_driver_install(uart_port_t,int,int,int,QueueHandle_t*,int);
esp_err_t uart_set_pin(uart_port_t,int,int,int,int);
esp_err_t uart_driver_delete(uart_port_t);
esp_err_t uart_flush_input(uart_port_t);
int uart_tx_chars(uart_port_t,const char*,std::uint32_t);
int uart_read_bytes(uart_port_t,void*,std::uint32_t,TickType_t);
