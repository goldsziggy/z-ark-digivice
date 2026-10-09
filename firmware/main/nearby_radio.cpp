#include "nearby_radio.hpp"
#include "esp_wifi.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <cstring>

// API pinned to ESP-IDF5.3.6/79e3454, components/esp_wifi/include/esp_now.h.
// https://github.com/espressif/esp-idf/blob/79e3454c68248bd7d881721c9b2e94561378a8ce/docs/en/api-reference/network/esp_now.rst
// Wi-Fi-task callbacks must not allocate, wait, log, parse gameplay or touch NVS.
namespace digivice::nearby {
namespace {
Radio* owner = nullptr;
portMUX_TYPE dispatchLock = portMUX_INITIALIZER_UNLOCKED;
constexpr std::uint8_t broadcast[6]{255,255,255,255,255,255};
std::uint64_t nowMs() { return static_cast<std::uint64_t>(esp_timer_get_time()/1000); }
bool equal(const std::uint8_t* a,const std::uint8_t* b) { return std::memcmp(a,b,6)==0; }
bool unicast(const std::uint8_t* mac) {
    if (!mac || (mac[0]&1)) return false;
    std::uint8_t any=0;for(unsigned i=0;i<6;++i)any|=mac[i];return any!=0;
}
void wipe(void* memory,std::size_t bytes) {
    auto* p=static_cast<volatile std::uint8_t*>(memory);while(bytes--)*p++=0;
}
}
esp_err_t Radio::begin() {
    if (initialized_ || wifiStarted_ || recoveryRequired_ || callbacks_.load()) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&dispatchLock);
    const bool available=owner==nullptr;
    if(available)owner=this;
    portEXIT_CRITICAL(&dispatchLock);
    if(!available)return ESP_ERR_INVALID_STATE;
    startedThisLease_=false;
    wifi_mode_t mode{};
    auto error=esp_wifi_get_mode(&mode);
    if (error==ESP_OK && mode!=WIFI_MODE_STA) error=ESP_ERR_INVALID_STATE;
    if (error==ESP_OK) error=esp_wifi_get_mac(WIFI_IF_STA,identity_);
    if (error==ESP_OK && !unicast(identity_)) error=ESP_ERR_INVALID_STATE;
    if (!receives_) receives_=xQueueCreateStatic(kRadioReceiveSlots,sizeof(RadioPacket),receiveBytes_,&receiveQueue_);
    if (!sends_) sends_=xQueueCreateStatic(1,sizeof(RadioSendResult),sendBytes_,&sendQueue_);
    if (!receives_ || !sends_) error=ESP_ERR_NO_MEM;
    if (error==ESP_OK) { error=esp_wifi_start(); if(error==ESP_OK)wifiStarted_=startedThisLease_=true; }
    if (error==ESP_OK) error=esp_wifi_set_channel(kRadioChannel,WIFI_SECOND_CHAN_NONE);
    std::uint8_t channel=0;wifi_second_chan_t secondary{};
    if (error==ESP_OK) error=esp_wifi_get_channel(&channel,&secondary);
    if (error==ESP_OK && (channel!=kRadioChannel || secondary!=WIFI_SECOND_CHAN_NONE)) error=ESP_ERR_INVALID_STATE;
    if (error==ESP_OK) { error=esp_now_init();if(error==ESP_OK)initialized_=true; }
    if (error==ESP_OK) {
        std::uint8_t pmk[ESP_NOW_KEY_LEN];esp_fill_random(pmk,sizeof(pmk));
        error=esp_now_set_pmk(pmk);wipe(pmk,sizeof(pmk));
    }
    if (error==ESP_OK) { error=esp_now_register_recv_cb(received);if(error==ESP_OK)recvRegistered_=true; }
    if (error==ESP_OK) { error=esp_now_register_send_cb(sent);if(error==ESP_OK)sendRegistered_=true; }
    if (error==ESP_OK) {
        esp_now_peer_info_t peer{};
        std::memcpy(peer.peer_addr,broadcast,6);peer.channel=kRadioChannel;peer.ifidx=WIFI_IF_STA;
        error=esp_now_add_peer(&peer);
    }
    if (error!=ESP_OK) {
        const auto cleanup=end();
        error_=error;
        if(cleanup!=ESP_OK)recoveryRequired_=true;
        return error;
    }
    xQueueReset(receives_);xQueueReset(sends_);
    portENTER_CRITICAL(&callbackLock_);
    ++generation_;txPending_=txCompleted_=false;peerSelected_=false;encrypted_=false;
    accepting_=true;
    portEXIT_CRITICAL(&callbackLock_);
    error_=ESP_OK;return ESP_OK;
}
esp_err_t Radio::end() {
    portENTER_CRITICAL(&callbackLock_);
    accepting_=false;++generation_;
    portEXIT_CRITICAL(&callbackLock_);
    esp_err_t first=ESP_OK;
    if (recvRegistered_) {
        const auto e=esp_now_unregister_recv_cb();
        if(e==ESP_OK || e==ESP_ERR_ESPNOW_NOT_INIT)recvRegistered_=false;else first=e;
    }
    if (sendRegistered_) {
        const auto e=esp_now_unregister_send_cb();
        if(e==ESP_OK || e==ESP_ERR_ESPNOW_NOT_INIT)sendRegistered_=false;else if(first==ESP_OK)first=e;
    }
    if (initialized_) {
        const auto e=esp_now_deinit();
        if(e==ESP_OK || e==ESP_ERR_ESPNOW_NOT_INIT) {
            initialized_=false;recvRegistered_=sendRegistered_=false;
        } else if(first==ESP_OK)first=e;
    }
    if (wifiStarted_ && !initialized_ && !recvRegistered_ && !sendRegistered_) {
        const auto e=esp_wifi_stop();
        if(e==ESP_OK)wifiStarted_=false;else if(first==ESP_OK)first=e;
    }
    portENTER_CRITICAL(&dispatchLock);
    const bool released=!initialized_ && !wifiStarted_ && !recvRegistered_ && !sendRegistered_ && !callbacks_.load();
    if(released && owner==this)owner=nullptr;
    portEXIT_CRITICAL(&dispatchLock);
    if (released) {
        portENTER_CRITICAL(&callbackLock_);
        txPending_=txCompleted_=peerSelected_=encrypted_=false;token_=0;
        wipe(peer_,sizeof(peer_));wipe(sendingTo_,sizeof(sendingTo_));
        portEXIT_CRITICAL(&callbackLock_);
        recoveryRequired_=false;
    } else {
        recoveryRequired_=true;
        if(first==ESP_OK)first=ESP_ERR_NOT_FINISHED;
    }
    error_=first;return first;
}
esp_err_t Radio::clearPeer() {
    if (!accepting_.load() || recoveryRequired_ || txPending_) return ESP_ERR_INVALID_STATE;
    if (!peerSelected_) return ESP_OK;
    const auto error=esp_now_del_peer(peer_);
    if(error!=ESP_OK && error!=ESP_ERR_ESPNOW_NOT_FOUND)return error_=error;
    portENTER_CRITICAL(&callbackLock_);
    peerSelected_=encrypted_=false;wipe(peer_,sizeof(peer_));
    portEXIT_CRITICAL(&callbackLock_);
    return ESP_OK;
}
esp_err_t Radio::selectPeer(const std::uint8_t mac[6],const std::uint8_t* lmk) {
    if (!unicast(mac) || equal(mac,identity_)) return ESP_ERR_INVALID_ARG;
    if (!accepting_.load() || recoveryRequired_ || txPending_) return ESP_ERR_INVALID_STATE;
    if(lmk) {std::uint8_t any=0;for(unsigned i=0;i<ESP_NOW_KEY_LEN;++i)any|=lmk[i];if(!any)return ESP_ERR_INVALID_ARG;}
    const auto cleared=clearPeer();if(cleared!=ESP_OK)return cleared;
    esp_now_peer_info_t info{};
    std::memcpy(info.peer_addr,mac,6);info.channel=kRadioChannel;info.ifidx=WIFI_IF_STA;info.encrypt=lmk!=nullptr;
    if(lmk)std::memcpy(info.lmk,lmk,ESP_NOW_KEY_LEN);
    const auto error=esp_now_add_peer(&info);wipe(&info,sizeof(info));
    if(error!=ESP_OK)return error_=error;
    portENTER_CRITICAL(&callbackLock_);
    std::memcpy(peer_,mac,6);peerSelected_=true;encrypted_=lmk!=nullptr;
    portEXIT_CRITICAL(&callbackLock_);
    return ESP_OK;
}
esp_err_t Radio::send(const std::uint8_t* destination,const void* bytes,std::size_t length,std::uint32_t token) {
    if(!bytes || !length || length>kRadioPacketBytes || !token)return ESP_ERR_INVALID_ARG;
    if(!accepting_.load() || recoveryRequired_ || txPending_ || callbacks_.load())return ESP_ERR_INVALID_STATE;
    // Reserve before SDK call: a send callback may precede its return.
    portENTER_CRITICAL(&callbackLock_);
    token_=token;std::memcpy(sendingTo_,destination,6);txPending_=true;txCompleted_=false;
    portEXIT_CRITICAL(&callbackLock_);
    sentAtMs_=nowMs();
    const auto error=esp_now_send(destination,static_cast<const std::uint8_t*>(bytes),length);
    if(error!=ESP_OK) {
        portENTER_CRITICAL(&callbackLock_);
        const bool ambiguous=txCompleted_ || callbacks_.load()!=0;
        if(!ambiguous)txPending_=false;
        portEXIT_CRITICAL(&callbackLock_);
        // SDK rejects the request without scheduling a callback. If an observed
        // callback contradicts that contract, require teardown before reuse.
        if(ambiguous)recoveryRequired_=true;else xQueueReset(sends_);
        error_=error;
    }
    return error;
}
esp_err_t Radio::sendBroadcast(const void* bytes,std::size_t length,std::uint32_t token) {
    return send(broadcast,bytes,length,token);
}
esp_err_t Radio::sendPeer(const void* bytes,std::size_t length,std::uint32_t token) {
    if(!peerSelected_)return ESP_ERR_INVALID_STATE;
    return send(peer_,bytes,length,token);
}
bool Radio::receive(RadioPacket& packet) {
    if(!receives_ || !accepting_.load() || recoveryRequired_)return false;
    for(unsigned i=0;i<kRadioReceiveSlots;++i) {
        if(xQueueReceive(receives_,&packet,0)!=pdTRUE)return false;
        if(packet.generation==generation_.load())return true;
    }
    return false;
}
bool Radio::sendResult(RadioSendResult& result) {
    if(!sends_ || !accepting_.load() || recoveryRequired_ || !txPending_ || callbacks_.load())return false;
    if(xQueueReceive(sends_,&result,0)!=pdTRUE)return false;
    if(result.generation!=generation_.load() || result.token!=token_)return false;
    portENTER_CRITICAL(&callbackLock_);txPending_=txCompleted_=false;portEXIT_CRITICAL(&callbackLock_);
    return true;
}
void Radio::tick() {
    if(!accepting_.load() || recoveryRequired_ || !txPending_)return;
    portENTER_CRITICAL(&callbackLock_);
    const bool completed=txCompleted_;
    portEXIT_CRITICAL(&callbackLock_);
    if(completed)return; // Owner may be delayed after the callback already queued its result.
    const auto now=nowMs();
    if(now<sentAtMs_ || now-sentAtMs_>1500) {
        // No fresh send after a lost callback: SDK callback has no request token.
        recoveryRequired_=true;error_=ESP_ERR_TIMEOUT;
    }
}
RadioStatus Radio::status() const {
    return {accepting_.load(),peerSelected_,encrypted_,txPending_,recoveryRequired_,dropped_.load(),error_};
}
bool Radio::quiescent() const {
    return !recoveryRequired_ && !accepting_.load() && !initialized_ && !wifiStarted_ && !recvRegistered_ && !sendRegistered_ && !callbacks_.load();
}
void Radio::received(const esp_now_recv_info_t* info,const std::uint8_t* bytes,int size) {
    portENTER_CRITICAL(&dispatchLock);
    auto* self=owner;
    if(self)++self->callbacks_;
    portEXIT_CRITICAL(&dispatchLock);
    if(!self)return;
    RadioPacket packet{};bool allowed=false;
    if(info && info->src_addr && info->des_addr && bytes && size>0 && size<=static_cast<int>(kRadioPacketBytes) && unicast(info->src_addr)) {
        portENTER_CRITICAL(&self->callbackLock_);
        if(self->accepting_.load() && !equal(info->src_addr,self->identity_)) {
            packet.broadcast=equal(info->des_addr,broadcast);
            allowed=(packet.broadcast || equal(info->des_addr,self->identity_)) &&
                (packet.broadcast || !self->peerSelected_ || equal(info->src_addr,self->peer_));
            if(allowed) {
                packet.generation=self->generation_.load();packet.size=static_cast<std::uint16_t>(size);
                std::memcpy(packet.source,info->src_addr,6);std::memcpy(packet.destination,info->des_addr,6);
                if(info->rx_ctrl)packet.rssi=info->rx_ctrl->rssi;
                std::memcpy(packet.bytes,bytes,static_cast<std::size_t>(size));
            }
        }
        portEXIT_CRITICAL(&self->callbackLock_);
    }
    if(allowed && xQueueSend(self->receives_,&packet,0)!=pdTRUE)++self->dropped_;
    portENTER_CRITICAL(&dispatchLock);
    --self->callbacks_;
    portEXIT_CRITICAL(&dispatchLock);
}
void Radio::sent(const std::uint8_t* mac,esp_now_send_status_t status) {
    portENTER_CRITICAL(&dispatchLock);
    auto* self=owner;
    if(self)++self->callbacks_;
    portEXIT_CRITICAL(&dispatchLock);
    if(!self)return;
    RadioSendResult result{};bool allowed=false;
    portENTER_CRITICAL(&self->callbackLock_);
    if(mac && self->accepting_.load() && self->txPending_ && equal(mac,self->sendingTo_)) {
        result={self->token_,self->generation_.load(),status==ESP_NOW_SEND_SUCCESS};allowed=true;
    }
    portEXIT_CRITICAL(&self->callbackLock_);
    if(allowed) {
        xQueueOverwrite(self->sends_,&result);
        portENTER_CRITICAL(&self->callbackLock_);
        if(self->accepting_.load() && self->generation_.load()==result.generation && self->token_==result.token)self->txCompleted_=true;
        portEXIT_CRITICAL(&self->callbackLock_);
    }
    portENTER_CRITICAL(&dispatchLock);
    --self->callbacks_;
    portEXIT_CRITICAL(&dispatchLock);
}
} // namespace digivice::nearby
