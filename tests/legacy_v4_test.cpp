// Frozen rules4 regression retained before full catalog/guard integration.
#include "legacy_v4.hpp"
#include "legacy_combat_v5.hpp"
#include "legacy_forms_v5.hpp"
#include "legacy_v3.hpp"
#include "legacy_combat_v3.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace digivice::legacy_v4;
namespace combat=digivice::legacy_v5::combat;
namespace forms=digivice::legacy_v5::forms;
namespace legacy_v3=digivice::legacy_v3;
namespace autobattle=digivice::autobattle;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)
bool same(const State& a,const State& b){Snapshot x,y;return encodeSnapshot(a,x)&&encodeSnapshot(b,y)&&!std::memcmp(x.bytes,y.bytes,sizeof(x.bytes));}
void step(State& s,Action a,std::uint32_t v=0){CHECK(apply(s,a,v)==Error::None);CHECK(isValid(s));}
void rejects(State& s,Action a,std::uint32_t v,Error error){const auto before=s;CHECK(apply(s,a,v)==error);CHECK(same(s,before));}
void fixture(State& s,unsigned form,unsigned level,unsigned bond=200){auto& m=s.collection[s.activeCreatureId-1];m.formId=form;m.xp=xpForLevel(level);m.level=s.level=level;m.bond=s.bond=bond;m.hp=s.hp=combat::formProfile(form,level).stats.maxHp;CHECK(isValid(s));}
void put(std::uint8_t* b,std::uint32_t n){for(unsigned i=0;i<4;++i)b[i]=static_cast<std::uint8_t>(n>>(8*i));}
std::uint32_t crc(const std::uint8_t* b,std::size_t n){std::uint32_t c=~0u;for(std::size_t i=0;i<n;++i){c^=b[i];for(unsigned j=0;j<8;++j)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
void seal(std::uint8_t* b,std::size_t n){put(b+n-4,crc(b,n-4));}
void onboardingAndCare(){
 for(unsigned starter=1;starter<=8;++starter){auto s=newDevice(starter);CHECK(isValid(s));rejects(s,Action::Rest,0,Error::WrongPhase);step(s,Action::Hatch,starter);CHECK(s.sequence==1&&s.collection[0].formId==11+7*(starter-1)&&s.collection[0].xp==0&&s.level==1);rejects(s,Action::Hatch,starter,Error::AlreadyHatched);const auto rng=s.rngState;for(unsigned i=0;i<30;++i){step(s,Action::Feed);step(s,Action::Rest);}CHECK(s.collection[0].xp==0&&s.level==1&&s.rngState==rng);const auto bond=s.bond;for(unsigned i=0;i<20;++i){step(s,Action::Feed);step(s,Action::Rest);}CHECK(s.bond==bond);s.energy=s.collection[0].energy=0;rejects(s,Action::Play,0,Error::LowEnergy);}
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
  CHECK(s.hp==17&&s.sequence==old.sequence&&s.rngState==old.rngState&&s.bond==old.bond&&s.starterId==old.starterId);
  CHECK(s.level==(tier==1?1u:tier==2?5u:10u)&&s.collection[0].xp==xpForLevel(s.level));
  const auto a=combat::formProfile(s.collection[0].formId,s.level).stats; const auto b=legacy_v3::combat::profile(static_cast<unsigned>(old.collection[0].species),tier).stats;
  CHECK(a.maxHp==b.maxHp&&a.attack==b.attack&&a.defense==b.defense&&a.magic==b.magic&&a.resistance==b.resistance);
  CHECK(!std::strcmp(memberName(s.collection[0]),legacy_v3::memberName(old.collection[0])));
 }
 auto egg=legacy_v3::newDevice();legacy_v3::Snapshot old;CHECK(legacy_v3::encodeSnapshot(egg,old));State restored;CHECK(decodeSnapshot(old.bytes,sizeof(old.bytes),restored)==SnapshotStatus::Migrated&&restored.phase==Phase::Egg);
 auto prior=legacy_v3::newGame();CHECK(legacy_v3::apply(prior,legacy_v3::Action::Walk,100)==legacy_v3::Error::None);CHECK(legacy_v3::apply(prior,legacy_v3::Action::Attack)==legacy_v3::Error::None);CHECK(legacy_v3::encodeSnapshot(prior,old));
 for(unsigned version: {4u,5u,6u}){const auto size=version==4?404u:version==5?412u:428u;auto copy=old;copy.bytes[4]=static_cast<std::uint8_t>(version);copy.bytes[6]=static_cast<std::uint8_t>(size-12);copy.bytes[7]=static_cast<std::uint8_t>((size-12)>>8);seal(copy.bytes,size);CHECK(decodeSnapshot(copy.bytes,size,restored)==SnapshotStatus::Migrated);CHECK(restored.wildHp==prior.wildHp&&restored.hp==prior.hp&&restored.wildLevel==1&&restored.wildTurn==0);}
}
void durabilityAndBudgets(){
 auto s=newDevice();step(s,Action::Hatch,8);fixture(s,60,5,200);Snapshot saved;CHECK(encodeSnapshot(s,saved));CHECK(sizeof(saved)==500&&sizeof(State)==476);
 for(unsigned i=0;i<sizeof(saved.bytes);++i){auto bad=saved;bad.bytes[i]^=1;auto target=s;CHECK(decodeSnapshot(bad.bytes,sizeof(bad.bytes),target)!=SnapshotStatus::Ok&&same(target,s));}
 for(const auto offset:{148u,152u,492u}){auto bad=saved;put(bad.bytes+offset,0xffffffffu);seal(bad.bytes,sizeof(bad.bytes));auto target=s;CHECK(decodeSnapshot(bad.bytes,sizeof(bad.bytes),target)==SnapshotStatus::InvalidState&&same(target,s));}
 auto forged=saved; put(forged.bytes+48,0); put(forged.bytes+136,0); put(forged.bytes+152,61); seal(forged.bytes,sizeof(forged.bytes));
 auto preserved=s; CHECK(decodeSnapshot(forged.bytes,sizeof(forged.bytes),preserved)==SnapshotStatus::InvalidState&&same(s,preserved));
 std::size_t largest=0;
 for(unsigned starter=1;starter<=8;++starter){s=newDevice();step(s,Action::Hatch,starter);fixture(s,s.collection[0].formId,20,200);s.sequence=UINT32_MAX;s.encounters=UINT32_MAX/100;s.steps=s.encounters*100;s.captures=7;s.collectionCount=8;
  for(unsigned i=1;i<8;++i){auto& m=s.collection[i];m={i+1,Species::Rill,1,100,100,100,200,20,i,7600,7};}
  CHECK(isValid(s));char json[kJsonCapacity];const auto n=writeJson(s,json,sizeof(json));CHECK(n&&n<sizeof(json));if(n>largest)largest=n;char small[32];CHECK(!writeJson(s,small,sizeof(small))&&small[0]=='\0');
  CHECK(std::strstr(json,"\"maxLevel\":20")&&std::strstr(json,"\"evolution\":{\"options\":["));
 }
 std::printf("State%zu / snapshot%zu / widest sampled JSON%zu of%zu\n",sizeof(State),sizeof(Snapshot),largest,kJsonCapacity);
}
}
int main(){onboardingAndCare();evolution();battles();migration();durabilityAndBudgets();std::printf("%u RPG checks, %u failures\n",checks,failures);return failures?1:0;}
