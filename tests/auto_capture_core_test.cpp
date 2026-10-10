#include "game.hpp"
#include "snapshot_test_helpers.hpp"
#include "trade.hpp"
#include "capture_ring.hpp"
#include "forms.hpp"
#include "legacy_combat_v8.hpp"
#include "legacy_v13.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace digivice;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)
unsigned updateCrc(unsigned crc,const unsigned char* data,unsigned n){for(unsigned i=0;i<n;++i){crc^=data[i];for(unsigned j=0;j<8;++j)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}return crc;}
void put32(std::uint8_t* p,std::uint32_t v){for(unsigned i=0;i<4;++i)p[i]=static_cast<std::uint8_t>(v>>(8*i));}
void step(State& s,Action a,unsigned v=0){CHECK(apply(s,a,v)==Error::None);CHECK(isValid(s));}
// Rules-17 encounters: the capture pause flow without rules-18 focus pauses (rules18_test covers those).
State start(unsigned seed=1,unsigned starter=1){auto s=newDevice(seed);step(s,Action::Hatch,starter);step(s,Action::Mode,1);step(s,Action::Explore,1000);s.wildRules=17;return s;}
void restore(State& s){Snapshot bytes;CHECK(encodeSnapshot(s,bytes));State decoded;CHECK(decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),decoded)==SnapshotStatus::Ok&&trade::sameState(s,decoded));s=decoded;}
void reject(State& s,Action a){const auto before=s;CHECK(apply(s,a)!=Error::None&&trade::sameState(s,before));}
void attackTrace(const autobattle::Trace& trace){
    CHECK(trace.count>0&&trace.count<=autobattle::kMaxTraceSteps&&trace.kind==autobattle::Kind::Wild);
    for(unsigned i=0;i<trace.count;++i){const auto& frame=trace.steps[i];CHECK(frame.action==autobattle::Move::Physical||frame.action==autobattle::Move::Magic||frame.action==autobattle::Move::Heavy);
        CHECK(!frame.captured&&!frame.captureAttempt&&!frame.captureChance&&!frame.captureResult);
        if(i)CHECK(trace.steps[i-1].playerHpAfter==frame.playerHpBefore&&trace.steps[i-1].enemyHpAfter==frame.enemyHpBefore);
    }
    char json[autobattle::kTraceJsonCapacity];CHECK(autobattle::writeJson(trace,json,sizeof(json))>0);
}
void historicalAutoUnchanged(){
    namespace old=legacy_v13;
    unsigned states=~0u,traces=~0u,captured=0,retreated=0;
    for(unsigned starter=1;starter<=8;++starter)for(unsigned seed=1;seed<=64;++seed){
        auto s=old::newDevice(seed);CHECK(old::apply(s,old::Action::Hatch,starter)==old::Error::None);CHECK(old::apply(s,old::Action::Mode,1)==old::Error::None);CHECK(old::apply(s,old::Action::Explore,1000)==old::Error::None);autobattle::Trace trace;CHECK(old::applyAuto(s,&trace)==old::Error::None);
        old::Snapshot bytes;char json[autobattle::kTraceJsonCapacity];CHECK(old::encodeSnapshot(s,bytes));const auto n=autobattle::writeJson(trace,json,sizeof(json));CHECK(n>0);
        states=updateCrc(states,bytes.bytes+12,636);traces=updateCrc(traces,reinterpret_cast<unsigned char*>(json),n);
        captured+=trace.outcome==autobattle::Outcome::Captured;retreated+=trace.outcome==autobattle::Outcome::Retreated;
        CHECK(s.autoCapture==old::AutoCapture::None&&!s.receivedTrades);
    }
    // Independently emitted from git f74ee4c game.cpp/.hpp before the new flow;
    // state CRC covers every old gameplay payload byte, trace CRC the entire JSON.
    CHECK(~states==0x4e7a0706u&&~traces==0x4f1c7b8fu&&captured==441&&retreated==71);
    std::printf("512 frozen f74 Auto histories unchanged: stateCRC=%08x traceCRC=%08x; %u captured/%u retreated\n",~states,~traces,captured,retreated);
}
void pausesAndResume(){
    unsigned pauses=0,terminal=0;
    for(unsigned starter=1;starter<=8;++starter)for(unsigned seed=1;seed<=64;++seed){
        auto s=start(seed,starter);const auto before=s;auto legacy=s;autobattle::Trace trace,old;CHECK(applyAuto(legacy,&old)==Error::None);
        CHECK(applyAutoFight(s,&trace)==Error::None&&isValid(s));attackTrace(trace);CHECK(s.captures==before.captures&&s.captureAttempts==0);
        CHECK(s.sequence==before.sequence+1&&s.foregroundSequence==s.sequence);restore(s);
        for(unsigned i=0;i<trace.count;++i){CHECK(i<old.count);if(i<old.count){const auto& a=trace.steps[i];const auto& b=old.steps[i];CHECK(a.action==b.action&&a.opponentAction==b.opponentAction&&a.playerHpBefore==b.playerHpBefore&&a.playerHpAfter==b.playerHpAfter&&a.enemyHpBefore==b.enemyHpBefore&&a.enemyHpAfter==b.enemyHpAfter&&a.guard==b.guard&&a.reflected==b.reflected&&a.captured==b.captured);}}
        if(s.autoCapture==AutoCapture::Awaiting){
            ++pauses;CHECK(s.phase==Phase::Encounter&&s.battleMode==BattleMode::Auto&&s.wildHp<=s.wildMaxHp/2&&captureChance(s));
            CHECK(trace.outcome==autobattle::Outcome::None&&trace.endSequence==s.foregroundSequence);
            CHECK(trace.steps[trace.count-1].enemyHpAfter==s.wildHp&&trace.steps[trace.count-1].playerHpAfter==s.hp);
            CHECK(s.collectionCount==before.collectionCount&&s.collection[0].xp==before.collection[0].xp&&s.bond==before.bond);
            reject(s,Action::AutoFight);reject(s,Action::Auto);reject(s,Action::Capture);reject(s,Action::Attack);reject(s,Action::Magic);
            const auto paused=s;step(s,Action::AccrueSteps,1000);CHECK(s.autoCapture==AutoCapture::Awaiting&&s.foregroundSequence==paused.foregroundSequence&&s.hp==paused.hp&&s.wildHp==paused.wildHp);restore(s);
            autobattle::Trace resumed;CHECK(applyAutoResume(s,&resumed)==Error::None&&s.phase==Phase::Home&&s.autoCapture==AutoCapture::None);attackTrace(resumed);
            CHECK(resumed.outcome!=autobattle::Outcome::None&&resumed.outcome!=autobattle::Outcome::Captured&&s.captures==before.captures);
            CHECK(resumed.steps[0].playerHpBefore==paused.hp&&resumed.steps[0].enemyHpBefore==paused.wildHp);
            reject(s,Action::AutoResume);restore(s);
        }else{++terminal;CHECK(s.phase==Phase::Home&&trace.outcome!=autobattle::Outcome::None&&trace.outcome!=autobattle::Outcome::Captured);}
    }
    CHECK(pauses>400&&terminal>0);std::printf("512 new Auto fights: %u durable flick pauses, %u terminal fights; all automatic traces attack-only\n",pauses,terminal);
}
State paused(unsigned seed=1){
    auto s=start(seed);s.wildFormId=18;s.wildSpecies=Species::Agumon;s.wildLevel=1;s.wildMaxHp=combat::formProfile(18,1).stats.maxHp;s.wildHp=s.wildMaxHp/2+1;
    CHECK(isValid(s));CHECK(applyAutoFight(s)==Error::None&&s.autoCapture==AutoCapture::Awaiting);return s;
}
void actualFlickOnly(){
    auto s=paused();const auto before=s;
    for(unsigned i=1;i<=3;++i){const auto hp=s.hp,foe=s.wildHp,rng=s.rngState;step(s,Action::Flick,0);CHECK(s.rngState==rng&&s.lastCapture.result==CaptureResult::Miss&&s.lastCapture.attempt==i&&s.lastCapture.chance==0&&s.hp==hp);
        CHECK(s.lastCapture.sequence==s.foregroundSequence&&s.captures==before.captures);restore(s);
        CHECK(s.phase==Phase::Encounter&&s.autoCapture==AutoCapture::None&&s.captureDeferred==1&&s.wildHp==foe&&s.message==Message::CaptureMissed);
        if(i<3){s.wildHp=s.wildMaxHp/2;s.hp=s.collection[0].hp=combat::formProfile(s.collection[0].formId,s.level).stats.maxHp;CHECK(isValid(s));
            CHECK(applyAutoFight(s)==Error::None&&s.autoCapture==AutoCapture::Awaiting&&s.captureAttempts==i&&s.phase==Phase::Encounter);restore(s);}
    }
    CHECK(s.phase==Phase::Encounter&&s.captureAttempts==3&&s.message!=Message::CaptureEnded);reject(s,Action::Flick);
    s.wildHp=s.wildMaxHp/2;s.hp=s.collection[0].hp=combat::formProfile(s.collection[0].formId,s.level).stats.maxHp;CHECK(isValid(s));
    autobattle::Trace continued;CHECK(applyAutoFight(s,&continued)==Error::None&&s.phase==Phase::Home&&s.autoCapture==AutoCapture::None&&s.message!=Message::CaptureEnded);attackTrace(continued);
    unsigned caught=0,escaped=0;
    for(unsigned seed=1;seed<=128;++seed){auto target=paused(seed);const auto chance=captureChance(target);const auto original=target;auto retry=target;
        step(target,Action::Flick,160*256+180);step(retry,Action::Flick,160*256+180);CHECK(trade::sameState(target,retry));
        CHECK(target.lastCapture.chance==chance&&target.lastCapture.attempt==1&&target.rngState!=original.rngState);
        if(target.lastCapture.result==CaptureResult::Captured){++caught;CHECK(target.phase==Phase::Home&&target.collectionCount==original.collectionCount+1&&target.autoCapture==AutoCapture::None);}
        else{++escaped;CHECK(target.phase==Phase::Encounter&&target.autoCapture==AutoCapture::None&&target.captureDeferred==1&&target.hp==original.hp&&target.wildHp==original.wildHp);reject(target,Action::Flick);}
        restore(target);
    }
    CHECK(caught&&escaped);std::printf("128 actual aimed flicks: %u captured/%u escaped; three missed flicks end calmly without RNG\n",caught,escaped);
    // Named encounters loaded from older epochs use the same manual pause, odds,
    // three-throw cap and saved reveal, while their combat profiles stay frozen.
    for(unsigned rules:{5u,8u,9u,11u,12u}){
        auto old=start(99);old.wildFormId=18;old.wildSpecies=Species::Agumon;old.wildLevel=1;old.wildRules=rules;
        old.wildMaxHp=old.wildHp=rules<9?legacy_v8::combat::formProfile(18,1).stats.maxHp:combat::formProfile(18,1).stats.maxHp;
        old.hp=old.collection[0].hp=rules<9?legacy_v8::combat::formProfile(11,1).stats.maxHp:combat::formProfile(11,1).stats.maxHp;
        CHECK(isValid(old));CHECK(applyAutoFight(old)==Error::None&&old.autoCapture==AutoCapture::Awaiting);
        for(unsigned i=1;i<=3;++i){step(old,Action::Flick,0);CHECK(old.lastCapture.result==CaptureResult::Miss&&old.lastCapture.attempt==i);restore(old);}
        CHECK(old.phase==Phase::Home&&old.message==Message::CaptureEnded);
    }
}
State eightMemberFight(unsigned seed,unsigned rules){
    auto s=start(seed);s.sequence=s.foregroundSequence=100;s.collectionCount=8;s.captures=7;s.encounters=8;s.steps=700;s.nextMemberId=9;
    for(unsigned i=1;i<8;++i){s.collection[i]=s.collection[0];s.collection[i].id=i+1;s.collection[i].capturedAtSequence=i+1;s.collection[i].mood=50+i;}
    s.wildFormId=18;s.wildSpecies=Species::Agumon;s.wildLevel=1;s.wildRules=rules;
    s.wildMaxHp=s.wildHp=(rules<9?legacy_v8::combat::formProfile(18,1).stats.maxHp:combat::formProfile(18,1).stats.maxHp);
    s.hp=s.collection[0].hp=(rules<9?legacy_v8::combat::formProfile(11,1).stats.maxHp:combat::formProfile(11,1).stats.maxHp);
    CHECK(isValid(s));return s;
}
void priorAutoCapacityAndNewInputs(){
    namespace old=legacy_v13;
    for(unsigned rules:{7u,12u,13u})for(unsigned seed=1;seed<=32;++seed){
        auto current=eightMemberFight(seed,rules);Snapshot snapshot;CHECK(encodeSnapshot(current,snapshot));
        auto oldBytes=snapshot_test::eightSlotBytes(snapshot.bytes);oldBytes[4]=20;oldBytes[6]=140;oldBytes[7]=2;put32(oldBytes.data()+8,13);
        put32(oldBytes.data()+660,~updateCrc(~0u,oldBytes.data(),660));
        old::State prior;CHECK(old::decodeSnapshot(oldBytes.data(),oldBytes.size(),prior)==old::SnapshotStatus::Ok);
        autobattle::Trace a,b;CHECK(old::applyAuto(prior,&a)==old::Error::None&&applyAuto(current,&b)==Error::None);
        old::Snapshot expected;CHECK(old::encodeSnapshot(prior,expected)&&encodeSnapshot(current,snapshot));
        CHECK(snapshot_test::sameOldPayload(expected.bytes,snapshot.bytes,sizeof(expected.bytes)));
        char x[autobattle::kTraceJsonCapacity],y[autobattle::kTraceJsonCapacity];
        CHECK(autobattle::writeJson(a,x,sizeof(x))&&autobattle::writeJson(b,y,sizeof(y))&&!std::strcmp(x,y));
        CHECK(prior.collectionCount==8&&current.collectionCount==8);
        for(unsigned i=0;i<b.count;++i)CHECK(b.steps[i].action!=autobattle::Move::Capture);
    }
    // The legacy Auto compatibility rule never blocks a new manual or AutoFight
    // input from using the ninth slot immediately, including an old active foe.
    for(unsigned rules:{7u,12u,13u,14u}){
        auto tactical=eightMemberFight(1,rules);tactical.battleMode=BattleMode::Tactical;tactical.wildHp=tactical.wildMaxHp/2;
        unsigned phase=0;while(capturering::sample(phase,tactical.wildFormId).grade!=capturering::Grade::Green)++phase;
        const auto chance=ringCaptureChance(tactical,phase);CHECK(chance>0);
        auto next=[](unsigned x){x^=x<<13;x^=x>>17;x^=x<<5;return x;};
        while(next(tactical.rngState)%100>=chance)tactical.rngState=next(tactical.rngState);
        step(tactical,Action::RingCapture,phase);CHECK(tactical.collectionCount==9&&tactical.collection[8].id==9&&tactical.nextMemberId==10);restore(tactical);
        auto automatic=eightMemberFight(1,rules);const auto rng=automatic.rngState;
        CHECK(applyAutoFight(automatic)==Error::None&&automatic.autoCapture==AutoCapture::Awaiting&&captureChance(automatic)>0);
        CHECK(automatic.collectionCount==8&&automatic.rngState==rng);restore(automatic);
    }
}
void boundsAndMigration(){
    auto full=start();full.sequence=full.foregroundSequence=100;full.collectionCount=60;full.captures=59;full.encounters=60;full.steps=5900;full.nextMemberId=61;
    for(unsigned i=1;i<60;++i){full.collection[i]=full.collection[0];full.collection[i].id=i+1;full.collection[i].capturedAtSequence=i+1;}
    CHECK(isValid(full));auto replay=full;autobattle::Trace trace,again;CHECK(applyAutoFight(full,&trace)==Error::None&&full.phase==Phase::Home&&full.autoCapture==AutoCapture::None);attackTrace(trace);
    CHECK(applyAutoFight(replay,&again)==Error::None&&trade::sameState(full,replay));
    auto s=paused();auto bad=s;bad.autoCapture=AutoCapture::None;CHECK(isValid(bad));bad=s;bad.captureDeferred=1;CHECK(!isValid(bad));bad=s;bad.autoCapture=static_cast<AutoCapture>(2);CHECK(!isValid(bad));bad=s;bad.battleMode=BattleMode::Tactical;CHECK(!isValid(bad));bad=s;bad.wildHp=bad.wildMaxHp;CHECK(!isValid(bad));
    const auto before=s;CHECK(apply(s,Action::AutoResume,1)==Error::InvalidValue&&trade::sameState(s,before));
    s.sequence=UINT32_MAX;reject(s,Action::Flick);reject(s,Action::AutoResume);
    auto fresh=start();fresh.sequence=UINT32_MAX;reject(fresh,Action::AutoFight);
    fresh=start();fresh.wildRules=13;Snapshot bytes;CHECK(encodeSnapshot(fresh,bytes));std::array<std::uint8_t,kV18SnapshotSize> prior;
    const auto projected=snapshot_test::eightSlotBytes(bytes.bytes);std::memcpy(prior.data(),projected.data(),prior.size());prior[4]=18;put32(prior.data()+8,13);prior[6]=132;prior[7]=2;put32(prior.data()+prior.size()-4,~updateCrc(~0u,prior.data(),prior.size()-4));
    State migrated;CHECK(decodeSnapshot(prior.data(),prior.size(),migrated)==SnapshotStatus::Migrated&&migrated.autoCapture==AutoCapture::None&&trade::sameState(fresh,migrated));
    CHECK(encodeSnapshot(migrated,bytes)&&snapshot_test::sameOldPayload(prior.data(),bytes.bytes,prior.size()));
    put32(bytes.bytes+snapshot_test::currentOffset(652),2);put32(bytes.bytes+kSnapshotSize-4,~updateCrc(~0u,bytes.bytes,kSnapshotSize-4));CHECK(decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),migrated)==SnapshotStatus::InvalidState);
    auto waiting=paused();autobattle::Trace partial;auto a=start();a.wildFormId=18;a.wildSpecies=Species::Agumon;a.wildLevel=1;a.wildMaxHp=combat::formProfile(18,1).stats.maxHp;a.wildHp=a.wildMaxHp/2+1;CHECK(applyAutoFight(a,&partial)==Error::None);CHECK(partial.outcome==autobattle::Outcome::None);
    char json[autobattle::kTraceJsonCapacity];
    auto invalid=partial;invalid.kind=autobattle::Kind::Practice;CHECK(!autobattle::writeJson(invalid,json,sizeof(json)));
    invalid=partial;invalid.steps[0].action=autobattle::Move::Capture;CHECK(!autobattle::writeJson(invalid,json,sizeof(json)));invalid=partial;invalid.steps[0].captureAttempt=1;CHECK(!autobattle::writeJson(invalid,json,sizeof(json)));
    CHECK(!trade::canOffer(waiting,1));Action action;CHECK(parseAction("auto-fight",action)&&action==Action::AutoFight);CHECK(parseAction("auto-resume",action)&&action==Action::AutoResume);
    CHECK(kSchemaVersion==25&&kRulesVersion==18&&kSnapshotSize==3216);
}
}
int main(){historicalAutoUnchanged();pausesAndResume();actualFlickOnly();priorAutoCapacityAndNewInputs();boundsAndMigration();std::printf("%u Auto manual capture checks, %u failures; State=%zu snapshot=%zu\n",checks,failures,sizeof(State),kSnapshotSize);return failures?1:0;}
