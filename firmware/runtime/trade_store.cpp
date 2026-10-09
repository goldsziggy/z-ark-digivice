#include "trade_store.hpp"
#include <cstring>

namespace digivice::devicetrade {
namespace {
void put(std::uint8_t* p,std::uint32_t v){for(unsigned i=0;i<4;++i)p[i]=static_cast<std::uint8_t>(v>>(8*i));}
std::uint32_t get(const std::uint8_t* p){std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(p[i])<<(8*i);return v;}
std::uint32_t crc(const std::uint8_t* p,std::size_t n){std::uint32_t v=~0u;while(n--){v^=*p++;for(unsigned i=0;i<8;++i)v=(v>>1)^(0xedb88320u&(0u-(v&1)));}return ~v;}
bool encode(const trade::Record& r,std::uint32_t revision,Bytes& out){
    if(!revision)return false;
    out={};std::memcpy(out.data,"DVTJ",4);put(out.data+4,1);put(out.data+8,revision);
    if(!trade::encodeRecord(r,out.data+16,trade::kRecordBytes))return false;
    put(out.data+kJournalBytes-4,crc(out.data,kJournalBytes-4));return true;
}
bool decode(const Slot& slot,trade::Record& r,std::uint32_t& revision){
    const auto* p=slot.bytes.data;
    if(!supportedJournalSize(slot.length) || std::memcmp(p,"DVTJ",4) || get(p+4)!=1 || !get(p+8) || get(p+12) ||
       get(p+slot.length-4)!=crc(p,slot.length-4))return false;
    if(!trade::decodeRecord(p+16,slot.length-20,r))return false;
    revision=get(p+8);return true;
}
bool terminal(trade::Phase p){return p==trade::Phase::Applied || p==trade::Phase::Aborted;}
bool sameRecord(const trade::Record& a,const trade::Record& b){
    return a.serial==b.serial && a.phase==b.phase && a.localSide==b.localSide &&
        trade::sameTranscript(a.transcript,b.transcript) && trade::sameState(a.before,b.before) && trade::sameState(a.after,b.after);
}
bool successor(const trade::Record& from,const trade::Record& to){
    if(!trade::valid(to))return false;
    if(to.serial==from.serial){
        if(to.localSide!=from.localSide || !trade::sameTranscript(from.transcript,to.transcript) ||
           !trade::sameState(from.before,to.before))return false;
        if(to.phase==from.phase)return sameRecord(from,to);
        if(from.phase==trade::Phase::Prepared)return to.phase==trade::Phase::Committed || to.phase==trade::Phase::Aborted;
        if(from.phase==trade::Phase::Committed)return to.phase==trade::Phase::Applied && trade::sameState(from.after,to.after);
        return false;
    }
    return from.serial!=UINT32_MAX && to.serial==from.serial+1 && terminal(from.phase) &&
        (to.phase==trade::Phase::Prepared || to.phase==trade::Phase::Aborted);
}
}
Boot Store::restore(){
    present_=writable_=mirrored_=false;revision_=0;
    Read reads[2];bool valid[2]{};std::uint32_t revisions[2]{};
    for(unsigned i=0;i<2;++i){
        scratch_[i]={};reads[i]=backend_.read(i,scratch_[i]);
        if(reads[i]==Read::Unreadable || (reads[i]==Read::Present && !(valid[i]=decode(scratch_[i],decoded_[i],revisions[i])))){
            diagnostic_="trade journal unreadable/invalid; preserve records and recover";return Boot::RecoveryRequired;
        }
    }
    if(!valid[0]&&!valid[1]){writable_=true;diagnostic_="no trade journal records";return Boot::Empty;}
    unsigned chosen=valid[1]&&(!valid[0]||revisions[1]>revisions[0])?1:0;
    if(valid[0]&&valid[1]){
        if(revisions[0]==revisions[1]){
            const bool sameLength=scratch_[0].length==scratch_[1].length;
            if(sameLength ? std::memcmp(scratch_[0].bytes.data,scratch_[1].bytes.data,scratch_[0].length)!=0 :
               !sameRecord(decoded_[0],decoded_[1])){
                diagnostic_="trade journal conflicting mirrored revision";return Boot::RecoveryRequired;
            }
            // Only schema representation may differ at the same revision. Keep
            // ownership locked until both new-format copies have been verified.
            mirrored_=sameLength;
        }else if(revisions[chosen]-revisions[1-chosen]!=1 || !successor(decoded_[1-chosen],decoded_[chosen])){
            diagnostic_="trade journal has ambiguous phase history";return Boot::RecoveryRequired;
        }
    }
    record_=decoded_[chosen];revision_=revisions[chosen];present_=writable_=true;
    diagnostic_=mirrored_?"trade journal mirrors verified":"trade journal pending mirror recovery; ownership locked";
    return mirrored_?Boot::Ready:Boot::NeedsMirror;
}
bool Store::writePair(const trade::Record& value,std::uint32_t revision){
    if(!writable_ || !encode(value,revision,scratch_[0].bytes))return false;
    for(unsigned i=0;i<2;++i){
        if(!backend_.write(i,scratch_[0].bytes)){
            writable_=mirrored_=false;diagnostic_="trade journal commit uncertain; no acknowledgement; restart recovery";return false;
        }
        scratch_[1]={};
        if(backend_.read(i,scratch_[1])!=Read::Present || scratch_[1].length!=kJournalBytes ||
           std::memcmp(scratch_[0].bytes.data,scratch_[1].bytes.data,kJournalBytes)){
            writable_=mirrored_=false;diagnostic_="trade journal readback failed; no acknowledgement; restart recovery";return false;
        }
    }
    record_=value;revision_=revision;present_=mirrored_=true;
    diagnostic_="trade journal phase mirrored and verified";return true;
}
bool Store::checkpoint(const trade::Record& value){
    if(writable_ && mirrored_ && present_ && sameRecord(record_,value))return true;
    if(value.phase==trade::Phase::Prepared && (!present_ || value.serial!=record_.serial) && revision_>UINT32_MAX-3)return false;
    if(!writable_ || !trade::valid(value) || revision_==UINT32_MAX ||
       (present_ && (!mirrored_ || !successor(record_,value))) ||
       (!present_ && (value.serial!=1 || (value.phase!=trade::Phase::Prepared && value.phase!=trade::Phase::Aborted))))return false;
    return writePair(value,revision_+1);
}
bool Store::repairMirror(){
    if(!writable_ || !present_)return false;
    return mirrored_ || writePair(record_,revision_);
}
} // namespace digivice::devicetrade
