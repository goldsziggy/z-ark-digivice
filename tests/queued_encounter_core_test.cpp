#include "game.hpp"
#include "snapshot_test_helpers.hpp"
#include "legacy_v12.hpp"
#include "forms.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>
using namespace digivice;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)
bool same(const State& a,const State& b){Snapshot x,y;return encodeSnapshot(a,x)&&encodeSnapshot(b,y)&&!std::memcmp(x.bytes,y.bytes,kSnapshotSize);}
void step(State& s,Action action,unsigned value=0){const auto error=apply(s,action,value);if(error!=Error::None)std::fprintf(stderr,"action %u value %u error %s\n",static_cast<unsigned>(action),value,errorText(error));CHECK(error==Error::None);CHECK(isValid(s));}
void rejects(State& s,Action action,unsigned value,Error error){const auto before=s;CHECK(apply(s,action,value)==error);CHECK(same(before,s));}
State hatched(unsigned seed){auto s=newDevice(seed);step(s,Action::Hatch,1);return s;}
void restore(State& s){Snapshot bytes;CHECK(encodeSnapshot(s,bytes));State restored;CHECK(decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),restored)==SnapshotStatus::Ok);CHECK(same(s,restored));s=restored;}
bool sameForeground(State a,const State& b){a.sequence=b.sequence;a.explorationSteps=b.explorationSteps;a.encounterRng=b.encounterRng;a.encounterTarget=b.encounterTarget;a.encounterProgress=b.encounterProgress;a.pendingEncounter=b.pendingEncounter;return same(a,b);}
void clearFight(State& s){for(unsigned n=0;s.phase==Phase::Encounter&&n<1000;++n)step(s,Action::Attack);CHECK(s.phase==Phase::Home);}
void pacingAndOneSlot(){
 for(unsigned seed=1;seed<=512;++seed){
  auto s=hatched(seed),legacy=s;const auto captureRng=s.rngState;unsigned count=0;
  do {step(s,Action::AccrueSteps,1);step(legacy,Action::Explore,1);++count;if(count==17)restore(s);}while(!s.pendingEncounter.formId&&count<100);
  CHECK(count>=40&&count<=80&&legacy.phase==Phase::Encounter&&s.phase==Phase::Home);
  CHECK(s.pendingEncounter.formId==legacy.wildFormId&&s.pendingEncounter.level==legacy.wildLevel&&s.pendingEncounter.rules==legacy.wildRules);
  CHECK(s.encounterTarget==legacy.encounterTarget&&s.encounterRng==legacy.encounterRng&&s.encounterProgress==0);
  CHECK(s.encounters==0&&s.walkingEncounters==0&&s.explorationSteps==count&&s.rngState==captureRng&&encounterStepsRemaining(s)==0);
  const auto queued=s;restore(s);
  for(unsigned n=0;n<4;++n)step(s,Action::AccrueSteps,1000);
  CHECK(s.explorationSteps==count+4000&&sameForeground(s,queued));
  CHECK(s.pendingEncounter.formId==queued.pendingEncounter.formId&&s.encounterTarget==queued.encounterTarget&&s.encounterRng==queued.encounterRng);
  rejects(s,Action::Explore,1,Error::InvalidAction);rejects(s,Action::Walk,100,Error::InvalidAction);
  restore(s);step(s,Action::PresentEncounter);CHECK(s.phase==Phase::Encounter&&s.encounters==1&&s.walkingEncounters==1&&!s.pendingEncounter.formId);
  CHECK(s.wildFormId==legacy.wildFormId&&s.wildLevel==legacy.wildLevel&&s.wildRules==legacy.wildRules&&s.wildHp==legacy.wildHp);
  rejects(s,Action::PresentEncounter,0,Error::WrongPhase);clearFight(s);
  rejects(s,Action::PresentEncounter,0,Error::InvalidAction);
  const auto gap=encounterStepsRemaining(s);CHECK(gap>=80&&gap<=140);
  step(s,Action::AccrueSteps,gap-1);CHECK(!s.pendingEncounter.formId&&encounterStepsRemaining(s)==1);
  restore(s);step(s,Action::AccrueSteps,1000);CHECK(s.pendingEncounter.formId&&s.encounterProgress==0&&s.encounters==1);
  step(s,Action::PresentEncounter);CHECK(s.encounters==2&&s.walkingEncounters==2&&!s.pendingEncounter.formId);
 }
}
void duringCurrentMatch(){
 for(unsigned mode=0;mode<2;++mode)for(unsigned seed=1;seed<=64;++seed){
  auto s=hatched(seed);step(s,Action::Mode,mode);step(s,Action::Walk,100);
  const auto active=s;step(s,Action::EncounterSeed,seed+1000);CHECK(sameForeground(s,active));
  step(s,Action::AccrueSteps,1000);CHECK(sameForeground(s,active)&&s.phase==Phase::Encounter&&s.pendingEncounter.formId);
  const auto form=s.pendingEncounter.formId,level=s.pendingEncounter.level,target=s.encounterTarget,rng=s.encounterRng;
  CHECK(form==selectWildForm(2,seed,activeMember(s)->formId,s.level));
  rejects(s,Action::PresentEncounter,0,Error::WrongPhase);restore(s);
  if(mode)step(s,Action::Auto);else clearFight(s);
  CHECK(s.pendingEncounter.formId==form&&s.pendingEncounter.level==level&&s.encounterTarget==target&&s.encounterRng==rng);
  restore(s);step(s,Action::PresentEncounter);CHECK(s.wildFormId==form&&s.wildLevel==level&&s.encounters==2&&s.walkingEncounters==1);
 }
 // A last throw record remains the original event, never relabeled by walking.
 auto s=hatched(99);step(s,Action::Explore,1000);s.wildHp=s.wildMaxHp/2;step(s,Action::Flick,0);
 const auto capture=s.lastCapture;step(s,Action::AccrueSteps,1000);restore(s);
 CHECK(s.lastCapture.sequence==capture.sequence&&s.lastCapture.result==capture.result&&s.foregroundSequence==capture.sequence&&s.sequence>capture.sequence&&s.pendingEncounter.formId);
}
void autoUnaffectedByBackground(){
 for(unsigned seed=1;seed<=256;++seed){
  auto untouched=hatched(seed);step(untouched,Action::Mode,1);step(untouched,Action::Explore,1000);
  auto walking=untouched;step(walking,Action::AccrueSteps,1000);restore(walking);
  CHECK(walking.foregroundSequence==untouched.sequence&&walking.sequence>untouched.sequence);
  autobattle::Trace a,b;CHECK(applyAuto(untouched,&a)==Error::None&&applyAuto(walking,&b)==Error::None);
  b.startSequence=a.startSequence;b.endSequence=a.endSequence;
  char x[autobattle::kTraceJsonCapacity],y[autobattle::kTraceJsonCapacity];
  CHECK(autobattle::writeJson(a,x,sizeof(x))&&autobattle::writeJson(b,y,sizeof(y))&&!std::strcmp(x,y));
  CHECK(walking.hp==untouched.hp&&walking.energy==untouched.energy&&walking.rngState==untouched.rngState&&walking.captures==untouched.captures&&walking.pendingEncounter.formId);
  CHECK(walking.foregroundSequence==walking.sequence);restore(walking);
 }
}
void carePartnerAndOff(){
 auto s=hatched(44);
 // A valid second partner with a different form, earned before this fixture.
 s.sequence=3;s.steps=100;s.encounters=1;s.captures=1;s.nextMemberId=3;s.collectionCount=2;
 auto& second=s.collection[1];second=s.collection[0];second.id=2;second.capturedAtSequence=2;second.species=Species::Agumon;second.formId=18;
 second.hp=combat::formProfile(second.formId,second.level).stats.maxHp;s.journal[(second.formId-1)/32]|=1u<<((second.formId-1)%32);CHECK(isValid(s));
 step(s,Action::AccrueSteps,1000);const auto pending=s.pendingEncounter;const auto target=s.encounterTarget,rng=s.encounterRng;
 step(s,Action::Select,2);step(s,Action::Feed);step(s,Action::Play);step(s,Action::Rest);step(s,Action::Release,1);step(s,Action::EncounterRate,0);
 CHECK(s.pendingEncounter.formId==pending.formId&&s.pendingEncounter.level==pending.level&&s.encounterTarget==target&&s.encounterRng==rng);
 restore(s);rejects(s,Action::AccrueSteps,1,Error::InvalidAction);step(s,Action::PresentEncounter);
 CHECK(s.wildFormId==pending.formId&&s.wildLevel==pending.level&&s.encounterRate==EncounterRate::Off);
 auto evolving=hatched(123);evolving.level=evolving.collection[0].level=5;evolving.collection[0].xp=xpForLevel(5);evolving.bond=evolving.collection[0].bond=200;
 const auto* edge=forms::outgoing(activeMember(evolving)->formId,0);CHECK(edge&&isValid(evolving));
 step(evolving,Action::AccrueSteps,1000);const auto frozen=evolving.pendingEncounter;
 if(edge)step(evolving,Action::Evolve,edge->to);CHECK(evolving.pendingEncounter.formId==frozen.formId&&evolving.pendingEncounter.level==frozen.level);
}
void put32(std::uint8_t* out,std::uint32_t value){for(unsigned i=0;i<4;++i)out[i]=static_cast<std::uint8_t>(value>>(i*8));}
std::uint32_t crc(const std::uint8_t* data,std::size_t length){std::uint32_t c=0xffffffffu;for(std::size_t i=0;i<length;++i){c^=data[i];for(unsigned n=0;n<8;++n)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
void migrationAndBounds(){
 for(unsigned phase=0;phase<4;++phase){
  namespace old=legacy_v12;auto prior=old::newDevice(7654);CHECK(old::apply(prior,old::Action::StarterOfferSeed,42)==old::Error::None);
  if(phase)CHECK(old::apply(prior,old::Action::Hatch,9)==old::Error::None);
  if(phase>1)CHECK(old::apply(prior,old::Action::Explore,1000)==old::Error::None);
  if(phase>2){prior.wildHp=prior.wildMaxHp/2;CHECK(old::apply(prior,old::Action::Flick,0)==old::Error::None);}
  old::Snapshot encoded;CHECK(old::encodeSnapshot(prior,encoded));std::uint8_t bytes[kV15SnapshotSize];std::memcpy(bytes,encoded.bytes,sizeof(bytes));bytes[4]=15;bytes[6]=112;bytes[7]=2;put32(bytes+sizeof(bytes)-4,crc(bytes,sizeof(bytes)-4));
  State decoded;CHECK(decodeSnapshot(bytes,sizeof(bytes),decoded)==SnapshotStatus::Migrated);CHECK(decoded.foregroundSequence==decoded.sequence&&!decoded.pendingEncounter.formId);
  Snapshot round;CHECK(encodeSnapshot(decoded,round));CHECK(snapshot_test::sameOldPayload(bytes,round.bytes,kV15SnapshotSize));
 }

 auto egg=newDevice();rejects(egg,Action::AccrueSteps,1,Error::WrongPhase);rejects(egg,Action::PresentEncounter,0,Error::WrongPhase);
 auto s=hatched(1);rejects(s,Action::AccrueSteps,0,Error::InvalidValue);rejects(s,Action::AccrueSteps,1001,Error::InvalidValue);rejects(s,Action::PresentEncounter,1,Error::InvalidValue);
 step(s,Action::AccrueSteps,1000);Snapshot bytes;CHECK(encodeSnapshot(s,bytes));
 for(unsigned field=0;field<3;++field){auto corrupt=bytes;put32(corrupt.bytes+snapshot_test::currentOffset(632)+field*4,field==0?9999:0);put32(corrupt.bytes+kSnapshotSize-4,crc(corrupt.bytes,kSnapshotSize-4));auto dest=s;CHECK(decodeSnapshot(corrupt.bytes,sizeof(corrupt.bytes),dest)==SnapshotStatus::InvalidState&&same(dest,s));}
 for(unsigned index=snapshot_test::currentOffset(632);index<sizeof(bytes.bytes);++index){auto corrupt=bytes;corrupt.bytes[index]^=1;auto dest=s;CHECK(decodeSnapshot(corrupt.bytes,sizeof(corrupt.bytes),dest)==SnapshotStatus::BadChecksum&&same(dest,s));}
 auto bad=s;bad.foregroundSequence=s.sequence+1;CHECK(!isValid(bad));bad=s;bad.pendingEncounter.formId=0;CHECK(!isValid(bad));bad=s;bad.encounterProgress=1;CHECK(!isValid(bad));bad=s;bad.pendingEncounter.rules=11;CHECK(!isValid(bad));
 s.sequence=UINT32_MAX;rejects(s,Action::AccrueSteps,1,Error::CounterOverflow);rejects(s,Action::PresentEncounter,0,Error::CounterOverflow);
 s.sequence=999;s.explorationSteps=UINT32_MAX;CHECK(isValid(s));rejects(s,Action::AccrueSteps,1,Error::CounterOverflow);
 Action parsed;CHECK(parseAction("accrue-steps",parsed)&&parsed==Action::AccrueSteps);CHECK(parseAction("present-encounter",parsed)&&parsed==Action::PresentEncounter);
 char json[kJsonCapacity];CHECK(writeJson(s,json,sizeof(json))&&std::strstr(json,"\"pendingEncounter\":{\"formId\":"));
}
}
int main(){pacingAndOneSlot();duringCurrentMatch();autoUnaffectedByBackground();carePartnerAndOff();migrationAndBounds();std::printf("%u queued encounter/migration checks, %u failures; State=%zu snapshot=%zu bytes\n",checks,failures,sizeof(State),kSnapshotSize);return failures?1:0;}
