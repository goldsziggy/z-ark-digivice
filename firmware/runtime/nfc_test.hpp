#pragma once
#include "pn532_hsu.hpp"
#include <cstdio>
#include <limits>

// Ephemeral diagnostics only. No game, allowlist, persistence or reward access.
namespace digivice::nfctest {
struct View {
    bool available=false, connected=false, scanning=false, rfOff=false, armed=true;
    unsigned firmwareMajor=0, firmwareMinor=0;
    std::uint32_t swipes=0;
    char uid[21]{}, type[32]{};
    const char* state="NOT STARTED";
    const char* error="none";
};
class Tracker {
public:
    const View& view() const { return view_; }
    void begin(bool available) {
        view_={}; view_.available=available; absent_=0; absentAt_=0;
        view_.state=available ? "CONNECTING" : "UNSUPPORTED PROFILE";
    }
    void scanning() { view_.scanning=true; view_.rfOff=false; view_.state=view_.connected ? "SCANNING TYPE A" : "CONNECTING"; }
    void suspend() { view_.scanning=false; view_.armed=false; absent_=0; view_.state="PAUSED"; }
    void reset() { view_.swipes=0; view_.uid[0]=view_.type[0]=0; } // Never re-arm a held card.
    void retry() { view_.error="none"; view_.connected=false; view_.state="CONNECTING"; }
    void fault(const char* error) {
        view_.scanning=false; view_.connected=false; view_.armed=false; absent_=0;
        view_.error=error; view_.state="READER ERROR";
    }
    void observe(const nfcdiag::Result& r, std::uint64_t now) {
        view_.scanning=false; view_.rfOff=r.rfOff;
        if(std::strcmp(r.error,"none") || !r.rfOff || !r.samReady || !r.firmwareValid) {
            fault(std::strcmp(r.error,"none") ? r.error : "unconfirmed"); return;
        }
        view_.connected=true; view_.error="none";
        view_.firmwareMajor=r.firmware[1]; view_.firmwareMinor=r.firmware[2];
        if(r.uidValid && (r.uidSize==4 || r.uidSize==7 || r.uidSize==10)) {
            for(unsigned i=0;i<r.uidSize;++i) std::snprintf(view_.uid+2*i,3,"%02X",r.uid[i]);
            std::snprintf(view_.type,sizeof(view_.type),"TYPE A / SAK %02X",r.sak);
            if(view_.armed && view_.swipes<std::numeric_limits<std::uint32_t>::max()) ++view_.swipes;
            view_.armed=false; absent_=0; view_.state="CARD HELD - REMOVE";
        } else if(r.noCard) {
            if(!absent_) absentAt_=now;
            if(absent_<2) ++absent_;
            if(absent_>=2 && now>=absentAt_ && now-absentAt_>=150) view_.armed=true;
            view_.state=view_.armed ? "NO CARD - SWIPE NOW" : "CHECKING REMOVAL";
        } else fault("invalid_observation");
    }
private:
    View view_{};
    unsigned absent_=0;
    std::uint64_t absentAt_=0;
};
}
