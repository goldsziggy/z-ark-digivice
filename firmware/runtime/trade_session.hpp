#pragma once
#include "trade_store.hpp"
#include "save_store.hpp"

namespace digivice::devicetrade {
// Main-task persistence coordinator. No radio/clock and no automatic timeout
// rollback. A receipt is durable before runtime authorizes its protocol packet.
class Session {
public:
    Session(State& state,storage::SaveStore& saves,Backend& backend):state_(state),saves_(saves),journal_(backend){}
    bool restore();
    bool prepare(const trade::Transcript&,unsigned side,bool priorPeerTerminal);
    bool commit();
    bool applyCommitted();
    bool abort(const trade::Transcript&,unsigned side,bool priorPeerTerminal);
    const trade::Record* record()const{return journal_.record();}
    bool healthy()const{return healthy_;}
    bool blocksForeground()const;
    bool freezesGameWrites()const;
    bool permitsBackground()const;
    const char* diagnostic()const{return diagnostic_;}
private:
    bool fault(const char* reason);
    bool careMatches(const trade::Record&)const;
    bool mayReplace(bool priorPeerTerminal)const;
    State& state_;storage::SaveStore& saves_;Store journal_;
    trade::Record candidate_{};
    bool healthy_=false;
    const char* diagnostic_="trade session not restored";
};
} // namespace digivice::devicetrade
