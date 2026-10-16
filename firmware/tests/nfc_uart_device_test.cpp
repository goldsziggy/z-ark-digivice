#include "../main/device_nfc_diag.hpp"
#include "../main/board_hal.hpp"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <cassert>
#include <cstdio>
#include <deque>
#include <vector>
using namespace digivice;
static unsigned calls=0,removed=0,resets=0,writes=0;
static bool installed=false,normal=false;
static int failStep=0,step=0,eventError=0;
static board::ProfileId profile=board::ProfileId::Waveshare21;
static std::deque<unsigned char> rx;
static bool check(int port) { ++calls; assert(port==1);return ++step==failStep; }
namespace digivice::board { const BoardProfile& selectedProfile() { return board::profile(::profile); } }
const char* esp_err_to_name(esp_err_t e) { return e ? "error" : "ESP_OK"; }
bool uart_is_driver_installed(uart_port_t p) { ++calls;assert(p==1);return installed; }
esp_err_t uart_param_config(uart_port_t p,const uart_config_t* c) {
    assert(c->baud_rate==115200 && c->data_bits==UART_DATA_8_BITS && c->stop_bits==UART_STOP_BITS_1 &&
        c->flow_ctrl==UART_HW_FLOWCTRL_DISABLE && c->parity==UART_PARITY_DISABLE);
    return check(p)?ESP_FAIL:ESP_OK;
}
esp_err_t uart_driver_install(uart_port_t p,int size,int tx,int count,QueueHandle_t* q,int flags) {
    assert(size==256&&tx==0&&count==8&&flags==0);if(check(p))return ESP_FAIL;
    installed=true;*q=&installed;return ESP_OK;
}
esp_err_t uart_set_pin(uart_port_t p,int tx,int r,int rts,int cts) {
    assert(tx==43&&r==44&&rts==-1&&cts==-1);return check(p)?ESP_FAIL:ESP_OK;
}
esp_err_t uart_driver_delete(uart_port_t p) { check(p);assert(installed);installed=false;++removed;return ESP_OK; }
esp_err_t uart_flush_input(uart_port_t p) { rx.clear();return check(p)?ESP_FAIL:ESP_OK; }
esp_err_t gpio_reset_pin(gpio_num_t pin) { ++calls;++resets;assert(pin==43||pin==44);return ESP_OK; }
int xQueueReset(QueueHandle_t q) { assert(q==&installed);++calls;eventError=0;return 1; }
int xQueueReceive(QueueHandle_t q,void* out,TickType_t t) { assert(q==&installed&&t==0);++calls;if(eventError){static_cast<uart_event_t*>(out)->type=eventError;eventError=0;return 1;}return 0; }
int uart_tx_chars(uart_port_t p,const char* raw,std::uint32_t size) {
    check(p);++writes;const auto* b=reinterpret_cast<const unsigned char*>(raw);
    assert(size<=16);
    if(size>8 && b[0]==0 && b[2]==0xff) {
        if(b[6]==0x14)normal=true;
        if(!normal)return static_cast<int>(size);
        const unsigned char ack[]={0,0,0xff,0,0xff,0};for(auto n:ack)rx.push_back(n);
        std::vector<unsigned char> data=b[6]==2?std::vector<unsigned char>{0x32,1,6,7}:std::vector<unsigned char>{};
        std::vector<unsigned char> frame{0,0,0xff,static_cast<unsigned char>(data.size()+2),static_cast<unsigned char>(-(data.size()+2)),0xd5,static_cast<unsigned char>(b[6]+1)};
        frame.insert(frame.end(),data.begin(),data.end());unsigned char sum=0;for(std::size_t i=5;i<frame.size();++i)sum+=frame[i];
        frame.push_back(-sum);frame.push_back(0);for(auto n:frame)rx.push_back(n);
    }
    return static_cast<int>(size);
}
int uart_read_bytes(uart_port_t p,void* buffer,std::uint32_t size,TickType_t wait) {
    check(p);assert(wait==0);unsigned n=0;auto* b=static_cast<unsigned char*>(buffer);
    while(n<size&&!rx.empty()) {b[n++]=rx.front();rx.pop_front();}return n;
}
static void reset() { calls=removed=resets=writes=0;step=failStep=eventError=0;installed=normal=false;rx.clear();profile=board::ProfileId::Waveshare21; }
int main() {
    reset();nfcdiag::Device d;d.poll(0,true);d.status();assert(calls==0);
    d.poll(1,true,true);assert(!d.cardsConfigured()&&calls==0); // Empty production allowlist.
    assert(d.command("nfc probe extra",0,true));assert(d.command("nfc scan",0,false));assert(calls==0);
    assert(!d.command("nfcx scan",0,true));assert(!d.busy());
    profile=board::ProfileId::Waveshare146;d.command("nfc probe",0,true);assert(calls==0);
    profile=board::ProfileId::Waveshare21;
#ifdef TEST_DISABLED
    d.command("nfc probe",0,true);assert(calls==0);std::puts("UART adapter disabled: zero hardware calls passed.");
#else
    installed=true;d.command("nfc probe",0,true);assert(removed==0&&writes==0&&installed);installed=false;
    for(int fail=1;fail<=4;++fail) {
        reset();failStep=fail;nfcdiag::Device bad;bad.command("nfc probe",0,true);
        assert(!bad.busy()&&!installed&&writes==0);assert(removed==(fail>=3?1U:0U));
        assert(resets==(fail>=3?2U:0U));
    }
    reset();nfcdiag::Device live;live.command("nfc probe",0,true);assert(live.busy()&&installed&&writes==0);
    live.command("nfc scan",0,true);assert(writes==0); // Cannot replace an active operation.
    for(unsigned now=0;now<2000&&live.busy();++now)live.poll(now,true);
    assert(!live.busy()&&!installed&&removed==1&&resets==2&&writes==4);
    const auto before=calls;live.poll(2001,true);live.command("nfc status",2001,true);assert(calls==before);
    reset();nfcdiag::Device cancelled;cancelled.command("nfc probe",0,true);cancelled.poll(1,false);
    for(unsigned now=2;now<2000&&cancelled.busy();++now)cancelled.poll(now,false);
    assert(!cancelled.busy()&&!installed&&removed==1&&resets==2);
    for(int event: {UART_FIFO_OVF,UART_BUFFER_FULL,UART_FRAME_ERR,UART_PARITY_ERR,UART_BREAK}) {
        reset();nfcdiag::Device noisy;noisy.command("nfc probe",0,true);
        unsigned now=0;while(writes<2&&now<1000)noisy.poll(now++,true);eventError=event;
        while(noisy.busy()&&now<2000)noisy.poll(now++,true);
        assert(!noisy.busy()&&!installed&&removed==1&&resets==2);
    }
    std::puts("UART adapter: build/profile/runtime gates, setup faults, UART error events, existing owner, nonblocking I/O and cleanup passed.");
#endif
}
