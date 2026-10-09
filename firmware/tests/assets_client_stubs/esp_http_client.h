#pragma once
#include "esp_err.h"
#include <cstdint>
struct FakeHttp;using esp_http_client_handle_t=FakeHttp*;
enum esp_http_client_event_id_t {HTTP_EVENT_ON_HEADER};
struct esp_http_client_event_t {esp_http_client_event_id_t event_id;void* user_data;char* header_key;char* header_value;};
enum esp_http_client_method_t {HTTP_METHOD_GET};
struct esp_http_client_config_t {const char*url;esp_http_client_method_t method;int timeout_ms;bool disable_auto_redirect;int max_authorization_retries;int buffer_size;int buffer_size_tx;bool skip_cert_common_name_check;esp_err_t(*event_handler)(esp_http_client_event_t*);void*user_data;esp_err_t(*crt_bundle_attach)(void*);};
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*);
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t,int);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char*,const char*);
esp_err_t esp_http_client_open(esp_http_client_handle_t,int);
std::int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
bool esp_http_client_is_chunked_response(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t,char*,int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
esp_err_t esp_http_client_close(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
