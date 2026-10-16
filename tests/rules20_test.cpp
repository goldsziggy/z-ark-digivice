#include "game.hpp"
#include "forms.hpp"
#include "legacy_v19.hpp"
#include "legacy_forms_v19.hpp"
#include "trade.hpp"
#include <array>
#include <cstdio>
#include <cstring>

using namespace digivice;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)
std::uint32_t crc(const std::uint8_t* p,std::size_t n){std::uint32_t c=~0u;for(std::size_t i=0;i<n;++i){c^=p[i];for(unsigned b=0;b<8;++b)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
void put(std::uint8_t* p,std::uint32_t v){for(unsigned i=0;i<4;++i)p[i]=static_cast<std::uint8_t>(v>>(8*i));}
legacy_v19::Snapshot oldBytes(const State& s){Snapshot current;CHECK(encodeSnapshot(s,current));legacy_v19::Snapshot b;std::memcpy(b.bytes,current.bytes,sizeof(b.bytes)-4);b.bytes[4]=27;b.bytes[6]=(sizeof(b.bytes)-12)&255;b.bytes[7]=(sizeof(b.bytes)-12)>>8;put(b.bytes+8,19);put(b.bytes+sizeof(b.bytes)-4,crc(b.bytes,sizeof(b.bytes)-4));return b;}
State fixture(const forms::EvolutionEdge& edge,bool active=true){
 auto s=newDevice(20);CHECK(apply(s,Action::Hatch,1)==Error::None);
 s.sequence=s.foregroundSequence=10;s.collectionCount=2;s.nextMemberId=3;s.captures=s.encounters=1;s.steps=100;
 const auto* f=forms::find(edge.from);const auto n=forms::evolutionNeed(edge);
 s.collection[1]={2,static_cast<Species>(f->lineage),combat::formProfile(f->id,n.level).stats.maxHp,70,60,80,n.bond,n.level,2,xpForLevel(n.level),f->id};
 s.journal[(f->id-1)/32]|=1u<<((f->id-1)%32);
 if(active){s.activeCreatureId=2;const auto& m=s.collection[1];s.hp=m.hp;s.energy=m.energy;s.fullness=m.fullness;s.mood=m.mood;s.bond=m.bond;s.level=m.level;}
 CHECK(isValid(s));return s;
}
void reject(State s,Action a,std::uint32_t v,Error e){auto before=s;CHECK(apply(s,a,v)==e);CHECK(trade::sameState(s,before));}
void everyRoute(){
 unsigned routes=0;
 for(std::size_t i=0;i<forms::edgeCount();++i){const auto* edge=forms::edgeAt(i);if(!forms::productionForm(edge->from)||!forms::productionForm(edge->to))continue;++routes;
  for(bool active:{false,true})for(unsigned mistakes=0;mistakes<8;++mistakes)for(unsigned missed=0;missed<2;++missed){
   auto s=fixture(*edge,active);auto& m=s.collection[1];m.careState=(mistakes<<27)|(missed<<14)|(25u<<7)|(2u<<15);const auto care=m.careState;
   const auto original=m;const auto old=s;const auto action=active?Action::Evolve:Action::EvolveMember;const auto value=active?edge->to:(2u<<16)|edge->to;
   CHECK(apply(s,action,value)==Error::None);CHECK(isValid(s));CHECK(m.formId==edge->to&&m.id==original.id&&m.xp==original.xp&&m.level==original.level&&m.bond==original.bond);
   CHECK(m.careState==(care&~((7u<<27)|0x7fu)));CHECK(s.sequence==old.sequence+1);CHECK(hasObtained(s,edge->to));
   CHECK(trade::sameMember(s.collection[0],old.collection[0]));
  }
 }
 CHECK(routes>100);std::printf("rules20 care-independent route sweep: %u edges\n",routes);
}
void gatesAndProjection(){
 const auto& edge=*forms::outgoing(11,0);const auto n=forms::evolutionNeed(edge);auto s=fixture(edge);s.collection[1].careState=7u<<27;
 auto low=s;low.level=low.collection[1].level=n.level-1;low.collection[1].xp=xpForLevel(n.level-1);low.hp=low.collection[1].hp=combat::formProfile(edge.from,low.level).stats.maxHp;reject(low,Action::Evolve,edge.to,Error::EvolutionUnavailable);
 low=s;low.bond=low.collection[1].bond=n.bond-1;reject(low,Action::Evolve,edge.to,Error::EvolutionUnavailable);
 low=s;low.collection[1].careState|=1u<<30;reject(low,Action::Evolve,edge.to,Error::MemberInjured);
 reject(s,Action::Evolve,31,Error::EvolutionUnavailable);
 static char json[kJsonCapacity];CHECK(writeJson(s,json,sizeof(json)));CHECK(std::strstr(json,"\"requiredCare\":0"));CHECK(!std::strstr(json,"\"maxCareMistakes\":3"));CHECK(std::strstr(json,"\"maxCareMistakes\":null,\"careRouteOpen\":true"));
 auto old=oldBytes(s);legacy_v19::State frozen;CHECK(legacy_v19::decodeSnapshot(old.bytes,sizeof(old.bytes),frozen)==legacy_v19::SnapshotStatus::Ok);CHECK(legacy_v19::apply(frozen,legacy_v19::Action::Evolve,edge.to)==legacy_v19::Error::EvolutionUnavailable);
 frozen.collection[1].careState=100u|(7u<<27);CHECK(legacy_v19::apply(frozen,legacy_v19::Action::Evolve,edge.to)==legacy_v19::Error::CareRouteLocked);
 CHECK(apply(s,Action::Evolve,edge.to)==Error::None);
 // A missed-care episode still accrues and the normal care action still clears it.
 auto care=newDevice(91);CHECK(apply(care,Action::Hatch,1)==Error::None);CHECK(apply(care,Action::CareMinute,1)==Error::None);
 for(unsigned minute=2;minute<100&&!careWasMissed(*activeMember(care));++minute)CHECK(apply(care,Action::CareMinute,minute)==Error::None);
 CHECK(careWasMissed(*activeMember(care))&&careMistakes(*activeMember(care))>0);CHECK(apply(care,Action::Toilet)==Error::None);CHECK(!careWasMissed(*activeMember(care)));
}
void snapshotMigration(){
 const auto& edge=*forms::outgoing(29,0);auto s=fixture(edge);s.collection[1].careState=47u|(31u<<7)|(1u<<14)|(7u<<27);s.dungeonKeys=1;s.dungeonWins=6;s.bossSteps=4123;
 const auto old=oldBytes(s);State migrated;CHECK(decodeSnapshot(old.bytes,sizeof(old.bytes),migrated)==SnapshotStatus::Migrated);CHECK(trade::sameState(s,migrated));
 Snapshot current;CHECK(encodeSnapshot(migrated,current));CHECK(current.bytes[4]==30&&current.bytes[8]==22);CHECK(!std::memcmp(old.bytes+12,current.bytes+12,sizeof(old.bytes)-16));
 auto corrupt=old;corrupt.bytes[8]=20;put(corrupt.bytes+sizeof(corrupt.bytes)-4,crc(corrupt.bytes,sizeof(corrupt.bytes)-4));CHECK(decodeSnapshot(corrupt.bytes,sizeof(corrupt.bytes),migrated)==SnapshotStatus::UnsupportedRules);
}
void futureEpochRejected(){
 auto s=fixture(*forms::outgoing(11,0));
 CHECK(apply(s,Action::Explore,1000)==Error::None);CHECK(s.wildRules==22);
 auto bytes=oldBytes(s);State out=newDevice(32);const auto unchanged=out;
 CHECK(decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),out)==SnapshotStatus::InvalidState);CHECK(trade::sameState(out,unchanged));
 bytes.bytes[4]=28;put(bytes.bytes+8,20);put(bytes.bytes+sizeof(bytes.bytes)-4,crc(bytes.bytes,sizeof(bytes.bytes)-4));
 CHECK(decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),out)==SnapshotStatus::InvalidState);CHECK(trade::sameState(out,unchanged));
 auto queued=fixture(*forms::outgoing(11,0));CHECK(apply(queued,Action::EncounterSeed,732)==Error::None);CHECK(apply(queued,Action::AccrueSteps,1000)==Error::None);CHECK(queued.pendingEncounter.rules==22);
 bytes=oldBytes(queued);CHECK(decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),out)==SnapshotStatus::InvalidState);CHECK(trade::sameState(out,unchanged));
 bytes.bytes[4]=28;put(bytes.bytes+8,20);put(bytes.bytes+sizeof(bytes.bytes)-4,crc(bytes.bytes,sizeof(bytes.bytes)-4));
 CHECK(decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),out)==SnapshotStatus::InvalidState);CHECK(trade::sameState(out,unchanged));
}
void historicalTrade(){
 auto a=fixture(*forms::outgoing(11,0),false),b=fixture(*forms::outgoing(18,0),false);trade::Transcript t;
 t.peers[0].bytes[0]=2;t.peers[1].bytes[0]=4;t.session=3;t.nonces[0]=5;t.nonces[1]=7;t.revision=1;t.rules=19;
 t.sourceSequences[0]=a.sequence;t.sourceSequences[1]=b.sequence;t.offers[0]=a.collection[1];t.offers[1]=b.collection[1];CHECK(trade::valid(t));
 for(unsigned side=0;side<2;++side){const auto& before=side?b:a;trade::Record prepared,committed,done,aborted;
  CHECK(trade::prepare(before,t,side,1,prepared));auto walked=before;CHECK(apply(walked,Action::EncounterSeed,732)==Error::None);CHECK(apply(walked,Action::AccrueSteps,17)==Error::None);
  CHECK(trade::commit(prepared,walked,committed));CHECK(trade::applied(committed,committed.after,done));CHECK(trade::abort(prepared,aborted));
  for(const auto& r:{prepared,committed,done,aborted}){std::array<std::uint8_t,trade::kRecordBytes> current{};CHECK(trade::encodeRecord(r,current.data(),current.size()));
   std::array<std::uint8_t,trade::kV28RecordBytes> bytes{};std::memcpy(bytes.data(),current.data(),168);
   const auto before=oldBytes(r.before),after=oldBytes(r.after);std::memcpy(bytes.data()+168,before.bytes,sizeof(before.bytes));std::memcpy(bytes.data()+168+sizeof(before.bytes),after.bytes,sizeof(after.bytes));
   put(bytes.data()+bytes.size()-4,crc(bytes.data(),bytes.size()-4));trade::Record loaded,scratch;CHECK(trade::decodeRecord(bytes.data(),bytes.size(),loaded,scratch));
   CHECK(loaded.phase==r.phase&&loaded.transcript.rules==19&&trade::sameTranscript(loaded.transcript,r.transcript));CHECK(trade::sameState(loaded.before,r.before)&&trade::sameState(loaded.after,r.after));
  }
 }
}
}
int main(){CHECK(kRulesVersion==22&&kSchemaVersion==30&&kSnapshotSize==6876);everyRoute();gatesAndProjection();snapshotMigration();futureEpochRejected();historicalTrade();std::printf("rules20: %u checks, %u failures\n",checks,failures);return failures?1:0;}
