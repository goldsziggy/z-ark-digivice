#include "game.hpp"
#include "legacy_v10.hpp"
#include "legacy_v12.hpp"
#include "forms.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
#include <initializer_list>
using namespace digivice;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)
bool same(const State& a,const State& b){Snapshot x,y;return encodeSnapshot(a,x)&&encodeSnapshot(b,y)&&!std::memcmp(x.bytes,y.bytes,sizeof(x.bytes));}
void step(State& s,Action action,unsigned value=0){CHECK(apply(s,action,value)==Error::None);CHECK(isValid(s));}
void rejects(State& s,Action action,unsigned value,Error error){const auto before=s;CHECK(apply(s,action,value)==error);CHECK(same(before,s));}
State hatched(unsigned seed){auto s=newDevice(seed);step(s,Action::Hatch,1);return s;}
void restore(State& s){Snapshot saved;CHECK(encodeSnapshot(s,saved));State recovered;CHECK(decodeSnapshot(saved.bytes,sizeof(saved.bytes),recovered)==SnapshotStatus::Ok);CHECK(same(s,recovered));s=recovered;}
void clearFight(State& s){CHECK(s.phase==Phase::Encounter);while(s.phase==Phase::Encounter)step(s,Action::Attack);}
void pacing(){
 unsigned total=0,smallest=1000,largest=0;
 for(unsigned seed=1;seed<=512;++seed){
  auto s=hatched(seed),replay=s;const auto captureRng=s.rngState;
  unsigned first=0;
  while(s.phase==Phase::Home){step(s,Action::Explore,1);++first;if(first==17)restore(s);}
  CHECK(first>=40&&first<=80&&s.walkingEncounters==1&&s.encounters==1&&s.stepCredit==0&&s.steps==0&&s.explorationSteps==first);
  CHECK(forms::productionForm(s.wildFormId)&&s.wildRules==kRulesVersion&&s.rngState==captureRng&&s.encounterProgress==0);
  step(replay,Action::Explore,first);CHECK(replay.wildFormId==s.wildFormId&&replay.encounterRng==s.encounterRng&&replay.encounterTarget==s.encounterTarget);
  const auto blocked=s;rejects(s,Action::Explore,1000,Error::WrongPhase);rejects(s,Action::EncounterRate,0,Error::WrongPhase);CHECK(same(s,blocked));
  clearFight(s);const auto gap=encounterStepsRemaining(s);CHECK(gap>=80&&gap<=140);
  if(gap<smallest)smallest=gap;if(gap>largest)largest=gap;total+=gap;
  const auto rng=s.encounterRng;step(s,Action::Explore,gap-1);CHECK(s.phase==Phase::Home&&s.encounterRng==rng&&encounterStepsRemaining(s)==1);
  auto crossing=s;restore(crossing);step(s,Action::Explore,1000);step(crossing,Action::Explore,1000);CHECK(same(s,crossing));
  CHECK(s.walkingEncounters==2&&s.encounters==2&&s.encounterProgress==0&&s.encounterRng!=rng&&s.stepCredit==0);
  CHECK(s.wildLevel+1>=s.level&&s.wildLevel<=s.level+1&&s.wildLevel>=1&&s.wildLevel<=kMaxLevel);
  CHECK(s.wildFormId==selectWildForm(2,seed,activeMember(s)->formId,s.wildLevel));
  clearFight(s);CHECK(encounterStepsRemaining(s)>=80);step(s,Action::Explore,1);CHECK(s.phase==Phase::Home&&s.encounterProgress==2); // no burst/backlog
 }
 CHECK(smallest==80&&largest==140&&total>512*106&&total<512*114);
 std::printf("512 seeded gaps: min%u max%u mean%.3f Normal steps; first40..80; single-crossing surplus discarded\n",smallest,largest,total/512.0);
}
void settings(){
 auto s=hatched(13);step(s,Action::Explore,7);const auto target=s.encounterTarget,rng=s.encounterRng,credit=s.encounterProgress,capture=s.rngState;
 for(unsigned setting=0;setting<=3;++setting){step(s,Action::EncounterRate,setting);restore(s);CHECK(s.encounterTarget==target&&s.encounterRng==rng&&s.encounterProgress==credit&&s.rngState==capture);
  if(!setting)rejects(s,Action::Explore,1000,Error::InvalidAction);
  else CHECK(encounterStepsRemaining(s)==(target-credit+setting-1)/setting);
 }
 step(s,Action::EncounterRate,2);step(s,Action::Explore,1);CHECK(s.encounterProgress==credit+2);
 std::uint32_t low=99,high=99;encounterStepRange(EncounterRate::Off,false,low,high);CHECK(low==0&&high==0);
 encounterStepRange(EncounterRate::Relaxed,false,low,high);CHECK(low==160&&high==280);
 encounterStepRange(EncounterRate::Normal,false,low,high);CHECK(low==80&&high==140);
 encounterStepRange(EncounterRate::Frequent,false,low,high);CHECK(low==54&&high==94);
 encounterStepRange(EncounterRate::Frequent,true,low,high);CHECK(low==27&&high==54);
 rejects(s,Action::EncounterRate,4,Error::InvalidValue);rejects(s,Action::Explore,0,Error::InvalidValue);rejects(s,Action::Explore,1001,Error::InvalidValue);
 auto egg=newDevice();rejects(egg,Action::Explore,1,Error::WrongPhase);rejects(egg,Action::EncounterRate,0,Error::WrongPhase);
 s.sequence=UINT32_MAX;rejects(s,Action::Explore,1,Error::CounterOverflow);
 s.sequence=42;s.explorationSteps=UINT32_MAX;CHECK(isValid(s));rejects(s,Action::Explore,1,Error::CounterOverflow);
 Action a;CHECK(parseAction("explore",a)&&a==Action::Explore);CHECK(parseAction("encounter-rate",a)&&a==Action::EncounterRate);
 char json[kJsonCapacity];s=hatched(5);step(s,Action::Explore,1);CHECK(writeJson(s,json,sizeof(json))&&std::strstr(json,"\"walking\":{\"rate\":2,\"name\":\"Normal\""));
}
void deviceEntropy(){
 auto a=hatched(12345),b=a;
 rejects(a,Action::EncounterSeed,0,Error::InvalidValue);
 step(a,Action::EncounterSeed,1);step(b,Action::EncounterSeed,0xffffffffu);
 CHECK(a.encounterRng!=b.encounterRng&&a.rngState==b.rngState&&a.encounterTarget>=80&&a.encounterTarget<=160);
 CHECK(a.explorationSteps==0&&a.encounterProgress==0&&a.walkingEncounters==0);
 restore(a);const auto rng=a.encounterRng,target=a.encounterTarget;
 rejects(a,Action::EncounterSeed,2,Error::InvalidAction);
 for(unsigned rate=0;rate<=3;++rate){step(a,Action::EncounterRate,rate);CHECK(a.encounterRng==rng&&a.encounterTarget==target);}
 step(a,Action::EncounterRate,2);step(a,Action::Explore,1);CHECK(a.encounterRng==rng&&a.encounterTarget==target);
 auto fallback=hatched(12345);step(fallback,Action::Explore,1);rejects(fallback,Action::EncounterSeed,1,Error::InvalidAction);
 auto egg=newDevice();rejects(egg,Action::EncounterSeed,1,Error::WrongPhase);
 Action parsed;CHECK(parseAction("encounter-seed",parsed)&&parsed==Action::EncounterSeed);
}
void migration(){
 namespace old=legacy_v10;
 for(unsigned seed=0;seed<64;++seed)for(unsigned phase=0;phase<3;++phase){
  auto prior=old::newDevice(seed);
  if(phase)CHECK(old::apply(prior,old::Action::Hatch,seed%8+1)==old::Error::None);
  if(phase==2){CHECK(old::apply(prior,old::Action::Mode,1)==old::Error::None);CHECK(old::apply(prior,old::Action::Walk,1000)==old::Error::None);}
  old::Snapshot saved;CHECK(old::encodeSnapshot(prior,saved));legacy_v12::State now;CHECK(legacy_v12::decodeSnapshot(saved.bytes,sizeof(saved.bytes),now)==legacy_v12::SnapshotStatus::Migrated);
  legacy_v12::Snapshot current;CHECK(legacy_v12::encodeSnapshot(now,current)&&!std::memcmp(saved.bytes+12,current.bytes+12,sizeof(saved.bytes)-16));
  CHECK(now.encounterRate==legacy_v12::EncounterRate::Normal&&!now.explorationSteps&&!now.walkingEncounters&&!now.encounterTarget&&!now.encounterRng&&!now.encounterProgress);
  {legacy_v12::Snapshot fresh;CHECK(legacy_v12::encodeSnapshot(now,fresh));legacy_v12::State recovered;CHECK(legacy_v12::decodeSnapshot(fresh.bytes,sizeof(fresh.bytes),recovered)==legacy_v12::SnapshotStatus::Ok);now=recovered;}
  if(phase==2){autobattle::Trace a,b;CHECK(old::applyAuto(prior,&a)==old::Error::None&&legacy_v12::applyAuto(now,&b)==legacy_v12::Error::None);
   char x[autobattle::kTraceJsonCapacity],y[autobattle::kTraceJsonCapacity];CHECK(autobattle::writeJson(a,x,sizeof(x))&&autobattle::writeJson(b,y,sizeof(y))&&!std::strcmp(x,y));
   CHECK(old::encodeSnapshot(prior,saved));CHECK(legacy_v12::encodeSnapshot(now,current)&&!std::memcmp(saved.bytes+12,current.bytes+12,sizeof(saved.bytes)-16));
   const auto legacySteps=now.steps,legacyCredit=now.stepCredit,encounters=now.encounters;
   CHECK(legacy_v12::apply(now,legacy_v12::Action::Explore,1000)==legacy_v12::Error::None);CHECK(now.steps==legacySteps&&now.stepCredit==legacyCredit&&now.encounters==encounters+1&&now.walkingEncounters==1);
  }
 }
 // Preserve ordinary Walk on Home and queued Walk during a legacy active fight.
 auto oldState=old::newGame(9);auto current=legacy_v12::newGame(9);
 for(unsigned value:{17u,83u,1000u}){CHECK(old::apply(oldState,old::Action::Walk,value)==old::Error::None);CHECK(legacy_v12::apply(current,legacy_v12::Action::Walk,value)==legacy_v12::Error::None);old::Snapshot a;legacy_v12::Snapshot b;CHECK(old::encodeSnapshot(oldState,a)&&legacy_v12::encodeSnapshot(current,b)&&!std::memcmp(a.bytes+12,b.bytes+12,sizeof(a.bytes)-16));}
}
void malformed(){
 auto s=hatched(1);step(s,Action::Explore,1);
 auto bad=s;bad.encounterProgress=bad.encounterTarget;CHECK(!isValid(bad));
 bad=s;bad.encounterTarget=282;CHECK(!isValid(bad));bad=s;bad.encounterTarget=81;CHECK(!isValid(bad));bad=s;bad.encounterRng=0;CHECK(!isValid(bad));
 bad=s;bad.encounterRate=static_cast<EncounterRate>(4);CHECK(!isValid(bad));bad=s;bad.walkingEncounters=1;CHECK(!isValid(bad));
 Snapshot saved;CHECK(encodeSnapshot(s,saved));
 for(unsigned i=572;i<sizeof(saved.bytes);++i){auto corrupt=saved;corrupt.bytes[i]^=1;auto dest=s;CHECK(decodeSnapshot(corrupt.bytes,sizeof(corrupt.bytes),dest)!=SnapshotStatus::Ok&&same(s,dest));}
}
void autoBalance(){
 unsigned outcomes[2][4]{},turns[2]{},longest[2]{};
 for(unsigned starter=1;starter<=8;++starter)for(unsigned tier=0;tier<4;++tier)for(unsigned seed=1;seed<=64;++seed){
  auto s=newDevice(seed);step(s,Action::Hatch,starter);
  const auto form=11+7*(starter-1)+tier,level=tier?tier*5:1;
  auto& m=s.collection[0];m.formId=form;m.level=s.level=level;m.xp=xpForLevel(level);m.bond=s.bond=200;
  m.hp=s.hp=combat::formProfile(form,level).stats.maxHp;s.journal[(form-1)/32]|=1u<<((form-1)%32);
  s.encounters=seed;s.steps=100*seed;s.sequence=5000;CHECK(isValid(s));
  step(s,Action::Mode,1);step(s,Action::Explore,1000);auto greedy=s;greedy.wildRules=10;
  autobattle::Trace trace[2];CHECK(applyAuto(greedy,&trace[0])==Error::None&&applyAuto(s,&trace[1])==Error::None);
  for(unsigned policy=0;policy<2;++policy){++outcomes[policy][static_cast<unsigned>(trace[policy].outcome)];turns[policy]+=static_cast<unsigned>(trace[policy].count);if(trace[policy].count>longest[policy])longest[policy]=static_cast<unsigned>(trace[policy].count);}
 }
 for(unsigned policy=0;policy<2;++policy)std::printf("Auto%s 2048 equal-state samples (8starters x4tiers x64seeds): won%u captured%u retreated%u mean%.3f/max%u turns\n",policy?"12 Care/random basics":"10 greedy",outcomes[policy][1],outcomes[policy][2],outcomes[policy][3],turns[policy]/2048.0,longest[policy]);
 CHECK(outcomes[1][1]+outcomes[1][2]+outcomes[1][3]==2048);
}
void autoPolicy(){
 unsigned physical=0,magic=0,outcomes[4]{},longest=0;
 for(unsigned seed=1;seed<=1024;++seed){
  // Rules-17 encounters keep the equal-odds basic policy; rules18_test covers the guard-aware one.
  auto s=hatched(seed);step(s,Action::Mode,1);step(s,Action::Explore,1000);s.wildRules=17;const auto before=s;auto replay=s;
  autobattle::Trace a,b;CHECK(applyAuto(s,&a)==Error::None&&applyAuto(replay,&b)==Error::None&&same(s,replay));
  char x[autobattle::kTraceJsonCapacity],y[autobattle::kTraceJsonCapacity];CHECK(autobattle::writeJson(a,x,sizeof(x))&&autobattle::writeJson(b,y,sizeof(y))&&!std::strcmp(x,y));
  auto policy=before.seed^0x9e3779b9u^(before.encounters*0x85ebca6bu)^before.sequence;
  bool captured=false;
  for(std::size_t i=0;i<a.count;++i){const auto& frame=a.steps[i];
   if(frame.action==autobattle::Move::Capture){CHECK(frame.enemyHpBefore<=before.wildMaxHp/2);captured|=frame.captured;continue;}
   const auto expected=(autobattle::nextRandom(policy)&1u)?autobattle::Move::Magic:autobattle::Move::Physical;
   CHECK(frame.action==expected);physical+=frame.action==autobattle::Move::Physical;magic+=frame.action==autobattle::Move::Magic;
   CHECK(frame.action!=autobattle::Move::Heavy);
   CHECK(frame.opponentAction==autobattle::Move::None||frame.opponentAction==autobattle::Move::Counter||frame.opponentAction==autobattle::Move::Physical||frame.opponentAction==autobattle::Move::Magic);
  }
  CHECK(a.count>=1&&a.count<=autobattle::kMaxTraceSteps&&s.sequence==before.sequence+1);
  CHECK(!captured||a.outcome==autobattle::Outcome::Captured);++outcomes[static_cast<unsigned>(a.outcome)];if(a.count>longest)longest=static_cast<unsigned>(a.count);
 }
 CHECK(physical>0&&magic>0&&physical*100/(physical+magic)>=45&&physical*100/(physical+magic)<=55);
 std::printf("Rules12 Auto1024 first-fight samples: physical%u magic%u; won%u captured%u retreated%u; max%u turns\n",physical,magic,outcomes[1],outcomes[2],outcomes[3],longest);
}
}
int main(){pacing();settings();deviceEntropy();migration();malformed();autoBalance();autoPolicy();std::printf("%u walking/migration/Auto checks, %u failures\n",checks,failures);return failures?1:0;}
