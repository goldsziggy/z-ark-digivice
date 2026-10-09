#include "nearby_protocol.hpp"
#include "forms.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace digivice::nearby;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)
constexpr Mac ma{{2,1,2,3,4,5}},mb{{2,1,2,3,4,6}},outsider{{2,1,2,3,4,7}};
constexpr Fighter fa{17,11,1},fb{23,18,1};
bool same(const Match& a,const Match& b){std::uint8_t x[kMatchBytes],y[kMatchBytes];return encode(a,x,sizeof(x))&&encode(b,y,sizeof(y))&&!std::memcmp(x,y,sizeof(x));}
std::uint32_t crc(const std::uint8_t* p,std::size_t n){std::uint32_t c=~0u;for(std::size_t i=0;i<n;++i){c^=p[i];for(unsigned b=0;b<8;++b)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
void put(std::uint8_t* p,std::uint32_t n){for(unsigned i=0;i<4;++i)p[i]=static_cast<std::uint8_t>(n>>(8*i));}
void reseal(Datagram& d){put(d.bytes+d.length-4,crc(d.bytes,d.length-4));}
struct Pair {
 Protocol a,b;std::uint64_t now=10;unsigned delivered=0,dropped=0;std::size_t largest=0;
 void open(const Fighter& host=fa,const Fighter& guest=fb){CHECK(a.open(ma,host,now,100));CHECK(b.open(mb,guest,now,200));pump();CHECK(a.view().peerCount==1&&b.view().peerCount==1);}
 void pump(unsigned drop=0,bool duplicate=false){
  for(unsigned loops=0;loops<100;++loops){bool more=false;Datagram d;
   for(unsigned side=0;side<2;++side){auto& from=side?b:a;auto& to=side?a:b;const auto& mac=side?mb:ma;
    while(from.pop(d)){more=true;CHECK(d.length<=kMaxPacket);if(d.length>largest)largest=d.length;
     if(drop&(1u<<d.bytes[5])){++dropped;continue;}
     to.receive(mac,d.bytes,d.length,now);++delivered;if(duplicate)to.receive(mac,d.bytes,d.length,now);
    }
   }
   if(!more)return;
  }
  CHECK(false); // ACKs must not form an unbounded ping-pong.
 }
 void tick(unsigned delta,unsigned drop=0,bool duplicate=false){now+=delta;a.tick(now);b.tick(now);pump(drop,duplicate);}
 void start(Mode mode=Mode::Tactical){open();CHECK(a.challenge(0,mode,0x123456789abcdef1ull,42,now));pump();CHECK(a.view().stage==Stage::Outgoing&&b.view().stage==Stage::Incoming);CHECK(!valid(a.view().match)&&!valid(b.view().match));CHECK(b.accept(now));pump();CHECK(a.view().stage==Stage::Playing&&b.view().stage==Stage::Playing&&a.view().peerAcknowledged&&same(a.view().match,b.view().match));}
 void turn(bool duplicates=false){const auto oldSequence=a.view().match.sequence;const auto attacker=a.view().match.attacker;CHECK(a.choose(attacker?Choice::Ward:Choice::Physical,now));CHECK(b.choose(attacker?Choice::Magic:Choice::Brace,now));pump(0,duplicates);if(a.view().match.sequence==oldSequence)tick(2400,0,duplicates);CHECK(a.view().match.sequence==oldSequence+1&&same(a.view().match,b.view().match));}
};
void core(){
 CHECK(!validFighter({0,11,1})&&!validFighter({1,999,1})&&!validFighter({1,11,0}));
 Match unchanged;CHECK(!begin(fa,fb,Mode::Auto,0,unchanged));unsigned draws=0,physical=0,magic=0;
 for(unsigned seed=1;seed<=512;++seed){Match m;CHECK(begin(fa,fb,Mode::Auto,seed,m));const auto initial=m;unsigned rounds=0;
  while(m.status==Status::Active){auto replay=m;const auto seq=m.sequence;CHECK(resolveAuto(m,seq)&&resolveAuto(replay,seq)&&same(m,replay));
   CHECK(m.sequence==seq+1&&m.lastAttack!=Choice::Heavy);physical+=m.lastAttack==Choice::Physical;magic+=m.lastAttack==Choice::Magic;
   const auto after=m;CHECK(!resolveAuto(m,seq)&&same(m,after));std::uint8_t bytes[kMatchBytes];CHECK(encode(m,bytes,sizeof(bytes)));Match restored;CHECK(decode(bytes,sizeof(bytes),restored)&&same(m,restored));
   ++rounds;CHECK(rounds<=kMaxExchanges);
  }
  CHECK(initial.fighters[0].memberId==m.fighters[0].memberId&&initial.fighters[1].memberId==m.fighters[1].memberId);
  draws+=m.status==Status::Draw;
 }
 CHECK(physical&&magic&&physical*100/(physical+magic)>45&&physical*100/(physical+magic)<55);
 Match m;CHECK(begin(fa,fb,Mode::Tactical,9,m));const auto before=m;CHECK(!resolve(m,0,Choice::Brace,Choice::Magic)&&same(m,before));
 CHECK(resolve(m,0,Choice::Heavy,Choice::Counter)&&m.lastReflected&&m.lastDamage[0]>0&&!m.lastDamage[1]&&m.energy[0]==94);
 std::uint8_t bytes[kMatchBytes];CHECK(encode(m,bytes,sizeof(bytes)));const auto original=m;
 for(unsigned offset:{0u,4u,8u,12u,16u,32u,40u,48u,52u,56u,60u,64u,68u,72u,76u,80u,84u,92u,96u}){auto copy=std::vector<std::uint8_t>(bytes,bytes+kMatchBytes);put(copy.data()+offset,offset==48||offset==52?0:UINT32_MAX);m=original;CHECK(!decode(copy.data(),copy.size(),m)&&same(m,original));}
 std::printf("Nearby core512 Auto duels: physical%u magic%u draws%u; maximum40 exchanges\n",physical,magic,draws);
}
void consentAndLoss(){
 Pair p;p.open();CHECK(!p.a.challenge(0,Mode::Tactical,0,42,p.now));CHECK(p.a.challenge(0,Mode::Tactical,123,42,p.now));p.pump(1u<<2);CHECK(p.b.view().stage==Stage::Discovering);
 p.tick(500);CHECK(p.b.view().stage==Stage::Incoming);p.tick(2500);CHECK(p.b.view().stage==Stage::Incoming&&!valid(p.a.view().match));
 CHECK(!p.b.choose(Choice::Brace,p.now));CHECK(p.b.accept(p.now));p.pump(1u<<3);CHECK(p.a.view().stage==Stage::Outgoing);
 p.tick(500,1u<<4);CHECK(p.a.view().stage==Stage::Playing&&p.b.view().stage==Stage::Accepting);p.tick(500);CHECK(same(p.a.view().match,p.b.view().match));
 CHECK(p.a.view().offered[0].memberId==fa.memberId&&p.a.view().offered[1].memberId==fb.memberId);
 // Lost choice ACK and state: host commits once; retry presents only that same result.
 CHECK(p.b.choose(Choice::Brace,p.now));Datagram oldChoice;CHECK(p.b.pop(oldChoice));CHECK(p.a.receive(mb,oldChoice.bytes,oldChoice.length,p.now));
 CHECK(p.a.choose(Choice::Physical,p.now));p.pump((1u<<4)|(1u<<6));CHECK(p.a.view().match.sequence==1&&p.b.view().match.sequence==0);
 const auto committed=p.a.view().match;CHECK(!p.a.receive(mb,oldChoice.bytes,oldChoice.length,p.now));CHECK(same(committed,p.a.view().match));
 p.tick(500,0,true);CHECK(same(p.a.view().match,p.b.view().match)&&!p.b.view().localChoicePending);
 for(unsigned i=0;i<40&&p.a.view().stage==Stage::Playing;++i)p.turn(true);
 CHECK(p.a.view().stage==Stage::Finished&&p.b.view().stage==Stage::Finished&&same(p.a.view().match,p.b.view().match));
 const auto final=p.a.view().match;p.tick(500,1u<<6);p.tick(500);CHECK(same(final,p.a.view().match)&&same(final,p.b.view().match));
 CHECK(p.largest==154);std::printf("Loss/duplicate duet: %u delivered/%u deliberately dropped; largest packet%zu bytes\n",p.delivered,p.dropped,p.largest);
}
void recoverAndTimeout(){
 Pair p;p.start();p.turn();const auto checkpoint=p.a.view().match;
 p.tick(6000,0xffffffffu);CHECK(p.a.view().stage==Stage::Reconnecting&&p.b.view().stage==Stage::Reconnecting&&same(checkpoint,p.a.view().match));
 p.tick(500);CHECK(p.a.view().stage==Stage::Playing&&p.b.view().stage==Stage::Playing&&same(checkpoint,p.b.view().match));p.turn();
 const auto last=p.a.view().match;p.tick(30000,0xffffffffu);CHECK(p.a.view().stage==Stage::TimedOut&&p.b.view().stage==Stage::TimedOut&&same(last,p.a.view().match));
 CHECK(!p.a.choose(Choice::Physical,p.now));p.tick(500);CHECK(p.a.view().stage==Stage::TimedOut);
 Pair cancel;cancel.start();cancel.a.cancel(cancel.now);cancel.pump(1u<<7);CHECK(cancel.a.view().stage==Stage::Cancelled&&cancel.b.view().stage==Stage::Playing);
 cancel.tick(500);CHECK(cancel.b.view().stage==Stage::Cancelled);CHECK(!cancel.b.accept(cancel.now));
}
void simultaneous(){
 Pair p;p.open();CHECK(p.a.challenge(0,Mode::Tactical,101,5,p.now));CHECK(p.b.challenge(0,Mode::Tactical,202,6,p.now));p.pump(0,true);
 CHECK(p.a.view().stage==Stage::Outgoing&&p.b.view().stage==Stage::Incoming&&p.a.view().session==101&&p.b.view().session==101);
 CHECK(p.b.accept(p.now));p.pump();CHECK(p.a.view().host&&!p.b.view().host&&same(p.a.view().match,p.b.view().match));
}
void wireGuards(){
 Pair p;p.start();CHECK(p.b.choose(Choice::Brace,p.now));Datagram choice;CHECK(p.b.pop(choice));const auto before=p.a.view().match;
 CHECK(!p.a.receive(outsider,choice.bytes,choice.length,p.now));CHECK(!p.a.receive(mb,choice.bytes,choice.length-1,p.now));
 for(unsigned i=0;i<choice.length;++i){auto corrupt=choice;corrupt.bytes[i]^=1;CHECK(!p.a.receive(mb,corrupt.bytes,corrupt.length,p.now)&&same(before,p.a.view().match));}
 auto future=choice;put(future.bytes+26,3);reseal(future);CHECK(!p.a.receive(mb,future.bytes,future.length,p.now)&&same(before,p.a.view().match));
 auto stale=choice;put(stale.bytes+30,999);reseal(stale);CHECK(!p.a.receive(mb,stale.bytes,stale.length,p.now));
 auto incompatible=choice;incompatible.bytes[14]=10;reseal(incompatible);CHECK(!p.a.receive(mb,incompatible.bytes,incompatible.length,p.now)&&p.a.view().stage==Stage::Incompatible);
 Pair replay;replay.open();CHECK(replay.a.challenge(0,Mode::Tactical,444,99,replay.now));Datagram old;CHECK(replay.a.pop(old));
 CHECK(replay.b.open(mb,fb,replay.now+1,201));CHECK(!replay.b.receive(ma,old.bytes,old.length,replay.now+1)&&replay.b.view().stage==Stage::Discovering);
}
void boundedQueuesAndReordering(){
 Pair p;p.start();CHECK(p.b.choose(Choice::Brace,p.now));Datagram choice;CHECK(p.b.pop(choice));
 for(unsigned i=0;i<100;++i)CHECK(p.a.receive(mb,choice.bytes,choice.length,p.now));
 unsigned count=0;Datagram d;while(p.a.pop(d))++count;CHECK(count==kTxCapacity);CHECK(p.a.view().match.sequence==0);
 CHECK(p.a.choose(Choice::Physical,p.now));Datagram state;CHECK(p.a.pop(state)&&state.bytes[5]==4);CHECK(p.b.receive(ma,state.bytes,state.length,p.now));p.pump();
 const auto current=p.b.view().match;
 // Reordered old choice can only elicit the committed current state.
 CHECK(!p.a.receive(mb,choice.bytes,choice.length,p.now));p.pump();CHECK(same(current,p.b.view().match));
 auto changed=state;changed.bytes[34+32]^=1;reseal(changed);CHECK(!p.b.receive(ma,changed.bytes,changed.length,p.now)&&same(current,p.b.view().match));
 // Live heartbeats do not make an abandoned human turn last forever.
 for(unsigned i=0;i<240;++i)p.tick(500);
 CHECK(p.a.view().stage==Stage::TimedOut&&p.b.view().stage==Stage::TimedOut);
 Pair discovery;discovery.open();Protocol extra[5];
 for(unsigned i=0;i<5;++i){Mac mac{{2,7,7,7,7,static_cast<std::uint8_t>(i+1)}};CHECK(extra[i].open(mac,fa,discovery.now,10+i));CHECK(extra[i].pop(d));discovery.a.receive(mac,d.bytes,d.length,discovery.now);}
 CHECK(discovery.a.view().peerCount==kMaxPeers);
}
void reviewedRegressions(){
 Pair p;p.start(Mode::Auto);p.now+=500;p.a.tick(p.now);Datagram state;CHECK(p.a.pop(state)&&state.bytes[5]==4);
 const auto before=p.b.view().match;auto future=before;for(unsigned i=0;i<3;++i)CHECK(resolveAuto(future,future.sequence));
 auto forged=state;CHECK(encode(future,forged.bytes+34,kMatchBytes));put(forged.bytes+26,future.sequence);reseal(forged);
 CHECK(!p.b.receive(ma,forged.bytes,forged.length,p.now)&&same(before,p.b.view().match));
 // A stale incompatible packet from an old open nonce cannot terminate a live session.
 auto wrongEpoch=state;wrongEpoch.bytes[14]=10;put(wrongEpoch.bytes+30,999);reseal(wrongEpoch);
 CHECK(!p.b.receive(ma,wrongEpoch.bytes,wrongEpoch.length,p.now)&&p.b.view().stage==Stage::Playing);
 for(unsigned i=0;i<40&&p.a.view().stage==Stage::Playing;++i)p.tick(2400);
 CHECK(p.b.view().stage==Stage::Finished);const auto terminal=p.b.view().match;
 auto resurrect=terminal;resurrect.status=Status::Active;++resurrect.sequence;resurrect.attacker=static_cast<std::uint8_t>(resurrect.sequence%2);resurrect.lastAttacker=static_cast<std::uint8_t>((resurrect.sequence-1)%2);
 resurrect.hp[0]=digivice::combat::formProfile(fa.formId,fa.level).stats.maxHp;resurrect.hp[1]=digivice::combat::formProfile(fb.formId,fb.level).stats.maxHp;
 CHECK(valid(resurrect));forged=state;CHECK(encode(resurrect,forged.bytes+34,kMatchBytes));put(forged.bytes+26,resurrect.sequence);reseal(forged);
 CHECK(!p.b.receive(ma,forged.bytes,forged.length,p.now)&&p.b.view().stage==Stage::Finished&&same(terminal,p.b.view().match));
 // The authority cannot substitute a different guard for the guest's submitted choice.
 Pair tactical;tactical.start();const auto initial=tactical.a.view().match;CHECK(tactical.b.choose(Choice::Brace,tactical.now));tactical.pump();CHECK(tactical.a.choose(Choice::Physical,tactical.now));
 CHECK(tactical.a.pop(state)&&state.bytes[5]==4);auto substitute=initial;CHECK(resolve(substitute,0,Choice::Physical,Choice::Ward));forged=state;CHECK(encode(substitute,forged.bytes+34,kMatchBytes));reseal(forged);
 CHECK(!tactical.b.receive(ma,forged.bytes,forged.length,tactical.now)&&same(initial,tactical.b.view().match));
 CHECK(tactical.b.receive(ma,state.bytes,state.length,tactical.now)&&same(tactical.a.view().match,tactical.b.view().match));
 // A delayed state whose ACK is lost still gets its full visible hold when Choice implies ACK.
 Pair gate;gate.start();CHECK(gate.b.choose(Choice::Brace,gate.now));gate.pump();CHECK(gate.a.choose(Choice::Physical,gate.now));CHECK(gate.a.pop(state)&&state.bytes[5]==4);
 gate.now+=3000;CHECK(gate.a.choose(Choice::Ward,gate.now));CHECK(gate.b.receive(ma,state.bytes,state.length,gate.now));
 Datagram pending;while(gate.b.pop(pending)){} // Lose the State ACK.
 CHECK(gate.b.choose(Choice::Magic,gate.now));gate.pump();CHECK(gate.a.view().match.sequence==1&&gate.a.view().peerAcknowledged);
 gate.tick(2399);CHECK(gate.a.view().match.sequence==1);gate.tick(1);CHECK(gate.a.view().match.sequence==2&&same(gate.a.view().match,gate.b.view().match));
 // Repeated Heavy can empty energy, but basic attacks remain legal as in wild combat.
 Match exhausted;CHECK(begin({1,22,19},{2,22,19},Mode::Tactical,7,exhausted));
 while(exhausted.sequence<36){const auto a=exhausted.attacker;const auto move=exhausted.energy[a]>=6?Choice::Heavy:Choice::Physical;CHECK(resolve(exhausted,exhausted.sequence,a?Choice::Brace:move,a?move:Choice::Brace));}
 CHECK(exhausted.energy[0]==0&&exhausted.energy[1]==0&&exhausted.status==Status::Active);
 CHECK(!legalChoice(exhausted,exhausted.attacker,Choice::Heavy)&&legalChoice(exhausted,exhausted.attacker,Choice::Physical)&&legalChoice(exhausted,exhausted.attacker,Choice::Magic));
 while(exhausted.status==Status::Active){const auto a=exhausted.attacker;CHECK(resolve(exhausted,exhausted.sequence,a?Choice::Brace:Choice::Physical,a?Choice::Physical:Choice::Brace));}
 CHECK(exhausted.sequence<=kMaxExchanges);
}
void autoPacing(){
 Pair p;p.start(Mode::Auto);CHECK(!p.a.choose(Choice::Physical,p.now));const auto start=p.a.view().match;
 p.tick(2399);CHECK(same(start,p.a.view().match));p.tick(1,1u<<6);CHECK(p.a.view().match.sequence==1&&!p.a.view().peerAcknowledged);
 const auto frame=p.a.view().match;p.tick(4800,1u<<6);CHECK(same(frame,p.a.view().match));p.tick(500);CHECK(p.a.view().peerAcknowledged&&same(frame,p.b.view().match));
 p.tick(2399);CHECK(same(frame,p.a.view().match));p.tick(1);CHECK(p.a.view().match.sequence==2);
 for(unsigned i=0;i<40&&p.a.view().stage==Stage::Playing;++i)p.tick(2400,0,true);
 CHECK(p.a.view().stage==Stage::Finished&&same(p.a.view().match,p.b.view().match));
 const auto terminal=p.a.view().match;p.b.close();p.tick(30001,0xffffffffu);CHECK(p.a.view().stage==Stage::Finished&&same(terminal,p.a.view().match));
}
void careBonusHandshake(){
 constexpr Fighter host{17,11,1,3,4},guest{23,18,1,5,1};
 CHECK(kRules==12&&kMatchBytes==116&&sizeof(Fighter)==20);
 for(unsigned offense=0;offense<=5;++offense)for(unsigned protection=0;protection<=5;++protection){
  const Fighter selected{17,11,1,offense,protection};Match original,decoded;
  CHECK(validFighter(selected)&&begin(selected,guest,Mode::Tactical,42,original));
  std::uint8_t bytes[kMatchBytes];CHECK(encode(original,bytes,sizeof(bytes))&&decode(bytes,sizeof(bytes),decoded));
  CHECK(sameFighter(decoded.fighters[0],selected)&&sameFighter(decoded.fighters[1],guest));
 }
 CHECK(!validFighter({17,11,1,6,0})&&!validFighter({17,11,1,0,6}));
 CHECK(!validFighter({17,11,1,UINT32_MAX,0})&&!validFighter({17,11,1,0,UINT32_MAX}));
 Protocol invalid;CHECK(!invalid.open(ma,{17,11,1,6,0},10,100));
 Pair p;p.open(host,guest);CHECK(sameFighter(p.a.view().peers[0].fighter,guest)&&sameFighter(p.b.view().peers[0].fighter,host));
 CHECK(p.a.challenge(0,Mode::Tactical,77,42,p.now));Datagram challenge;CHECK(p.a.pop(challenge)&&challenge.bytes[5]==2&&challenge.length==87);
 // Even valid-range replacements must not change either announced fighter.
 for(unsigned offset:{12u,16u,32u,36u})for(unsigned value:{0u,6u,UINT32_MAX}){
  auto changed=challenge;put(changed.bytes+34+offset,value);reseal(changed);
  CHECK(!p.b.receive(ma,changed.bytes,changed.length,p.now)&&p.b.view().stage==Stage::Discovering);
 }
 CHECK(p.b.receive(ma,challenge.bytes,challenge.length,p.now));
 CHECK(p.b.receive(ma,challenge.bytes,challenge.length,p.now));
 CHECK(sameFighter(p.b.view().offered[0],host)&&sameFighter(p.b.view().offered[1],guest));
 CHECK(p.b.accept(p.now));Datagram accept;CHECK(p.b.pop(accept)&&accept.bytes[5]==3&&accept.length==87);
 for(unsigned offset:{12u,16u,32u,36u})for(unsigned value:{0u,6u}){
  auto changed=accept;put(changed.bytes+34+offset,value);reseal(changed);
  CHECK(!p.a.receive(mb,changed.bytes,changed.length,p.now)&&p.a.view().stage==Stage::Outgoing);
 }
 CHECK(p.a.receive(mb,accept.bytes,accept.length,p.now));Datagram state;CHECK(p.a.pop(state)&&state.bytes[5]==4&&state.length==154);
 for(unsigned offset:{100u,104u,108u,112u})for(unsigned value:{0u,6u}){
  auto changed=state;put(changed.bytes+34+offset,value);reseal(changed);
  CHECK(!p.b.receive(ma,changed.bytes,changed.length,p.now)&&p.b.view().stage==Stage::Accepting);
 }
 CHECK(p.b.receive(ma,state.bytes,state.length,p.now));p.pump();
 CHECK(same(p.a.view().match,p.b.view().match)&&sameFighter(p.b.view().match.fighters[0],host));
 // Duplicate acceptance and state replay can acknowledge only the same match.
 CHECK(p.a.receive(mb,accept.bytes,accept.length,p.now));p.pump(0,true);p.turn(true);
 const auto after=p.b.view().match;
 auto staleChanged=state;put(staleChanged.bytes+34+100,0);reseal(staleChanged);
 CHECK(!p.b.receive(ma,staleChanged.bytes,staleChanged.length,p.now)&&same(after,p.b.view().match));
 CHECK(p.b.receive(ma,state.bytes,state.length,p.now)&&same(after,p.b.view().match));p.pump();
 p.tick(6000,0xffffffffu);p.tick(500);CHECK(same(p.a.view().match,p.b.view().match)&&sameFighter(p.a.view().match.fighters[1],guest));
 // Reopening never imports a prior session's offer, bonus or match state.
 CHECK(p.b.open(mb,guest,p.now+1,201));
 CHECK(!p.b.receive(ma,challenge.bytes,challenge.length,p.now+1)&&p.b.view().stage==Stage::Discovering);
 CHECK(!p.b.receive(ma,state.bytes,state.length,p.now+1)&&p.b.view().stage==Stage::Discovering);
}
void previousRulesHandshake(){
 Protocol a,b;CHECK(a.open(ma,fa,10,100)&&b.open(mb,fb,10,200));Datagram hello;CHECK(a.pop(hello)&&hello.length==59);
 // A real rules11 Hello has only three fighter words plus availability.
 auto legacy=hello;legacy.bytes[14]=11;legacy.bytes[34+12]=hello.bytes[34+20];legacy.length=51;legacy.bytes[6]=51;legacy.bytes[7]=0;reseal(legacy);
 CHECK(!b.receive(ma,legacy.bytes,legacy.length,10)&&b.view().peerCount==0);
 for(unsigned offset:{12u,16u}){
  auto invalidHello=hello;put(invalidHello.bytes+34+offset,6);reseal(invalidHello);
  CHECK(!b.receive(ma,invalidHello.bytes,invalidHello.length,10)&&b.view().peerCount==0);
 }
 // A differently tagged extended Hello may be shown as incompatible, never challenged.
 auto incompatible=hello;incompatible.bytes[14]=11;reseal(incompatible);
 CHECK(b.receive(ma,incompatible.bytes,incompatible.length,10)&&!b.view().peers[0].compatible);
 CHECK(!b.challenge(0,Mode::Auto,99,42,10));
 Pair p;p.open();CHECK(p.a.challenge(0,Mode::Auto,99,42,p.now));Datagram offered;CHECK(p.a.pop(offered));
 legacy=offered;legacy.bytes[14]=11;
 std::memcpy(legacy.bytes+34+12,offered.bytes+34+20,12);
 std::memcpy(legacy.bytes+34+24,offered.bytes+34+40,9);
 legacy.length=71;legacy.bytes[6]=71;legacy.bytes[7]=0;reseal(legacy);
 CHECK(!p.b.receive(ma,legacy.bytes,legacy.length,p.now)&&p.b.view().stage==Stage::Discovering);
 // Retagging an old short packet cannot bypass the new exact-length check.
 legacy.bytes[14]=12;reseal(legacy);CHECK(!p.b.receive(ma,legacy.bytes,legacy.length,p.now));
 CHECK(p.b.receive(ma,offered.bytes,offered.length,p.now)&&p.b.accept(p.now));p.pump();
 const auto before=p.b.view().match;p.now+=500;p.a.tick(p.now);Datagram state;CHECK(p.a.pop(state)&&state.bytes[5]==4);
 state.bytes[14]=11;reseal(state);CHECK(!p.b.receive(ma,state.bytes,state.length,p.now)&&p.b.view().stage==Stage::Incompatible&&same(before,p.b.view().match));
}
}
int main(){core();consentAndLoss();recoverAndTimeout();simultaneous();wireGuards();boundedQueuesAndReordering();reviewedRegressions();autoPacing();careBonusHandshake();previousRulesHandshake();std::printf("%u nearby checks, %u failures; Fighter%zuB / Match%zuB / Protocol%zuB / max packet%zuB\n",checks,failures,sizeof(Fighter),sizeof(Match),sizeof(Protocol),34+kMatchBytes+4);return failures?1:0;}
