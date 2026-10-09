#include "game.hpp"
#include "snapshot_test_helpers.hpp"
#include "forms.hpp"
#include "legacy_v12.hpp"
#include "../firmware/tests/legacy_v15_snapshot.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <initializer_list>
using namespace digivice;
namespace old=digivice::legacy_v12;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)
void step(State& s,Action a,unsigned v=0){const auto error=apply(s,a,v);if(error!=Error::None)std::fprintf(stderr,"action%u: %s\n",unsigned(a),errorText(error));CHECK(error==Error::None);CHECK(isValid(s));}
void prior(old::State& s,old::Action a,unsigned v=0){CHECK(old::apply(s,a,v)==old::Error::None);CHECK(old::isValid(s));}
bool same(const State& a,const State& b){Snapshot x,y;return encodeSnapshot(a,x)&&encodeSnapshot(b,y)&&!std::memcmp(x.bytes,y.bytes,kSnapshotSize);}
void reject(State& s,Action a,Error error){auto before=s;CHECK(apply(s,a)==error);CHECK(same(s,before));}
void restore(State& s){Snapshot saved;CHECK(encodeSnapshot(s,saved));State next;CHECK(decodeSnapshot(saved.bytes,sizeof(saved.bytes),next)==SnapshotStatus::Ok&&same(s,next));s=next;}
State migrate(const old::State& s){old::Snapshot bytes;CHECK(old::encodeSnapshot(s,bytes));State current;CHECK(decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),current)==SnapshotStatus::Migrated);Snapshot saved;CHECK(encodeSnapshot(current,saved));CHECK(snapshot_test::sameOldPayload(bytes.bytes,saved.bytes,sizeof(bytes.bytes))&&current.receivedTrades==0);return current;}
void productionPool(){
 unsigned counts[4]{};bool seen[forms::kFormCount+1]{};unsigned draws=0;
 for(unsigned partner=1;partner<=forms::kFormCount;++partner)for(unsigned level:{1u,5u,10u,15u,20u}){
  if(!combat::validFormProfile(partner,level))continue;
  auto tier=forms::combatTier(partner);if(tier<forms::CombatTier::Rookie)tier=forms::CombatTier::Rookie;
  for(unsigned seed=0;seed<32;++seed)for(unsigned encounter:{1u,2u,11u,1000000u}){
   const auto selected=encounters::selectProduction(encounter,seed,partner,level);const auto* f=forms::find(selected);
   CHECK(f&&forms::productionForm(selected)&&forms::encounterObtainable(selected));
   if(f){CHECK(f->minLevel<=level&&forms::combatTier(selected)<=tier);seen[selected]=true;}
   CHECK(selected==selectWildForm(encounter,seed,partner,level));++draws;
  }
 }
 for(unsigned id=1;id<=forms::kFormCount;++id)CHECK(forms::productionForm(id)==(id>=11)&&forms::encounterObtainable(id)==(id>=11));
 for(unsigned seed=1;seed<=8192;++seed){const auto id=encounters::selectProduction(1,seed,11,1);++counts[unsigned(encounters::rarityForForm(id))];}
 CHECK(!counts[0]&&counts[1]>5000&&counts[2]>1500&&counts[3]>200);
 unsigned covered=0;for(unsigned id=1;id<=forms::kFormCount;++id)covered+=seen[id];
 for(unsigned seed=0;seed<16384&&covered<266;++seed){const auto id=encounters::selectProduction(17,seed,14,20);CHECK(forms::productionForm(id));if(!seen[id]){seen[id]=true;++covered;}}
 CHECK(covered==266);
 for(unsigned encounter=1;encounter<=100;++encounter)CHECK(encounters::select(encounter,42,11,1)==old::selectWildForm(encounter,42,11,1));
 CHECK(encounters::select(1,42,11,1)==4); // Existing rules12 histories still reproduce exactly.
 CHECK(!encounters::selectProduction(0,1,11,1)&&!encounters::selectProduction(1,1,0,1)&&!encounters::selectProduction(1,1,11,0));
 std::printf("Production selector: %u draws; all266 released forms reached; first-encounter rarity %u/%u/%u of8192; zero original fixtures\n",draws,counts[1],counts[2],counts[3]);
}
void releaseEvolutionEdges(){
 unsigned released=0,retainedOnly=0;
 for(unsigned i=0;i<forms::edgeCount();++i){
  const auto* edge=forms::edgeAt(i);
  if(!forms::productionForm(edge->from)||!forms::productionForm(edge->to)){++retainedOnly;continue;}
  ++released;const auto* from=forms::find(edge->from);const auto* to=forms::find(edge->to);
  const auto level=edge->minLevel>from->minLevel?edge->minLevel:from->minLevel;
  const auto bond=edge->minBond>from->minBond?edge->minBond:from->minBond;
  auto s=newDevice(73);step(s,Action::Hatch,1);s.sequence=s.foregroundSequence=3;s.steps=100;s.captures=s.encounters=1;
  s.collectionCount=2;s.nextMemberId=3;s.activeCreatureId=2;
  s.collection[1]={2,static_cast<Species>(from->lineage),17,80,70,80,bond,level,2,xpForLevel(level),from->id};
  s.hp=17;s.energy=80;s.fullness=70;s.mood=80;s.bond=bond;s.level=level;s.journal[(from->id-1)/32]|=1u<<((from->id-1)%32);CHECK(isValid(s));
  const auto before=s;const auto maxBefore=combat::formProfile(from->id,level).stats.maxHp;step(s,Action::Evolve,to->id);
  CHECK(s.collection[1].id==2&&s.collection[1].formId==to->id&&unsigned(s.collection[1].species)==to->lineage&&s.collection[1].xp==before.collection[1].xp);
  CHECK(s.hp==(17*combat::formProfile(to->id,level).stats.maxHp+maxBefore-1)/maxBefore&&s.rngState==before.rngState&&hasObtained(s,to->id));restore(s);
 }
 CHECK(released==166&&retainedOnly==6);std::printf("All%u released evolution routes preserve partner identity/XP and HP fraction; %u original-only routes remain historical\n",released,retainedOnly);
}
void firstStepsAndStarters(){
 auto egg=newDevice();reject(egg,Action::ResolveTestEncounter,Error::WrongPhase);
 unsigned offerCount=0;for(unsigned id=1;id<=forms::kFormCount;++id)if(validStarterOfferForm(id)){++offerCount;CHECK(forms::productionForm(id));}CHECK(offerCount==32);
 for(unsigned seed=1;seed<=128;++seed)for(unsigned starter=1;starter<=8;++starter){
  auto s=newDevice(seed);CHECK(forms::productionForm(starterForm(s,starter)));step(s,Action::Hatch,starter);step(s,Action::EncounterSeed,seed);
  const auto before=s;const auto remaining=encounterStepsRemaining(s);CHECK(remaining>=40&&remaining<=80);
  step(s,Action::AccrueSteps,remaining);CHECK(s.phase==Phase::Home&&s.pendingEncounter.rules==kRulesVersion&&forms::productionForm(s.pendingEncounter.formId));
  CHECK(s.hp==before.hp&&s.energy==before.energy&&s.rngState==before.rngState&&s.encounters==0);
  restore(s);step(s,Action::PresentEncounter);CHECK(s.wildRules==kRulesVersion&&forms::productionForm(s.wildFormId)&&!needsTestEncounterResolution(s));
  auto direct=newDevice(seed);step(direct,Action::Hatch,starter);step(direct,Action::Walk,100);CHECK(direct.wildRules==kRulesVersion&&forms::productionForm(direct.wildFormId));
 }
}
void preservedApartFromDismissal(const State& before,const State& after){
 auto permitted=after;permitted.sequence=before.sequence;permitted.foregroundSequence=before.foregroundSequence;permitted.message=before.message;
 permitted.phase=before.phase;permitted.wildHp=before.wildHp;permitted.wildMaxHp=before.wildMaxHp;permitted.wildFormId=before.wildFormId;permitted.wildSpecies=before.wildSpecies;
 permitted.wildLevel=before.wildLevel;permitted.wildTurn=before.wildTurn;permitted.wildRules=before.wildRules;permitted.captureAttempts=before.captureAttempts;
 permitted.cardUsed=before.cardUsed;permitted.attackBoost=before.attackBoost;permitted.shield=before.shield;permitted.pendingEncounter=before.pendingEncounter;
 CHECK(same(before,permitted));
}
void resolution(){
 for(unsigned mode=0;mode<2;++mode)for(unsigned kind=0;kind<4;++kind){
  auto legacy=old::newDevice(99);prior(legacy,old::Action::Hatch,1);prior(legacy,old::Action::Mode,mode);
  // kind0 active test only; kind1 pending test only; kind2 both; kind3 active test + real pending.
  if(kind!=1)prior(legacy,old::Action::Explore,1000);
  if(kind)prior(legacy,old::Action::AccrueSteps,1000);
  if(kind==2)legacy.pendingEncounter={4,1,12};
  if(kind==3)legacy.pendingEncounter={11,1,12};
  auto s=migrate(legacy);CHECK(needsTestEncounterResolution(s));const auto before=s;
  reject(s,Action::Rest,Error::InvalidAction);if(mode){autobattle::Trace trace;CHECK(applyAuto(s,&trace)==(s.phase==Phase::Encounter?Error::InvalidAction:Error::WrongPhase)&&same(s,before));}
  step(s,Action::ResolveTestEncounter);CHECK(!needsTestEncounterResolution(s)&&s.sequence==before.sequence+1);preservedApartFromDismissal(before,s);
  CHECK(s.phase==Phase::Home);CHECK(kind==3?s.pendingEncounter.formId==11:!s.pendingEncounter.formId);
  if(kind==1)CHECK(s.foregroundSequence==before.foregroundSequence&&s.message==before.message);
  else CHECK(s.foregroundSequence==s.sequence&&s.message==Message::EncounterCleared);
  restore(s);reject(s,Action::ResolveTestEncounter,Error::InvalidAction);
 }
 // Retiring only a pending test foe cannot alter the currently active real match.
 auto active=newDevice(753);step(active,Action::Hatch,1);step(active,Action::Mode,1);step(active,Action::Explore,1000);step(active,Action::AccrueSteps,1000);
 active.pendingEncounter={4,1,12};CHECK(isValid(active));auto before=active;step(active,Action::ResolveTestEncounter);
 auto expected=before;expected.pendingEncounter={};++expected.sequence;CHECK(same(active,expected));
 auto baseline=before;baseline.pendingEncounter={};autobattle::Trace a,b;CHECK(applyAuto(active,&a)==Error::None&&applyAuto(baseline,&b)==Error::None);
 b.startSequence=a.startSequence;b.endSequence=a.endSequence;char x[autobattle::kTraceJsonCapacity],y[autobattle::kTraceJsonCapacity];CHECK(autobattle::writeJson(a,x,sizeof(x))&&autobattle::writeJson(b,y,sizeof(y))&&!std::strcmp(x,y));
 // Ownership is never destructively rewritten, even for a historical original partner.
 auto originals=migrate(old::newGame(44));const auto owner=originals;CHECK(!needsTestEncounterResolution(originals));reject(originals,Action::ResolveTestEncounter,Error::InvalidAction);
 CHECK(apply(originals,Action::Select,1)==Error::InvalidAction&&same(originals,owner));CHECK(apply(originals,Action::Evolve,2)==Error::InvalidValue&&same(originals,owner));
 step(originals,Action::Feed);CHECK(originals.collection[0].id==owner.collection[0].id&&originals.collection[0].formId==owner.collection[0].formId);
 auto pending=old::newDevice(9);prior(pending,old::Action::Hatch,1);prior(pending,old::Action::AccrueSteps,1000);auto overflow=migrate(pending);overflow.sequence=UINT32_MAX;reject(overflow,Action::ResolveTestEncounter,Error::CounterOverflow);
}
unsigned nibble(char ch){return ch>='0'&&ch<='9'?unsigned(ch-'0'):unsigned(ch-'a'+10);}
void frozenInstalledFixtures(){
 for(const char* text:{kLegacyV15HomeHex,kLegacyV15EncounterHex,kLegacyV15CaptureHex}){
  std::uint8_t data[kV15SnapshotSize];CHECK(std::strlen(text)==sizeof(data)*2);for(unsigned i=0;i<sizeof(data);++i)data[i]=static_cast<std::uint8_t>(nibble(text[i*2])*16+nibble(text[i*2+1]));
  State s;CHECK(decodeSnapshot(data,sizeof(data),s)==SnapshotStatus::Migrated);Snapshot round;CHECK(encodeSnapshot(s,round)&&snapshot_test::sameOldPayload(data,round.bytes,sizeof(data)));
  if(needsTestEncounterResolution(s)){auto before=s;step(s,Action::ResolveTestEncounter);preservedApartFromDismissal(before,s);restore(s);}
 }
 // No rules17 record can invent a current-rules original foe, but oldrules remain decodable until explicit repair.
 auto s=newDevice();step(s,Action::Hatch,1);step(s,Action::AccrueSteps,1000);auto bad=s;bad.pendingEncounter={4,1,13};CHECK(!isValid(bad));bad.pendingEncounter.rules=12;CHECK(isValid(bad));
 Action action;CHECK(parseAction("resolve-test-encounter",action)&&action==Action::ResolveTestEncounter);
 CHECK(kSchemaVersion==22&&kRulesVersion==15&&kSnapshotSize==2964);
}
}
int main(){productionPool();releaseEvolutionEdges();firstStepsAndStarters();resolution();frozenInstalledFixtures();std::printf("%u production-roster/migration checks, %u failures\n",checks,failures);return failures?1:0;}
