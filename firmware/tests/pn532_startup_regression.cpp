#ifndef PN532_SESSION_HEADER
#define PN532_SESSION_HEADER "../runtime/pn532_hsu.hpp"
#endif
#include PN532_SESSION_HEADER
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <vector>
using namespace digivice::nfcdiag;
using Bytes=std::vector<std::uint8_t>;
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL startup line %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
struct ColdReader:Link {
    bool normal=false,badSam=false,silentSam=false;
    unsigned delay=0,readChunk=64,writeChunk=16,io=0;
    std::uint64_t now=0,readyAt=0;
    Bytes tx,pending,commands,wire;
    std::deque<std::uint8_t> rx;
    void answer(std::uint8_t command,Bytes data){
        pending={0,0,255,0,255,0,0,0,255,static_cast<std::uint8_t>(data.size()+2),static_cast<std::uint8_t>(-(data.size()+2)),0xd5,static_cast<std::uint8_t>(command+1)};
        pending.insert(pending.end(),data.begin(),data.end());std::uint8_t sum=0;
        for(std::size_t i=11;i<pending.size();++i)sum+=pending[i];
        pending.push_back(-sum);pending.push_back(0);readyAt=now+delay;
    }
    int write(const std::uint8_t* data,std::size_t size)override {
        ++io;CHECK(size<=16);const auto n=std::min(size,std::size_t(writeChunk));
        wire.insert(wire.end(),data,data+n);tx.insert(tx.end(),data,data+n);
        // Accept both old/new preambles: isolate SAM ordering, not wake length.
        if(tx.front()==0x55)return n;
        if(tx.size()==6&&tx[3]==0){tx.clear();return n;}
        if(tx.size()<5||tx.size()!=std::size_t(tx[3])+7)return n;
        const auto c=tx[6];commands.push_back(c);
        if(c==0x14&&!silentSam){normal=!badSam;answer(c,badSam?Bytes{1}:Bytes{});}
        else if(normal){
            if(c==2)answer(c,{0x32,1,6,7});
            else if(c==0x4a)answer(c,{0});
            else answer(c,{});
        }
        tx.clear();return n;
    }
    int read(std::uint8_t* data,std::size_t size)override {
        ++io;CHECK(size<=64);
        if(!pending.empty()&&now>=readyAt){rx.insert(rx.end(),pending.begin(),pending.end());pending.clear();}
        const auto n=std::min({size,std::size_t(readChunk),rx.size()});
        for(std::size_t i=0;i<n;++i){data[i]=rx.front();rx.pop_front();}return n;
    }
    bool flush()override{tx.clear();rx.clear();pending.clear();return true;}
};
void finish(Session& session,ColdReader& reader){
    const auto deadline=reader.now+11000;
    while(session.busy()&&reader.now<deadline){const auto before=reader.io;session.poll(reader.now++);CHECK(reader.io-before<=1);}
    CHECK(!session.busy());
}
int main(){
    ColdReader cold;Session probe(cold);CHECK(probe.start(false,0));finish(probe,cold);
#ifdef EXPECT_OLD_STARTUP
    CHECK(!probe.result().firmwareValid&&!probe.result().rfOff);
    CHECK(!std::strcmp(probe.result().error,"ack_timeout"));CHECK(cold.commands.front()==2);
    CHECK(std::find(cold.commands.begin(),cold.commands.end(),0x14)==cold.commands.end());
    std::printf("PASS: preserved pre-fix session reproduces cold-start ACK timeout (%u checks); firmware queried before SAM.\n",checks);
#else
    const auto& r=probe.result();CHECK(r.samReady&&r.firmwareValid&&r.rfOff&&!std::strcmp(r.error,"none"));
    CHECK(r.ackCount==3&&r.rxBytes==49&&cold.wire.size()==48);
    CHECK(cold.commands==Bytes({0x14,2,0x32}));
    CHECK(cold.wire[0]==0x55&&cold.wire[1]==0x55);
    for(unsigned i=2;i<16;++i)CHECK(cold.wire[i]==0);
    CHECK(cold.wire[16]==0&&cold.wire[18]==0xff);
    // SAM must be acknowledged successfully before a firmware/scan request.
    for(bool malformed:{false,true}){
        ColdReader bad;bad.badSam=malformed;bad.silentSam=!malformed;Session s(bad);CHECK(s.start(true,0));finish(s,bad);
        CHECK(!s.result().samReady&&!s.result().firmwareValid&&!s.result().uidValid);
        CHECK(std::find(bad.commands.begin(),bad.commands.end(),2)==bad.commands.end());
        CHECK(std::find(bad.commands.begin(),bad.commands.end(),0x4a)==bad.commands.end());
    }
    for(unsigned delay:{0u,349u,500u,900u,1100u}){
        ColdReader f;f.delay=delay;f.readChunk=1;f.writeChunk=1;Session s(f);CHECK(s.start(false,0));finish(s,f);
        CHECK(s.result().firmwareValid==(delay<1000));
        if(delay<1000)CHECK(s.result().samReady&&s.result().rfOff);
        else CHECK(std::strcmp(s.result().error,"none"));
    }
    ColdReader scans;Session s(scans);CHECK(s.start(true,0));finish(s,scans);
    CHECK(s.result().samReady&&s.result().noCard&&s.result().rfOff);
    const auto wireBefore=scans.wire.size();scans.commands.clear();CHECK(s.rescan(scans.now));finish(s,scans);
    CHECK(s.result().samReady&&s.result().firmwareValid&&s.result().noCard&&s.result().rfOff);
    CHECK(scans.commands==Bytes({0x4a,0x32})&&scans.wire.size()-wireBefore==22);
    CHECK(s.result().ackCount==2); // No repeated SAM initialization or stale sample.
    CHECK(s.start(false,scans.now));finish(s,scans);CHECK(!s.result().noCard&&!s.result().uidValid);
    std::printf("PASS: fixed cold-start, SAM rejection, 0/349/500/900/1100ms deadlines, fragmentation and clean rescan (%u checks); probe matches 3 ACK / 49 RX / 48 TX bytes.\n",checks);
#endif
}
