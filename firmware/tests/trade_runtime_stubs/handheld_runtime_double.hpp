#pragma once
#define CONFIG_DIGIVICE_DISPLAY_TOUCH 1
#include "esp_err.h"
#include "trade_session.hpp"
#include "trade_protocol.hpp"
#include "nearby_protocol.hpp"
#include "device_ui.hpp"
#include <cstring>
namespace digivice {
struct TradeJournalDouble final : devicetrade::Backend {
    devicetrade::Slot slots[2]{};bool present[2]{};unsigned writes=0,fail=0;bool landed=false;
    esp_err_t initialize(){return ESP_OK;}
    devicetrade::Read read(unsigned i,devicetrade::Slot& out)override{if(!present[i])return devicetrade::Read::Missing;out=slots[i];return devicetrade::Read::Present;}
    bool write(unsigned i,const devicetrade::Bytes& bytes)override{++writes;if(writes==fail&&!landed)return false;slots[i].bytes=bytes;slots[i].length=devicetrade::kJournalBytes;present[i]=true;return writes!=fail;}
};
class HandheldRuntime {
public:
    HandheldRuntime(State& s,storage::SaveStore& saves,TradeJournalDouble& backend):state_(s),saves_(saves),tradeBackend_(backend){}
    void beginTradeStorage();bool openTradeRadio(std::uint64_t);void pollTradePersistence(std::uint64_t);
    void tradeIntent(deviceui::Intent);bool tradeNegotiating()const;
    enum class NearbyPhase { Idle,Active };
    void closeNearby(){nearbyPhase_=NearbyPhase::Idle;tradeWire_.close();}
    struct Radio {trade::Identity mac{};const std::uint8_t* identity()const{return mac.bytes;}} nearbyRadio_;
    struct Ui {unsigned cancels=0;void cancelTouch(){++cancels;}} ui_;
    State& state_;storage::SaveStore& saves_;TradeJournalDouble& tradeBackend_;
    devicetrade::Session tradeSession_{state_,saves_,tradeBackend_};
    tradewire::Protocol tradeWire_;nearby::Protocol nearby_;
    NearbyPhase nearbyPhase_=NearbyPhase::Active;
    bool tradePeerTerminal_=false,touchNeedsRelease_=false,interfaceDirty_=false;
    const char* tradeStatus_="";
};
}
