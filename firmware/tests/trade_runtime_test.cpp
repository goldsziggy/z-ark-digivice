#include "handheld_runtime_double.hpp"
#include "forms.hpp"
#include <cstdio>
#include <cstdlib>
using namespace digivice;
namespace { unsigned checks=0;std::uint64_t clockMs=1;std::uint32_t randomValue=70;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
struct Care final:storage::Backend {
 storage::Slot slots[2]{};bool present[2]{};unsigned writes=0;
 storage::ReadStatus readSlot(unsigned i,storage::Slot& out)override{if(!present[i])return storage::ReadStatus::Missing;out=slots[i];return storage::ReadStatus::Present;}
 bool writeSlot(unsigned i,const Snapshot& bytes)override{++writes;std::memcpy(slots[i].bytes,bytes.bytes,kSnapshotSize);slots[i].length=kSnapshotSize;present[i]=true;return true;}
};
State fixture(unsigned starter){auto s=newDevice(900+starter);CHECK(apply(s,Action::Hatch,starter)==Error::None);s.sequence=s.foregroundSequence=100;s.collectionCount=2;s.nextMemberId=3;s.steps=100;s.captures=s.encounters=1;
 const auto* f=forms::find(79);s.collection[1]={2,static_cast<Species>(f->lineage),17,63,47,89,100,5,2,xpForLevel(5)+17,79};s.journal[78/32]|=1u<<(78%32);CHECK(isValid(s));return s;}
struct Device {State state;Care care;storage::SaveStore saves{care};TradeJournalDouble journal;HandheldRuntime runtime{state,saves,journal};
 Device(unsigned side):state(fixture(side+1)){CHECK(saves.restore(state)==storage::BootStatus::Empty);state=fixture(side+1);CHECK(saves.checkpointMirrored(state));runtime.nearbyRadio_.mac.bytes[0]=static_cast<std::uint8_t>(2+2*side);runtime.beginTradeStorage();CHECK(runtime.openTradeRadio(clockMs));}
};
void deliver(Device& from,Device& to){tradewire::Datagram p;unsigned count=0;while(from.runtime.tradeWire_.pop(p)){CHECK(++count<=16);(void)to.runtime.tradeWire_.receive(from.runtime.nearbyRadio_.mac,p.bytes,p.length,clockMs);}}
void pump(Device& a,Device& b,unsigned turns=5){for(unsigned i=0;i<turns;++i){++clockMs;deliver(a,b);deliver(b,a);a.runtime.pollTradePersistence(clockMs);b.runtime.pollTradePersistence(clockMs);}}
deviceui::Intent intent(Device& d,deviceui::IntentKind kind){deviceui::Intent i;i.kind=kind;const auto& t=d.runtime.tradeWire_.view().transcript;i.tradeSession=t.session;i.tradeRevision=t.revision;i.tradeFingerprint=trade::fingerprint(t);return i;}
void review(Device& a,Device& b){pump(a,b);deviceui::Intent i;i.kind=deviceui::IntentKind::TradeInvite;i.value=2;i.peer=b.runtime.nearbyRadio_.mac;a.runtime.tradeIntent(i);pump(a,b);CHECK(a.runtime.tradeWire_.view().stage==tradewire::Stage::Reviewing);CHECK(b.runtime.tradeWire_.view().stage==tradewire::Stage::Reviewing);}
}
std::uint32_t esp_random(){return ++randomValue;}
std::int64_t esp_timer_get_time(){return static_cast<std::int64_t>(clockMs*1000);}
int main(){
 {Device a(0),b(1);review(a,b);auto stale=intent(a,deviceui::IntentKind::TradeConfirm);++stale.tradeRevision;a.runtime.tradeIntent(stale);CHECK(!a.runtime.tradeSession_.record());
  a.runtime.tradeIntent(intent(a,deviceui::IntentKind::TradeConfirm));CHECK(a.runtime.tradeSession_.record()->phase==trade::Phase::Prepared);
  CHECK(a.journal.writes==2&&a.care.writes==4);CHECK(a.runtime.tradeWire_.view().durable==tradewire::Durable::Prepared);CHECK(a.runtime.tradeSession_.blocksForeground());
  // Confirming only one screen cannot exchange either collection.
  pump(a,b);CHECK(a.state.receivedTrades==0&&b.state.receivedTrades==0);
  b.runtime.tradeIntent(intent(b,deviceui::IntentKind::TradeConfirm));pump(a,b);
  CHECK(a.state.receivedTrades==1&&b.state.receivedTrades==1);CHECK(!findMember(a.state,2)&&!findMember(b.state,1)&&findMember(b.state,2));
  CHECK(!a.runtime.tradeSession_.blocksForeground()&&!b.runtime.tradeSession_.blocksForeground());
  const auto aw=a.journal.writes,bw=b.journal.writes,ac=a.care.writes,bc=b.care.writes;
  for(unsigned i=0;i<100;++i){clockMs+=501;a.runtime.tradeWire_.tick(clockMs);b.runtime.tradeWire_.tick(clockMs);pump(a,b,1);}
  CHECK(a.journal.writes==aw&&b.journal.writes==bw&&a.care.writes==ac&&b.care.writes==bc);
  CHECK(a.runtime.tradePeerTerminal_&&b.runtime.tradePeerTerminal_);
 }
 {Device a(0),b(1);review(a,b);a.journal.fail=1;a.journal.landed=true;a.runtime.tradeIntent(intent(a,deviceui::IntentKind::TradeConfirm));
  CHECK(!a.runtime.tradeSession_.healthy());CHECK(a.runtime.tradeWire_.view().durable==tradewire::Durable::None);
  tradewire::Datagram p;while(a.runtime.tradeWire_.pop(p)){tradewire::Message m;CHECK(tradewire::decode(a.runtime.nearbyRadio_.mac,p.bytes,p.length,m));CHECK(m.kind!=tradewire::Kind::Prepared&&m.kind!=tradewire::Kind::Commit&&m.kind!=tradewire::Kind::Applied);}
  a.journal.fail=0;HandheldRuntime reboot(a.state,a.saves,a.journal);reboot.nearbyRadio_.mac=a.runtime.nearbyRadio_.mac;reboot.beginTradeStorage();CHECK(reboot.tradeSession_.blocksForeground());CHECK(reboot.openTradeRadio(clockMs));CHECK(reboot.tradeWire_.view().durable==tradewire::Durable::Prepared);
 }
 {Device a(0),b(1);review(a,b);a.runtime.tradeIntent(intent(a,deviceui::IntentKind::TradeConfirm));pump(a,b);
  auto close=intent(a,deviceui::IntentKind::TradeClose);a.runtime.tradeIntent(close);CHECK(a.runtime.nearbyPhase_==HandheldRuntime::NearbyPhase::Idle);CHECK(a.runtime.tradeSession_.blocksForeground());
  CHECK(a.runtime.openTradeRadio(clockMs));CHECK(a.runtime.tradeWire_.view().durable==tradewire::Durable::Prepared);
 }
 std::printf("%u actual handheld trade integration checks PASS (radio/UI/SDK doubled, persistence+wire+core real)\n",checks);
}
