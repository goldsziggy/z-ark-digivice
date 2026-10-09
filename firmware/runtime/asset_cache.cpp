#include "asset_cache.hpp"
#include <algorithm>
#include <cstring>

namespace digivice::assets {
namespace {
constexpr std::uint32_t kMagic = 0x43415644, kCommit = 0x454e4f44;
constexpr std::size_t kHeaderBytes = 128, kJournalEntryBytes = 16;
std::uint16_t get16(const std::uint8_t* p) { return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8); }
std::uint32_t get32(const std::uint8_t* p) { return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
void put16(std::uint8_t* p, std::uint16_t v) { p[0]=std::uint8_t(v); p[1]=std::uint8_t(v>>8); }
void put32(std::uint8_t* p, std::uint32_t v) { for (unsigned i=0;i<4;++i) p[i]=std::uint8_t(v>>(8*i)); }
bool erased(const std::uint8_t* p, std::size_t n) { for(std::size_t i=0;i<n;++i) if(p[i]!=255) return false; return true; }
std::uint32_t crc(const std::uint8_t* p, std::size_t n) {
    std::uint32_t c=UINT32_MAX;
    for(std::size_t i=0;i<n;++i) { c^=p[i]; for(unsigned b=0;b<8;++b) c=(c>>1)^(0xedb88320U & (0U-(c&1))); }
    return ~c;
}
// Incremental SHA-256, fixed 64-byte block. Constants are the standard FIPS180-4
// cube-root constants. Host tests cover known digests and actual generated packs.
class Sha256 {
    std::uint32_t h_[8]{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::uint8_t block_[64]{}; std::size_t used_=0; std::uint64_t length_=0;
    static std::uint32_t rotate(std::uint32_t n,unsigned b) {return(n>>b)|(n<<(32-b));}
    void compress() {
        static constexpr std::uint32_t k[64]{
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::uint32_t w[64]{};
        for(unsigned i=0;i<16;++i) w[i]=(std::uint32_t(block_[i*4])<<24)|(std::uint32_t(block_[i*4+1])<<16)|(std::uint32_t(block_[i*4+2])<<8)|block_[i*4+3];
        for(unsigned i=16;i<64;++i) { auto x=w[i-15],y=w[i-2]; w[i]=w[i-16]+(rotate(x,7)^rotate(x,18)^(x>>3))+w[i-7]+(rotate(y,17)^rotate(y,19)^(y>>10)); }
        auto a=h_[0],b=h_[1],c=h_[2],d=h_[3],e=h_[4],f=h_[5],g=h_[6],h=h_[7];
        for(unsigned i=0;i<64;++i) { const auto t1=h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+k[i]+w[i]; const auto t2=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c)); h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2; }
        const std::uint32_t values[]{a,b,c,d,e,f,g,h}; for(unsigned i=0;i<8;++i) h_[i]+=values[i];
    }
public:
    void update(const std::uint8_t* p,std::size_t n) {length_+=n;for(std::size_t i=0;i<n;++i){block_[used_++]=p[i];if(used_==64){compress();used_=0;}}}
    void finish(std::uint8_t* out) {
        const auto bits=length_*8; block_[used_++]=128;
        if(used_>56){while(used_<64)block_[used_++]=0;compress();used_=0;}
        while(used_<56)block_[used_++]=0;
        for(unsigned i=0;i<8;++i)block_[56+i]=std::uint8_t(bits>>(56-8*i));
        compress();for(unsigned i=0;i<8;++i)for(unsigned j=0;j<4;++j)out[i*4+j]=std::uint8_t(h_[i]>>(24-j*8));
    }
};
void encode(const Record& record,std::uint8_t* bytes) {
    std::memset(bytes,0,kHeaderBytes);put32(bytes,kMagic);put16(bytes+4,1);put16(bytes+6,kHeaderBytes);
    put32(bytes+8,record.generation);put32(bytes+12,record.spec.version);put32(bytes+16,record.spec.bytes);
    put16(bytes+20,record.spec.width);put16(bytes+22,record.spec.height);bytes[24]=static_cast<std::uint8_t>(record.spec.kind);
    std::memcpy(bytes+28,record.spec.id,48);std::memcpy(bytes+76,record.spec.sha256,32);
    put32(bytes+120,crc(bytes,120));put32(bytes+124,UINT32_MAX);
}
bool decode(const std::uint8_t* bytes,Record& record) {
    if(get32(bytes)!=kMagic||get16(bytes+4)!=1||get16(bytes+6)!=kHeaderBytes||get32(bytes+120)!=crc(bytes,120))return false;
    record={};record.generation=get32(bytes+8);record.spec.version=get32(bytes+12);record.spec.bytes=get32(bytes+16);
    record.spec.width=get16(bytes+20);record.spec.height=get16(bytes+22);record.spec.kind=static_cast<Kind>(bytes[24]);
    std::memcpy(record.spec.id,bytes+28,48);std::memcpy(record.spec.sha256,bytes+76,32);
    record.complete=get32(bytes+124)==kCommit;record.present=true;
    return record.generation>0&&validSpec(record.spec);
}
std::size_t base(std::size_t slot){return slot*kSlotBytes;}
std::size_t data(std::size_t slot){return base(slot)+2*kChunkBytes;}
}
bool validSpec(const Spec& spec) {
    const auto length=::strnlen(spec.id,sizeof(spec.id));
    if(length==0||length>=sizeof(spec.id)||spec.id[0]<'a'||spec.id[0]>'z'||spec.version==0||spec.bytes==0||spec.bytes>kMaximumBlobBytes)return false;
    for(std::size_t i=0;i<length;++i)if(!((spec.id[i]>='a'&&spec.id[i]<='z')||(spec.id[i]>='0'&&spec.id[i]<='9')||spec.id[i]=='-'))return false;
    for(std::size_t i=length+1;i<sizeof(spec.id);++i)if(spec.id[i]!=0)return false;
    return(spec.kind==Kind::Sprite&&spec.width==32&&spec.height==32)||(spec.kind==Kind::Background&&spec.width==412&&spec.height==412);
}
bool sameSpec(const Spec& a,const Spec& b){return std::memcmp(a.id,b.id,48)==0&&a.version==b.version&&a.bytes==b.bytes&&a.kind==b.kind&&a.width==b.width&&a.height==b.height&&std::memcmp(a.sha256,b.sha256,32)==0;}
Result Cache::failIo(){recovery_=true;transfer_.active=false;return Result::Io;}
Result Cache::digest(std::size_t slot,const Spec& spec){
    Sha256 hash;std::uint8_t buffer[1024],actual[32];
    for(std::size_t offset=0;offset<spec.bytes;){const auto n=std::min(sizeof(buffer),std::size_t(spec.bytes)-offset);if(!storage_.read(data(slot)+offset,buffer,n))return Result::Io;hash.update(buffer,n);offset+=n;}
    hash.finish(actual);return std::memcmp(actual,spec.sha256,32)==0?Result::Ok:Result::Integrity;
}
Result Cache::boot(){
    transfer_={};stage_=kSlotCount;generation_=0;protectedCount_=0;recovery_=false;restartStage_=false;
    for(std::size_t i=0;i<kSlotCount;++i){
        records_[i]={};std::uint8_t header[kHeaderBytes];if(!storage_.read(base(i),header,sizeof(header)))return failIo();
        if(erased(header,sizeof(header)))continue;
        // Only a credible complete header can establish an unknown-format barrier.
        // A torn first write commonly leaves version=FFFF and must remain replaceable.
        if(get32(header)==kMagic&&get16(header+4)!=1&&get16(header+6)==kHeaderBytes&&get32(header+120)==crc(header,120)){recovery_=true;continue;}
        Record candidate;if(!decode(header,candidate))continue;
        generation_=std::max(generation_,candidate.generation);
        if(candidate.complete){
            const auto checked=digest(i,candidate.spec);
            if(checked==Result::Io)return failIo();
            if(checked!=Result::Ok)continue;
        }
        records_[i]=candidate;
        if(!candidate.complete&&(stage_==kSlotCount||candidate.generation>records_[stage_].generation))stage_=i;
    }
    if(recovery_)return Result::RecoveryRequired;
    if(stage_!=kSlotCount){
        auto& spec=records_[stage_].spec;std::uint32_t offset=0;std::uint8_t bytes[kChunkBytes];
        while(offset<spec.bytes){
            std::uint8_t entry[kJournalEntryBytes];const auto index=offset/kChunkBytes;
            if(!storage_.read(base(stage_)+kChunkBytes+index*kJournalEntryBytes,entry,sizeof(entry)))return failIo();
            if(erased(entry,sizeof(entry)))break;
            const auto n=std::min(std::uint32_t(kChunkBytes),spec.bytes-offset);
            if(get32(entry)!=offset||get32(entry+4)!=n||get32(entry+12)!=crc(entry,12)){restartStage_=true;offset=0;break;}
            if(!storage_.read(data(stage_)+offset,bytes,n))return failIo();
            if(get32(entry+8)!=crc(bytes,n)){restartStage_=true;offset=0;break;}
            offset+=n;
        }
        transfer_={0,offset,spec.bytes,false};
    }
    return Result::Ok;
}
bool Cache::protect(const char* const* ids,std::size_t count){
    if(count>4||(!ids&&count))return false;
    for(std::size_t i=0;i<count;++i)if(!ids[i]||::strnlen(ids[i],48)>=48)return false;
    std::memset(protected_,0,sizeof(protected_));for(std::size_t i=0;i<count;++i)std::strcpy(protected_[i],ids[i]);protectedCount_=count;return true;
}
bool Cache::isProtected(const char* id)const{for(std::size_t i=0;i<protectedCount_;++i)if(std::strcmp(id,protected_[i])==0)return true;return false;}
bool Cache::contains(const Spec& spec)const{if(!validSpec(spec))return false;for(const auto& record:records_)if(record.present&&record.complete&&sameSpec(record.spec,spec))return true;return false;}
Result Cache::begin(const Spec& spec,std::uint32_t& token){
    token=0;if(recovery_)return Result::RecoveryRequired;if(!validSpec(spec))return Result::Invalid;
    if(contains(spec))return Result::Ok;
    // Existing exact historical bytes may still be read; a new transfer cannot
    // roll back a resident ID or replace an immutable version with new bytes.
    // This is not a persistent publisher high-water mark after slot eviction.
    for(const auto& record:records_)if(record.present&&std::strcmp(record.spec.id,spec.id)==0){
        if(record.spec.version>spec.version||(record.spec.version==spec.version&&!sameSpec(record.spec,spec)))return Result::Invalid;
    }
    if(tokenCounter_==UINT32_MAX||generation_==UINT32_MAX){recovery_=true;return Result::RecoveryRequired;}
    if(stage_<kSlotCount&&records_[stage_].present&&!restartStage_&&sameSpec(records_[stage_].spec,spec)){
        transfer_.token=++tokenCounter_;transfer_.active=true;token=transfer_.token;return Result::Ok;
    }
    std::size_t slot=kSlotCount;
    if(stage_<kSlotCount&&!records_[stage_].complete)slot=stage_;
    for(std::size_t i=0;slot==kSlotCount&&i<kSlotCount;++i)if(!records_[i].present)slot=i;
    if(slot==kSlotCount){for(std::size_t i=0;i<kSlotCount;++i)if(!isProtected(records_[i].spec.id)&&(slot==kSlotCount||records_[i].generation<records_[slot].generation))slot=i;}
    if(slot==kSlotCount)return Result::Full;
    // An erase may partially succeed. Never expose the selected old record again
    // in this runtime, including when storage reports an uncertain failure.
    records_[slot]={};
    if(!storage_.erase(base(slot),kSlotBytes))return failIo();
    Record record{spec,++generation_,false,true};std::uint8_t header[kHeaderBytes],verify[kHeaderBytes];encode(record,header);
    if(!storage_.program(base(slot),header,sizeof(header))||!storage_.read(base(slot),verify,sizeof(verify))||std::memcmp(header,verify,sizeof(header))!=0)return failIo();
    records_[slot]=record;stage_=slot;restartStage_=false;transfer_={++tokenCounter_,0,spec.bytes,true};token=transfer_.token;return Result::Ok;
}
Result Cache::append(std::uint32_t token,std::uint32_t offset,const std::uint8_t* bytes,std::size_t length){
    if(recovery_)return Result::RecoveryRequired;
    if(!transfer_.active||token!=transfer_.token||stage_>=kSlotCount)return Result::WrongTransfer;
    if(!bytes||offset!=transfer_.received||offset>=transfer_.total||offset%kChunkBytes||length!=std::min(std::size_t(kChunkBytes),std::size_t(transfer_.total-offset)))return Result::Invalid;
    // Erase the unacknowledged sector before every write, including after reboot.
    if(!storage_.erase(data(stage_)+offset,kChunkBytes)||!storage_.program(data(stage_)+offset,bytes,length))return failIo();
    std::uint8_t check[kChunkBytes];if(!storage_.read(data(stage_)+offset,check,length)||std::memcmp(bytes,check,length)!=0)return failIo();
    std::uint8_t entry[kJournalEntryBytes],verify[kJournalEntryBytes];put32(entry,offset);put32(entry+4,std::uint32_t(length));put32(entry+8,crc(bytes,length));put32(entry+12,crc(entry,12));
    const auto where=base(stage_)+kChunkBytes+(offset/kChunkBytes)*kJournalEntryBytes;
    if(!storage_.program(where,entry,sizeof(entry))||!storage_.read(where,verify,sizeof(verify))||std::memcmp(entry,verify,sizeof(entry))!=0)return failIo();
    transfer_.received+=std::uint32_t(length);return Result::Ok;
}
Result Cache::finish(std::uint32_t token){
    if(recovery_)return Result::RecoveryRequired;
    if(!transfer_.active||token!=transfer_.token||stage_>=kSlotCount)return Result::WrongTransfer;
    if(transfer_.received!=transfer_.total)return Result::Invalid;
    const auto checked=digest(stage_,records_[stage_].spec);
    if(checked==Result::Io)return failIo();
    if(checked!=Result::Ok){restartStage_=true;transfer_.active=false;return Result::Integrity;}
    std::uint8_t commit[4],verify[4];put32(commit,kCommit);
    if(!storage_.program(base(stage_)+124,commit,sizeof(commit))||!storage_.read(base(stage_)+124,verify,sizeof(verify))||std::memcmp(commit,verify,4)!=0)return failIo();
    records_[stage_].complete=true;transfer_.active=false;stage_=kSlotCount;return Result::Ok;
}
Result Cache::read(const Spec& spec,std::size_t offset,void* bytes,std::size_t length){
    if(!bytes||!validSpec(spec)||offset>spec.bytes||length>spec.bytes-offset)return Result::Invalid;
    for(std::size_t i=0;i<kSlotCount;++i)if(records_[i].present&&records_[i].complete&&sameSpec(records_[i].spec,spec))return storage_.read(data(i)+offset,bytes,length)?Result::Ok:Result::Io;
    return Result::Missing;
}
const char* resultName(Result result){switch(result){case Result::Ok:return"ok";case Result::Missing:return"missing";case Result::Invalid:return"invalid";case Result::Full:return"full";case Result::Io:return"io";case Result::Integrity:return"integrity";case Result::RecoveryRequired:return"recovery-required";case Result::WrongTransfer:return"stale-transfer";}return"unknown";}
} // namespace digivice::assets
