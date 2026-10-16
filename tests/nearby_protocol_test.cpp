#include "nearby_protocol.hpp"
#include "nearby_reward.hpp"
#include "forms.hpp"
#include "expeditions.hpp"
#include "game.hpp"
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
 void open(const Fighter& host=fa,const Fighter& guest=fb,std::uint8_t hostActivity=255,std::uint8_t guestActivity=255){CHECK(a.open(ma,host,now,100,hostActivity));CHECK(b.open(mb,guest,now,200,guestActivity));pump();CHECK(a.view().peerCount==1&&b.view().peerCount==1);}
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
 CHECK(p.a.view().peerAcknowledged&&p.b.view().peerAcknowledged);
 const auto quiet=p.delivered;p.tick(500);CHECK(p.delivered==quiet); // Accepted result does not keep the radios talking.
 const auto terminal=p.a.view().match;p.b.close();p.tick(30001,0xffffffffu);CHECK(p.a.view().stage==Stage::Finished&&same(terminal,p.a.view().match));
}
void autoModeConsent(){
 Pair p;p.open();CHECK(p.a.challenge(0,Mode::Auto,901,42,p.now));Datagram challenge;CHECK(p.a.pop(challenge)&&challenge.bytes[5]==2);
 CHECK(p.b.receive(ma,challenge.bytes,challenge.length,p.now));CHECK(p.b.view().offeredMode==Mode::Auto&&p.b.view().stage==Stage::Incoming);
 CHECK(!p.a.challenge(0,Mode::Tactical,902,42,p.now)&&!p.b.challenge(0,Mode::Tactical,903,42,p.now));
 auto changed=challenge;changed.bytes[34+40]=static_cast<unsigned>(Mode::Tactical);reseal(changed);
 CHECK(!p.b.receive(ma,changed.bytes,changed.length,p.now)&&p.b.view().offeredMode==Mode::Auto);
 CHECK(p.b.receive(ma,challenge.bytes,challenge.length,p.now));p.tick(2500);
 CHECK(p.a.view().stage==Stage::Outgoing&&p.b.view().stage==Stage::Incoming&&!valid(p.a.view().match)&&!valid(p.b.view().match));
 CHECK(p.b.accept(p.now));Datagram accept;CHECK(p.b.pop(accept)&&accept.bytes[5]==3);
 changed=accept;changed.bytes[34+40]=static_cast<unsigned>(Mode::Tactical);reseal(changed);
 CHECK(!p.a.receive(mb,changed.bytes,changed.length,p.now)&&p.a.view().stage==Stage::Outgoing);
 CHECK(p.a.receive(mb,accept.bytes,accept.length,p.now));Datagram initial;CHECK(p.a.pop(initial)&&initial.bytes[5]==4);
 changed=initial;put(changed.bytes+34+60,static_cast<unsigned>(Mode::Tactical));reseal(changed);
 CHECK(!p.b.receive(ma,changed.bytes,changed.length,p.now)&&p.b.view().stage==Stage::Accepting);
 CHECK(p.b.receive(ma,initial.bytes,initial.length,p.now));p.pump();
 CHECK(p.a.view().match.mode==Mode::Auto&&same(p.a.view().match,p.b.view().match));
 CHECK(!p.a.choose(Choice::Physical,p.now)&&!p.b.choose(Choice::Magic,p.now));
 // A mode change is a new invitation with fresh consent, never a live toggle.
 p.a.cancel(p.now);p.pump();CHECK(p.b.view().stage==Stage::Cancelled);
 CHECK(p.a.open(ma,fa,++p.now,101)&&p.b.open(mb,fb,p.now,201));p.pump();CHECK(p.a.challenge(0,Mode::Tactical,904,43,p.now));p.pump();
 CHECK(p.b.view().offeredMode==Mode::Tactical&&p.b.view().stage==Stage::Incoming);
 CHECK(!p.a.receive(mb,accept.bytes,accept.length,p.now)&&!valid(p.a.view().match));
 CHECK(!p.b.receive(ma,initial.bytes,initial.length,p.now)&&p.b.view().stage==Stage::Incoming);
 CHECK(p.b.accept(p.now));p.pump();CHECK(p.a.view().match.mode==Mode::Tactical&&same(p.a.view().match,p.b.view().match));
 // Crossing invitations with different modes choose one offer, not consent.
 for(const auto hostMode:{Mode::Tactical,Mode::Auto}){
  Pair crossed;crossed.open();const auto other=hostMode==Mode::Auto?Mode::Tactical:Mode::Auto;
  CHECK(crossed.a.challenge(0,hostMode,501,7,crossed.now));CHECK(crossed.b.challenge(0,other,502,8,crossed.now));crossed.pump(0,true);
  CHECK(crossed.a.view().stage==Stage::Outgoing&&crossed.b.view().stage==Stage::Incoming&&crossed.b.view().offeredMode==hostMode);
  CHECK(!valid(crossed.a.view().match)&&!valid(crossed.b.view().match));crossed.tick(1000);
  CHECK(crossed.b.view().stage==Stage::Incoming);CHECK(crossed.b.accept(crossed.now));crossed.pump(0,true);
  CHECK(crossed.a.view().match.mode==hostMode&&same(crossed.a.view().match,crossed.b.view().match));
 }
}
void autoRandomAlternation(){
 // A CRC-valid, legal but non-random host result is not the agreed Auto turn.
 Pair checked;checked.start(Mode::Auto);const auto baseline=checked.b.view().match;checked.now+=kAutoPaceMs;checked.a.tick(checked.now);Datagram frame;CHECK(checked.a.pop(frame)&&frame.bytes[5]==4);
 const auto actual=checked.a.view().match;auto strategic=baseline;const auto alternative=actual.lastAttack==Choice::Physical?Choice::Magic:Choice::Physical;
 CHECK(resolve(strategic,baseline.sequence,alternative,actual.lastDefense));strategic.rng=actual.rng;CHECK(valid(strategic));auto forged=frame;CHECK(encode(strategic,forged.bytes+34,kMatchBytes));reseal(forged);
 CHECK(!checked.b.receive(ma,forged.bytes,forged.length,checked.now)&&same(baseline,checked.b.view().match));CHECK(checked.b.receive(ma,frame.bytes,frame.length,checked.now)&&same(actual,checked.b.view().match));
 unsigned seen[2][2]{};
 for(unsigned seed=1;seed<=256;++seed){
  Match ordinary,differentStats;CHECK(begin(fa,fb,Mode::Auto,seed,ordinary));
  CHECK(begin({90,22,19,5,0},{91,22,19,0,5},Mode::Auto,seed,differentStats));
  while(ordinary.status==Status::Active&&differentStats.status==Status::Active){
   const auto actor=ordinary.attacker;const auto sequence=ordinary.sequence;
   CHECK(resolveAuto(ordinary,sequence)&&resolveAuto(differentStats,sequence));
   // Identical seeds choose identical moves despite radically different stats
   // and care bonuses. Auto is random, not a matchup optimizer.
   CHECK(ordinary.lastAttack==differentStats.lastAttack&&ordinary.lastDefense==differentStats.lastDefense&&ordinary.rng==differentStats.rng);
   CHECK(ordinary.lastAttack==Choice::Physical||ordinary.lastAttack==Choice::Magic);
   CHECK(ordinary.lastAttacker==actor&&ordinary.attacker==1u-actor&&ordinary.attacker==ordinary.sequence%2);
   CHECK(differentStats.lastAttacker==actor&&differentStats.attacker==ordinary.attacker);
   ++seen[actor][ordinary.lastAttack==Choice::Magic?1:0];
  }
 }
 CHECK(seen[0][0]&&seen[0][1]&&seen[1][0]&&seen[1][1]);
 // Either radio identity can initiate; both still view the same host/guest
 // ordering and alternating sequence, independent of their local screen side.
 for(unsigned initiator=0;initiator<2;++initiator){Pair p;p.open();auto& host=initiator?p.b:p.a;auto& guest=initiator?p.a:p.b;
  CHECK(host.challenge(0,Mode::Auto,700+initiator,42,p.now));p.pump();CHECK(guest.accept(p.now));p.pump();CHECK(host.view().host&&!guest.view().host);
  while(host.view().stage==Stage::Playing){const auto previous=host.view().match;p.tick(kAutoPaceMs,0,true);CHECK(host.view().match.sequence==previous.sequence+1);CHECK(host.view().match.lastAttacker==previous.attacker);CHECK(same(host.view().match,guest.view().match));}
  CHECK(host.view().stage==Stage::Finished&&guest.view().stage==Stage::Finished);
 }
}
void autoReconnectAndReboot(){
 for(const unsigned lost:{2u,3u,4u,6u,8u}){
  Pair p;p.open();CHECK(p.a.challenge(0,Mode::Auto,800+lost,42,p.now));p.pump(1u<<lost,true);p.tick(500,0,true);
  CHECK(p.b.view().stage==Stage::Incoming&&!valid(p.b.view().match));CHECK(p.b.accept(p.now));p.pump(1u<<lost,true);p.tick(500,0,true);
  CHECK(p.a.view().stage==Stage::Playing&&p.b.view().stage==Stage::Playing&&same(p.a.view().match,p.b.view().match));
  p.tick(kAutoPaceMs,1u<<lost,true);p.tick(500,0,true);const auto checkpoint=p.a.view().match;
  CHECK(checkpoint.sequence==1&&same(checkpoint,p.b.view().match));
  p.tick(kReconnectMs+1,0xffffffffu);CHECK(p.a.view().stage==Stage::Reconnecting&&p.b.view().stage==Stage::Reconnecting&&same(checkpoint,p.a.view().match));
  p.tick(500,0,true);CHECK(p.a.view().stage==Stage::Playing&&p.b.view().stage==Stage::Playing&&same(p.a.view().match,p.b.view().match));
  // An already acknowledged frame may resume after the disconnected hold;
  // stop-and-wait still permits at most one new, shared exchange.
  CHECK(p.a.view().match.sequence<=checkpoint.sequence+1);
  for(unsigned turn=0;turn<kMaxExchanges&&p.a.view().stage==Stage::Playing;++turn)p.tick(kAutoPaceMs,0,true);
  CHECK(p.a.view().stage==Stage::Finished&&same(p.a.view().match,p.b.view().match));
 }
 Pair unacked;unacked.start(Mode::Auto);unacked.now+=kAutoPaceMs;unacked.a.tick(unacked.now);Datagram frame;CHECK(unacked.a.pop(frame)&&frame.bytes[5]==4);const auto committed=unacked.a.view().match;
 unacked.tick(kReconnectMs+1,0xffffffffu);CHECK(unacked.a.view().stage==Stage::Reconnecting&&unacked.b.view().stage==Stage::Reconnecting);
 unacked.tick(500,0,true);CHECK(same(committed,unacked.a.view().match)&&same(committed,unacked.b.view().match));
 unacked.tick(kAutoPaceMs-1);CHECK(same(committed,unacked.a.view().match));unacked.tick(1);CHECK(unacked.a.view().match.sequence==committed.sequence+1);
 const auto newer=unacked.b.view().match;CHECK(unacked.b.receive(ma,frame.bytes,frame.length,unacked.now));unacked.pump(0,true);CHECK(same(newer,unacked.b.view().match));
 // Reboot/close deliberately drops an ephemeral duel. Old state/consent cannot
 // resurrect it; the remaining device times out without rewards or captures.
 for(unsigned rebooted=0;rebooted<2;++rebooted){Pair p;p.start(Mode::Auto);p.tick(kAutoPaceMs);const auto before=p.a.view().match;
  p.now+=500;p.a.tick(p.now);Datagram old;CHECK(p.a.pop(old)&&old.bytes[5]==4);
  auto& reset=rebooted?p.b:p.a;reset.close();CHECK(reset.open(rebooted?mb:ma,rebooted?fb:fa,p.now,900+rebooted));
  CHECK(!reset.receive(rebooted?ma:mb,old.bytes,old.length,p.now)&&reset.view().stage==Stage::Discovering);
  p.tick(kSessionTimeoutMs,0xffffffffu);const auto& survivor=rebooted?p.a:p.b;
  CHECK(survivor.view().stage==Stage::TimedOut&&same(before,survivor.view().match)&&!valid(reset.view().match));
 }
 Pair clockReset;clockReset.start(Mode::Auto);clockReset.a.tick(0);CHECK(clockReset.a.view().stage==Stage::Cancelled);
}
void careBonusHandshake(){
 constexpr Fighter host{17,11,1,3,4},guest{23,18,1,5,1};
 CHECK(kRules==13&&kMatchBytes==116&&sizeof(Fighter)==20);
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
 CHECK(p.a.challenge(0,Mode::Tactical,77,42,p.now));Datagram challenge;CHECK(p.a.pop(challenge)&&challenge.bytes[5]==2&&challenge.length==88);
 // Even valid-range replacements must not change either announced fighter.
 for(unsigned offset:{12u,16u,32u,36u})for(unsigned value:{0u,6u,UINT32_MAX}){
  auto changed=challenge;put(changed.bytes+34+offset,value);reseal(changed);
  CHECK(!p.b.receive(ma,changed.bytes,changed.length,p.now)&&p.b.view().stage==Stage::Discovering);
 }
 CHECK(p.b.receive(ma,challenge.bytes,challenge.length,p.now));
 CHECK(p.b.receive(ma,challenge.bytes,challenge.length,p.now));
 CHECK(sameFighter(p.b.view().offered[0],host)&&sameFighter(p.b.view().offered[1],guest));
 CHECK(p.b.accept(p.now));Datagram accept;CHECK(p.b.pop(accept)&&accept.bytes[5]==3&&accept.length==88);
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
Choice raidInput(const Raid& raid,unsigned actor,Choice strike=Choice::Physical,Choice guard=Choice::Brace) {
 return !raid.hp[actor]?Choice::None:raidAttacker(raid)==actor?strike:guard;
}
void raidJoin(){
 Pair sameLobby;sameLobby.open(fa,fb,0,0);
 CHECK(sameLobby.a.view().peers[0].activity==0&&sameLobby.b.view().peers[0].activity==0&&sameLobby.a.view().peers[0].available);
 CHECK(!sameLobby.a.challenge(0,Mode::Tactical,123,42,sameLobby.now)&&sameLobby.a.view().stage==Stage::Discovering);
 const auto seed=raidSeed(18,1,0);
 CHECK(sameLobby.a.challenge(0,Mode::Coop,0xabc,seed,sameLobby.now));sameLobby.pump();
 CHECK(sameLobby.a.view().stage==Stage::Outgoing&&sameLobby.b.view().stage==Stage::Incoming&&sameLobby.b.view().foeForm==18&&sameLobby.b.view().foeLevel==1);
 CHECK(sameLobby.b.accept(sameLobby.now));sameLobby.pump();
 CHECK(sameLobby.a.view().stage==Stage::Playing&&sameLobby.b.view().stage==Stage::Playing&&sameRaid(sameLobby.a.view().raid,sameLobby.b.view().raid));
 CHECK(sameLobby.a.view().raid.sequence==0&&sameLobby.a.view().raid.foeHp&&sameLobby.a.view().raid.status==RaidStatus::Active);
 for(unsigned i=0;i<24&&sameLobby.a.view().stage==Stage::Playing;++i){
  const auto before=sameLobby.a.view().raid.sequence;
  const auto prior=sameLobby.a.view().raid;
  if(prior.hp[0])CHECK(sameLobby.a.choose(raidInput(prior,0),sameLobby.now));
  if(prior.hp[1])CHECK(sameLobby.b.choose(raidInput(prior,1),sameLobby.now));
  sameLobby.pump();
  if(sameLobby.a.view().raid.sequence==before)sameLobby.tick(2400);
  CHECK(sameLobby.a.view().raid.sequence==before+1&&sameRaid(sameLobby.a.view().raid,sameLobby.b.view().raid));
 }
 CHECK(sameLobby.a.view().stage==Stage::Finished&&sameLobby.b.view().stage==Stage::Finished);
 CHECK(sameRaid(sameLobby.a.view().raid,sameLobby.b.view().raid)&&sameLobby.a.view().raid.status!=RaidStatus::Active);
 Pair otherLobby;otherLobby.open(fa,fb,0,1);
 CHECK(!otherLobby.a.challenge(0,Mode::Coop,7,seed,otherLobby.now)&&otherLobby.a.view().stage==Stage::Discovering);
}
constexpr Fighter strongHost{17,11,99}, strongGuest{23,18,99};
void startRaid(Pair& pair, unsigned scenario=0, bool automatic=false) {
 pair.open(strongHost,strongGuest,static_cast<std::uint8_t>(scenario),static_cast<std::uint8_t>(scenario));
 CHECK(pair.a.challenge(0,Mode::Coop,0xabcdef,raidSeed(68,1,scenario),pair.now,automatic));pair.pump();
 CHECK(pair.b.accept(pair.now));pair.pump();
 CHECK(pair.a.view().stage==Stage::Playing&&pair.b.view().stage==Stage::Playing);
 CHECK(sameRaid(pair.a.view().raid,pair.b.view().raid));
}
void raidTurn(Pair& pair,unsigned drop=0,bool duplicate=false) {
 const auto sequence=pair.a.view().raid.sequence;
 if(pair.a.view().coopAuto){
  pair.tick(kAutoPaceMs,drop,duplicate);
  CHECK(pair.a.view().raid.sequence==sequence+1);return;
 }
 const auto prior=pair.a.view().raid;
 if(prior.hp[0])CHECK(pair.a.choose(raidInput(prior,0),pair.now));
 if(prior.hp[1])CHECK(pair.b.choose(raidInput(prior,1),pair.now));pair.pump(drop,duplicate);
 if(pair.a.view().raid.sequence==sequence)pair.tick(2400,drop,duplicate);
 CHECK(pair.a.view().raid.sequence==sequence+1);
}
void raidTiersAndEncoding() {
 CHECK(digivice::expeditions::tierCount(0)==3&&digivice::expeditions::tierCount(2)==3);
 CHECK(digivice::expeditions::tierCount(3)==1&&digivice::expeditions::tierCount(5)==1&&digivice::expeditions::tierCount(6)==0);
 CHECK(digivice::expeditions::tierLevel(1,1)==1&&digivice::expeditions::tierLevel(1,3)==3);
 CHECK(digivice::expeditions::tierLevel(98,3)==99&&digivice::expeditions::tierLevel(99,3)==99);
 for(unsigned scenario=0;scenario<6;++scenario){
  Raid raid;CHECK(beginRaid(strongHost,strongGuest,raidSeed(18,1,scenario),raid));
  const auto tiers=digivice::expeditions::tierCount(scenario);unsigned visited=1;
  CHECK(raid.tier==1&&raid.sequence==0&&raid.tierRound==0&&!raid.lastTier);
  while(raid.status==RaidStatus::Active&&raid.sequence<72){
   const auto before=raid;const auto host=raidInput(raid,0),guest=raidInput(raid,1);
   CHECK(resolveRaid(raid,before.sequence,host,guest));
   CHECK(raid.sequence==before.sequence+1&&sameFighter(raid.players[0],strongHost)&&sameFighter(raid.players[1],strongGuest));
   CHECK(raid.hp[0]<=before.hp[0]&&raid.hp[1]<=before.hp[1]&&raid.energy[0]<=before.energy[0]&&raid.energy[1]<=before.energy[1]);
   CHECK(raid.lastFoeForm==before.foeForm&&raid.lastFoeLevel==before.foeLevel&&raid.lastTier==before.tier);
   if(raid.tier!=before.tier){
    ++visited;CHECK(raid.tier==before.tier+1&&raid.tierRound==0&&raid.foeHp==raid.foeMax);
    CHECK(raid.status==RaidStatus::Active&&raid.foeLevel==before.foeLevel+1&&raid.foeForm==before.foeForm);
   }
   const auto committed=raid;CHECK(!resolveRaid(raid,before.sequence,host,guest)&&sameRaid(raid,committed));
   std::uint8_t bytes[kRaidBytes];Raid restored;CHECK(encodeRaid(raid,bytes,sizeof(bytes)));
   CHECK(decodeRaid(bytes,sizeof(bytes),restored)&&sameRaid(raid,restored));
   CHECK(!decodeRaid(bytes,108,restored)); // V1 raid layout cannot be silently upgraded.
   for(unsigned offset:{104u,108u,112u,116u}){
    auto corrupt=std::vector<std::uint8_t>(bytes,bytes+kRaidBytes);put(corrupt.data()+offset,UINT32_MAX);
    CHECK(!decodeRaid(corrupt.data(),corrupt.size(),restored)&&sameRaid(restored,committed));
   }
  }
  CHECK(visited==tiers&&raid.tier==tiers&&raid.status==RaidStatus::Cleared&&raid.foeHp==0);
  CHECK(!resolveRaid(raid,raid.sequence,Choice::Physical,Choice::Brace));
 }
 // Per-tier exhaustion cannot accidentally open a later tier or reward a loss.
 Raid exhausted;CHECK(beginRaid(fa,fb,raidSeed(18,99,0),exhausted));
 unsigned steps=0;while(exhausted.status==RaidStatus::Active&&steps++<24){const auto q=exhausted.sequence;
  CHECK(resolveRaid(exhausted,q,raidInput(exhausted,0),raidInput(exhausted,1)));}
 CHECK(exhausted.status==RaidStatus::Wiped&&exhausted.tier==1&&exhausted.sequence<=24);
}
void raidLossReorderAndReconnect() {
 Pair p;startRaid(p);
 CHECK(p.b.choose(Choice::Brace,p.now));Datagram oldChoice;CHECK(p.b.pop(oldChoice)&&oldChoice.bytes[5]==5);
 CHECK(p.a.receive(mb,oldChoice.bytes,oldChoice.length,p.now));CHECK(p.a.choose(Choice::Physical,p.now));
 p.pump((1u<<9)|(1u<<6));
 CHECK(p.a.view().raid.tier==2&&p.a.view().raid.sequence==1&&p.b.view().raid.tier==1);
 const auto committed=p.a.view().raid;
 CHECK(!p.a.receive(mb,oldChoice.bytes,oldChoice.length,p.now)&&sameRaid(committed,p.a.view().raid));
 p.tick(500,0,true);CHECK(sameRaid(p.a.view().raid,p.b.view().raid)&&p.b.view().raid.tier==2);
 CHECK(!claimableRaid(p.a.view(),0)&&!claimableRaid(p.b.view(),0));
 p.tick(6000,0xffffffffu);CHECK(p.a.view().stage==Stage::Reconnecting&&p.b.view().stage==Stage::Reconnecting);
 CHECK(sameRaid(committed,p.a.view().raid)&&!claimableRaid(p.a.view(),0));
 p.tick(500,0,true);CHECK(p.a.view().stage==Stage::Playing&&p.b.view().stage==Stage::Playing&&sameRaid(committed,p.b.view().raid));
 raidTurn(p,0,true);CHECK(p.a.view().raid.tier==3&&p.a.view().raid.sequence==2&&sameRaid(p.a.view().raid,p.b.view().raid));
 CHECK(!claimableRaid(p.a.view(),0));
 // A stale previous-tier state is ACKed, never installed over the newer tier.
 p.now+=500;p.a.tick(p.now);Datagram current;CHECK(p.a.pop(current)&&current.bytes[5]==9);
 auto stale=current;CHECK(encodeRaid(committed,stale.bytes+34,kRaidBytes));put(stale.bytes+26,committed.sequence);reseal(stale);
 const auto third=p.b.view().raid;CHECK(p.b.receive(ma,stale.bytes,stale.length,p.now)&&sameRaid(third,p.b.view().raid));p.pump();
 // A one-sequence-ahead forged tier cannot pass deterministic replay.
 auto forged=current;auto changed=third;changed.foeHp-=1;CHECK(encodeRaid(changed,forged.bytes+34,kRaidBytes));reseal(forged);
 CHECK(!p.b.receive(ma,forged.bytes,forged.length,p.now)&&sameRaid(third,p.b.view().raid));
 p.a.cancel(p.now);Datagram cancellation;CHECK(p.a.pop(cancellation)&&cancellation.bytes[5]==7);
 CHECK(cancellation.bytes[26]==2&&p.b.receive(ma,cancellation.bytes,cancellation.length,p.now));
 CHECK(p.a.view().stage==Stage::Cancelled&&p.b.view().stage==Stage::Cancelled&&!claimableRaid(p.a.view(),0));
 Pair timeout;startRaid(timeout);raidTurn(timeout);const auto checkpoint=timeout.a.view().raid;
 timeout.tick(30000,0xffffffffu);CHECK(timeout.a.view().stage==Stage::TimedOut&&timeout.b.view().stage==Stage::TimedOut);
 CHECK(sameRaid(checkpoint,timeout.a.view().raid)&&!claimableRaid(timeout.a.view(),0)&&!timeout.a.choose(Choice::Brace,timeout.now));
 Pair reboot;startRaid(reboot);raidTurn(reboot);reboot.b.close();
 CHECK(reboot.b.open(mb,strongGuest,reboot.now+1,201,0));reboot.now+=1;reboot.pump();reboot.tick(500);
 CHECK(reboot.b.view().stage==Stage::Discovering&&!claimableRaid(reboot.b.view(),0));
 reboot.tick(30000);CHECK(reboot.a.view().stage==Stage::TimedOut&&!claimableRaid(reboot.a.view(),0));
}
void raidFinalClaims(bool automatic=false) {
 Pair p;startRaid(p,0,automatic);raidTurn(p);raidTurn(p);
 CHECK(p.a.view().raid.tier==3&&p.a.view().stage==Stage::Playing&&!claimableRaid(p.a.view(),0));
 raidTurn(p,1u<<6,true); // Final result arrives; final ACK is lost.
 CHECK(p.a.view().stage==Stage::Finished&&p.b.view().stage==Stage::Finished);
 CHECK(!p.a.view().peerAcknowledged&&p.b.view().peerAcknowledged);
 CHECK(!claimableRaid(p.a.view(),0)&&claimableRaid(p.b.view(),0));
 p.now+=500;p.a.tick(p.now);Datagram terminal;CHECK(p.a.pop(terminal)&&terminal.bytes[5]==9);
 CHECK(p.b.receive(ma,terminal.bytes,terminal.length,p.now));CHECK(p.b.receive(ma,terminal.bytes,terminal.length,p.now));p.pump();
 CHECK(claimableRaid(p.a.view(),0)&&claimableRaid(p.b.view(),0));
 digivice::State saves[]{digivice::newDevice(11),digivice::newDevice(19)};std::uint64_t claimed[2]{};
 std::uint32_t awardedXp[2]{};unsigned checkpoints[2]{};digivice::Snapshot persisted[2];
 for(unsigned side=0;side<2;++side){
  auto& save=saves[side];CHECK(digivice::apply(save,digivice::Action::Hatch,1)==digivice::Error::None);save.dungeonKeys=3;
  const auto& view=side?p.b.view():p.a.view();const auto before=save;
  CHECK(claimableRaid(view,claimed[side]));
  CHECK(digivice::encodeSnapshot(save,persisted[side]));const auto previousSnapshot=persisted[side];
  CHECK(checkpointRaidReward(view,save,claimed[side],[&](const digivice::State& candidate){
   ++checkpoints[side];CHECK(candidate.dungeonKeys==2);return false;
  })==RewardResult::RetryCheckpoint);
  CHECK(save.sequence==before.sequence&&save.dungeonKeys==3&&claimed[side]==0&&claimableRaid(view,claimed[side]));
  CHECK(!std::memcmp(persisted[side].bytes,previousSnapshot.bytes,sizeof(previousSnapshot.bytes)));
  CHECK(checkpointRaidReward(view,save,claimed[side],[&](const digivice::State& candidate){
   ++checkpoints[side];return digivice::encodeSnapshot(candidate,persisted[side]);
  })==RewardResult::Committed);
  CHECK(checkpoints[side]==2);
  CHECK(save.dungeonKeys==2&&save.dungeonWins==before.dungeonWins+1&&save.dungeonClears==before.dungeonClears+1);
  CHECK(save.sequence==before.sequence+1&&!claimableRaid(view,claimed[side]));awardedXp[side]=digivice::activeMember(save)->xp;
 }
 // Duplicate final traffic and repeated runtime polling cannot spend a second
 // key, even though two keys remain and another raw claim could be accepted.
 const auto final=p.a.view().raid;
 for(unsigned i=0;i<10;++i){
  p.tick(500,0,true);CHECK(p.b.receive(ma,terminal.bytes,terminal.length,p.now));p.pump(0,true);
  for(unsigned side=0;side<2;++side){const auto& view=side?p.b.view():p.a.view();
   CHECK(checkpointRaidReward(view,saves[side],claimed[side],[&](const digivice::State&){++checkpoints[side];return true;})==RewardResult::Unavailable);
  }
 }
 CHECK(sameRaid(final,p.a.view().raid)&&sameRaid(final,p.b.view().raid));
 for(unsigned side=0;side<2;++side){const auto& view=side?p.b.view():p.a.view();CHECK(!claimableRaid(view,claimed[side])&&checkpoints[side]==2);
  CHECK(saves[side].dungeonKeys==2&&saves[side].dungeonClears==1&&digivice::activeMember(saves[side])->xp==awardedXp[side]);
  digivice::State restored;CHECK(digivice::decodeSnapshot(persisted[side].bytes,sizeof(persisted[side].bytes),restored)==digivice::SnapshotStatus::Ok);
  CHECK(restored.dungeonKeys==2&&restored.dungeonClears==1&&digivice::activeMember(restored)->xp==awardedXp[side]);
 }
 CHECK(saves[0].dungeonWins==saves[1].dungeonWins&&saves[0].dungeonKeys==saves[1].dungeonKeys&&awardedXp[0]==awardedXp[1]);
 // Closure/reopen (or reboot) gives a fresh nonce and no remembered session.
 // A delayed terminal packet cannot create a second claim against saved state.
 p.b.close();CHECK(p.b.open(mb,strongGuest,p.now+1,201,0));++p.now;claimed[1]=0;
 CHECK(!p.b.receive(ma,terminal.bytes,terminal.length,p.now)&&p.b.view().stage==Stage::Discovering);
 CHECK(checkpointRaidReward(p.b.view(),saves[1],claimed[1],[&](const digivice::State&){++checkpoints[1];return true;})==RewardResult::Unavailable);
 CHECK(checkpoints[1]==2&&saves[1].dungeonKeys==2&&saves[1].dungeonClears==1);
}
void raidEnergyBudget() {
 // A naturally reached regression: the old 24-round raid never ran long
 // enough to spend 100 energy. Three tiers can, and must not leave no legal turn.
 Raid raid;CHECK(beginRaid({17,25,99,5,5},{23,25,99,5,5},raidSeed(25,99,0),raid));
 while(raid.status==RaidStatus::Active&&raid.sequence<72){
  const auto actor=raidAttacker(raid);const auto strike=raid.energy[actor]>=6?Choice::Heavy:Choice::Physical;
  const auto guard=actor?Choice::Ward:Choice::Brace;
  CHECK(legalRaidChoice(raid,actor,strike));CHECK(!raidEnergyExhausted(raid));
  CHECK(resolveRaid(raid,raid.sequence,actor?guard:strike,actor?strike:guard));
 }
 CHECK(raid.status==RaidStatus::Wiped&&raid.tier==3&&raid.sequence==36&&raidEnergyExhausted(raid));
 CHECK(raid.hp[0]&&raid.hp[1]&&raid.foeHp&&!raid.energy[0]&&!raid.energy[1]);
 CHECK(!legalRaidChoice(raid,0,Choice::Physical)&&!legalRaidChoice(raid,1,Choice::Physical));
 std::uint8_t bytes[kRaidBytes];Raid restored;CHECK(encodeRaid(raid,bytes,sizeof(bytes))&&decodeRaid(bytes,sizeof(bytes),restored)&&sameRaid(raid,restored));
 Pair pair;pair.open({17,25,99,5,5},{23,25,99,5,5},0,0);
 CHECK(pair.a.challenge(0,Mode::Coop,9191,raidSeed(25,99,0),pair.now));pair.pump();CHECK(pair.b.accept(pair.now));pair.pump();
 while(pair.a.view().stage==Stage::Playing&&pair.a.view().raid.sequence<72){
  const auto& current=pair.a.view().raid;const auto sequence=current.sequence;const auto actor=raidAttacker(current);
  const auto strike=current.energy[actor]>=6?Choice::Heavy:Choice::Physical;const auto guard=actor?Choice::Ward:Choice::Brace;
  CHECK(pair.a.choose(actor?guard:strike,pair.now));CHECK(pair.b.choose(actor?strike:guard,pair.now));pair.pump(0,true);
  if(pair.a.view().raid.sequence==sequence)pair.tick(2400,0,true);
  CHECK(pair.a.view().raid.sequence==sequence+1&&sameRaid(pair.a.view().raid,pair.b.view().raid));
 }
 CHECK(pair.a.view().stage==Stage::Finished&&pair.b.view().stage==Stage::Finished&&sameRaid(raid,pair.a.view().raid));
 CHECK(!claimableRaid(pair.a.view(),0)&&!claimableRaid(pair.b.view(),0));
}

void raidWireCompatibility() {
 Pair p;CHECK(p.a.open(ma,strongHost,p.now,100,0));CHECK(p.b.open(mb,strongGuest,p.now,200,0));
 Datagram hello;CHECK(p.b.pop(hello)&&hello.bytes[5]==1&&hello.bytes[4]==kProtocolVersion);
 Datagram old;
 for(unsigned version=1;version<kProtocolVersion;++version){
  old=hello;old.bytes[4]=static_cast<std::uint8_t>(version);reseal(old);
  CHECK(p.a.receive(mb,old.bytes,old.length,p.now)&&p.a.view().peerCount==1&&!p.a.view().peers[0].compatible);
  CHECK(!p.a.challenge(0,Mode::Coop,3,raidSeed(18,1,0),p.now));
  CHECK(!p.a.challenge(0,Mode::Coop,3,raidSeed(18,1,0),p.now,true));
 }
 CHECK(p.a.receive(mb,hello.bytes,hello.length,p.now)&&p.a.view().peers[0].compatible);p.pump();
 CHECK(p.a.challenge(0,Mode::Coop,3,raidSeed(18,1,0),p.now));Datagram offer;CHECK(p.a.pop(offer));
 old=offer;old.bytes[4]=1;reseal(old);CHECK(!p.b.receive(ma,old.bytes,old.length,p.now)&&p.b.view().stage==Stage::Discovering);
 CHECK(p.b.receive(ma,offer.bytes,offer.length,p.now)&&p.b.accept(p.now));p.pump();
 p.now+=500;p.a.tick(p.now);Datagram state;CHECK(p.a.pop(state)&&state.bytes[5]==9);
 const auto unchanged=p.b.view().raid;state.bytes[4]=1;reseal(state);
 CHECK(!p.b.receive(ma,state.bytes,state.length,p.now)&&p.b.view().stage==Stage::Incompatible&&sameRaid(unchanged,p.b.view().raid));
}


void autoRaidCore() {
 unsigned physical=0,magic=0,guards[3]{},critical=0;
 for(unsigned scenario=0;scenario<6;++scenario)for(unsigned level=1;level<=24;++level){
  Raid raid;CHECK(beginRaid(strongHost,strongGuest,raidSeed(18,level,scenario),raid));
  unsigned visited=1;
  while(raid.status==RaidStatus::Active&&raid.sequence<72){
   const auto before=raid;auto replay=raid;const auto actor=raidAttacker(before);
   CHECK(resolveAutoRaid(raid,before.sequence)&&resolveAutoRaid(replay,before.sequence)&&sameRaid(raid,replay));
   CHECK(raid.sequence==before.sequence+1&&raid.lastAttack!=Choice::Heavy);
   CHECK(raid.lastAttack==Choice::Physical||raid.lastAttack==Choice::Magic);
   CHECK(sameFighter(raid.players[0],before.players[0])&&sameFighter(raid.players[1],before.players[1]));
   CHECK(raid.hp[0]<=before.hp[0]&&raid.hp[1]<=before.hp[1]);
   CHECK(raid.energy[actor]==before.energy[actor]-2&&raid.energy[1u-actor]==before.energy[1u-actor]);
   CHECK(raid.lastTier==before.tier&&raid.lastFoeForm==before.foeForm&&raid.lastFoeLevel==before.foeLevel);
   if(raid.tier!=before.tier){++visited;CHECK(raid.tier==before.tier+1&&raid.tierRound==0&&raid.foeHp==raid.foeMax);}
   physical+=raid.lastAttack==Choice::Physical;magic+=raid.lastAttack==Choice::Magic;
   if(raid.lastDefense!=Choice::None)++guards[static_cast<unsigned>(raid.lastDefense)-static_cast<unsigned>(Choice::Brace)];
   critical+=raid.lastCritical;
   const auto committed=raid;CHECK(!resolveAutoRaid(raid,before.sequence)&&sameRaid(raid,committed));
   std::uint8_t bytes[kRaidBytes];Raid restored;CHECK(encodeRaid(raid,bytes,sizeof(bytes))&&decodeRaid(bytes,sizeof(bytes),restored)&&sameRaid(raid,restored));
  }
  CHECK(raid.status!=RaidStatus::Active);
  if(level==1)CHECK(raid.status==RaidStatus::Cleared&&visited==digivice::expeditions::tierCount(scenario));
  const auto final=raid;CHECK(!resolveAutoRaid(raid,raid.sequence)&&sameRaid(raid,final));
 }
 CHECK(physical&&magic&&guards[0]&&guards[1]&&guards[2]&&critical);
 std::printf("Auto co-op144 runs: physical%u magic%u guards%u/%u/%u critical%u\n",physical,magic,guards[0],guards[1],guards[2],critical);
}
void autoRaidConsent() {
 Pair duel;duel.open();CHECK(!duel.a.challenge(0,Mode::Auto,7,42,duel.now,true));
 Pair manual;startRaid(manual);CHECK(!manual.a.view().coopAuto&&!manual.b.view().coopAuto);
 const auto before=manual.a.view().raid;manual.tick(kAutoPaceMs,0,true);CHECK(sameRaid(before,manual.a.view().raid));
 CHECK(manual.a.choose(Choice::Physical,manual.now));CHECK(manual.b.choose(Choice::Brace,manual.now));manual.pump();
 CHECK(manual.a.view().raid.sequence==1&&sameRaid(manual.a.view().raid,manual.b.view().raid));
 Pair p;p.open(strongHost,strongGuest,0,0);
 CHECK(p.a.challenge(0,Mode::Coop,908,raidSeed(68,1,0),p.now,true));
 Datagram offer;CHECK(p.a.pop(offer)&&offer.bytes[5]==2&&offer.length==88);
 auto invalid=offer;invalid.bytes[34+49]=2;reseal(invalid);
 CHECK(!p.b.receive(ma,invalid.bytes,invalid.length,p.now)&&p.b.view().stage==Stage::Discovering);
 CHECK(p.b.receive(ma,offer.bytes,offer.length,p.now)&&p.b.view().coopAuto&&p.b.view().stage==Stage::Incoming);
 invalid=offer;invalid.bytes[34+49]=0;reseal(invalid);
 CHECK(!p.b.receive(ma,invalid.bytes,invalid.length,p.now)&&p.b.view().coopAuto);
 p.tick(2500,0,true);CHECK(p.b.view().stage==Stage::Incoming&&p.a.view().raid.sequence==0);
 CHECK(p.b.accept(p.now));Datagram acceptance;CHECK(p.b.pop(acceptance)&&acceptance.bytes[5]==3);
 invalid=acceptance;invalid.bytes[34+49]=0;reseal(invalid);
 CHECK(!p.a.receive(mb,invalid.bytes,invalid.length,p.now)&&p.a.view().stage==Stage::Outgoing);
 CHECK(p.a.receive(mb,acceptance.bytes,acceptance.length,p.now));p.pump();
 CHECK(p.a.view().coopAuto&&p.b.view().coopAuto&&sameRaid(p.a.view().raid,p.b.view().raid));
 CHECK(!p.a.choose(Choice::Physical,p.now)&&!p.b.choose(Choice::Brace,p.now));
 // A raw manual choice packet cannot bypass the agreed Auto policy.
 auto injected=acceptance;injected.bytes[5]=5;injected.length=39;injected.bytes[6]=39;injected.bytes[7]=0;
 injected.bytes[34]=static_cast<std::uint8_t>(Choice::Brace);reseal(injected);
 CHECK(!p.a.receive(mb,injected.bytes,injected.length,p.now)&&p.a.view().raid.sequence==0);
 // Explicit decline cancels the negotiated offer without resolving a turn.
 Pair declined;declined.open(strongHost,strongGuest,0,0);
 CHECK(declined.a.challenge(0,Mode::Coop,910,raidSeed(68,1,0),declined.now,true));declined.pump();
 declined.b.cancel(declined.now);declined.pump();
 CHECK(declined.a.view().stage==Stage::Cancelled&&declined.b.view().stage==Stage::Cancelled&&!validRaid(declined.a.view().raid));
 // Crossed offers freeze the same winner's mode and require fresh recipient consent.
 for(bool lowerMacAuto:{false,true}){
  Pair crossed;crossed.open(strongHost,strongGuest,0,0);
  CHECK(crossed.a.challenge(0,Mode::Coop,911,raidSeed(68,1,0),crossed.now,lowerMacAuto));
  CHECK(crossed.b.challenge(0,Mode::Coop,912,raidSeed(68,1,0),crossed.now,!lowerMacAuto));crossed.pump(0,true);
  CHECK(crossed.a.view().host&&!crossed.b.view().host&&crossed.b.view().stage==Stage::Incoming);
  CHECK(crossed.a.view().coopAuto==lowerMacAuto&&crossed.b.view().coopAuto==lowerMacAuto);
  CHECK(crossed.b.accept(crossed.now));crossed.pump();CHECK(sameRaid(crossed.a.view().raid,crossed.b.view().raid));
 }
}
void autoRaidPacingAndVerification() {
 for(unsigned scenario=0;scenario<6;++scenario){
  Pair p;startRaid(p,scenario,true);const auto initial=p.a.view().raid;
  p.tick(kAutoPaceMs-1,0,true);CHECK(sameRaid(initial,p.a.view().raid));
  p.tick(1,1u<<9,true);CHECK(p.a.view().raid.sequence==1&&p.b.view().raid.sequence==0&&!p.a.view().peerAcknowledged);
  const auto committed=p.a.view().raid;p.tick(kAutoPaceMs,1u<<9,true);CHECK(sameRaid(committed,p.a.view().raid));
  p.tick(500,0,true);CHECK(sameRaid(committed,p.b.view().raid)&&p.a.view().peerAcknowledged);
  if(p.a.view().stage==Stage::Playing){
   p.tick(kAutoPaceMs-1,0,true);CHECK(sameRaid(committed,p.a.view().raid));p.tick(1,0,true);
   CHECK(p.a.view().raid.sequence==committed.sequence+1);
  }
  while(p.a.view().stage==Stage::Playing&&p.a.view().raid.sequence<72){
   const auto q=p.a.view().raid.sequence;p.tick(kAutoPaceMs,0,true);CHECK(p.a.view().raid.sequence==q+1);
   CHECK(p.a.view().coopAuto&&p.b.view().coopAuto&&sameRaid(p.a.view().raid,p.b.view().raid));
  }
  CHECK(p.a.view().stage==Stage::Finished&&p.b.view().stage==Stage::Finished&&p.a.view().raid.status==RaidStatus::Cleared);
  CHECK(p.a.view().raid.tier==digivice::expeditions::tierCount(scenario));
 }
 Pair p;startRaid(p,0,true);const auto before=p.b.view().raid;
 p.now+=kAutoPaceMs;p.a.tick(p.now);Datagram frame;CHECK(p.a.pop(frame)&&frame.bytes[5]==9);
 // Manual results can be internally valid but are not the agreed random Auto turn.
 auto manual=before;CHECK(resolveRaid(manual,manual.sequence,Choice::Physical,Choice::Brace));
 auto forged=frame;CHECK(encodeRaid(manual,forged.bytes+34,kRaidBytes));put(forged.bytes+26,manual.sequence);reseal(forged);
 CHECK(!p.b.receive(ma,forged.bytes,forged.length,p.now)&&sameRaid(before,p.b.view().raid));
 // A host cannot skip the acknowledgement barrier with a future tier result.
 auto future=before;CHECK(resolveAutoRaid(future,future.sequence)&&resolveAutoRaid(future,future.sequence));
 forged=frame;CHECK(encodeRaid(future,forged.bytes+34,kRaidBytes));put(forged.bytes+26,future.sequence);reseal(forged);
 CHECK(!p.b.receive(ma,forged.bytes,forged.length,p.now)&&sameRaid(before,p.b.view().raid));
 CHECK(p.b.receive(ma,frame.bytes,frame.length,p.now));p.pump(1u<<6);const auto committed=p.a.view().raid;
 p.now+=kAutoPaceMs;p.a.tick(p.now);CHECK(sameRaid(committed,p.a.view().raid));
 Datagram retransmit;CHECK(p.a.pop(retransmit)&&retransmit.bytes[5]==9);
 CHECK(p.b.receive(ma,retransmit.bytes,retransmit.length,p.now));p.pump();
 p.tick(kAutoPaceMs-1);CHECK(sameRaid(committed,p.a.view().raid));p.tick(1);CHECK(p.a.view().raid.sequence==committed.sequence+1);
}
void autoRaidReconnectAndRewards() {
 // Challenge, acceptance, raid result, ACK and sync loss recover without
 // changing the negotiated mode or resolving an extra exchange.
 for(unsigned lost:{2u,3u,9u,6u,8u}){
  Pair fault;fault.open(strongHost,strongGuest,0,0);
  CHECK(fault.a.challenge(0,Mode::Coop,1000+lost,raidSeed(68,1,0),fault.now,true));fault.pump(1u<<lost,true);fault.tick(500,0,true);
  CHECK(fault.b.view().stage==Stage::Incoming&&fault.b.view().coopAuto);CHECK(fault.b.accept(fault.now));fault.pump(1u<<lost,true);fault.tick(500,0,true);
  CHECK(fault.a.view().stage==Stage::Playing&&fault.b.view().stage==Stage::Playing&&sameRaid(fault.a.view().raid,fault.b.view().raid));
  fault.tick(kAutoPaceMs,1u<<lost,true);fault.tick(500,0,true);
  CHECK(fault.a.view().raid.sequence==1&&sameRaid(fault.a.view().raid,fault.b.view().raid));
  CHECK(fault.a.view().coopAuto&&fault.b.view().coopAuto);
 }
 // Either device may host; identities remain frozen across all three tiers.
 for(unsigned initiator=0;initiator<2;++initiator){
  Pair side;side.open(strongHost,strongGuest,0,0);auto& host=initiator?side.b:side.a;auto& guest=initiator?side.a:side.b;
  CHECK(host.challenge(0,Mode::Coop,1100+initiator,raidSeed(68,1,0),side.now,true));side.pump();CHECK(guest.accept(side.now));side.pump();
  const auto offeredHost=host.view().offered[0],offeredGuest=host.view().offered[1];
  while(host.view().stage==Stage::Playing&&host.view().raid.sequence<72){
   const auto sequence=host.view().raid.sequence;side.tick(kAutoPaceMs,0,true);
   CHECK(host.view().raid.sequence==sequence+1&&sameRaid(host.view().raid,guest.view().raid));
   CHECK(sameFighter(host.view().raid.players[0],offeredHost)&&sameFighter(host.view().raid.players[1],offeredGuest));
  }
  CHECK(host.view().raid.status==RaidStatus::Cleared&&host.view().raid.tier==3);
 }
 Pair p;startRaid(p,0,true);p.tick(kAutoPaceMs,0,true);const auto first=p.a.view().raid;
 p.tick(kReconnectMs+1,0xffffffffu);CHECK(p.a.view().stage==Stage::Reconnecting&&p.b.view().stage==Stage::Reconnecting&&sameRaid(first,p.a.view().raid));
 p.tick(500,0,true);CHECK(p.a.view().stage==Stage::Playing&&p.b.view().stage==Stage::Playing&&p.a.view().coopAuto&&p.b.view().coopAuto);
 // As in existing friendly Auto, a frame already acknowledged before the
 // outage has already completed its visible hold and may advance once.
 CHECK(p.a.view().raid.sequence<=first.sequence+1&&sameRaid(p.a.view().raid,p.b.view().raid));
 const auto resumed=p.a.view().raid;
 p.tick(kAutoPaceMs-1);CHECK(sameRaid(resumed,p.a.view().raid));p.tick(1);CHECK(p.a.view().raid.sequence==resumed.sequence+1);
 const auto second=p.a.view().raid;p.a.cancel(p.now);p.pump(0,true);
 CHECK(p.a.view().stage==Stage::Cancelled&&p.b.view().stage==Stage::Cancelled&&!claimableRaid(p.a.view(),0)&&sameRaid(second,p.a.view().raid));
 Pair reboot;startRaid(reboot,0,true);reboot.tick(kAutoPaceMs);const auto committed=reboot.a.view().raid;
 reboot.b.close();CHECK(reboot.b.open(mb,strongGuest,reboot.now+1,201,0));++reboot.now;reboot.pump();reboot.tick(500);
 CHECK(reboot.b.view().stage==Stage::Discovering&&!reboot.b.view().coopAuto&&!claimableRaid(reboot.b.view(),0));
 reboot.tick(kSessionTimeoutMs);CHECK(reboot.a.view().stage==Stage::TimedOut&&sameRaid(committed,reboot.a.view().raid));
 // An unacknowledged result needs its full hold after it actually arrives.
 Pair unacked;startRaid(unacked,0,true);unacked.now+=kAutoPaceMs;unacked.a.tick(unacked.now);
 Datagram held;CHECK(unacked.a.pop(held)&&held.bytes[5]==9);const auto pending=unacked.a.view().raid;
 unacked.tick(kReconnectMs+1,0xffffffffu);CHECK(unacked.a.view().stage==Stage::Reconnecting);
 unacked.tick(500,0,true);CHECK(sameRaid(pending,unacked.a.view().raid)&&sameRaid(pending,unacked.b.view().raid));
 unacked.tick(kAutoPaceMs-1);CHECK(sameRaid(pending,unacked.a.view().raid));unacked.tick(1);CHECK(unacked.a.view().raid.sequence==pending.sequence+1);
 const auto newer=unacked.b.view().raid;CHECK(unacked.b.receive(ma,held.bytes,held.length,unacked.now));unacked.pump(0,true);
 CHECK(sameRaid(newer,unacked.b.view().raid));
 // The same complete lost-final-ACK, checkpoint failure, duplicate award and
 // reboot rejection test runs against Auto as well as Manual.
 raidFinalClaims(true);
}


void survivorProtocol() {
 for(bool automatic:{false,true})for(unsigned dead:{0u,1u}) {
  Pair p;
  const Fighter host{17,18,automatic?10u:dead?40u:10u,2,2},guest{23,automatic?18u:11u,automatic?10u:dead?10u:40u,2,2};
  const auto seed=automatic?(dead?raidSeed(18,8,0):raidSeed(11,10,0)):
      raidSeed(25,dead?26:17,0);
  p.open(host,guest,0,0);CHECK(p.a.challenge(0,Mode::Coop,8000+dead,seed,p.now,automatic));p.pump();
  CHECK(p.b.accept(p.now));p.pump();
  for(unsigned n=0;n<72&&p.a.view().raid.hp[dead]&&p.a.view().stage==Stage::Playing;++n)raidTurn(p,0,true);
  const bool naturalKo=p.a.view().stage==Stage::Playing&&!p.a.view().raid.hp[dead]&&p.a.view().raid.hp[1u-dead];
  CHECK(naturalKo);if(!naturalKo)continue; // Never parse later packets after fixture failure.
  CHECK(sameRaid(p.a.view().raid,p.b.view().raid));
  const auto before=p.a.view().raid;const unsigned survivor=1u-dead;
  CHECK(raidAttacker(before)==survivor&&raidDefender(before)==kNoRaidActor);
  auto& knockedOut=dead?p.b:p.a;auto& living=dead?p.a:p.b;
  for(auto input:{Choice::None,Choice::Physical,Choice::Heavy,Choice::Magic,Choice::Brace,Choice::Ward,Choice::Counter})
   CHECK(!knockedOut.choose(input,p.now));
  if(!automatic){
   p.tick(kAutoPaceMs,0,true);CHECK(sameRaid(before,p.a.view().raid)); // No fabricated turn for the dead actor.
   CHECK(living.choose(Choice::Physical,p.now));p.pump(1u<<9,true);
  }else p.tick(kAutoPaceMs,1u<<9,true);
  const auto committed=p.a.view().raid;
  CHECK(committed.sequence==before.sequence+1&&committed.lastDefense==Choice::None);
  CHECK(raidLastAttacker(committed)==survivor&&committed.energy[dead]==before.energy[dead]);
  CHECK(!p.a.view().peerAcknowledged&&sameRaid(before,p.b.view().raid));
  // Retrieve a retry, reject a structurally valid but divergent result, then
  // acknowledge the exact result twice. KO guests have no pending choice.
  p.now+=kRetryMs;p.a.tick(p.now);Datagram frame{};const bool hasFrame=p.a.pop(frame)&&frame.bytes[5]==9;
  CHECK(hasFrame);if(!hasFrame)continue;
  auto forged=frame;auto divergent=committed;divergent.rng^=0x100u;
  CHECK(encodeRaid(divergent,forged.bytes+34,kRaidBytes));reseal(forged);
  CHECK(!p.b.receive(ma,forged.bytes,forged.length,p.now)&&sameRaid(before,p.b.view().raid));
  CHECK(p.b.receive(ma,frame.bytes,frame.length,p.now));CHECK(p.b.receive(ma,frame.bytes,frame.length,p.now));p.pump(1u<<6,true);
  CHECK(sameRaid(committed,p.b.view().raid)&&!p.a.view().peerAcknowledged);
  p.tick(kAutoPaceMs,1u<<6,true);CHECK(sameRaid(committed,p.a.view().raid));
  p.tick(kRetryMs,0,true);CHECK(sameRaid(committed,p.a.view().raid)&&sameRaid(committed,p.b.view().raid));
  CHECK(p.a.view().peerAcknowledged);
  unsigned survivorTurns=1;
  while(p.a.view().stage==Stage::Playing&&p.a.view().raid.sequence<72){
   const auto prior=p.a.view().raid;raidTurn(p,0,true);++survivorTurns;
   CHECK(sameRaid(p.a.view().raid,p.b.view().raid)&&p.a.view().raid.lastDefense==Choice::None);
   CHECK(raidLastAttacker(p.a.view().raid)==survivor&&p.a.view().raid.energy[dead]==prior.energy[dead]);
  }
  CHECK(p.a.view().stage==Stage::Finished&&p.b.view().stage==Stage::Finished);
  if(!automatic)CHECK(survivorTurns>=10&&p.a.view().raid.status==RaidStatus::Cleared);
  CHECK(!knockedOut.choose(Choice::None,p.now));
  std::printf("%s survivor%u: natural KO at%u, %u continuing exchanges, terminal%u\n",automatic?"Auto":"Manual",survivor,before.sequence,survivorTurns,static_cast<unsigned>(p.a.view().raid.status));
 }
}
}
int main(){core();consentAndLoss();recoverAndTimeout();simultaneous();wireGuards();boundedQueuesAndReordering();reviewedRegressions();autoPacing();autoModeConsent();autoRandomAlternation();autoReconnectAndReboot();careBonusHandshake();previousRulesHandshake();raidJoin();raidTiersAndEncoding();raidLossReorderAndReconnect();raidFinalClaims();raidEnergyBudget();raidWireCompatibility();autoRaidCore();autoRaidConsent();autoRaidPacingAndVerification();autoRaidReconnectAndRewards();survivorProtocol();std::printf("%u nearby checks, %u failures; Fighter%zuB / Match%zuB / Protocol%zuB / max packet%zuB\n",checks,failures,sizeof(Fighter),sizeof(Match),sizeof(Protocol),34+kMatchBytes+4);return failures?1:0;}
