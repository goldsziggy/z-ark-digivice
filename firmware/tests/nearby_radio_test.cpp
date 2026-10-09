#include "nearby_radio.hpp"
#include "esp_wifi.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace digivice::nearby;
namespace {
unsigned checks=0;void check(bool condition,const char* expression,int line){++checks;if(!condition){std::fprintf(stderr,"line%d: %s\n",line,expression);std::exit(1);}}
#define CHECK(x) check((x),#x,__LINE__)
Radio radio;
std::uint64_t clockMs=0;
wifi_mode_t mode=WIFI_MODE_STA;
std::uint8_t channel=1;
std::uint8_t selfMac[6]{2,1,2,3,4,5},peerMac[6]{2,9,8,7,6,5},otherMac[6]{2,4,3,2,1,9},broadcastMac[6]{255,255,255,255,255,255};
esp_now_recv_cb_t receiveCallback=nullptr;
esp_now_send_cb_t sendCallback=nullptr;
bool wifi=false,now=false,immediateSend=false,failQueue=false;
const char* failure=nullptr;
unsigned starts=0,stops=0,adds=0,sendCalls=0,deinits=0,pmks=0;
esp_now_peer_info_t lastPeer{};
void (*afterUnlock)()=nullptr;
void (*afterQueue)()=nullptr;
void (*afterSend)()=nullptr;
void hook(void (*&fn)()){auto saved=fn;fn=nullptr;if(saved)saved();}
esp_err_t result(const char* name){return failure && std::strcmp(failure,name)==0?ESP_FAIL:ESP_OK;}
void reset(){CHECK(radio.end()==ESP_OK);CHECK(radio.quiescent());failure=nullptr;mode=WIFI_MODE_STA;clockMs=0;channel=1;failQueue=immediateSend=false;afterUnlock=afterQueue=afterSend=nullptr;}
void begin(){CHECK(radio.begin()==ESP_OK);CHECK(radio.status().active);CHECK(!radio.quiescent());CHECK(wifi&&now);CHECK(channel==1);}
void emit(const std::uint8_t* source=peerMac,const std::uint8_t* destination=broadcastMac,int size=3){
    std::array<std::uint8_t,241> bytes{};bytes[0]=17;bytes[1]=29;bytes[2]=41;
    wifi_pkt_rx_ctrl_t ctrl{-67};esp_now_recv_info_t info{const_cast<std::uint8_t*>(source),const_cast<std::uint8_t*>(destination),&ctrl};
    CHECK(receiveCallback!=nullptr);receiveCallback(&info,bytes.data(),size);bytes.fill(0);ctrl.rssi=0;
}
void complete(const std::uint8_t* destination=broadcastMac,esp_now_send_status_t status=ESP_NOW_SEND_SUCCESS){CHECK(sendCallback!=nullptr);sendCallback(destination,status);}
void pendingCallbackChecks(){RadioSendResult output{};CHECK(!radio.sendResult(output));CHECK(radio.sendBroadcast("x",1,9)==ESP_ERR_INVALID_STATE);}
void callbackDuringEnd(){CHECK(radio.end()==ESP_ERR_NOT_FINISHED);CHECK(!radio.quiescent());CHECK(radio.begin()==ESP_ERR_INVALID_STATE);}
}
void testEnter(portMUX_TYPE* lock){CHECK(!lock->held);lock->held=true;}
void testExit(portMUX_TYPE* lock){CHECK(lock->held);lock->held=false;hook(afterUnlock);}
QueueHandle_t xQueueCreateStatic(UBaseType_t slots,UBaseType_t size,std::uint8_t* bytes,StaticQueue_t* queue){if(failQueue)return nullptr;*queue={bytes,slots,size,0,0};return queue;}
BaseType_t xQueueReset(QueueHandle_t q){q->read=q->count=0;return pdTRUE;}
BaseType_t xQueueSend(QueueHandle_t q,const void* value,TickType_t wait){CHECK(wait==0);if(q->count==q->slots)return pdFALSE;std::memcpy(q->bytes+((q->read+q->count)%q->slots)*q->size,value,q->size);++q->count;hook(afterQueue);return pdTRUE;}
BaseType_t xQueueReceive(QueueHandle_t q,void* value,TickType_t wait){CHECK(wait==0);if(!q->count)return pdFALSE;std::memcpy(value,q->bytes+q->read*q->size,q->size);q->read=(q->read+1)%q->slots;--q->count;return pdTRUE;}
BaseType_t xQueueOverwrite(QueueHandle_t q,const void* value){CHECK(q->slots==1);q->read=0;q->count=1;std::memcpy(q->bytes,value,q->size);hook(afterQueue);return pdTRUE;}
std::int64_t esp_timer_get_time(){return static_cast<std::int64_t>(clockMs*1000);}
void esp_fill_random(void* data,std::size_t length){CHECK(length==16);std::memset(data,0xa7,length);}
esp_err_t esp_wifi_get_mode(wifi_mode_t* value){*value=mode;return result("mode");}
esp_err_t esp_wifi_get_mac(wifi_interface_t iface,std::uint8_t* value){CHECK(iface==WIFI_IF_STA);std::memcpy(value,selfMac,6);return result("mac");}
esp_err_t esp_wifi_start(){++starts;auto r=result("start");if(r==ESP_OK)wifi=true;return r;}
esp_err_t esp_wifi_stop(){++stops;CHECK(!now);auto r=result("stop");if(r==ESP_OK)wifi=false;return r;}
esp_err_t esp_wifi_set_channel(std::uint8_t value,wifi_second_chan_t secondary){CHECK(wifi);CHECK(value==1&&secondary==WIFI_SECOND_CHAN_NONE);channel=value;return result("set-channel");}
esp_err_t esp_wifi_get_channel(std::uint8_t* value,wifi_second_chan_t* secondary){*value=channel;*secondary=WIFI_SECOND_CHAN_NONE;return result("get-channel");}
esp_err_t esp_now_init(){CHECK(wifi);auto r=result("init");if(r==ESP_OK)now=true;return r;}
esp_err_t esp_now_deinit(){++deinits;auto r=result("deinit");if(r==ESP_OK){now=false;receiveCallback=nullptr;sendCallback=nullptr;}return r;}
esp_err_t esp_now_set_pmk(const std::uint8_t* pmk){++pmks;for(unsigned i=0;i<16;++i)CHECK(pmk[i]==0xa7);return result("pmk");}
esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb){auto r=result("register-rx");if(r==ESP_OK)receiveCallback=cb;return r;}
esp_err_t esp_now_unregister_recv_cb(){auto r=result("unregister-rx");if(r==ESP_OK)receiveCallback=nullptr;return r;}
esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb){auto r=result("register-tx");if(r==ESP_OK)sendCallback=cb;return r;}
esp_err_t esp_now_unregister_send_cb(){auto r=result("unregister-tx");if(r==ESP_OK)sendCallback=nullptr;return r;}
esp_err_t esp_now_add_peer(const esp_now_peer_info_t* peer){++adds;lastPeer=*peer;CHECK(peer->channel==1&&peer->ifidx==WIFI_IF_STA);return result("add");}
esp_err_t esp_now_del_peer(const std::uint8_t* peer){CHECK(std::memcmp(peer,peerMac,6)==0);return result("delete");}
esp_err_t esp_now_send(const std::uint8_t* mac,const std::uint8_t* data,std::size_t length){++sendCalls;CHECK(data&&length>0&&length<=240);if(immediateSend)complete(mac);hook(afterSend);return result("send");}
int main(){
    // Failed setup rolls back only resources actually acquired, without Wi-Fi/NVS deinit.
    const char* stages[]{"mode","mac","start","set-channel","get-channel","init","pmk","register-rx","register-tx","add"};
    for(auto* stage:stages){reset();failure=stage;CHECK(radio.begin()==ESP_FAIL);CHECK(!radio.status().active);CHECK(!wifi&&!now);CHECK(radio.quiescent());failure=nullptr;}
    reset();mode=WIFI_MODE_AP;auto oldStarts=starts;CHECK(radio.begin()==ESP_ERR_INVALID_STATE);CHECK(starts==oldStarts);mode=WIFI_MODE_STA;
    begin();CHECK(std::memcmp(radio.identity(),selfMac,6)==0);CHECK(!lastPeer.encrypt);CHECK(std::memcmp(lastPeer.peer_addr,broadcastMac,6)==0);CHECK(radio.begin()==ESP_ERR_INVALID_STATE);
    Radio second;CHECK(second.begin()==ESP_ERR_INVALID_STATE);
    RadioPacket packet{};emit();CHECK(radio.receive(packet));CHECK(packet.size==3&&packet.bytes[0]==17&&packet.bytes[2]==41&&packet.rssi==-67&&packet.broadcast);CHECK(std::memcmp(packet.source,peerMac,6)==0);CHECK(!radio.receive(packet));
    // Invalid receive input, oversize, loopback, multicast source and other destinations are dropped.
    receiveCallback(nullptr,nullptr,0);emit(peerMac,broadcastMac,0);emit(peerMac,broadcastMac,241);emit(selfMac);emit(broadcastMac);emit(peerMac,otherMac);CHECK(!radio.receive(packet));
    emit(peerMac,broadcastMac,240);CHECK(radio.receive(packet)&&packet.size==240);
    std::uint8_t zero[16]{};CHECK(radio.selectPeer(peerMac,zero)==ESP_ERR_INVALID_ARG);CHECK(radio.selectPeer(selfMac)==ESP_ERR_INVALID_ARG);CHECK(radio.selectPeer(broadcastMac)==ESP_ERR_INVALID_ARG);
    std::uint8_t lmk[16];std::memset(lmk,0x31,16);CHECK(radio.selectPeer(peerMac,lmk)==ESP_OK);CHECK(lastPeer.encrypt&&std::memcmp(lastPeer.lmk,lmk,16)==0);CHECK(radio.status().encrypted);
    emit(otherMac,selfMac);CHECK(!radio.receive(packet));emit(peerMac,selfMac);CHECK(radio.receive(packet)&&!packet.broadcast);emit(otherMac);CHECK(radio.receive(packet));
    CHECK(radio.clearPeer()==ESP_OK);CHECK(!radio.status().peerSelected);CHECK(radio.sendPeer("x",1,1)==ESP_ERR_INVALID_STATE);
    CHECK(radio.selectPeer(peerMac)==ESP_OK);CHECK(!radio.status().encrypted);
    CHECK(radio.sendBroadcast(nullptr,1,1)==ESP_ERR_INVALID_ARG);CHECK(radio.sendBroadcast("",0,1)==ESP_ERR_INVALID_ARG);CHECK(radio.sendBroadcast("x",241,1)==ESP_ERR_INVALID_ARG);CHECK(radio.sendBroadcast("x",1,0)==ESP_ERR_INVALID_ARG);
    // A receive flood cannot use the independent completion slot. SDK callbacks may be immediate.
    for(unsigned i=0;i<10;++i)emit();CHECK(radio.status().receivedDropped==2);
    immediateSend=true;afterQueue=pendingCallbackChecks;CHECK(radio.sendPeer("abc",3,42)==ESP_OK);CHECK(radio.status().sending);CHECK(radio.selectPeer(peerMac)==ESP_ERR_INVALID_STATE);
    clockMs=2000;radio.tick();CHECK(!radio.status().recoveryRequired);RadioSendResult sent{};CHECK(radio.sendResult(sent));CHECK(sent.token==42&&sent.delivered);CHECK(!radio.status().sending);CHECK(!radio.sendResult(sent));
    for(unsigned i=0;i<8;++i)CHECK(radio.receive(packet));CHECK(!radio.receive(packet));
    // Failed MAC delivery is a result, not a transport teardown or application ACK.
    immediateSend=false;CHECK(radio.sendBroadcast("x",1,43)==ESP_OK);complete(otherMac);CHECK(!radio.sendResult(sent));complete(broadcastMac,ESP_NOW_SEND_FAIL);CHECK(radio.sendResult(sent)&&!sent.delivered);
    // Missing callback cannot release the send slot; a late callback cannot permit another request.
    CHECK(radio.sendBroadcast("x",1,44)==ESP_OK);clockMs=3500;radio.tick();CHECK(!radio.status().recoveryRequired);clockMs=3501;radio.tick();CHECK(radio.status().recoveryRequired);complete();CHECK(!radio.sendResult(sent));CHECK(radio.sendBroadcast("x",1,45)==ESP_ERR_INVALID_STATE);
    reset();begin();CHECK(radio.sendBroadcast("x",1,46)==ESP_OK);clockMs=1;complete();clockMs=10000;radio.tick();CHECK(radio.sendResult(sent)&&sent.token==46);CHECK(!radio.status().recoveryRequired);
    // Immediate rejection can be retried, but callback+rejection ambiguity fails closed.
    failure="send";CHECK(radio.sendBroadcast("x",1,47)==ESP_FAIL);CHECK(!radio.status().sending);failure=nullptr;CHECK(radio.sendBroadcast("x",1,48)==ESP_OK);complete();CHECK(radio.sendResult(sent));
    failure="send";immediateSend=true;CHECK(radio.sendBroadcast("x",1,49)==ESP_FAIL);CHECK(radio.status().recoveryRequired);failure=nullptr;reset();begin();
    // Owner registration/reservation is atomic with teardown. Callback paused immediately after
    // reservation prevents end/rebegin from reassigning it to a fresh transport generation.
    auto heldReceive=receiveCallback;afterUnlock=callbackDuringEnd;emit();CHECK(!radio.receive(packet));CHECK(!radio.quiescent());CHECK(radio.end()==ESP_OK);CHECK(radio.quiescent());heldReceive(nullptr,nullptr,0);CHECK(radio.quiescent());begin();CHECK(!radio.receive(packet));
    // Teardown after queue insertion also invalidates that message; app-lifetime storage survives.
    afterQueue=callbackDuringEnd;emit();CHECK(radio.end()==ESP_OK);begin();CHECK(!radio.receive(packet));
    // Do not stop borrowed Wi-Fi when ESP-NOW deinitialization has failed.
    failure="deinit";oldStarts=stops;CHECK(radio.end()==ESP_FAIL);CHECK(stops==oldStarts&&wifi&&now);CHECK(!radio.quiescent());CHECK(radio.begin()==ESP_ERR_INVALID_STATE);failure=nullptr;CHECK(radio.end()==ESP_OK);CHECK(radio.quiescent());
    begin();failure="stop";CHECK(radio.end()==ESP_FAIL);CHECK(!now&&wifi);CHECK(!radio.quiescent());failure=nullptr;CHECK(radio.end()==ESP_OK);
    begin();failure="unregister-rx";CHECK(radio.end()==ESP_FAIL);CHECK(radio.quiescent());failure=nullptr;
    begin();failure="delete";CHECK(radio.selectPeer(peerMac)==ESP_OK);CHECK(radio.clearPeer()==ESP_FAIL);CHECK(radio.status().peerSelected);failure=nullptr;CHECK(radio.clearPeer()==ESP_OK);CHECK(radio.end()==ESP_OK);
    std::printf("nearby radio: %u checks passed; Radio=%zuB Packet=%zuB; SDK doubles, no RF proof\n",checks,sizeof(Radio),sizeof(RadioPacket));
}
