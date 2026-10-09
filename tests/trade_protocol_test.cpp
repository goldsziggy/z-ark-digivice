#include "trade_protocol.hpp"
#include "nearby_protocol.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace digivice;
using namespace digivice::tradewire;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)
constexpr Identity ma{{2,1,2,3,4,5}},mb{{2,1,2,3,4,6}},mc{{2,1,2,3,4,7}};
CreatureMember offer(unsigned starter){auto state=newDevice(12345);CHECK(apply(state,Action::Hatch,starter)==Error::None);return *activeMember(state);}
std::uint32_t crc(const std::uint8_t* p,std::size_t n){std::uint32_t c=~0u;for(std::size_t i=0;i<n;++i){c^=p[i];for(unsigned b=0;b<8;++b)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
void put(std::uint8_t* p,std::uint32_t n){for(unsigned i=0;i<4;++i)p[i]=static_cast<std::uint8_t>(n>>(8*i));}
void reseal(Datagram& d){put(d.bytes+d.length-4,crc(d.bytes,d.length-4));}
Datagram packet(Kind kind,const trade::Transcript& t,unsigned actor,Durable durable=Durable::None){
 tradewire::Message m;m.kind=kind;m.sender=t.peers[actor];m.transcript=t;m.durable=durable;
 Datagram d;std::size_t length=0;CHECK(encode(m,d.bytes,sizeof(d.bytes),length));d.length=static_cast<std::uint16_t>(length);d.destination=t.peers[1-actor];return d;
}
struct Pair {
 Protocol a,b;std::uint64_t now=100;unsigned delivered=0,largest=0;
 void open(){CHECK(a.open(ma,offer(1),1,100,now));CHECK(b.open(mb,offer(2),1,200,now));pump();CHECK(a.view().peerCount==1&&b.view().peerCount==1);}
 void pump(unsigned drop=0,bool duplicate=false){
  for(unsigned loop=0;loop<100;++loop){bool more=false;Datagram d;
   for(unsigned actor=0;actor<2;++actor){auto& from=actor?b:a;auto& to=actor?a:b;const auto& id=actor?mb:ma;
    while(from.pop(d)){more=true;CHECK(d.length<=kMaxPacket);if(d.length>largest)largest=d.length;
     if(drop&(1u<<d.bytes[5]))continue;
     (void)to.receive(id,d.bytes,d.length,now);++delivered;if(duplicate)(void)to.receive(id,d.bytes,d.length,now);
    }
   }
   if(!more)return;
  }
  CHECK(false); // No receipt loop may consume unbounded radio/main-task work.
 }
 void tick(unsigned elapsed,unsigned drop=0,bool duplicate=false){now+=elapsed;a.tick(now);b.tick(now);pump(drop,duplicate);}
 void review(){open();CHECK(a.invite(mb,0x1122334455667788ull,now));pump();CHECK(a.view().stage==Stage::Reviewing&&b.view().stage==Stage::Reviewing);CHECK(a.view().peerReviewed&&b.view().peerReviewed);CHECK(trade::sameTranscript(a.view().transcript,b.view().transcript));}
 void confirm(){CHECK(a.confirm(a.view().transcript,now));CHECK(b.confirm(b.view().transcript,now));CHECK(a.view().requested==Request::Prepare&&b.view().requested==Request::Prepare);}
 void prepared(){confirm();CHECK(a.setDurable(Durable::Prepared,now));CHECK(b.setDurable(Durable::Prepared,now));pump();CHECK(a.view().requested==Request::Commit&&b.view().requested==Request::None);}
 void complete(){CHECK(a.setDurable(Durable::Committed,now));CHECK(a.view().requested==Request::Apply);pump();CHECK(b.view().requested==Request::Commit);CHECK(b.setDurable(Durable::Committed,now));CHECK(b.view().requested==Request::Apply);CHECK(a.setDurable(Durable::Applied,now));CHECK(b.setDurable(Durable::Applied,now));pump();CHECK(a.view().durable==Durable::Applied&&b.view().durable==Durable::Applied&&a.peerApplied()&&b.peerApplied());}
};
void codec(){
 Pair p;p.review();const auto t=p.a.view().transcript;
 for(unsigned raw=1;raw<=static_cast<unsigned>(Kind::Cancel);++raw){
  tradewire::Message m;m.kind=static_cast<Kind>(raw);m.sender=(m.kind==Kind::Offer||m.kind==Kind::Cancel)?mb:ma;m.transcript=t;
  m.advertisement={100,1,offer(1),true,kRulesVersion,kCatalogVersion,kTradeCapability};
  if(m.kind==Kind::Prepared)m.durable=Durable::Prepared;
  if(m.kind==Kind::Commit)m.durable=Durable::Committed;
  if(m.kind==Kind::Applied)m.durable=Durable::Applied;
  if(m.kind==Kind::Abort)m.durable=Durable::Aborted;
  Datagram d;std::size_t length=999;CHECK(encode(m,d.bytes,sizeof(d.bytes),length));d.length=static_cast<std::uint16_t>(length);CHECK(length==(m.kind==Kind::Hello?84:172));
  tradewire::Message read;CHECK(decode(m.sender,d.bytes,length,read));CHECK(read.kind==m.kind&&read.durable==m.durable);
  if(m.kind!=Kind::Hello)CHECK(trade::sameTranscript(read.transcript,t));else CHECK(trade::sameMember(read.advertisement.member,m.advertisement.member));
  for(unsigned byte=0;byte<length;++byte){auto bad=d;bad.bytes[byte]^=1;const auto before=read;CHECK(!decode(m.sender,bad.bytes,bad.length,read));CHECK(read.kind==before.kind&&read.durable==before.durable&&trade::sameIdentity(read.sender,before.sender));}
  CHECK(!decode(mc,d.bytes,length,read));CHECK(!decode(m.sender,d.bytes,length-1,read));CHECK(!decode(m.sender,d.bytes,length+1,read));
  for(unsigned offset:{4u,15u}){auto bad=d;bad.bytes[offset]=42;reseal(bad);CHECK(!decode(m.sender,bad.bytes,bad.length,read));}
  auto battle=d;std::memcpy(battle.bytes,"DGN1",4);reseal(battle);CHECK(!decode(m.sender,battle.bytes,battle.length,read));
  nearby::Protocol legacy;nearby::Mac local;std::memcpy(local.bytes,mc.bytes,6);CHECK(legacy.open(local,{1,11,1},1,42));nearby::Mac source;std::memcpy(source.bytes,m.sender.bytes,6);CHECK(!legacy.receive(source,d.bytes,d.length,2));CHECK(legacy.view().stage==nearby::Stage::Discovering);
 }
 auto wrong=packet(Kind::Commit,t,0,Durable::Committed);wrong.bytes[14]=static_cast<unsigned>(Durable::None);reseal(wrong);tradewire::Message out;CHECK(!decode(ma,wrong.bytes,wrong.length,out));
 auto badTranscript=t;badTranscript.nonces[0]=0;tradewire::Message m;m.kind=Kind::Status;m.sender=ma;m.transcript=badTranscript;std::uint8_t unchanged[kMaxPacket];std::memset(unchanged,0xa5,sizeof(unchanged));std::size_t length=77;CHECK(!encode(m,unchanged,sizeof(unchanged),length));CHECK(length==77&&unchanged[0]==0xa5);
 CHECK(sizeof(Protocol)<4096);std::printf("DGT1 codec: Hello84B, transcript packets172B, Protocol%zuB, no legacy battle mutation\n",sizeof(Protocol));
}
void legacyConsentEpoch(){
 for(unsigned epoch:{13u,14u}){
 Pair source;source.review();auto consent=source.a.view().transcript;consent.rules=epoch;
 std::uint8_t original[trade::kTranscriptBytes],encoded[trade::kTranscriptBytes];
 CHECK(trade::encodeTranscript(consent,original,sizeof(original)));
 CHECK(original[8]==epoch);trade::Transcript restored;
 CHECK(trade::decodeTranscript(original,sizeof(original),restored)&&restored.rules==epoch);
 CHECK(trade::encodeTranscript(restored,encoded,sizeof(encoded))&&!std::memcmp(original,encoded,sizeof(original)));
 CHECK(trade::fingerprint(consent)==trade::fingerprint(restored));
 auto current=consent;current.rules=kRulesVersion;CHECK(!trade::sameTranscript(consent,current));
 auto unknown=consent;unknown.rules=kRulesVersion+1;CHECK(!trade::valid(unknown));
 // Both updated peers complete an installed rules13/14 promise using the exact
 // old canonical bytes, despite advertising rules15 for new discovery.
 Pair recovering;recovering.now=0;
 CHECK(recovering.a.restore(ma,consent,Durable::Prepared,0));
 CHECK(recovering.b.restore(mb,consent,Durable::Prepared,0));
 recovering.pump();CHECK(recovering.a.view().requested==Request::Commit);recovering.complete();
 CHECK(trade::sameTranscript(recovering.a.view().transcript,consent));
 CHECK(trade::sameTranscript(recovering.b.view().transcript,consent));
 // Recognizing an old epoch for recovery never permits new rules13/14 offers.
 Pair fresh;fresh.open();CHECK(fresh.a.invite(mb,123456,fresh.now));
 auto obsolete=fresh.a.view().transcript;obsolete.rules=epoch;
 const auto invite=packet(Kind::Invite,obsolete,0);
 CHECK(!fresh.b.receive(ma,invite.bytes,invite.length,fresh.now));
 CHECK(fresh.b.view().stage==Stage::Discovering&&fresh.b.view().requested==Request::None);
 }
}
void durableOrdering(){
 Pair p;p.review();CHECK(!p.a.setDurable(Durable::Prepared,p.now));p.confirm();
 Datagram d;while(p.a.pop(d))CHECK(d.bytes[5]!=static_cast<unsigned>(Kind::Prepared));
 CHECK(p.a.view().durable==Durable::None&&!p.a.view().localConfirmed);
 CHECK(!p.a.setDurable(Durable::Applied,p.now));CHECK(p.a.setDurable(Durable::Prepared,p.now));p.pump();CHECK(p.a.view().requested==Request::None&&!p.b.view().localConfirmed&&p.b.view().peerConfirmed);
 CHECK(!p.a.setDurable(Durable::Committed,p.now));CHECK(!p.a.offer(offer(3),1,p.now));CHECK(!p.a.open(ma,offer(1),1,101,p.now));
 CHECK(p.b.setDurable(Durable::Prepared,p.now));p.pump();CHECK(p.a.view().requested==Request::Commit);p.complete();
 CHECK(!p.a.setDurable(Durable::Applied,p.now)&&!p.b.setDurable(Durable::Committed,p.now));
 const auto t=p.a.view().transcript;auto old=packet(Kind::Prepared,t,1,Durable::Prepared);CHECK(p.a.receive(mb,old.bytes,old.length,p.now));CHECK(p.a.peerApplied());
 auto changed=t;changed.offers[1].mood-=1;auto spoof=packet(Kind::Prepared,changed,1,Durable::Prepared);CHECK(!p.a.receive(mb,spoof.bytes,spoof.length,p.now));CHECK(p.a.view().durable==Durable::Applied);
 Pair early;early.review();auto impossible=packet(Kind::Applied,early.a.view().transcript,1,Durable::Applied);CHECK(!early.a.receive(mb,impossible.bytes,impossible.length,early.now));CHECK(early.a.view().peerDurable==Durable::None&&early.a.view().requested==Request::None);
}
void retries(){
 for(const auto lost:{Kind::Invite,Kind::ReviewAck,Kind::Review,Kind::Offer}){
  Pair p;p.open();CHECK(p.a.invite(mb,17,p.now));p.pump(1u<<static_cast<unsigned>(lost));
  for(unsigned n=0;n<8&&(!p.a.view().peerReviewed||!p.b.view().peerReviewed);++n)p.tick(500);
  CHECK(p.a.view().peerReviewed&&p.b.view().peerReviewed);
  auto changed=offer(3);CHECK(p.b.offer(changed,2,p.now));p.pump(1u<<static_cast<unsigned>(lost),true);for(unsigned n=0;n<8&&(p.b.view().offerPending||!p.a.view().peerReviewed);++n)p.tick(500,0,true);
  CHECK(trade::sameTranscript(p.a.view().transcript,p.b.view().transcript));CHECK(trade::sameMember(p.a.view().transcript.offers[1],changed));
 }
 for(const auto lost:{Kind::Prepared,Kind::Commit,Kind::Applied}){
  Pair p;p.review();p.confirm();CHECK(p.a.setDurable(Durable::Prepared,p.now));CHECK(p.b.setDurable(Durable::Prepared,p.now));p.pump(1u<<static_cast<unsigned>(lost),true);p.tick(500,0,true);CHECK(p.a.view().requested==Request::Commit);
  CHECK(p.a.setDurable(Durable::Committed,p.now));p.pump(1u<<static_cast<unsigned>(lost),true);p.tick(500,0,true);CHECK(p.b.view().requested==Request::Commit);
  CHECK(p.b.setDurable(Durable::Committed,p.now));CHECK(p.a.setDurable(Durable::Applied,p.now));CHECK(p.b.setDurable(Durable::Applied,p.now));p.pump(1u<<static_cast<unsigned>(lost),true);p.tick(500,0,true);
  CHECK(p.a.peerApplied()&&p.b.peerApplied());CHECK(p.largest==172);
 }
}
void offersAndCrossing(){
 // One UI gesture selects a member then invites. adopt() discards queued
 // discovery updates, so invite must re-announce its exact selected offer.
 for(unsigned actor=0;actor<2;++actor){Pair picked;picked.open();auto& initiator=actor?picked.b:picked.a;CHECK(initiator.offer(offer(3),2,picked.now));CHECK(initiator.invite(actor?ma:mb,888,picked.now));picked.pump();CHECK(picked.a.view().stage==Stage::Reviewing&&picked.b.view().stage==Stage::Reviewing);CHECK(trade::sameTranscript(picked.a.view().transcript,picked.b.view().transcript));CHECK(trade::sameMember(picked.a.view().transcript.offers[actor],offer(3)));}
 Pair p;p.review();const auto old=p.a.view().transcript;CHECK(p.b.offer(offer(3),2,p.now));CHECK(p.b.view().offerPending);CHECK(!p.b.confirm(old,p.now));p.pump();CHECK(p.a.view().transcript.revision==2&&p.b.view().transcript.revision==2);
 CHECK(!p.a.confirm(old,p.now)&&!p.b.confirm(old,p.now));CHECK(!p.a.view().localConfirmed&&!p.a.view().peerConfirmed);p.prepared();p.complete();
 // Guest's unacknowledged edit crosses coordinator's durable confirmation.
 Pair guestEdit;guestEdit.review();const auto first=guestEdit.a.view().transcript;CHECK(guestEdit.b.offer(offer(3),2,guestEdit.now));CHECK(guestEdit.a.confirm(first,guestEdit.now));CHECK(guestEdit.a.setDurable(Durable::Prepared,guestEdit.now));guestEdit.pump();CHECK(trade::sameTranscript(first,guestEdit.b.view().transcript));CHECK(guestEdit.b.view().peerConfirmed&&!guestEdit.b.view().offerPending);CHECK(!guestEdit.b.offer(offer(4),2,guestEdit.now));
 CHECK(guestEdit.b.cancel(first,guestEdit.now));guestEdit.pump();CHECK(guestEdit.a.view().requested==Request::Abort);CHECK(guestEdit.a.setDurable(Durable::Aborted,guestEdit.now));guestEdit.pump();CHECK(guestEdit.b.view().stage==Stage::Aborted);
 // Coordinator edit crosses guest Prepared on the previous acknowledged pair.
 Pair hostEdit;hostEdit.review();const auto frozen=hostEdit.a.view().transcript;CHECK(hostEdit.a.offer(offer(4),2,hostEdit.now));CHECK(hostEdit.b.confirm(frozen,hostEdit.now));CHECK(hostEdit.b.setDurable(Durable::Prepared,hostEdit.now));hostEdit.pump();CHECK(hostEdit.a.view().recoveryOffer&&trade::sameTranscript(hostEdit.a.view().transcript,frozen));CHECK(hostEdit.a.view().requested==Request::None);
 CHECK(hostEdit.a.cancel(frozen,hostEdit.now));CHECK(hostEdit.a.view().requested==Request::Abort);CHECK(hostEdit.a.setDurable(Durable::Aborted,hostEdit.now));hostEdit.pump();CHECK(hostEdit.b.view().requested==Request::Abort);CHECK(hostEdit.b.setDurable(Durable::Aborted,hostEdit.now));hostEdit.pump();CHECK(hostEdit.a.view().peerDurable==Durable::Aborted);
}
void cancellation(){
 Pair p;p.review();const auto t=p.a.view().transcript;CHECK(p.a.cancel(t,p.now));CHECK(p.a.view().requested==Request::Abort);CHECK(!p.a.confirm(t,p.now));CHECK(p.a.setDurable(Durable::Aborted,p.now));p.pump(1u<<static_cast<unsigned>(Kind::Abort));p.tick(500);CHECK(p.b.view().stage==Stage::Aborted);
 Pair both;both.review();both.confirm();CHECK(both.b.setDurable(Durable::Prepared,both.now));both.pump();CHECK(both.b.cancel(both.b.view().transcript,both.now));both.pump();CHECK(both.a.view().requested==Request::Prepare);CHECK(both.a.setDurable(Durable::Prepared,both.now));CHECK(both.a.view().requested==Request::Abort);CHECK(both.a.setDurable(Durable::Aborted,both.now));both.pump();CHECK(both.b.view().requested==Request::Abort);CHECK(both.b.setDurable(Durable::Aborted,both.now));both.pump();
 // Cancelling a reviewed pair cannot be undone by a crossing coordinator edit.
 Pair crossing;crossing.review();const auto cancelled=crossing.b.view().transcript;CHECK(crossing.a.offer(offer(4),2,crossing.now));const auto edited=crossing.a.view().transcript;CHECK(crossing.b.cancel(cancelled,crossing.now));crossing.pump();CHECK(trade::sameTranscript(crossing.a.view().transcript,cancelled));CHECK(crossing.a.view().requested==Request::Abort);CHECK(crossing.a.setDurable(Durable::Aborted,crossing.now));crossing.pump();CHECK(crossing.b.view().stage==Stage::Aborted);auto delayed=packet(Kind::Review,edited,0);CHECK(!crossing.b.receive(ma,delayed.bytes,delayed.length,crossing.now));CHECK(crossing.b.view().stage==Stage::Aborted&&!crossing.b.confirm(edited,crossing.now));
 Pair decision;decision.review();decision.prepared();CHECK(decision.b.cancel(decision.b.view().transcript,decision.now));decision.pump();CHECK(decision.a.view().requested==Request::Commit);CHECK(!decision.a.cancel(decision.a.view().transcript,decision.now));decision.complete();CHECK(!decision.b.cancel(decision.b.view().transcript,decision.now));
}
void recovery(){
 Pair p;p.review();p.prepared();const auto t=p.a.view().transcript;
 p.a.close();p.b.close();CHECK(p.a.restore(ma,t,Durable::Prepared,0));CHECK(p.b.restore(mb,t,Durable::Prepared,0));p.now=0;p.pump();CHECK(p.a.view().requested==Request::Commit);p.complete();
 // A durable commit survives before either the care write or its reply.
 Protocol committed,participant;CHECK(committed.restore(ma,t,Durable::Committed,50));CHECK(committed.view().requested==Request::Apply);CHECK(participant.restore(mb,t,Durable::Prepared,50));Datagram d;while(committed.pop(d))(void)participant.receive(ma,d.bytes,d.length,50);CHECK(participant.view().requested==Request::Commit);
 // Unconfirmed coordinator reboot: participant's old frozen nonce is retained;
 // fresh discovery nonce never relabels the prepared transaction.
 Pair interrupted;interrupted.review();const auto old=interrupted.a.view().transcript;CHECK(interrupted.b.confirm(old,interrupted.now));CHECK(interrupted.b.setDurable(Durable::Prepared,interrupted.now));interrupted.a.close();CHECK(interrupted.a.open(ma,offer(4),20,999,interrupted.now));interrupted.b.close();CHECK(interrupted.b.restore(mb,old,Durable::Prepared,interrupted.now));interrupted.pump();CHECK(interrupted.a.view().stage==Stage::Recovering&&interrupted.a.view().recoveryOffer);CHECK(trade::sameTranscript(old,interrupted.a.view().transcript));CHECK(!interrupted.a.view().localConfirmed&&interrupted.a.view().requested==Request::None);CHECK(interrupted.a.cancel(old,interrupted.now));CHECK(interrupted.a.setDurable(Durable::Aborted,interrupted.now));interrupted.pump();CHECK(interrupted.b.view().requested==Request::Abort);
 // Neither elapsed time nor monotonic clock reset releases uncertain escrow.
 Protocol stranded;CHECK(stranded.restore(mb,t,Durable::Prepared,100));stranded.tick(1000000000);CHECK(stranded.view().durable==Durable::Prepared&&stranded.view().requested==Request::None&&!stranded.view().connected);stranded.tick(1);CHECK(stranded.view().durable==Durable::Prepared);
 // Applied is final locally; exact old Prepared gets the same durable result.
 Protocol applied;CHECK(applied.restore(ma,t,Durable::Applied,1));while(applied.pop(d)){};auto ready=packet(Kind::Prepared,t,1,Durable::Prepared);CHECK(applied.receive(mb,ready.bytes,ready.length,1));CHECK(applied.pop(d)&&d.bytes[5]==static_cast<unsigned>(Kind::Applied));CHECK(applied.view().requested==Request::None);
 auto altered=t;++altered.nonces[1];ready=packet(Kind::Prepared,altered,1,Durable::Prepared);CHECK(!applied.receive(mb,ready.bytes,ready.length,1));
 Protocol fresh;CHECK(fresh.open(mb,offer(2),1,777,1));auto oldCommit=packet(Kind::Commit,t,0,Durable::Committed);CHECK(!fresh.receive(ma,oldCommit.bytes,oldCommit.length,1));CHECK(fresh.view().stage==Stage::Discovering);
}
void retiredReceipts(){
 // Guest observes the coordinator's Applied; its own final reply is lost.
 Pair old;old.review();old.prepared();const auto t=old.a.view().transcript;
 CHECK(old.a.setDurable(Durable::Committed,old.now));old.pump();CHECK(old.b.setDurable(Durable::Committed,old.now));CHECK(old.a.setDurable(Durable::Applied,old.now));CHECK(old.b.setDurable(Durable::Applied,old.now));
 Datagram d;while(old.b.pop(d)){};while(old.a.pop(d))(void)old.b.receive(ma,d.bytes,d.length,old.now);
 CHECK(old.b.peerApplied()&&!old.a.peerApplied());CHECK(old.b.open(mb,offer(2),20,999,old.now,true,1));
 old.tick(500);CHECK(old.a.peerApplied());CHECK(old.b.view().stage==Stage::Discovering&&old.b.view().requested==Request::None);
 // Both are negotiating trade2 when old terminal packets are replayed. Their
 // live pair and consent remain unchanged and Applied replies cannot ping-pong.
 Pair next;CHECK(next.a.open(ma,offer(1),20,300,next.now,true,1));CHECK(next.b.open(mb,offer(2),20,400,next.now,true,1));next.pump();CHECK(next.a.invite(mb,321,next.now));next.pump();const auto live=next.a.view().transcript;CHECK(live.receivedTrades[0]==1&&live.receivedTrades[1]==1);
 auto terminal=packet(Kind::Applied,t,1,Durable::Applied);CHECK(next.a.receive(mb,terminal.bytes,terminal.length,next.now));next.pump(0,true);CHECK(trade::sameTranscript(next.a.view().transcript,live)&&trade::sameTranscript(next.b.view().transcript,live));CHECK(next.a.view().requested==Request::None&&next.b.view().requested==Request::None);CHECK(next.a.view().durable==Durable::None&&next.b.view().durable==Durable::None);
 auto undecided=packet(Kind::Prepared,t,1,Durable::Prepared);CHECK(!next.a.receive(mb,undecided.bytes,undecided.length,next.now));CHECK(!next.a.pop(d));
 undecided=packet(Kind::Status,t,1);CHECK(!next.a.receive(mb,undecided.bytes,undecided.length,next.now));CHECK(!next.a.pop(d));
 // Delayed coordinator Abort gets only a stateless Aborted receipt, preserving
 // the new review. The current prepared Abort still requires a durable write.
 next.now+=500;auto aborted=packet(Kind::Abort,t,0,Durable::Aborted);CHECK(next.b.receive(ma,aborted.bytes,aborted.length,next.now));CHECK(next.b.pop(d));tradewire::Message read;CHECK(decode(mb,d.bytes,d.length,read)&&read.kind==Kind::Status&&read.durable==Durable::Aborted&&trade::sameTranscript(read.transcript,t));CHECK(trade::sameTranscript(next.b.view().transcript,live)&&next.b.view().requested==Request::None);CHECK(!next.b.pop(d));
 Protocol noFloor;CHECK(noFloor.open(mb,offer(2),20,999,0,true,0));while(noFloor.pop(d)){};auto commit=packet(Kind::Commit,t,0,Durable::Committed);CHECK(!noFloor.receive(ma,commit.bytes,commit.length,0));CHECK(!noFloor.pop(d));
 Protocol later;CHECK(later.open(mb,offer(2),20,999,0,true,2));while(later.pop(d)){};CHECK(later.receive(ma,commit.bytes,commit.length,0));CHECK(later.pop(d)&&d.bytes[5]==static_cast<unsigned>(Kind::Applied));CHECK(later.view().stage==Stage::Discovering&&later.view().requested==Request::None);
 for(unsigned i=0;i<100;++i)CHECK(later.receive(ma,commit.bytes,commit.length,0));CHECK(!later.pop(d));CHECK(later.receive(ma,commit.bytes,commit.length,500));CHECK(later.pop(d));
 // Wrong radio identity and a non-coordinator Commit never authorize receipts.
 CHECK(!later.receive(mc,commit.bytes,commit.length,1000));auto invalid=commit;std::memcpy(invalid.bytes+8,mb.bytes,6);reseal(invalid);CHECK(!later.receive(mb,invalid.bytes,invalid.length,1000));
}
void durableRecoveryMatrix(){
 Pair baseline;baseline.review();const auto t=baseline.a.view().transcript;
 const Durable states[][2]={{Durable::Prepared,Durable::Prepared},{Durable::Committed,Durable::Prepared},{Durable::Applied,Durable::Prepared},{Durable::Committed,Durable::Committed},{Durable::Applied,Durable::Committed},{Durable::Applied,Durable::Applied},{Durable::Aborted,Durable::Prepared},{Durable::Aborted,Durable::Aborted}};
 for(const auto& initial:states)for(unsigned lost=2;lost<=static_cast<unsigned>(Kind::Cancel);++lost){
  Pair p;p.now=0;Durable disk[2]{initial[0],initial[1]};CHECK(p.a.restore(ma,t,disk[0],0));CHECK(p.b.restore(mb,t,disk[1],0));unsigned applied[2]{disk[0]==Durable::Applied?1u:0u,disk[1]==Durable::Applied?1u:0u};
  for(unsigned iteration=0;iteration<24;++iteration){
   p.pump(iteration<12?1u<<lost:0,true);
   for(unsigned actor=0;actor<2;++actor){auto& wire=actor?p.b:p.a;const auto requested=wire.view().requested;
    if(requested==Request::None)continue;
    CHECK(requested!=Request::Prepare);const auto phase=requested==Request::Commit?Durable::Committed:requested==Request::Apply?Durable::Applied:Durable::Aborted;
    if(phase==Durable::Applied)++applied[actor];disk[actor]=phase;CHECK(wire.setDurable(phase,p.now));
   }
   p.now+=500;
   // Simulated reset after any durable boundary, with old packets still queued
   // at the other peer. Only the checkpointed phase/transcript survives.
   if(iteration==2||iteration==7||iteration==13){const auto actor=(iteration+lost)%2;auto& wire=actor?p.b:p.a;wire.close();CHECK(wire.restore(actor?mb:ma,t,disk[actor],p.now));}
   p.a.tick(p.now);p.b.tick(p.now);
  }
  p.pump(0,true);const bool abort=initial[0]==Durable::Aborted;
  CHECK(p.a.view().durable==(abort?Durable::Aborted:Durable::Applied));CHECK(p.b.view().durable==(abort?Durable::Aborted:Durable::Applied));CHECK(applied[0]==(abort?0u:1u)&&applied[1]==(abort?0u:1u));
  CHECK(p.a.view().requested==Request::None&&p.b.view().requested==Request::None);
 }
}
void boundedAndSimultaneous(){
 Pair p;p.open();CHECK(p.a.invite(mb,111,p.now));CHECK(p.b.invite(ma,222,p.now));p.pump(0,true);CHECK(p.a.view().transcript.session==111&&trade::sameTranscript(p.a.view().transcript,p.b.view().transcript));p.prepared();p.complete();
 Pair flood;flood.review();auto ack=packet(Kind::ReviewAck,flood.a.view().transcript,1);for(unsigned i=0;i<100;++i)CHECK(flood.a.receive(mb,ack.bytes,ack.length,flood.now));Datagram d;unsigned queued=0;while(flood.a.pop(d))++queued;CHECK(queued<=kTxCapacity);
 CHECK(flood.a.confirm(flood.a.view().transcript,flood.now));CHECK(flood.a.setDurable(Durable::Prepared,flood.now));auto status=packet(Kind::Status,flood.a.view().transcript,1);for(unsigned i=0;i<100;++i)CHECK(flood.a.receive(mb,status.bytes,status.length,flood.now));queued=0;while(flood.a.pop(d))++queued;CHECK(queued==kTxCapacity);CHECK(flood.a.view().durable==Durable::Prepared&&flood.a.view().requested==Request::None);
 Protocol extras[5];for(unsigned i=0;i<5;++i){Identity id{{2,7,7,7,7,static_cast<std::uint8_t>(i+1)}};CHECK(extras[i].open(id,offer(1),1,300+i,flood.now));CHECK(extras[i].pop(d));(void)flood.a.receive(id,d.bytes,d.length,flood.now);}CHECK(flood.a.view().peerCount==kMaxPeers);
}
}
int main(){codec();legacyConsentEpoch();durableOrdering();retries();offersAndCrossing();cancellation();recovery();retiredReceipts();durableRecoveryMatrix();boundedAndSimultaneous();std::printf("Trade protocol: %u checks, %u failures\n",checks,failures);return failures?1:0;}
