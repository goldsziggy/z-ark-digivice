#include "../runtime/pn532_hsu.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <deque>
#include <vector>
using namespace digivice::nfcdiag;
using Bytes = std::vector<std::uint8_t>;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; assert(x); } while (false)
static const Bytes ack{0,0,0xff,0,0xff,0};
static Bytes response(std::uint8_t cmd, const Bytes& data) {
    Bytes out{0,0,0xff,static_cast<std::uint8_t>(data.size()+2),static_cast<std::uint8_t>(-(data.size()+2)),0xd5,cmd};
    out.insert(out.end(),data.begin(),data.end());
    std::uint8_t sum=0; for (std::size_t i=5;i<out.size();++i) sum+=out[i];
    out.push_back(static_cast<std::uint8_t>(-sum)); out.push_back(0); return out;
}
struct Fake : Link {
    std::deque<std::uint8_t> rx;
    Bytes tx, commands;
    std::size_t readChunk=64, writeChunk=16;
    bool missing=false, noCard=false, badFirmware=false, badTarget=false, offMissing=false;
    bool ackOnly=false, wrongCommand=false, ioError=false, flushError=false, neverWrite=false;
    bool scanMissing=false, trailing=false;
    unsigned aborts=0, writes=0, reads=0, flushes=0;
    Bytes uid{0x04,0x12,0x34,0x56,0x78,0x90,0xab};
    void answer(std::uint8_t cmd, Bytes data={}) {
        if (missing || (cmd==0x32 && offMissing) || (cmd==0x4a && scanMissing)) return;
        for(auto b:ack) rx.push_back(b);
        if (ackOnly) return;
        auto packet=response(wrongCommand ? 0x99 : static_cast<std::uint8_t>(cmd+1),data);
        for(auto b:packet) rx.push_back(b);
        if(trailing) rx.push_back(0x77);
    }
    int write(const std::uint8_t* data,std::size_t size) override {
        ++writes; if(ioError) return -1; if(neverWrite) return 0;
        const auto n=std::min(size,writeChunk); tx.insert(tx.end(),data,data+n);
        if(tx.front()==0x55) { if(tx.size()==16) tx.clear(); return static_cast<int>(n); }
        if(tx.size()>=6 && std::equal(ack.begin(),ack.end(),tx.begin())) { ++aborts; tx.clear(); return static_cast<int>(n); }
        if(tx.size()<5 || tx.size()!=static_cast<std::size_t>(tx[3])+7) return static_cast<int>(n);
        std::uint8_t checksum=0; for(std::size_t i=5;i+1<tx.size();++i)checksum+=tx[i];
        if(checksum || tx[5]!=0xd4 || tx.back()!=0) { tx.clear(); return static_cast<int>(n); }
        const auto cmd=tx[6]; commands.push_back(cmd);
        if(cmd==0x02) answer(cmd,{static_cast<std::uint8_t>(badFirmware?0x33:0x32),1,6,7});
        else if(cmd==0x14) { CHECK((tx==Bytes{0,0,0xff,5,0xfb,0xd4,0x14,1,0x14,0,3,0})); answer(cmd); }
        else if(cmd==0x32) {
            CHECK(tx[7]==1 || tx[7]==5);
            if(tx[7]==1) CHECK(tx[8]==0);
            else CHECK(tx[8]==0 && tx[9]==0 && tx[10]==0);
            answer(cmd);
        } else if(cmd==0x4a) {
            CHECK(tx[7]==1 && tx[8]==0);
            Bytes targetData{1,1,0x04,0,0,static_cast<std::uint8_t>(badTarget?11:uid.size())};
            targetData.insert(targetData.end(),uid.begin(),uid.end());
            answer(cmd,noCard?Bytes{0}:targetData);
        } // Partial command plus cancellation may be rejected by the peripheral.

        tx.clear(); return static_cast<int>(n);
    }
    int read(std::uint8_t* out,std::size_t size) override {
        ++reads; if(ioError) return -1;
        const auto n=std::min({size,readChunk,rx.size()});
        for(std::size_t i=0;i<n;++i) { out[i]=rx.front();rx.pop_front(); }
        return static_cast<int>(n);
    }
    bool flush() override { ++flushes; rx.clear(); return !flushError; }
};
static std::uint64_t run(Session& s,std::uint64_t start=0) {
    for(auto now=start;now<start+11000;++now) { s.poll(now); if(!s.busy()) return now; }
    CHECK(false); return 0;
}
static void parserTests() {
    const auto valid=response(3,{0x32,1,6,7});
    for(std::size_t n=0;n<valid.size();++n) {
        Parser p; for(std::size_t i=0;i<n;++i) CHECK(p.push(valid[i])==Frame::Pending);
    }
    Parser p; for(auto b:ack) p.push(b); CHECK(p.push(0)==Frame::Invalid);
    p.reset(); for(auto b:valid) p.push(b); CHECK(p.command()==3 && p.size()==4 && p.data()[0]==0x32);
    for(std::size_t i=3;i<valid.size();++i) for(unsigned bit=0;bit<8;++bit) {
        auto damaged=valid;damaged[i]^=1U<<bit;Parser q;Frame last=Frame::Pending;
        for(auto b:damaged) { last=q.push(b); if(last==Frame::Invalid)break; }
        CHECK(last!=Frame::Response);
    }
    for(unsigned length=0;length<256;++length) {
        Parser q;Bytes header{0,0,0xff,static_cast<std::uint8_t>(length),static_cast<std::uint8_t>(-length)};
        Frame last=Frame::Pending;for(auto b:header)last=q.push(b);
        if(length<2 || length>66) CHECK(last==Frame::Invalid);
    }
    std::uint32_t rng=123456;
    for(unsigned test=0;test<20000;++test) {
        Parser q; for(unsigned i=0;i<200;++i) { rng=rng*1664525+1013904223;auto kind=q.push(rng>>24);if(kind!=Frame::Pending)q.reset(); }
    }
    std::uint8_t out[16]{};const std::uint8_t cmd[]={2};CHECK(frame(cmd,1,out)==9);
    CHECK((Bytes(out,out+9)==Bytes{0,0,0xff,2,0xfe,0xd4,2,0x2a,0}));
    CHECK(frame(nullptr,1,out)==0 && frame(cmd,9,out)==0);
}
static void uidTests() {
    for(unsigned n=0;n<16;++n) {
        Bytes data{1,1,4,0,0,static_cast<std::uint8_t>(n)};for(unsigned i=0;i<n;++i)data.push_back(i);
        Result r;CHECK(target(data.data(),data.size(),r)==(n==4 || n==7 || n==10));
        if(r.uidValid) { CHECK(r.uidSize==n);data.pop_back();Result t;CHECK(!target(data.data(),data.size(),t)); }
    }
    for(unsigned ats=0;ats<8;++ats) {
        Bytes data{1,1,4,0,0x20,4,1,2,3,4,static_cast<std::uint8_t>(ats)};
        for(unsigned i=1;i<ats;++i)data.push_back(i);
        Result r;CHECK(target(data.data(),data.size(),r)==(ats>0));
    }
    Result r;const std::uint8_t absent[]={0};CHECK(target(absent,1,r)&&r.noCard&&!r.uidValid);
    CHECK(!target(nullptr,0,r));const std::uint8_t multi[]={2,1,4,0,0,4,1,2,3,4};CHECK(!target(multi,sizeof(multi),r));
}
static void sessions() {
    for(unsigned write=1;write<=16;++write) for(unsigned read=1;read<=64;++read) for(bool scan:{false,true}) {
        Fake f;f.writeChunk=write;f.readChunk=read;Session s(f);CHECK(!s.busy()&&f.writes==0);
        CHECK(s.start(scan,0));CHECK(!s.start(scan,0));run(s);
        const auto& r=s.result();CHECK(!std::strcmp(r.error,"none")&&r.samReady&&r.firmwareValid&&r.rfOff);
        CHECK(r.uidValid==scan && r.ackCount==(scan?5U:3U));
        CHECK(f.commands==(scan?Bytes{0x14,2,0x32,0x4a,0x32}:Bytes{0x14,2,0x32}));
        auto calls=f.writes+f.reads;s.poll(9999);CHECK(f.writes+f.reads==calls);
        CHECK(s.start(false,10000));run(s,10000);CHECK(!s.result().uidValid); // stale UID cleared.
    }
    Fake f;f.noCard=true;Session s(f);CHECK(s.start(true,0));run(s);CHECK(s.result().noCard&&!s.result().uidValid&&s.result().rfOff);
    for(unsigned failure=0;failure<11;++failure) {
        Fake bad;
        switch(failure) {
        case 0:bad.missing=true;break;case 1:bad.badFirmware=true;break;case 2:bad.badTarget=true;break;
        case 3:bad.offMissing=true;break;case 4:bad.ackOnly=true;break;case 5:bad.wrongCommand=true;break;
        case 6:bad.ioError=true;break;case 7:bad.flushError=true;break;case 8:bad.neverWrite=true;break;
        case 9:bad.scanMissing=true;break;case 10:bad.trailing=true;break;
        }
        Session trial(bad);trial.start(true,0);auto duration=run(trial);CHECK(duration<11000);
        CHECK(std::strcmp(trial.result().error,"none"));CHECK(bad.aborts<=1);
        if(failure==1 || failure==2 || failure==9) CHECK(trial.result().rfOff);
        if(failure==0 || failure==3 || failure==4 || failure==5 || failure==6) CHECK(!trial.result().rfOff);
    }
    for(unsigned cancelAt=0;cancelAt<200;++cancelAt) {
        Fake cancel;cancel.readChunk=1;cancel.writeChunk=1;Session trial(cancel);trial.start(true,0);
        for(unsigned now=0;now<cancelAt;++now)trial.poll(now);
        const bool active=trial.busy();trial.cancel(cancelAt);run(trial,cancelAt);
        if(active)CHECK(!std::strcmp(trial.result().error,"cancelled"));CHECK(cancel.aborts<=1);
    }
    Fake clock;Session trial(clock);trial.start(false,100);trial.poll(99);CHECK(!trial.busy()&&!trial.result().rfOff);
}
int main() { parserTests();uidTests();sessions();std::printf("PN532 HSU: %u checks; fragmentation, framing, UID, timeout, cancellation and 4M fuzz bytes passed.\n",checks); }
