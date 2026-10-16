#pragma once
#include "digi_modify.hpp"
#include "pn532_hsu.hpp"
#include "../../core/digi_modify_cards.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace digivice::nfccards {
struct Binding { std::uint8_t uid[10]{}; std::uint8_t size=0; std::uint32_t cardId=0; };
constexpr std::size_t kMaxBindings=16;
constexpr bool validUidSize(unsigned size){return size==4||size==7||size==10;}
constexpr bool sameUid(const Binding& a,const Binding& b){
    if(a.size!=b.size)return false;
    for(unsigned i=0;i<a.size;++i)if(a.uid[i]!=b.uid[i])return false;
    return true;
}
constexpr bool valid(const Binding* bindings,std::size_t count){
    if(count>kMaxBindings||(!bindings&&count))return false;
    for(std::size_t i=0;i<count;++i){
        if(!validUidSize(bindings[i].size)||!digimodify::find(bindings[i].cardId))return false;
        for(unsigned j=bindings[i].size;j<10;++j)if(bindings[i].uid[j])return false;
        for(std::size_t j=0;j<i;++j)if(sameUid(bindings[i],bindings[j]))return false;
    }
    return true;
}
inline std::uint32_t lookup(const Binding* bindings,std::size_t count,const nfcdiag::Result& r){
    if(!r.uidValid||!validUidSize(r.uidSize)||!valid(bindings,count))return 0;
    for(std::size_t i=0;i<count;++i)
        if(bindings[i].size==r.uidSize&&!std::memcmp(bindings[i].uid,r.uid,r.uidSize))return bindings[i].cardId;
    return 0; // Unknown UID is a PRESENT unknown card, never absence.
}
// Full UID identity guards the logical-ID latch: swapping two cards mapped to
// the same effect is not stable presence and cannot manufacture a removal.
class Identity {
public:
    void reset(){size_=0;}
    digimodify::Sample observe(const nfcdiag::Result& r,const Binding* bindings,std::size_t count){
        using P=digimodify::Presence;
        if(std::strcmp(r.error,"none")||!r.samReady||!r.firmwareValid||!r.rfOff||!r.scan||r.noCard==r.uidValid){reset();return {};}
        if(r.noCard){reset();return {P::Absent,0};}
        if(!validUidSize(r.uidSize)){reset();return {};}
        const bool changed=size_&&(size_!=r.uidSize||std::memcmp(uid_,r.uid,r.uidSize));
        size_=r.uidSize;std::memcpy(uid_,r.uid,size_);
        if(changed)return {}; // Disarm; require confirmed absence before rearm.
        return {P::Present,lookup(bindings,count,r)};
    }
private:
    std::uint8_t uid_[10]{},size_=0;
};
} // namespace digivice::nfccards
