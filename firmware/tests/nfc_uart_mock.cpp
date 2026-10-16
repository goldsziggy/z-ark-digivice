#include "nfc_uart_mock.hpp"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <cassert>
#include <algorithm>
#include <deque>
extern std::int64_t esp_timer_get_time();
namespace nfcmock {
bool installed=false,silent=false,corrupt=false,failOff=false,badFirmware=false,wrongResponse=false;
unsigned calls=0,writes=0,reads=0,opens=0,closes=0,scans=0,offCommands=0,readChunk=64,writeChunk=16;
int eventError=0;
unsigned responseDelayMs=0,samCommands=0,firmwareBeforeSam=0;
bool normal=false;
std::uint64_t responseDue=0;
std::vector<std::uint8_t> pendingResponse;
std::vector<std::uint8_t> uid,tx;
std::deque<std::uint8_t> rx;
void reset(){installed=silent=corrupt=failOff=badFirmware=wrongResponse=false;calls=writes=reads=opens=closes=scans=offCommands=0;eventError=0;responseDelayMs=samCommands=firmwareBeforeSam=0;normal=false;pendingResponse.clear();readChunk=64;writeChunk=16;uid.clear();rx.clear();tx.clear();}
void check(int port){++calls;assert(port==1);}
void response(unsigned command,std::vector<std::uint8_t> data){
 if(silent)return;
 for(auto b:{0,0,255,0,255,0})rx.push_back(b);
 std::vector<std::uint8_t> f{0,0,255,static_cast<std::uint8_t>(data.size()+2),static_cast<std::uint8_t>(-(data.size()+2)),0xd5,static_cast<std::uint8_t>(command+1+(wrongResponse?2:0))};
 f.insert(f.end(),data.begin(),data.end());unsigned sum=0;for(std::size_t i=5;i<f.size();++i)sum+=f[i];
 f.push_back(static_cast<std::uint8_t>(-sum+(corrupt?1:0)));f.push_back(0);
 if(responseDelayMs){pendingResponse=f;responseDue=esp_timer_get_time()/1000+responseDelayMs;}else for(auto b:f)rx.push_back(b);
}
}
using namespace nfcmock;
bool uart_is_driver_installed(uart_port_t p){check(p);return installed;}
esp_err_t uart_param_config(uart_port_t p,const uart_config_t* c){check(p);assert(c->baud_rate==115200&&c->data_bits==UART_DATA_8_BITS&&c->parity==0&&c->stop_bits==1&&c->flow_ctrl==0);return ESP_OK;}
esp_err_t uart_driver_install(uart_port_t p,int size,int txBytes,int count,QueueHandle_t* q,int flags){check(p);assert(size==256&&txBytes==0&&count==8&&flags==0&&!installed);installed=true;++opens;*q=&installed;return ESP_OK;}
esp_err_t uart_set_pin(uart_port_t p,int txPin,int rxPin,int rts,int cts){check(p);assert(txPin==43&&rxPin==44&&rts==-1&&cts==-1);return ESP_OK;}
esp_err_t uart_driver_delete(uart_port_t p){check(p);assert(installed);installed=false;++closes;tx.clear();rx.clear();return ESP_OK;}
esp_err_t uart_flush_input(uart_port_t p){check(p);rx.clear();tx.clear();pendingResponse.clear();return ESP_OK;}
esp_err_t gpio_reset_pin(gpio_num_t p){++calls;assert(p==43||p==44);return ESP_OK;}
int xQueueReset(QueueHandle_t q){assert(q==&installed);eventError=0;return 1;}
int xQueueReceive(QueueHandle_t q,void* out,TickType_t wait){assert(q==&installed&&wait==0);if(eventError){static_cast<uart_event_t*>(out)->type=eventError;eventError=0;return 1;}return 0;}
int uart_tx_chars(uart_port_t p,const char* b,std::uint32_t n){
 check(p);++writes;assert(installed&&n<=16);const auto sent=std::min(n,writeChunk);tx.insert(tx.end(),b,b+sent);
 if(tx[0]==0x55){if(tx.size()==16)tx.clear();return sent;}
 if(tx.size()==6&&tx[3]==0){tx.clear();return sent;}
 if(tx.size()<5||tx.size()!=std::size_t(tx[3])+7)return sent;
 const unsigned command=tx[6];std::vector<std::uint8_t> data;
 if(command==0x14){normal=true;++samCommands;}
 if(command==2&&!normal)++firmwareBeforeSam;
 if(!normal){tx.clear();return sent;} // Cold reader requires SAM normal mode first.
 if(command==2)data={static_cast<std::uint8_t>(badFirmware?0:0x32),1,6,7};
 if(command==0x4a){++scans;if(uid.empty())data={0};else {data={1,1,0,4,0,static_cast<std::uint8_t>(uid.size())};data.insert(data.end(),uid.begin(),uid.end());}}
 if(command==0x32&&tx[7]==1){++offCommands;if(failOff)data={1};}
 response(command,data);tx.clear();return sent;
}
int uart_read_bytes(uart_port_t p,void* bytes,std::uint32_t n,TickType_t wait){
 check(p);++reads;assert(installed&&n<=64&&wait==0);unsigned count=0;auto* b=static_cast<std::uint8_t*>(bytes);
 if(!pendingResponse.empty()&&static_cast<std::uint64_t>(esp_timer_get_time()/1000)>=responseDue){for(auto byte:pendingResponse)rx.push_back(byte);pendingResponse.clear();}
 while(count<std::min(n,readChunk)&&!rx.empty()){b[count++]=rx.front();rx.pop_front();}return count;
}
