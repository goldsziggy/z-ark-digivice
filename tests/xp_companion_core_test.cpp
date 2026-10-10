#include "game.hpp"
#include "trade.hpp"
#include "forms.hpp"
#include "capture_ring.hpp"
#include "legacy_v14.hpp"
#include "legacy_v15.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace digivice;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)
void step(State& s,Action a,unsigned value=0){CHECK(apply(s,a,value)==Error::None);CHECK(isValid(s));}
bool same(const State& a,const State& b){return trade::sameState(a,b);}
void reject(State& s,Action a,unsigned value,Error e){const auto before=s;CHECK(apply(s,a,value)==e);CHECK(same(s,before));}
void restore(State& s){Snapshot bytes;CHECK(encodeSnapshot(s,bytes));State after;CHECK(decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),after)==SnapshotStatus::Ok&&same(s,after));s=after;}
unsigned crc(const std::uint8_t* p,std::size_t n){unsigned c=~0u;for(std::size_t i=0;i<n;++i){c^=p[i];for(unsigned b=0;b<8;++b)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
void put(std::uint8_t* p,unsigned value){for(unsigned i=0;i<4;++i)p[i]=value>>(8*i);}
unsigned nextRandom(unsigned x){x^=x<<13;x^=x>>17;x^=x<<5;return x;}
State roster(unsigned count=6){
 auto s=newDevice(31);step(s,Action::Hatch,1);s.sequence=s.foregroundSequence=100;
 s.collectionCount=count;s.captures=s.encounters=count-1;s.steps=100*(count-1);s.nextMemberId=count+1;
 for(unsigned i=1;i<count;++i){s.collection[i]={i+1,Species::Agumon,17,40+i,30+i,20+i,17,1,i+1,0,18};s.journal[0]|=1u<<17;}
 CHECK(isValid(s));return s;
}
void level(State& s,unsigned id,unsigned xp){auto& m=*const_cast<CreatureMember*>(findMember(s,id));m.xp=xp;m.level=levelForXp(xp);m.hp=combat::formProfile(m.formId,m.level).stats.maxHp/2;if(id==s.activeCreatureId){s.hp=m.hp;s.level=m.level;}CHECK(isValid(s));}
void party(State& s,unsigned count){for(unsigned i=0;i<count;++i)step(s,Action::PartyAdd,2+i);}
void fight(State& s,unsigned wildLevel=1){step(s,Action::Explore,1000);const auto* form=forms::find(19);if(wildLevel<form->minLevel)wildLevel=form->minLevel;s.wildFormId=19;s.wildSpecies=static_cast<Species>(form->lineage);s.wildLevel=wildLevel;s.wildRules=kRulesVersion;s.wildMaxHp=combat::formProfile(19,wildLevel).stats.maxHp;s.wildHp=1;
 while(wildGuard(s)==combat::Defense::Counter)++s.wildTurn;CHECK(isValid(s));}
void rewardMember(const CreatureMember& before,const CreatureMember& after,unsigned award,unsigned bondAward){
 const auto expectedXp=before.xp+award>kMaxXp?kMaxXp:before.xp+award,expectedLevel=levelForXp(expectedXp);
 const auto oldMax=combat::formProfile(before.formId,before.level).stats.maxHp,newMax=combat::formProfile(before.formId,expectedLevel).stats.maxHp;
 const auto expectedHp=expectedLevel>before.level?(before.hp*newMax+oldMax-1)/oldMax:before.hp;
 const auto expectedBond=before.bond+bondAward>200?200:before.bond+bondAward;
 CHECK(after.xp==expectedXp&&after.level==expectedLevel&&after.hp==expectedHp&&after.bond==expectedBond);
 auto normalized=after;normalized.xp=before.xp;normalized.level=before.level;normalized.hp=before.hp;normalized.bond=before.bond;
 CHECK(trade::sameMember(before,normalized));
}
void contractAndSelection(){
 CHECK(kSchemaVersion==26&&kRulesVersion==19&&kPartyCapacity==3&&kCollectionCapacity==60&&kSnapshotSize==3216&&sizeof(State)==3188);
 Action parsed=Action::Feed;CHECK(!parseAction(nullptr,parsed)&&parsed==Action::Feed);CHECK(!parseAction("",parsed)&&parsed==Action::Feed);CHECK(parseAction("party-add",parsed)&&parsed==Action::PartyAdd);CHECK(parseAction("party-remove",parsed)&&parsed==Action::PartyRemove);
 auto egg=newDevice();reject(egg,Action::PartyAdd,2,Error::WrongPhase);
 auto s=roster(60);reject(s,Action::PartyAdd,0,Error::InvalidValue);reject(s,Action::PartyAdd,UINT32_MAX,Error::InvalidValue);reject(s,Action::PartyAdd,61,Error::UnknownMember);
 reject(s,Action::PartyAdd,1,Error::ActiveMemberParty);reject(s,Action::PartyRemove,2,Error::NotPartyMember);
 for(unsigned id:{60u,2u,59u})step(s,Action::PartyAdd,id);
 CHECK(partyCount(s)==3&&s.partyMemberIds[0]==60&&s.partyMemberIds[1]==2&&s.partyMemberIds[2]==59);
 reject(s,Action::PartyAdd,3,Error::PartyFull);reject(s,Action::PartyAdd,60,Error::PartyMemberExists);restore(s);
 static char json[kJsonCapacity];CHECK(writeJson(s,json,sizeof(json))&&std::strstr(json,"\"partyCapacity\":3,\"partyMemberIds\":[60,2,59]"));
 const auto durable=s;const unsigned pinned[]{1,60,2,59};bool seen[61]{};
 for(unsigned index=0;index<60;++index){const auto* member=collectionMemberAtDisplayIndex(s,index);CHECK(member&&!seen[member->id]);if(!member)continue;seen[member->id]=true;
  CHECK(displayIndexForMember(s,member->id)==index);if(index<4)CHECK(member->id==pinned[index]);else CHECK(member->id==62-index);}
 CHECK(!collectionMemberAtDisplayIndex(s,60)&&displayIndexForMember(s,99)==60&&same(durable,s));
 step(s,Action::PartyRemove,2);CHECK(partyCount(s)==2&&s.partyMemberIds[0]==60&&s.partyMemberIds[1]==59&&!s.partyMemberIds[2]);step(s,Action::PartyAdd,2);
 CHECK(s.partyMemberIds[2]==2);step(s,Action::Select,59);CHECK(s.activeCreatureId==59&&s.partyMemberIds[0]==60&&s.partyMemberIds[1]==2&&!s.partyMemberIds[2]&&!isPartyMember(s,1));
 step(s,Action::Release,60);CHECK(!findMember(s,60)&&s.partyMemberIds[0]==2&&!s.partyMemberIds[1]);restore(s);
 step(s,Action::Explore,1000);reject(s,Action::PartyAdd,3,Error::WrongPhase);reject(s,Action::PartyRemove,2,Error::WrongPhase);
}
void terminalRewards(){
 for(unsigned selected=0;selected<=3;++selected)for(unsigned cap:{0u,1u})for(unsigned wildLevel:{1u,20u})for(bool capture:{false,true}){
  auto s=roster(capture?59:60);party(s,selected);level(s,1,cap?kMaxXp:0);level(s,2,30);level(s,3,kMaxXp-10);level(s,4,kMaxXp);fight(s,wildLevel);
  unsigned phase=0;if(capture){while(phase<capturering::kCycleMs&&capturering::sample(phase,s.wildFormId).grade!=capturering::Grade::Green)++phase;const auto chance=ringCaptureChance(s,phase);CHECK(chance>0);for(unsigned guard=0;chance&&guard<10000&&nextRandom(s.rngState)%100>=chance;++guard)s.rngState=nextRandom(s.rngState);}
  const auto before=s;step(s,capture?Action::RingCapture:Action::Attack,phase);CHECK(s.phase==Phase::Home&&s.sequence==before.sequence+1);
  const auto foughtLevel=before.wildLevel;const auto amount=20+6*foughtLevel;CHECK(s.message==(capture?Message::Captured:(s.level>before.level?Message::Trained:Message::Won)));
  for(unsigned i=1;i<before.collectionCount;++i){if(isPartyMember(before,before.collection[i].id))rewardMember(before.collection[i],s.collection[i],amount,capture?12:8);else CHECK(trade::sameMember(before.collection[i],s.collection[i]));}
  CHECK(s.collection[0].xp==(cap?kMaxXp:amount));if(capture)CHECK(s.collectionCount==60&&!isPartyMember(s,60)&&s.collection[59].xp==xpForLevel(foughtLevel));
  restore(s);const auto rewarded=s;reject(s,capture?Action::RingCapture:Action::Attack,phase,Error::WrongPhase);CHECK(same(s,rewarded));
 }
 // A real unsuccessful timing throw spends its attempt but grants no XP.
 auto failed=roster();party(failed,3);fight(failed,20);failed.wildHp=failed.wildMaxHp/2;
 const auto chance=ringCaptureChance(failed,0);CHECK(chance>0&&chance<100);
 while(nextRandom(failed.rngState)%100<chance)failed.rngState=nextRandom(failed.rngState);
 const auto beforeThrow=failed;step(failed,Action::RingCapture,0);CHECK(failed.lastCapture.result==CaptureResult::Escaped&&failed.captureAttempts==1);
 for(unsigned i=1;i<6;++i)CHECK(trade::sameMember(beforeThrow.collection[i],failed.collection[i]));
 // No reward for missed attempts, explicit capture stop or retreat.
 auto s=roster();party(s,3);fight(s,20);
 s.wildFormId=18;s.wildSpecies=static_cast<Species>(forms::find(18)->lineage);s.wildLevel=1;s.wildRules=kRulesVersion;
 s.wildMaxHp=combat::formProfile(18,1).stats.maxHp;s.wildHp=s.wildMaxHp/2;
 s.hp=s.collection[0].hp=combat::formProfile(s.collection[0].formId,s.level).stats.maxHp;
 while(wildGuard(s)==combat::Defense::Counter)++s.wildTurn;CHECK(isValid(s));const auto before=s;
 for(unsigned attempt=0;attempt<3;++attempt){step(s,Action::Flick,0);if(attempt<2)step(s,Action::Attack);}
 CHECK(s.phase==Phase::Encounter&&s.message==Message::CaptureMissed&&s.captureAttempts==3&&s.captureDeferred==1);
 reject(s,Action::Flick,0,Error::InvalidAction);
 for(unsigned i=1;i<6;++i)CHECK(trade::sameMember(before.collection[i],s.collection[i]));
 s=roster();party(s,3);fight(s,20);s.hp=s.collection[0].hp=1;s.wildHp=s.wildMaxHp;const auto retreat=s;step(s,Action::Attack);CHECK(s.phase==Phase::Home&&s.message==Message::Retreated);
 for(unsigned i=1;i<6;++i)CHECK(trade::sameMember(retreat.collection[i],s.collection[i]));
 // Useful care/evolution changes the active member only and keeps selections.
 s=roster();party(s,3);const auto* edge=forms::outgoing(s.collection[0].formId,0);CHECK(edge);
 if(edge){const auto need=forms::evolutionNeed(*edge);level(s,1,xpForLevel(need.level));s.bond=s.collection[0].bond=need.bond;s.collection[0].careState=need.care;CHECK(isValid(s));}
 const auto evolution=s;if(edge)step(s,Action::Evolve,edge->to);
 for(unsigned i=1;i<6;++i)CHECK(trade::sameMember(evolution.collection[i],s.collection[i]));CHECK(partyCount(s)==3);
}
void autoRewards(){
 unsigned wins=0,captures=0,retreats=0;
 for(unsigned seed=1;seed<=96;++seed)for(bool legacy:{false,true}){
  auto s=roster();s.seed=s.rngState=seed;party(s,3);step(s,Action::Mode,1);step(s,Action::Explore,1000);const auto before=s;const auto award=20+6*s.wildLevel;
  if(legacy)CHECK(applyAuto(s)==Error::None);else{CHECK(applyAutoFight(s)==Error::None);
    // Rules 18: a focus pause may come first; an untapped answer continues the same chunk.
    while(s.autoCapture==AutoCapture::FocusStrike||s.autoCapture==AutoCapture::FocusBlock)CHECK(applyFocus(s,kFocusNoTap)==Error::None);
    if(s.autoCapture==AutoCapture::Awaiting){
    for(unsigned i=1;i<4;++i)CHECK(trade::sameMember(before.collection[i],s.collection[i]));restore(s);CHECK(applyAutoResume(s)==Error::None);}}
  CHECK(isValid(s)&&s.phase==Phase::Home);const bool reward=s.lastAutoOutcome!=autobattle::Outcome::Retreated;
  wins+=s.lastAutoOutcome==autobattle::Outcome::Won;captures+=s.lastAutoOutcome==autobattle::Outcome::Captured;retreats+=!reward;
  for(unsigned i=1;i<4;++i){if(reward)rewardMember(before.collection[i],s.collection[i],award,s.lastAutoOutcome==autobattle::Outcome::Captured?12:8);else CHECK(trade::sameMember(before.collection[i],s.collection[i]));}restore(s);
 }
 CHECK(wins&&captures&&retreats);
}
trade::Transcript transcript(const State& a,const State& b,unsigned offered){trade::Transcript t;t.peers[0].bytes[0]=2;t.peers[1].bytes[0]=4;t.session=17;t.nonces[0]=1;t.nonces[1]=2;t.revision=1;t.sourceSequences[0]=a.sequence;t.sourceSequences[1]=b.sequence;t.offers[0]=*findMember(a,offered);t.offers[1]=*findMember(b,2);return t;}
void exchangeReconciliation(){
 for(unsigned offered:{1u,2u,3u,6u}){auto s=roster(),peer=roster();party(s,3);const auto t=transcript(s,peer,offered);trade::Record prepared,committed;
  CHECK(trade::prepare(s,t,0,1,prepared)&&trade::commit(prepared,s,committed));const auto& after=committed.after;CHECK(!findMember(after,offered)&&!isPartyMember(after,7)&&!isPartyMember(after,after.activeCreatureId));
  unsigned expected[3]{},count=0;for(const auto id:s.partyMemberIds)if(id!=offered&&id!=after.activeCreatureId)expected[count++]=id;
  for(unsigned i=0;i<3;++i)CHECK(after.partyMemberIds[i]==expected[i]);CHECK(after.collectionCount==s.collectionCount);
  auto changed=s;step(changed,Action::PartyRemove,2);trade::Record ignored;CHECK(!trade::backgroundOnly(s,changed)&&!trade::commit(prepared,changed,ignored));
 }
}
void migrationAndBounds(){
 namespace old=legacy_v15;
 auto historical=old::newDevice(31);
 CHECK(old::apply(historical,old::Action::Hatch,1)==old::Error::None);
 historical.level=historical.collection[0].level=20;
 historical.collection[0].xp=old::xpForLevel(20);
 historical.bond=historical.collection[0].bond=40;
 historical.hp=historical.collection[0].hp=combat::formProfile(historical.collection[0].formId,20).stats.maxHp;
 CHECK(old::isValid(historical));
 old::Snapshot original;CHECK(old::encodeSnapshot(historical,original));
 State migrated;CHECK(decodeSnapshot(original.bytes,sizeof(original.bytes),migrated)==SnapshotStatus::Migrated);
 CHECK(migrated.level==20&&migrated.collection[0].xp==xpForLevel(20)&&migrated.bond==40);
 CHECK(migrated.collection[0].formId==historical.collection[0].formId&&migrated.activeCreatureId==historical.activeCreatureId);
 CHECK(migrated.seed==historical.seed&&!migrated.collection[0].careState&&!migrated.careMinute&&!migrated.lastCritical&&!migrated.captureDeferred);
 restore(migrated);
 auto s=roster();party(s,3);Snapshot current;CHECK(encodeSnapshot(s,current));
 for(const auto ids:{std::array<unsigned,3>{2,2,3},{2,0,3},{1,2,3},{2,3,99},{0,0,UINT32_MAX}}){auto corrupt=current;
  for(unsigned i=0;i<3;++i)put(corrupt.bytes+3188+4*i,ids[i]);put(corrupt.bytes+kSnapshotSize-4,crc(corrupt.bytes,kSnapshotSize-4));
  auto dest=s;CHECK(decodeSnapshot(corrupt.bytes,kSnapshotSize,dest)==SnapshotStatus::InvalidState&&same(dest,s));}
 for(unsigned i=3188;i<kSnapshotSize;++i){auto corrupt=current;corrupt.bytes[i]^=1;auto dest=s;CHECK(decodeSnapshot(corrupt.bytes,kSnapshotSize,dest)==SnapshotStatus::BadChecksum&&same(dest,s));}
 auto invalid=s;invalid.partyMemberIds[1]=invalid.partyMemberIds[0];Snapshot untouched;std::memset(untouched.bytes,0xa5,sizeof(untouched.bytes));const auto retained=untouched;
 CHECK(!encodeSnapshot(invalid,untouched)&&!std::memcmp(untouched.bytes,retained.bytes,sizeof(retained.bytes)));
 auto exhausted=s;exhausted.sequence=UINT32_MAX;reject(exhausted,Action::PartyRemove,2,Error::CounterOverflow);
}
}
int main(){contractAndSelection();terminalRewards();autoRewards();exchangeReconciliation();migrationAndBounds();std::printf("XP companions: %u checks, %u failures; State%zu Snapshot%zu party%zu\n",checks,failures,sizeof(State),sizeof(Snapshot),kPartyCapacity);return failures?1:0;}
