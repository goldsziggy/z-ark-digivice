// Historical original-roster and rules3..12 continuation corpus.
// Current release behavior is covered by production-roster, walking, care-capture and queued tests.
#include "legacy_v12.hpp"
#include "combat.hpp"
#include "forms.hpp"
#include "legacy_v3.hpp"
#include "legacy_v4.hpp"
#include "legacy_v5.hpp"
#include "legacy_v6.hpp"
#include "legacy_v7.hpp"
#include "legacy_v8.hpp"
#include "legacy_v9.hpp"
#include "legacy_forms_v9.hpp"
#include "legacy_combat_v9.hpp"
#include "legacy_forms_v8.hpp"
#include "legacy_combat_v8.hpp"
#include "legacy_forms_v7.hpp"
#include "legacy_combat_v7.hpp"
#include "legacy_forms_v6.hpp"
#include "legacy_combat_v6.hpp"
#include "legacy_forms_v5.hpp"
#include "legacy_combat_v5.hpp"
#include "legacy_combat_v3.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>
namespace historical {
using namespace digivice;
using namespace digivice::legacy_v12;
using digivice::legacy_v12::State;
using digivice::legacy_v12::Snapshot;
using digivice::legacy_v12::SnapshotStatus;
using digivice::legacy_v12::Species;
using digivice::legacy_v12::Phase;
using digivice::legacy_v12::Action;
using digivice::legacy_v12::Message;
using digivice::legacy_v12::Error;
using digivice::legacy_v12::CreatureMember;
using digivice::legacy_v12::CaptureResult;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)
bool same(const State& a,const State& b){Snapshot x,y;return encodeSnapshot(a,x)&&encodeSnapshot(b,y)&&!std::memcmp(x.bytes,y.bytes,sizeof(x.bytes));}
void step(State& s,Action a,std::uint32_t v=0){CHECK(apply(s,a,v)==Error::None);CHECK(isValid(s));}
void rejects(State& s,Action a,std::uint32_t v,Error error){const auto before=s;CHECK(apply(s,a,v)==error);CHECK(same(s,before));}
void fixture(State& s,unsigned form,unsigned level,unsigned bond=200){auto& m=*const_cast<CreatureMember*>(activeMember(s));s.journal[(form-1)/32]|=1u<<((form-1)%32);m.formId=form;m.xp=xpForLevel(level);m.level=s.level=level;m.bond=s.bond=bond;m.hp=s.hp=combat::formProfile(form,level).stats.maxHp;CHECK(isValid(s));}
void put(std::uint8_t* b,std::uint32_t n){for(unsigned i=0;i<4;++i)b[i]=static_cast<std::uint8_t>(n>>(8*i));}
std::uint32_t crc(const std::uint8_t* b,std::size_t n){std::uint32_t c=~0u;for(std::size_t i=0;i<n;++i){c^=b[i];for(unsigned j=0;j<8;++j)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
void seal(std::uint8_t* b,std::size_t n){put(b+n-4,crc(b,n-4));}
void legacyHeader(Snapshot& saved,unsigned version,unsigned rules) {
 saved.bytes[4]=static_cast<std::uint8_t>(version); put(saved.bytes+8,rules);
 saved.bytes[6]=static_cast<std::uint8_t>(kV13SnapshotSize-12);saved.bytes[7]=static_cast<std::uint8_t>((kV13SnapshotSize-12)>>8);
 seal(saved.bytes,kV13SnapshotSize);
}
void onboardingAndCare(){
 for(unsigned starter=1;starter<=8;++starter){auto s=newDevice(starter);CHECK(isValid(s));rejects(s,Action::Rest,0,Error::WrongPhase);step(s,Action::Hatch,starter);CHECK(s.sequence==1&&s.collection[0].formId==11+7*(starter-1)&&s.collection[0].xp==0&&s.level==1);rejects(s,Action::Hatch,starter,Error::AlreadyHatched);const auto rng=s.rngState;for(unsigned i=0;i<30;++i){step(s,Action::Feed);step(s,Action::Rest);}CHECK(s.collection[0].xp==0&&s.level==1&&s.rngState==rng);const auto bond=s.bond;for(unsigned i=0;i<20;++i){step(s,Action::Feed);step(s,Action::Rest);}CHECK(s.bond==bond);s.energy=s.collection[0].energy=0;s.mood=s.collection[0].mood=99;rejects(s,Action::Play,0,Error::LowEnergy);}
 CHECK(xpForLevel(1)==0&&xpForLevel(5)==400&&xpForLevel(10)==1800&&xpForLevel(15)==4200&&xpForLevel(20)==7600);
 for(unsigned l=1;l<=20;++l){CHECK(levelForXp(xpForLevel(l))==l);if(l>1)CHECK(levelForXp(xpForLevel(l)-1)==l-1);}
}
void evolution(){
 for(unsigned starter=1;starter<=8;++starter)for(unsigned branch: {1u,4u}){
  auto s=newDevice();step(s,Action::Hatch,starter);const auto root=s.collection[0].formId;
  rejects(s,Action::Evolve,root+branch,Error::EvolutionUnavailable);
  fixture(s,root,5,19);rejects(s,Action::Evolve,root+branch,Error::EvolutionUnavailable);
  s.bond=s.collection[0].bond=20;s.hp=s.collection[0].hp=17;const auto oldMax=combat::formProfile(root,5).stats.maxHp;
  step(s,Action::Evolve,root+branch);CHECK(s.hp==(17*combat::formProfile(root+branch,5).stats.maxHp+oldMax-1)/oldMax);
  rejects(s,Action::Evolve,root+branch,Error::EvolutionUnavailable);
  rejects(s,Action::Evolve,root+(branch==1?4:1),Error::EvolutionUnavailable);
  fixture(s,root+branch,10,50);step(s,Action::Evolve,root+branch+1);
  fixture(s,root+branch+1,15,80);step(s,Action::Evolve,root+branch+2);
  CHECK(forms::find(s.collection[0].formId)->stage==forms::Stage::Mega);CHECK(s.starterId==starter&&s.collection[0].id==1);
  Snapshot saved;CHECK(encodeSnapshot(s,saved));auto loaded=newGame();CHECK(decodeSnapshot(saved.bytes,sizeof(saved.bytes),loaded)==SnapshotStatus::Ok&&same(s,loaded));
  step(s,Action::Walk,100);rejects(s,Action::Evolve,root,Error::WrongPhase);
 }
}
void battles(){
 auto s=newDevice();step(s,Action::Hatch,1);step(s,Action::Walk,100);CHECK(s.wildLevel==1&&s.wildTurn==0);
 const auto physical=combat::resolveForms(4,1,11,1,combat::Move::Physical,combat::Defense::None).damage;
 const auto magic=combat::resolveForms(4,1,11,1,combat::Move::Magic,combat::Defense::None).damage;
 const auto hp=s.hp;step(s,Action::Attack);CHECK(s.hp==hp-physical&&s.wildTurn==1);step(s,Action::Attack);CHECK(s.hp==hp-physical-magic&&s.wildTurn==2);
 Snapshot snap;CHECK(encodeSnapshot(s,snap));State recovered;CHECK(decodeSnapshot(snap.bytes,sizeof(snap.bytes),recovered)==SnapshotStatus::Ok&&same(s,recovered));
 s.energy=s.collection[0].energy=0;rejects(s,Action::Heavy,0,Error::LowEnergy);step(s,Action::Attack);
 unsigned longest=0;
 for(unsigned starter=1;starter<=8;++starter)for(unsigned level: {1u,5u,10u,15u,20u})for(unsigned seed=1;seed<=16;++seed){
  auto a=newDevice(seed);step(a,Action::Hatch,starter);fixture(a,a.collection[0].formId,level);step(a,Action::Mode,1);step(a,Action::Walk,100);const auto before=a;auto b=a;autobattle::Trace trace;
  const auto xp=a.collection[0].xp;CHECK(applyAuto(a,&trace)==Error::None);CHECK(apply(b,Action::Auto)==Error::None&&same(a,b));CHECK(isValid(a)&&a.phase==Phase::Home&&a.sequence==before.sequence+1);CHECK(trace.count>0&&trace.count<=48);if(trace.count>longest)longest=static_cast<unsigned>(trace.count);
  CHECK(trace.playerFormId==before.collection[0].formId&&trace.enemyLevel==before.wildLevel);
  const auto reward=a.lastAutoOutcome==autobattle::Outcome::Retreated?0u:20+6*before.wildLevel;
  CHECK(a.collection[0].xp==(xp+reward>kMaxXp?kMaxXp:xp+reward));
  if(a.collectionCount>1)CHECK(a.collection[1].capturedAtSequence==a.sequence&&a.collection[1].level==before.wildLevel);
  const auto playerMax=combat::formProfile(trace.playerFormId,trace.playerLevel).stats.maxHp;
  for(std::size_t i=0;i<trace.count;++i)CHECK(trace.steps[i].playerHpBefore<=playerMax&&trace.steps[i].playerHpAfter<=playerMax);
  rejects(a,Action::Auto,0,Error::WrongPhase);
 }
 std::printf("Auto640 samples: longest %u turns\n",longest);
}
void migration(){
 for(unsigned starter=0;starter<=8;++starter)for(unsigned tier=1;tier<=3;++tier){
  auto old=starter?legacy_v3::newDevice():legacy_v3::newGame();if(starter)CHECK(legacy_v3::apply(old,legacy_v3::Action::Hatch,starter)==legacy_v3::Error::None);
  while(old.level<tier)CHECK(legacy_v3::apply(old,legacy_v3::Action::Feed)==legacy_v3::Error::None);
  old.hp=old.collection[0].hp=17;
  legacy_v3::Snapshot saved;CHECK(legacy_v3::encodeSnapshot(old,saved));auto s=newGame();CHECK(decodeSnapshot(saved.bytes,sizeof(saved.bytes),s)==SnapshotStatus::Migrated);
  CHECK(s.hp==(17*combat::formProfile(s.collection[0].formId,s.level).stats.maxHp+legacy_v3::combat::profile(static_cast<unsigned>(old.collection[0].species),old.level).stats.maxHp-1)/legacy_v3::combat::profile(static_cast<unsigned>(old.collection[0].species),old.level).stats.maxHp&&s.sequence==old.sequence&&s.rngState==old.rngState&&s.bond==old.bond&&s.starterId==old.starterId);
  CHECK(s.level==(tier==1?1u:tier==2?5u:10u)&&s.collection[0].xp==xpForLevel(s.level));
  const auto a=combat::formProfile(s.collection[0].formId,s.level).stats; const auto b=legacy_v3::combat::profile(static_cast<unsigned>(old.collection[0].species),tier).stats;
  if(s.collection[0].formId!=3&&s.collection[0].formId!=7)CHECK(a.maxHp==b.maxHp&&a.attack==b.attack&&a.defense==b.defense&&a.magic==b.magic&&a.resistance==b.resistance);
  CHECK(!std::strcmp(memberName(s.collection[0]),legacy_v3::memberName(old.collection[0])));
 }
 auto egg=legacy_v3::newDevice();legacy_v3::Snapshot old;CHECK(legacy_v3::encodeSnapshot(egg,old));State restored;CHECK(decodeSnapshot(old.bytes,sizeof(old.bytes),restored)==SnapshotStatus::Migrated&&restored.phase==Phase::Egg);
 auto prior=legacy_v3::newGame();CHECK(legacy_v3::apply(prior,legacy_v3::Action::Walk,100)==legacy_v3::Error::None);CHECK(legacy_v3::apply(prior,legacy_v3::Action::Attack)==legacy_v3::Error::None);CHECK(legacy_v3::encodeSnapshot(prior,old));
 for(unsigned version: {4u,5u,6u}){const auto size=version==4?404u:version==5?412u:428u;auto copy=old;copy.bytes[4]=static_cast<std::uint8_t>(version);copy.bytes[6]=static_cast<std::uint8_t>(size-12);copy.bytes[7]=static_cast<std::uint8_t>((size-12)>>8);seal(copy.bytes,size);CHECK(decodeSnapshot(copy.bytes,size,restored)==SnapshotStatus::Migrated);CHECK(restored.wildHp==prior.wildHp&&restored.hp==prior.hp&&restored.wildLevel==1&&restored.wildTurn==0);}
}
void durabilityAndBudgets(){
 auto s=newDevice();step(s,Action::Hatch,8);fixture(s,60,5,200);Snapshot saved;CHECK(encodeSnapshot(s,saved));CHECK(sizeof(saved)==652&&sizeof(State)==624);
 for(unsigned i=0;i<sizeof(saved.bytes);++i){auto bad=saved;bad.bytes[i]^=1;auto target=s;CHECK(decodeSnapshot(bad.bytes,sizeof(bad.bytes),target)!=SnapshotStatus::Ok&&same(target,s));}
 for(const auto offset:{148u,152u,492u}){auto bad=saved;put(bad.bytes+offset,0xffffffffu);seal(bad.bytes,sizeof(bad.bytes));auto target=s;CHECK(decodeSnapshot(bad.bytes,sizeof(bad.bytes),target)==SnapshotStatus::InvalidState&&same(target,s));}
 auto forged=saved; put(forged.bytes+48,0); put(forged.bytes+136,0); put(forged.bytes+152,61); seal(forged.bytes,sizeof(forged.bytes));
 auto preserved=s; CHECK(decodeSnapshot(forged.bytes,sizeof(forged.bytes),preserved)==SnapshotStatus::InvalidState&&same(s,preserved));
 std::size_t largest=0;
 for(unsigned starter=1;starter<=8;++starter){s=newDevice();step(s,Action::Hatch,starter);fixture(s,s.collection[0].formId,20,200);s.sequence=UINT32_MAX;s.encounters=UINT32_MAX/100;s.steps=s.encounters*100;s.captures=7;s.collectionCount=8;s.nextMemberId=9;for(unsigned id=1;id<=forms::kFormCount;++id)s.journal[(id-1)/32]|=1u<<((id-1)%32);
  for(unsigned i=1;i<8;++i){auto& m=s.collection[i];m={i+1,Species::Rill,1,100,100,100,200,20,i,7600,7};}
  CHECK(isValid(s));char json[kJsonCapacity];const auto n=writeJson(s,json,sizeof(json));CHECK(n&&n<sizeof(json));if(n>largest)largest=n;char small[32];CHECK(!writeJson(s,small,sizeof(small))&&small[0]=='\0');
  CHECK(std::strstr(json,"\"maxLevel\":20")&&std::strstr(json,"\"evolution\":{\"options\":["));
 }
 std::printf("State%zu / snapshot%zu / widest sampled JSON%zu of%zu\n",sizeof(State),sizeof(Snapshot),largest,kJsonCapacity);
}

void maximumJson(){
 std::uint32_t longest=1;std::size_t width=0;
 for(unsigned id=1;id<=forms::kFormCount;++id){const auto* f=forms::find(id);char profile[combat::kProfileJsonCapacity];
  const auto n=combat::writeFormProfileJson(id,20,profile,sizeof(profile))+std::strlen(f->name)+std::strlen(combat::speciesName(f->lineage));
  if(n>width){width=n;longest=id;}
 }
 std::size_t largest=0;
 for(unsigned activeForm=1;activeForm<=forms::kFormCount;++activeForm){
  auto s=newGame();s.sequence=UINT32_MAX;s.encounters=UINT32_MAX/100;s.steps=s.encounters*100;s.captures=s.encounters;
  s.collectionCount=8;s.nextMemberId=s.captures+2;s.activeCreatureId=s.nextMemberId-8;
  for(unsigned id=1;id<=forms::kFormCount;++id)s.journal[(id-1)/32]|=1u<<((id-1)%32);
  for(unsigned i=0;i<8;++i){const auto id=i?longest:activeForm;const auto* f=forms::find(id);s.collection[i]={s.nextMemberId-8+i,static_cast<Species>(f->lineage),combat::formProfile(id,20).stats.maxHp,100,100,100,200,20,UINT32_MAX-8+i,7600,id};}
  const auto& m=*activeMember(s);s.hp=m.hp;s.energy=m.energy;s.fullness=m.fullness;s.mood=m.mood;s.bond=m.bond;s.level=m.level;
  s.phase=Phase::Encounter;s.wildFormId=longest;s.wildSpecies=static_cast<Species>(forms::find(longest)->lineage);s.wildLevel=20;s.wildTurn=999;s.wildRules=12;
  s.wildHp=s.wildMaxHp=combat::formProfile(longest,20).stats.maxHp;s.captureAttempts=2;s.cardUsed=true;s.attackBoost=5;
  s.explorationSteps=UINT32_MAX;s.walkingEncounters=1;s.steps=(s.encounters-1)*100;s.encounterRng=UINT32_MAX;s.encounterTarget=280;s.encounterProgress=279;s.encounterRate=EncounterRate::Relaxed;
  auto offered=newDevice();step(offered,Action::StarterOfferSeed,UINT32_MAX);s.starterOfferSeed=offered.starterOfferSeed;for(unsigned i=0;i<3;++i)s.starterOffers[i]=offered.starterOffers[i];
  s.lastCapture={UINT32_MAX,longest,90,3,20,CaptureResult::Escaped};
  CHECK(isValid(s));char json[kJsonCapacity];const auto n=writeJson(s,json,sizeof(json));CHECK(n&&n<sizeof(json));if(n>largest)largest=n;
 }
 std::printf("Maximum-width sweep276 active forms/full8/fulljournal/large counters/encounter: %zu of%zu bytes\n",largest,kJsonCapacity);
}

void stableIdsAndJournal(){
 auto s=newDevice();step(s,Action::Hatch,1);fixture(s,11,20);step(s,Action::Mode,1);
 for(unsigned n=0;n<200 && s.collectionCount<3;++n){while(s.hp<combat::formProfile(activeMember(s)->formId,s.level).stats.maxHp||s.energy<100)step(s,Action::Rest);step(s,Action::Walk,100);step(s,Action::Auto);}
 CHECK(s.collectionCount>=3);if(s.collectionCount<3)return;
 const auto released=s.collection[1],selected=s.collection[2];const auto next=s.nextMemberId;
 step(s,Action::Release,released.id);CHECK(!findMember(s,released.id)&&hasObtained(s,released.formId)&&s.nextMemberId==next);
 rejects(s,Action::Select,released.id,Error::UnknownMember);step(s,Action::Select,selected.id);CHECK(s.hp==selected.hp);
 rejects(s,Action::Release,selected.id,Error::ActiveMemberRelease);step(s,Action::Release,1);CHECK(s.starterId==1&&!findMember(s,1)&&s.activeCreatureId==selected.id);
 Snapshot snap;CHECK(encodeSnapshot(s,snap));State restored;CHECK(decodeSnapshot(snap.bytes,sizeof(snap.bytes),restored)==SnapshotStatus::Ok&&same(s,restored));
 const auto count=s.collectionCount;
 for(unsigned n=0;n<200&&s.collectionCount==count;++n){while(s.hp<combat::formProfile(activeMember(s)->formId,s.level).stats.maxHp||s.energy<100)step(s,Action::Rest);step(s,Action::Walk,100);step(s,Action::Auto);}
 CHECK(s.collectionCount>count&&s.collection[s.collectionCount-1].id==next);
}
void captureEveryForm(){
 bool obtained[kJournalCapacity]{};unsigned distinct=0;
 // Rules 12 (this historical namespace) draws from the frozen 276-form roster only.
 const unsigned capturable=forms::kRules18FormCount;
 for(unsigned encounter=2;encounter<100000&&distinct<capturable;++encounter){
  const auto prospective=selectWildForm(encounter,kDevelopmentSeed,14,20);
  if(obtained[prospective-1])continue;
  auto s=newDevice();step(s,Action::Hatch,1);fixture(s,14,20);
  s.sequence=encounter+10;s.encounters=encounter-1;s.steps=(encounter-1)*100;CHECK(isValid(s));
  step(s,Action::Walk,100);const auto id=s.wildFormId;const auto* f=forms::find(id);
  // Use real encounter creation and actual native attacks, never inject rival HP.
  for(unsigned turn=0;turn<30&&s.phase==Phase::Encounter&&s.wildHp>s.wildMaxHp/2;++turn){
   Action selected=Action::Attack;unsigned best=0;
   for(const auto action:{Action::Attack,Action::Magic,Action::Heavy}){
    if(action==Action::Heavy&&s.energy<6)continue;
    const auto move=action==Action::Attack?combat::Move::Physical:action==Action::Magic?combat::Move::Magic:combat::Move::Heavy;
    const auto hit=combat::resolveForms(activeMember(s)->formId,s.level,id,s.wildLevel,move,wildGuard(s));
    if(!hit.reflected&&hit.damage<s.wildHp&&hit.damage>best){best=hit.damage;selected=action;}
   }
   CHECK(best);if(!best)break;step(s,selected);
  }
  CHECK(s.phase==Phase::Encounter&&s.wildHp<=s.wildMaxHp/2);
  if(s.phase!=Phase::Encounter||s.wildHp>s.wildMaxHp/2)continue;
  step(s,Action::Capture);CHECK(s.phase==Phase::Home&&s.collectionCount==2&&s.collection[1].formId==id&&s.collection[1].bond==f->minBond&&hasObtained(s,id));
  if(s.collectionCount<2)continue;
  obtained[id-1]=true;++distinct;step(s,Action::Select,2);CHECK(activeMember(s)->formId==id&&s.level==20);
  Snapshot snap;CHECK(encodeSnapshot(s,snap));State restored;CHECK(decodeSnapshot(snap.bytes,sizeof(snap.bytes),restored)==SnapshotStatus::Ok&&same(s,restored));
 }
 for(unsigned id=1;id<=forms::kFormCount;++id)CHECK(obtained[id-1]==(id<=forms::kRules18FormCount));
}

void poolsAndGuard(){
 for(unsigned form=1;form<=forms::kFormCount;++form) if(forms::combatTier(form)<forms::CombatTier::Rookie) {
  bool rookie=false;
  for(unsigned n=2;n<32770;++n){const auto id=selectWildForm(n,12345,form,1);if(forms::find(id)->lineage>=5&&forms::find(id)->lineage<=12&&forms::find(id)->stage==forms::Stage::Rookie)rookie=true;}
  CHECK(rookie);
 }
 bool seen[kJournalCapacity]{};
 for(unsigned n=2;n<32770;++n){const auto id=selectWildForm(n,12345,14,20);CHECK(id&&id<=forms::kFormCount);seen[id-1]=true;}
 for(unsigned id=1;id<=forms::kFormCount;++id)CHECK(seen[id-1]==(id<=forms::kRules18FormCount)); // rules 12 pool is frozen at 276
 for(unsigned n=2;n<100;++n){const auto id=selectWildForm(n,77,11,20);CHECK(forms::combatTier(id)<=forms::CombatTier::Rookie);}
 auto s=newDevice();step(s,Action::Hatch,1);step(s,Action::Walk,100);CHECK(wildGuard(s)==combat::Defense::Brace);
 step(s,Action::Card,1);const auto boost=s.attackBoost;s.wildTurn=2;CHECK(isValid(s));
 const auto hp=s.hp,energy=s.energy,wild=s.wildHp;
 const auto reflected=combat::resolveForms(11,1,s.wildFormId,1,combat::Move::Heavy,combat::Defense::Counter).damage;
 step(s,Action::Heavy);CHECK(s.hp==hp-reflected&&s.wildHp==wild&&s.attackBoost==boost&&s.energy==energy-6&&s.wildTurn==3);
 s=newDevice();step(s,Action::Hatch,1);step(s,Action::Walk,100);step(s,Action::Card,2);s.wildTurn=2;const auto before=s.hp;step(s,Action::Heavy);CHECK(s.hp==before&&s.shield==12-reflected);
 for(unsigned seed=0;seed<64;++seed){s=newDevice(seed);step(s,Action::Hatch,1);step(s,Action::Mode,1);step(s,Action::Walk,100);autobattle::Trace trace;CHECK(applyAuto(s,&trace)==Error::None);for(std::size_t i=0;i<trace.count;++i)CHECK(!trace.steps[i].reflected);CHECK(trace.includeFormIds);}
}
void rules4Migration(){
 for(unsigned starter=0;starter<=8;++starter)for(unsigned seed=0;seed<8;++seed){
  auto old=starter?legacy_v4::newDevice(seed):legacy_v4::newGame(seed);
  if(starter)CHECK(legacy_v4::apply(old,legacy_v4::Action::Hatch,starter)==legacy_v4::Error::None);
  CHECK(legacy_v4::apply(old,legacy_v4::Action::Mode,1)==legacy_v4::Error::None);
  CHECK(legacy_v4::apply(old,legacy_v4::Action::Walk,100)==legacy_v4::Error::None);
  legacy_v4::Snapshot saved;CHECK(legacy_v4::encodeSnapshot(old,saved));State now;CHECK(decodeSnapshot(saved.bytes,sizeof(saved.bytes),now)==SnapshotStatus::Migrated);
  CHECK(now.wildRules==4&&wildGuard(now)==combat::Defense::None&&now.wildFormId==4);
  autobattle::Trace a,b;CHECK(legacy_v4::applyAuto(old,&a)==legacy_v4::Error::None);CHECK(applyAuto(now,&b)==Error::None);
  legacy_v4::Snapshot terminal;CHECK(legacy_v4::encodeSnapshot(old,terminal));State expected;CHECK(decodeSnapshot(terminal.bytes,sizeof(terminal.bytes),expected)==SnapshotStatus::Migrated&&same(now,expected));
  char ta[autobattle::kTraceJsonCapacity],tb[autobattle::kTraceJsonCapacity];CHECK(autobattle::writeJson(a,ta,sizeof(ta))&&autobattle::writeJson(b,tb,sizeof(tb))&&!std::strcmp(ta,tb));
 }
}

State capturedFixture(unsigned form,unsigned level,unsigned bond) {
 auto s=newGame();s.sequence=2;s.steps=100;s.encounters=s.captures=1;s.nextMemberId=3;s.collectionCount=2;s.activeCreatureId=2;
 const auto* f=forms::find(form);
 s.collection[1]={2,static_cast<Species>(f->lineage),combat::formProfile(form,level).stats.maxHp,80,70,80,bond,level,1,xpForLevel(level),form};
 const auto& m=s.collection[1];s.hp=m.hp;s.energy=m.energy;s.fullness=m.fullness;s.mood=m.mood;s.bond=m.bond;s.level=m.level;
 s.journal[(form-1)/32]|=1u<<((form-1)%32);CHECK(isValid(s));return s;
}
void graphEdges() {
 unsigned edges=0,cross=0,babies=0,maxBabyFights=0;
 for(unsigned id=1;id<=forms::kFormCount;++id) for(unsigned index=0;index<2;++index) if(const auto* edge=forms::outgoing(id,index)) {
  ++edges;const auto* parent=forms::find(id);const auto* child=forms::find(edge->to);
  CHECK(edge->from==id&&edge->minLevel>=child->minLevel&&edge->minBond>=child->minBond);
  const auto level=edge->minLevel>parent->minLevel?edge->minLevel:parent->minLevel;
  const auto bond=edge->minBond>parent->minBond?edge->minBond:parent->minBond;
  auto s=capturedFixture(id,level,bond);const auto before=s;const auto oldMax=combat::formProfile(id,level).stats.maxHp;
  s.hp=s.collection[1].hp=17;step(s,Action::Evolve,edge->to);
  CHECK(s.collection[1].formId==edge->to&&static_cast<unsigned>(s.collection[1].species)==child->lineage);
  CHECK(s.hp==(17*combat::formProfile(edge->to,level).stats.maxHp+oldMax-1)/oldMax);
  CHECK(s.collection[1].id==before.collection[1].id&&s.collection[1].xp==before.collection[1].xp&&s.nextMemberId==before.nextMemberId&&s.rngState==before.rngState);
  CHECK(hasObtained(s,id)&&hasObtained(s,edge->to));
  Snapshot encoded;CHECK(encodeSnapshot(s,encoded));State restored;CHECK(decodeSnapshot(encoded.bytes,sizeof(encoded.bytes),restored)==SnapshotStatus::Ok&&same(s,restored));
  if(edge->minBond>parent->minBond){auto low=capturedFixture(id,level,edge->minBond-1);rejects(low,Action::Evolve,edge->to,Error::EvolutionUnavailable);}
  if(edge->minLevel>parent->minLevel){auto low=capturedFixture(id,edge->minLevel-1,bond);rejects(low,Action::Evolve,edge->to,Error::EvolutionUnavailable);}
  if(parent->lineage!=child->lineage)++cross;
  if(forms::combatTier(id)<forms::CombatTier::Rookie&&edge->minLevel==1){
   ++babies;auto care=capturedFixture(id,1,parent->minBond);
   // Useful Care adds bond but does not farm it once every need is full.
   for(unsigned turn=0;turn<12;++turn){step(care,Action::Feed);step(care,Action::Play);step(care,Action::Rest);}
   const auto homeBond=care.bond;
   step(care,Action::Play);step(care,Action::Rest);step(care,Action::Feed);
   CHECK(care.bond==homeBond&&care.level==1&&activeMember(care)->xp==0);
   unsigned fights=0,rests=0;
   while(care.bond<edge->minBond&&fights<64){
    step(care,Action::Explore,1000);++fights;unsigned attacks=0;
    while(care.phase==Phase::Encounter&&attacks++<256)step(care,Action::Attack);
    CHECK(care.phase==Phase::Home);
    const auto recovery=recoveryRestCount(care);for(unsigned n=0;n<recovery;++n){step(care,Action::Rest);++rests;}
   }
   CHECK(care.bond>=edge->minBond);if(fights>maxBabyFights)maxBabyFights=fights;
   if(babies<=2)std::printf("Baby route %s -> %s: useful Home Care bond%u, then%u encounters/%u recovery Rests -> bond%u level%u (gate Lv%u/bond%u)\n",parent->name,child->name,homeBond,fights,rests,care.bond,care.level,edge->minLevel,edge->minBond);
   step(care,Action::Evolve,edge->to);
  }
 }
 CHECK(edges>=99&&cross>0&&babies>0);std::printf("Graph actions: %u edges / %u cross-lineage / %u baby routes through useful Care + encounter/recovery (max%u encounters)\n",edges,cross,babies,maxBabyFights);
}
void founderCrossLineage() {
 unsigned count=0;
 for(unsigned starter=1;starter<=8;++starter){const auto root=11+7*(starter-1);
  for(unsigned id=1;id<=forms::kFormCount;++id) if(forms::find(id)->lineage!=combat::starterSpecies(starter)&&forms::canReach(root,id)) {
   ++count;auto s=newDevice();step(s,Action::Hatch,starter);s.collection[0].species=static_cast<Species>(forms::find(id)->lineage);fixture(s,id,20);
   CHECK(s.starterId==starter&&s.collection[0].id==1&&s.collection[0].capturedAtSequence==0);
   Snapshot snap;CHECK(encodeSnapshot(s,snap));State recovered;CHECK(decodeSnapshot(snap.bytes,sizeof(snap.bytes),recovered)==SnapshotStatus::Ok&&same(s,recovered));
   // Re-labelling a new cross-lineage founder as old8 must not pass old invariants.
   auto forged=snap;legacyHeader(forged,8,5);
   const auto before=recovered;CHECK(decodeSnapshot(forged.bytes,kV13SnapshotSize,recovered)==SnapshotStatus::InvalidState&&same(before,recovered));
  }
 }
 CHECK(count>0);std::printf("Founder cross-lineage restore cases: %u\n",count);
}
void rules5Migration() {
 namespace old=legacy_v5;
 for(unsigned id=1;id<=old::forms::kFormCount;++id){
  const auto* f=old::forms::find(id);auto prior=old::newGame();prior.sequence=2;prior.steps=100;prior.encounters=prior.captures=1;prior.nextMemberId=3;prior.collectionCount=2;prior.activeCreatureId=2;
  prior.collection[1]={2,static_cast<old::Species>(f->lineage),old::combat::formProfile(id,20).stats.maxHp,80,70,80,200,20,1,7600,id};
  prior.hp=prior.collection[1].hp;prior.energy=80;prior.fullness=70;prior.mood=80;prior.bond=200;prior.level=20;prior.journal[(id-1)/32]|=1u<<((id-1)%32);
  CHECK(old::isValid(prior));old::Snapshot before;CHECK(old::encodeSnapshot(prior,before));State now;
  CHECK(decodeSnapshot(before.bytes,sizeof(before.bytes),now)==SnapshotStatus::Migrated);
  Snapshot after;CHECK(encodeSnapshot(now,after));if(id==3||id==7){put(before.bytes+32,now.hp);put(before.bytes+164,now.collection[1].hp);}
  CHECK(!std::memcmp(before.bytes+12,after.bytes+12,kV13SnapshotSize-16));
  CHECK(static_cast<unsigned>(activeMember(now)->species)==f->lineage&&activeMember(now)->formId==id&&now.journal[(id-1)/32]==prior.journal[(id-1)/32]);
 }
 for(unsigned starter=0;starter<=8;++starter)for(unsigned seed=0;seed<8;++seed){
  auto prior=starter?old::newDevice(seed):old::newGame(seed);
  if(starter)CHECK(old::apply(prior,old::Action::Hatch,starter)==old::Error::None);
  CHECK(old::apply(prior,old::Action::Mode,1)==old::Error::None);CHECK(old::apply(prior,old::Action::Walk,100)==old::Error::None);
  old::Snapshot before;CHECK(old::encodeSnapshot(prior,before));State now;CHECK(decodeSnapshot(before.bytes,sizeof(before.bytes),now)==SnapshotStatus::Migrated&&now.wildRules==5);
  autobattle::Trace a,b;CHECK(old::applyAuto(prior,&a)==old::Error::None);CHECK(applyAuto(now,&b)==Error::None);
  old::Snapshot terminal;CHECK(old::encodeSnapshot(prior,terminal));State expected;CHECK(decodeSnapshot(terminal.bytes,sizeof(terminal.bytes),expected)==SnapshotStatus::Migrated&&same(now,expected));
  char ta[autobattle::kTraceJsonCapacity],tb[autobattle::kTraceJsonCapacity];CHECK(autobattle::writeJson(a,ta,sizeof(ta))&&autobattle::writeJson(b,tb,sizeof(tb))&&!std::strcmp(ta,tb));
 }
}
void rules6Migration() {
 namespace old=legacy_v6;
 for(unsigned id=1;id<=old::forms::kFormCount;++id){
  const auto* f=old::forms::find(id);auto prior=old::newGame();prior.sequence=2;prior.steps=100;prior.encounters=prior.captures=1;prior.nextMemberId=3;prior.collectionCount=2;prior.activeCreatureId=2;
  prior.collection[1]={2,static_cast<old::Species>(f->lineage),old::combat::formProfile(id,20).stats.maxHp,80,70,80,200,20,1,7600,id};
  prior.hp=prior.collection[1].hp;prior.energy=80;prior.fullness=70;prior.mood=80;prior.bond=200;prior.level=20;prior.journal[(id-1)/32]|=1u<<((id-1)%32);
  CHECK(old::isValid(prior));old::Snapshot before;CHECK(old::encodeSnapshot(prior,before));State now;
  CHECK(decodeSnapshot(before.bytes,sizeof(before.bytes),now)==SnapshotStatus::Migrated);
  Snapshot after;CHECK(encodeSnapshot(now,after));if(id==3||id==7){put(before.bytes+32,now.hp);put(before.bytes+164,now.collection[1].hp);}
  CHECK(!std::memcmp(before.bytes+12,after.bytes+12,kV13SnapshotSize-16));
  CHECK(static_cast<unsigned>(activeMember(now)->species)==f->lineage&&activeMember(now)->formId==id&&now.journal[(id-1)/32]==prior.journal[(id-1)/32]);
 }
 for(unsigned starter=0;starter<=8;++starter)for(unsigned seed=0;seed<8;++seed){
  auto prior=starter?old::newDevice(seed):old::newGame(seed);
  if(starter)CHECK(old::apply(prior,old::Action::Hatch,starter)==old::Error::None);
  CHECK(old::apply(prior,old::Action::Mode,1)==old::Error::None);CHECK(old::apply(prior,old::Action::Walk,100)==old::Error::None);
  old::Snapshot before;CHECK(old::encodeSnapshot(prior,before));State now;CHECK(decodeSnapshot(before.bytes,sizeof(before.bytes),now)==SnapshotStatus::Migrated&&now.wildRules==6);
  autobattle::Trace a,b;CHECK(old::applyAuto(prior,&a)==old::Error::None);CHECK(a.combatRulesVersion==6);CHECK(applyAuto(now,&b)==Error::None);
  old::Snapshot terminal;CHECK(old::encodeSnapshot(prior,terminal));State expected;CHECK(decodeSnapshot(terminal.bytes,sizeof(terminal.bytes),expected)==SnapshotStatus::Migrated&&same(now,expected));
  char ta[autobattle::kTraceJsonCapacity],tb[autobattle::kTraceJsonCapacity];CHECK(autobattle::writeJson(a,ta,sizeof(ta))&&autobattle::writeJson(b,tb,sizeof(tb))&&!std::strcmp(ta,tb));
 }
}
void rules7Migration() {
 namespace old=legacy_v7;
 for(unsigned id=1;id<=old::forms::kFormCount;++id){
  const auto* f=old::forms::find(id);auto prior=old::newGame();prior.sequence=2;prior.steps=100;prior.encounters=prior.captures=1;prior.nextMemberId=3;prior.collectionCount=2;prior.activeCreatureId=2;
  prior.collection[1]={2,static_cast<old::Species>(f->lineage),old::combat::formProfile(id,20).stats.maxHp,80,70,80,200,20,1,7600,id};
  prior.hp=prior.collection[1].hp;prior.energy=80;prior.fullness=70;prior.mood=80;prior.bond=200;prior.level=20;prior.journal[(id-1)/32]|=1u<<((id-1)%32);
  CHECK(old::isValid(prior));old::Snapshot before;CHECK(old::encodeSnapshot(prior,before));State now;
  CHECK(decodeSnapshot(before.bytes,sizeof(before.bytes),now)==SnapshotStatus::Migrated);
  Snapshot after;CHECK(encodeSnapshot(now,after));if(id==3||id==7){put(before.bytes+32,now.hp);put(before.bytes+164,now.collection[1].hp);}
  CHECK(!std::memcmp(before.bytes+12,after.bytes+12,kV13SnapshotSize-16));
  CHECK(static_cast<unsigned>(activeMember(now)->species)==f->lineage&&activeMember(now)->formId==id&&now.journal[(id-1)/32]==prior.journal[(id-1)/32]);
 }
 for(unsigned starter=0;starter<=8;++starter)for(unsigned seed=0;seed<8;++seed){
  auto prior=starter?old::newDevice(seed):old::newGame(seed);
  if(starter)CHECK(old::apply(prior,old::Action::Hatch,starter)==old::Error::None);
  CHECK(old::apply(prior,old::Action::Mode,1)==old::Error::None);CHECK(old::apply(prior,old::Action::Walk,100)==old::Error::None);
  old::Snapshot before;CHECK(old::encodeSnapshot(prior,before));State now;CHECK(decodeSnapshot(before.bytes,sizeof(before.bytes),now)==SnapshotStatus::Migrated&&now.wildRules==7);
  autobattle::Trace a,b;CHECK(old::applyAuto(prior,&a)==old::Error::None);CHECK(a.combatRulesVersion==7);CHECK(applyAuto(now,&b)==Error::None);
  old::Snapshot terminal;CHECK(old::encodeSnapshot(prior,terminal));State expected;CHECK(decodeSnapshot(terminal.bytes,sizeof(terminal.bytes),expected)==SnapshotStatus::Migrated&&same(now,expected));
  char ta[autobattle::kTraceJsonCapacity],tb[autobattle::kTraceJsonCapacity];CHECK(autobattle::writeJson(a,ta,sizeof(ta))&&autobattle::writeJson(b,tb,sizeof(tb))&&!std::strcmp(ta,tb));
 }
}
void newGraphEpochGuards() {
 unsigned added=0, founderPaths=0, sheetRoutes=0;
 for(unsigned id=1;id<=forms::kFormCount;++id)for(unsigned n=0;n<2;++n)if(const auto* edge=forms::outgoing(id,n)) {
  bool existed=false;for(unsigned j=0;j<2;++j)if(const auto* old=legacy_v6::forms::outgoing(id,j))if(old->to==edge->to)existed=true;
  if(existed)continue;
  const bool parentKnown=legacy_v6::forms::find(id);const bool childKnown=legacy_v6::forms::find(edge->to);
  if(!parentKnown){++sheetRoutes;continue;}
  const auto* f=forms::find(id);const auto level=edge->minLevel>f->minLevel?edge->minLevel:f->minLevel;
  const auto bond=edge->minBond>f->minBond?edge->minBond:f->minBond;
  auto now=capturedFixture(id,level,bond);Snapshot saved;CHECK(encodeSnapshot(now,saved));
  legacyHeader(saved,9,6);
  legacy_v6::State prior;CHECK(legacy_v6::decodeSnapshot(saved.bytes,kV13SnapshotSize,prior)==legacy_v6::SnapshotStatus::Ok);
  legacy_v6::Snapshot a,b;CHECK(legacy_v6::encodeSnapshot(prior,a));
  const auto rejected=legacy_v6::apply(prior,legacy_v6::Action::Evolve,edge->to);
  CHECK(childKnown?rejected==legacy_v6::Error::EvolutionUnavailable:rejected==legacy_v6::Error::InvalidValue);
  CHECK(legacy_v6::encodeSnapshot(prior,b)&&!std::memcmp(a.bytes,b.bytes,sizeof(a.bytes)));
  if(childKnown){++added;step(now,Action::Evolve,edge->to);}else ++sheetRoutes;
 }
 for(unsigned starter=1;starter<=8;++starter){const auto root=11+7*(starter-1);
  for(unsigned id=1;id<=forms::kFormCount;++id)if(forms::canReach(root,id)&&!legacy_v6::forms::canReach(root,id)) {
   ++founderPaths;auto s=newDevice();step(s,Action::Hatch,starter);
   s.collection[0].species=static_cast<Species>(forms::find(id)->lineage);fixture(s,id,20);
   Snapshot bytes;CHECK(encodeSnapshot(s,bytes));legacyHeader(bytes,9,6);
   const auto before=s;CHECK(decodeSnapshot(bytes.bytes,kV13SnapshotSize,s)==SnapshotStatus::InvalidState&&same(s,before));
  }
 }
 auto s=newGame();step(s,Action::Walk,100);Snapshot bytes;CHECK(encodeSnapshot(s,bytes));
 legacyHeader(bytes,9,6);const auto before=s;
 CHECK(decodeSnapshot(bytes.bytes,kV13SnapshotSize,s)==SnapshotStatus::InvalidState&&same(s,before));
 CHECK(added>=16&&founderPaths>0&&sheetRoutes==82);
 std::printf("Rules6 freeze: 276 progress-byte migrations, 72 active Auto continuations, %u historical routes rejected, %u sheet routes unavailable, %u future founder paths rejected\n",added,sheetRoutes,founderPaths);
}
void newRules8EpochGuards() {
 unsigned added=0, founderPaths=0, sheetRoutes=0;
 for(unsigned id=1;id<=forms::kFormCount;++id)for(unsigned n=0;n<2;++n)if(const auto* edge=forms::outgoing(id,n)) {
  bool existed=false;for(unsigned j=0;j<2;++j)if(const auto* old=legacy_v7::forms::outgoing(id,j))if(old->to==edge->to)existed=true;
  if(existed)continue;
  const bool parentKnown=legacy_v7::forms::find(id);const bool childKnown=legacy_v7::forms::find(edge->to);
  if(!parentKnown){++sheetRoutes;continue;}
  const auto* f=forms::find(id);const auto level=edge->minLevel>f->minLevel?edge->minLevel:f->minLevel;
  const auto bond=edge->minBond>f->minBond?edge->minBond:f->minBond;
  auto now=capturedFixture(id,level,bond);Snapshot saved;CHECK(encodeSnapshot(now,saved));
  legacyHeader(saved,10,7);
  legacy_v7::State prior;CHECK(legacy_v7::decodeSnapshot(saved.bytes,kV13SnapshotSize,prior)==legacy_v7::SnapshotStatus::Ok);
  legacy_v7::Snapshot a,b;CHECK(legacy_v7::encodeSnapshot(prior,a));
  const auto rejected=legacy_v7::apply(prior,legacy_v7::Action::Evolve,edge->to);
  CHECK(childKnown?rejected==legacy_v7::Error::EvolutionUnavailable:rejected==legacy_v7::Error::InvalidValue);
  CHECK(legacy_v7::encodeSnapshot(prior,b)&&!std::memcmp(a.bytes,b.bytes,sizeof(a.bytes)));
  if(childKnown){++added;step(now,Action::Evolve,edge->to);}else ++sheetRoutes;
 }
 for(unsigned starter=1;starter<=8;++starter){const auto root=11+7*(starter-1);
  for(unsigned id=1;id<=forms::kFormCount;++id)if(forms::canReach(root,id)&&!legacy_v7::forms::canReach(root,id)) {
   ++founderPaths;auto s=newDevice();step(s,Action::Hatch,starter);
   s.collection[0].species=static_cast<Species>(forms::find(id)->lineage);fixture(s,id,20);
   Snapshot bytes;CHECK(encodeSnapshot(s,bytes));legacyHeader(bytes,10,7);
   const auto before=s;CHECK(decodeSnapshot(bytes.bytes,kV13SnapshotSize,s)==SnapshotStatus::InvalidState&&same(s,before));
  }
 }
 auto s=newGame();step(s,Action::Walk,100);Snapshot bytes;CHECK(encodeSnapshot(s,bytes));
 legacyHeader(bytes,10,7);const auto before=s;
 CHECK(decodeSnapshot(bytes.bytes,kV13SnapshotSize,s)==SnapshotStatus::InvalidState&&same(s,before));
 CHECK(added==9&&sheetRoutes==82);
 std::printf("Rules7 freeze: 276 progress-byte migrations, 72 active Auto continuations, %u historical routes rejected, %u sheet routes unavailable, %u future founder paths rejected\n",added,sheetRoutes,founderPaths);
}


void captureOdds() {
 auto s=newDevice(7);step(s,Action::Hatch,1);CHECK(captureChance(s)==0);step(s,Action::Walk,100);
 CHECK(s.wildRules==10&&captureChance(s)==0);
 s.wildHp=s.wildMaxHp/2;CHECK(captureChance(s)==50);
 s.wildHp=1;CHECK(captureChance(s)==89);
 s.wildHp=s.wildMaxHp/2+1;CHECK(captureChance(s)==0);
 s.wildHp=1;s.captureAttempts=2;CHECK(captureChance(s)==89);
 s.captureAttempts=3;CHECK(captureChance(s)==0);s.captureAttempts=0;
 for(unsigned rules=4;rules<=7;++rules){s.wildRules=rules;CHECK(captureChance(s)==75);s.wildHp=s.wildMaxHp/2;CHECK(captureChance(s)==75);s.wildHp=1;}
 s.wildRules=8;s.sequence=UINT32_MAX;CHECK(captureChance(s)==0);s.sequence=2;
 for(unsigned seed=1;seed<=128;++seed){auto a=newDevice(seed);step(a,Action::Hatch,1);step(a,Action::Walk,100);a.wildHp=a.wildMaxHp/2;auto b=a;
  const auto chance=captureChance(a);auto expectedRng=a.rngState;expectedRng^=expectedRng<<13;expectedRng^=expectedRng>>17;expectedRng^=expectedRng<<5;
  CHECK(chance==50);step(a,Action::Capture);step(b,Action::Capture);CHECK(same(a,b)&&a.rngState==expectedRng);
  CHECK(a.captures==(expectedRng%100<chance?1u:0u));if(!a.captures)CHECK(a.captureAttempts==1&&a.wildTurn==1&&a.phase==Phase::Encounter);
 }
}

void flickCapture() {
 constexpr auto center=160u*256u+180u;
 const auto packed=[](int dx,unsigned reach){return static_cast<unsigned>(dx+160)*256u+reach;};
 struct Vector {int dx;unsigned reach;bool hit;};
 for(const auto& v: {Vector{0,180,true},Vector{-48,180,true},Vector{48,180,true},
     Vector{0,132,true},Vector{0,228,true},Vector{-49,180,false},Vector{49,180,false},
     Vector{0,131,false},Vector{0,229,false},Vector{33,146,true},Vector{34,146,false},
     Vector{-160,0,false},Vector{160,255,false}}) {
  FlickTrajectory result;CHECK(decodeFlick(packed(v.dx,v.reach),result));
  CHECK(result.landingX==206+v.dx&&result.landingY==300-static_cast<int>(v.reach)&&result.hit==v.hit);
 }
 FlickTrajectory untouched{123,456,true};
 CHECK(!decodeFlick(kFlickMaxValue+1,untouched)&&untouched.landingX==123&&untouched.landingY==456&&untouched.hit);
 CHECK(!decodeFlick(UINT32_MAX,untouched));
 Action parsed;CHECK(parseAction("flick",parsed)&&parsed==Action::Flick);
 auto egg=newDevice();rejects(egg,Action::Flick,center,Error::WrongPhase);
 auto home=newGame();rejects(home,Action::Flick,center,Error::WrongPhase);
 rejects(home,Action::Flick,kFlickMaxValue+1,Error::InvalidValue);
 auto wild=home;step(wild,Action::Walk,100);rejects(wild,Action::Flick,center,Error::WildTooStrong);
 auto automatic=home;step(automatic,Action::Mode,1);step(automatic,Action::Walk,100);
 rejects(automatic,Action::Flick,center,Error::WrongMode);
 wild.wildHp=1;rejects(wild,Action::Flick,kFlickMaxValue+1,Error::InvalidValue);
 rejects(wild,Action::Flick,UINT32_MAX,Error::InvalidValue);
 rejects(wild,Action::Capture,center,Error::InvalidValue);
 auto exhausted=wild;exhausted.captureAttempts=3;rejects(exhausted,Action::Flick,center,Error::CaptureLimit);
 auto overflow=wild;overflow.sequence=UINT32_MAX;rejects(overflow,Action::Flick,center,Error::CounterOverflow);
 auto full=newGame();full.sequence=20;full.steps=700;full.encounters=full.captures=7;full.nextMemberId=9;full.collectionCount=8;
 for(unsigned i=1;i<8;++i)full.collection[i]={i+1,Species::Flicker,88,80,70,80,0,1,i,0,4};full.journal[0]|=1u<<3;
 step(full,Action::Walk,100);full.wildHp=1;rejects(full,Action::Flick,center,Error::CollectionFull);
 // Every hit executes the existing button path identically, including RNG,
 // success/failure, reward, terminal reset and carried-over rules9 encounters.
 unsigned caught=0,escaped=0;
 for(unsigned seed=1;seed<=128;++seed)for(unsigned rules: {9u,10u}) {
  auto button=newDevice(seed);step(button,Action::Hatch,1);step(button,Action::Walk,100);
  button.wildHp=button.wildMaxHp/2;button.wildRules=rules;auto gesture=button;
  step(button,Action::Capture);step(gesture,Action::Flick,center);CHECK(same(button,gesture));
  if(gesture.captures)++caught;else ++escaped;
 }
 CHECK(caught&&escaped);
 // An aim miss makes exactly one ordinary response and preserves capture RNG.
 // A failed RNG capture is the reference response; only its RNG draw differs.
 auto miss=newDevice(12345);step(miss,Action::Hatch,1);step(miss,Action::Walk,100);miss.wildHp=miss.wildMaxHp/2;
 for(unsigned attempt=1;attempt<=3;++attempt) {
  const auto before=miss;step(miss,Action::Flick,packed(160,180));
  CHECK(miss.phase==Phase::Encounter&&miss.captureAttempts==attempt&&miss.wildTurn==before.wildTurn+1);
  CHECK(miss.rngState==before.rngState&&miss.energy==before.energy&&miss.wildHp==before.wildHp&&miss.hp<before.hp);
  CHECK(miss.captures==before.captures&&miss.collectionCount==before.collectionCount&&miss.message==Message::CaptureMissed);
  Snapshot snapshot;CHECK(encodeSnapshot(miss,snapshot));State restored;
  CHECK(decodeSnapshot(snapshot.bytes,sizeof(snapshot.bytes),restored)==SnapshotStatus::Ok&&same(miss,restored));
 }
 rejects(miss,Action::Flick,center,Error::CaptureLimit);
 auto retreat=wild;retreat.hp=retreat.collection[0].hp=1;const auto rng=retreat.rngState;
 step(retreat,Action::Flick,0);CHECK(retreat.phase==Phase::Home&&retreat.message==Message::Retreated&&retreat.rngState==rng&&!retreat.captures);
 std::printf("Flick input v1: bounds/circle/context, 256 hit equivalences, three miss attempts and durable replay checked\n");
}

legacy_v8::State old8Captured(unsigned form,unsigned level,unsigned hp,unsigned seed=12345) {
 namespace old=legacy_v8;
 auto s=old::newGame(seed);s.sequence=2;s.steps=100;s.encounters=s.captures=1;s.nextMemberId=3;s.collectionCount=2;s.activeCreatureId=2;
 const auto* f=old::forms::find(form);
 s.collection[1]={2,static_cast<old::Species>(f->lineage),hp,80,70,80,200,level,1,old::xpForLevel(level),form};
 s.hp=hp;s.energy=80;s.fullness=70;s.mood=80;s.bond=200;s.level=level;s.journal[(form-1)/32]|=1u<<((form-1)%32);
 CHECK(old::isValid(s));return s;
}
State migrate8(const legacy_v8::State& old) {
 legacy_v8::Snapshot bytes;CHECK(legacy_v8::encodeSnapshot(old,bytes));State now;
 CHECK(decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),now)==SnapshotStatus::Migrated);return now;
}
void rules9ProfilesAndMigration() {
 namespace old=legacy_v8;
 unsigned changed=0;
 for(unsigned id=1;id<=old::forms::kFormCount;++id)for(unsigned level=forms::find(id)->minLevel;level<=20;++level){
  const auto a=combat::formProfile(id,level);const auto b=old::combat::formProfile(id,level);
  const bool equal=a.stats.maxHp==b.stats.maxHp&&a.stats.attack==b.stats.attack&&a.stats.defense==b.stats.defense&&a.stats.magic==b.stats.magic&&a.stats.resistance==b.stats.resistance;
  CHECK(equal==(id!=3&&id!=7));CHECK(!std::strcmp(a.name,b.name)&&!std::strcmp(a.type,b.type)&&!std::strcmp(a.physicalSkill,b.physicalSkill)&&!std::strcmp(a.heavySkill,b.heavySkill)&&!std::strcmp(a.magicSkill,b.magicSkill));
  if(!equal)++changed;
 }
 CHECK(changed==22);
 for(unsigned form:{3u,7u})for(unsigned level:{10u,15u,20u}){
  const auto oldMax=old::combat::formProfile(form,level).stats.maxHp,nowMax=combat::formProfile(form,level).stats.maxHp;
  for(unsigned hp=1;hp<=oldMax;++hp){auto prior=old8Captured(form,level,hp);auto now=migrate8(prior);
   CHECK(now.hp==(hp*nowMax+oldMax-1)/oldMax&&now.collection[1].hp==now.hp&&now.sequence==prior.sequence&&now.rngState==prior.rngState&&now.collection[1].xp==prior.collection[1].xp);
   Snapshot saved;CHECK(encodeSnapshot(now,saved));State reload;CHECK(decodeSnapshot(saved.bytes,sizeof(saved.bytes),reload)==SnapshotStatus::Ok&&same(now,reload));
  }
  auto prior=old8Captured(form,level,oldMax);old::Snapshot malformed;CHECK(old::encodeSnapshot(prior,malformed));
  put(malformed.bytes+32,oldMax+1);put(malformed.bytes+164,oldMax+1);seal(malformed.bytes,sizeof(malformed.bytes));
  auto target=newDevice(),before=target;CHECK(decodeSnapshot(malformed.bytes,sizeof(malformed.bytes),target)==SnapshotStatus::InvalidState&&same(target,before));
  CHECK(old::encodeSnapshot(prior,malformed));put(malformed.bytes+32,oldMax-1);seal(malformed.bytes,sizeof(malformed.bytes));
  CHECK(decodeSnapshot(malformed.bytes,sizeof(malformed.bytes),target)==SnapshotStatus::InvalidState&&same(target,before));
 }
 unsigned captured=0,won=0,retreated=0;
 for(unsigned form:{3u,7u})for(unsigned seed=1;seed<=16;++seed)for(unsigned variant=0;variant<4;++variant){
  const auto oldMax=old::combat::formProfile(form,10).stats.maxHp;
  auto prior=old8Captured(form,10,variant==3?1:oldMax,seed);
  // Cross a numeric level during reward to verify old rounding then conversion.
  prior.collection[1].xp=old::xpForLevel(11)-1;
  CHECK(old::apply(prior,old::Action::Mode,variant==0?1:0)==old::Error::None);
  CHECK(old::apply(prior,old::Action::Walk,100)==old::Error::None);
  prior.wildFormId=form==3?7:3;prior.wildSpecies=static_cast<old::Species>(old::forms::find(prior.wildFormId)->lineage);
  prior.wildMaxHp=prior.wildHp=old::combat::formProfile(prior.wildFormId,10).stats.maxHp;
  if(variant==1||variant==2)prior.wildHp=1;
  if(variant==3)prior.wildTurn=(4-(prior.encounters-1)%3)%3; // Ward: Heavy receives normal lethal response.
  CHECK(old::isValid(prior));auto now=migrate8(prior);CHECK(now.hp==prior.hp&&now.wildMaxHp==prior.wildMaxHp&&now.wildRules==8);
  char stateJson[kJsonCapacity],oldProfile[combat::kProfileJsonCapacity];CHECK(writeJson(now,stateJson,sizeof(stateJson)));
  CHECK(old::combat::writeFormProfileJson(form,10,oldProfile,sizeof(oldProfile))&&std::strstr(stateJson,oldProfile));
  old::Snapshot oldBytes;CHECK(old::encodeSnapshot(prior,oldBytes));Snapshot currentBytes;CHECK(encodeSnapshot(now,currentBytes));
  CHECK(!std::memcmp(oldBytes.bytes+12,currentBytes.bytes+12,kV13SnapshotSize-16));
  const auto start=now;autobattle::Trace a,b;
  if(variant==0){CHECK(old::applyAuto(prior,&a)==old::Error::None);CHECK(applyAuto(now,&b)==Error::None);char ta[autobattle::kTraceJsonCapacity],tb[autobattle::kTraceJsonCapacity];CHECK(a.combatRulesVersion==8&&b.combatRulesVersion==8&&autobattle::writeJson(a,ta,sizeof(ta))&&autobattle::writeJson(b,tb,sizeof(tb))&&!std::strcmp(ta,tb));}
  else {const auto action=variant==2?Action::Capture:Action::Heavy;CHECK(old::apply(prior,static_cast<old::Action>(action))==old::Error::None);CHECK(apply(now,action)==Error::None);}
  const auto expected=migrate8(prior);CHECK(same(now,expected));
  if(now.phase==Phase::Home){captured+=now.message==Message::Captured||now.lastAutoOutcome==autobattle::Outcome::Captured;won+=now.message==Message::Won||now.lastAutoOutcome==autobattle::Outcome::Won;retreated+=now.message==Message::Retreated;CHECK(now.hp<=combat::formProfile(form,now.level).stats.maxHp);}
  auto retry=start;if(variant==0)CHECK(applyAuto(retry)==Error::None);else CHECK(apply(retry,variant==2?Action::Capture:Action::Heavy)==Error::None);CHECK(same(now,retry));
 }
 CHECK(captured&&won&&retreated);
 // An inactive old member converts immediately even while the active old fighter waits.
 auto prior=old8Captured(3,10,68);prior.collection[0].formId=3;prior.collection[0].level=10;prior.collection[0].xp=1800;prior.collection[0].bond=200;prior.collection[0].hp=68;
 CHECK(old::apply(prior,old::Action::Walk,100)==old::Error::None);auto now=migrate8(prior);
 CHECK(now.collection[0].hp==82&&now.hp==68);char json[kJsonCapacity];CHECK(writeJson(now,json,sizeof(json))&&std::strstr(json,"\"maxHp\":164")&&std::strstr(json,"\"maxHp\":136"));
 auto fresh=newGame();step(fresh,Action::Walk,100);Snapshot forged;CHECK(encodeSnapshot(fresh,forged));legacyHeader(forged,11,8);
 const auto before=fresh;CHECK(decodeSnapshot(forged.bytes,kV13SnapshotSize,fresh)==SnapshotStatus::InvalidState&&same(fresh,before));
 std::printf("Rules9: exactly22 adjusted form/levels; old HP fractions, active8 reward/capture/retreat/Auto boundaries and retries passed (%u captured/%u won/%u retreated)\n",captured,won,retreated);
}

void parkSelectionAndRecovery() {
 unsigned table[4]{};for(unsigned id=1;id<=forms::kFormCount;++id){const auto rarity=encounters::rarityForForm(id);CHECK(rarity>=encounters::Rarity::Common&&rarity<=encounters::Rarity::Rare);CHECK(encounters::rarityName(rarity));++table[static_cast<unsigned>(rarity)];}
 CHECK(table[1]==212&&table[2]==186&&table[3]==67);CHECK(!encounters::rarityName(encounters::Rarity::Unknown));CHECK(encounters::rarityForForm(0)==encounters::Rarity::Unknown&&encounters::rarityForForm(forms::kFormCount+1)==encounters::Rarity::Unknown);
 for(unsigned player:{11u,12u,13u,14u}){
  unsigned bucket[4]{};const auto level=forms::find(player)->minLevel;
  CHECK(selectWildForm(1,0,player,level)==4);CHECK(!selectWildForm(0,1,player,level));
  for(unsigned n=2;n<8194;++n){const auto form=selectWildForm(n,12345,player,level);CHECK(form==selectWildForm(n,12345,player,level));CHECK(combat::validFormProfile(form,level)&&forms::combatTier(form)<=forms::combatTier(player));++bucket[static_cast<unsigned>(encounters::rarityForForm(form))];}
  CHECK(bucket[1]>5500&&bucket[1]<5950&&bucket[2]>1820&&bucket[2]<2240&&bucket[3]>285&&bucket[3]<530);
  std::printf("Rarity gate%u: %u/%u/%u of8192 draws\n",player,bucket[1],bucket[2],bucket[3]);
 }
 auto waiting=newDevice();CHECK(!recoveryRestCount(waiting));step(waiting,Action::Hatch,1);const auto initial=waiting;CHECK(recoveryRestCount(waiting)==1&&same(waiting,initial));step(waiting,Action::Rest);CHECK(!recoveryRestCount(waiting));
 step(waiting,Action::Walk,100);const auto rng=waiting.rngState,form=waiting.wildFormId,hp=waiting.hp,wildHp=waiting.wildHp;CHECK(!recoveryRestCount(waiting));
 step(waiting,Action::Walk,1000);CHECK(waiting.steps==1100&&waiting.stepCredit==1000&&waiting.rngState==rng&&waiting.wildFormId==form&&waiting.hp==hp&&waiting.wildHp==wildHp);
 char json[kJsonCapacity];CHECK(writeJson(waiting,json,sizeof(json))&&std::strstr(json,"\"queuedEncounters\":10")&&std::strstr(json,"\"stepsToNextEncounter\":0")&&std::strstr(json,"\"wildRarity\":\"common\""));rejects(waiting,Action::Walk,0,Error::InvalidValue);
 unsigned maximum=0;
 for(unsigned id=1;id<=forms::kFormCount;++id){auto care=capturedFixture(id,20,200);care.hp=care.collection[1].hp=1;care.energy=care.collection[1].energy=0;const auto before=care;const auto count=recoveryRestCount(care);CHECK(count>0&&count<=40&&same(care,before));if(count>maximum)maximum=count;
  for(unsigned n=0;n<count;++n)step(care,Action::Rest);
  CHECK(care.hp==combat::formProfile(id,20).stats.maxHp&&care.energy==100&&care.sequence==before.sequence+count&&care.collection[1].xp==before.collection[1].xp&&care.rngState==before.rngState&&!recoveryRestCount(care));
  care=before;care.sequence=UINT32_MAX-count+1;const auto unchanged=care;CHECK(!recoveryRestCount(care)&&same(care,unchanged));
 }
 CHECK(maximum==12);std::printf("Park recovery: all276 forms, max%u existingRest events; overflow/phase/realstep queue checked\n",maximum);
}
void rules9ParkMigration() {
 namespace old=legacy_v9;
 for(unsigned seed=1;seed<=32;++seed){auto prior=old::newDevice(seed);CHECK(old::apply(prior,old::Action::Hatch,seed%8+1)==old::Error::None);CHECK(old::apply(prior,old::Action::Mode,1)==old::Error::None);CHECK(old::apply(prior,old::Action::Walk,100)==old::Error::None);CHECK(old::apply(prior,old::Action::Walk,500)==old::Error::None);
  old::Snapshot saved;CHECK(old::encodeSnapshot(prior,saved));State now;CHECK(decodeSnapshot(saved.bytes,sizeof(saved.bytes),now)==SnapshotStatus::Migrated);Snapshot current;CHECK(encodeSnapshot(now,current)&&!std::memcmp(saved.bytes+12,current.bytes+12,kV13SnapshotSize-16));
  char json[kJsonCapacity];CHECK(writeJson(now,json,sizeof(json))&&std::strstr(json,"\"wildRarity\":null")&&std::strstr(json,"\"queuedEncounters\":5"));
  autobattle::Trace a,b;CHECK(old::applyAuto(prior,&a)==old::Error::None&&applyAuto(now,&b)==Error::None);old::Snapshot final;CHECK(old::encodeSnapshot(prior,final));State expected;CHECK(decodeSnapshot(final.bytes,sizeof(final.bytes),expected)==SnapshotStatus::Migrated&&same(now,expected));
  char x[autobattle::kTraceJsonCapacity],y[autobattle::kTraceJsonCapacity];CHECK(autobattle::writeJson(a,x,sizeof(x))&&autobattle::writeJson(b,y,sizeof(y))&&!std::strcmp(x,y));
 }
 auto s=newGame();step(s,Action::Walk,100);Snapshot forged;CHECK(encodeSnapshot(s,forged));legacyHeader(forged,12,9);const auto before=s;CHECK(decodeSnapshot(forged.bytes,kV13SnapshotSize,s)==SnapshotStatus::InvalidState&&same(s,before));
}
void parkReleaseDuringEncounter() {
 for(unsigned mode=0;mode<2;++mode){
  auto s=newGame();s.sequence=20;s.steps=700;s.encounters=s.captures=7;s.nextMemberId=9;s.collectionCount=8;
  for(unsigned i=1;i<8;++i)s.collection[i]={i+1,Species::Flicker,88,80,70,80,0,1,i,0,4};s.journal[0]|=1u<<3;CHECK(isValid(s));step(s,Action::Mode,mode);step(s,Action::Walk,100);CHECK(s.wildRules==10);
  rejects(s,Action::Release,1,Error::ActiveMemberRelease);rejects(s,Action::Release,99,Error::UnknownMember);
  const auto before=s;step(s,Action::Release,2);auto expected=before;
  for(unsigned i=1;i+1<expected.collectionCount;++i)expected.collection[i]=expected.collection[i+1];expected.collection[--expected.collectionCount]={};++expected.sequence;expected.foregroundSequence=expected.sequence;expected.message=Message::Released;
  CHECK(same(s,expected)&&s.collectionCount==7&&s.wildFormId==before.wildFormId&&s.rngState==before.rngState);
  Snapshot saved;CHECK(encodeSnapshot(s,saved));State restored;CHECK(decodeSnapshot(saved.bytes,sizeof(saved.bytes),restored)==SnapshotStatus::Ok&&same(s,restored));
  if(mode){autobattle::Trace trace;CHECK(applyAuto(restored,&trace)==Error::None&&restored.phase==Phase::Home);}
  else {restored.wildHp=1;step(restored,Action::Capture);CHECK(restored.collectionCount==8&&restored.collection[7].id==9&&restored.phase==Phase::Home);}
  auto old=before;old.wildRules=9;rejects(old,Action::Release,2,mode?Error::WrongMode:Error::WrongPhase);
 }
}

}
int run(){onboardingAndCare();evolution();battles();migration();durabilityAndBudgets();maximumJson();stableIdsAndJournal();captureEveryForm();poolsAndGuard();rules4Migration();rules5Migration();rules6Migration();rules7Migration();newGraphEpochGuards();newRules8EpochGuards();captureOdds();flickCapture();rules9ProfilesAndMigration();parkSelectionAndRecovery();rules9ParkMigration();parkReleaseDuringEncounter();graphEdges();founderCrossLineage();std::printf("%u frozen rules12 compatibility checks, %u failures\n",checks,failures);return failures?1:0;}

} // namespace historical
int main(){return historical::run();}
