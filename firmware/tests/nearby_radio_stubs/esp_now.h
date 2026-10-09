#pragma once
#include "esp_wifi.h"
#include <cstddef>
constexpr unsigned ESP_NOW_KEY_LEN=16,ESP_NOW_MAX_DATA_LEN=250;
enum esp_now_send_status_t {ESP_NOW_SEND_SUCCESS,ESP_NOW_SEND_FAIL};
struct esp_now_recv_info_t {std::uint8_t* src_addr;std::uint8_t* des_addr;wifi_pkt_rx_ctrl_t* rx_ctrl;};
using esp_now_recv_cb_t=void(*)(const esp_now_recv_info_t*,const std::uint8_t*,int);
using esp_now_send_cb_t=void(*)(const std::uint8_t*,esp_now_send_status_t);
struct esp_now_peer_info_t {std::uint8_t peer_addr[6],lmk[16],channel;wifi_interface_t ifidx;bool encrypt;void* priv;};
esp_err_t esp_now_init();esp_err_t esp_now_deinit();esp_err_t esp_now_set_pmk(const std::uint8_t*);
esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t);esp_err_t esp_now_unregister_recv_cb();
esp_err_t esp_now_register_send_cb(esp_now_send_cb_t);esp_err_t esp_now_unregister_send_cb();
esp_err_t esp_now_add_peer(const esp_now_peer_info_t*);esp_err_t esp_now_del_peer(const std::uint8_t*);
esp_err_t esp_now_send(const std::uint8_t*,const std::uint8_t*,std::size_t);
