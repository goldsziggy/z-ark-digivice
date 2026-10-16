#include "digi_modify.hpp"
#include "live_battle.hpp"
#include "forms.hpp"
#include "trade.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace digivice;
namespace dm=digimodify;
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
State fixture(unsigned seed=381,BattleMode mode=BattleMode::Tactical){
 auto s=newDevice(seed);CHECK(apply(s,Action::Hatch,1)==Error::None);CHECK(apply(s,Action::WorldSeed,171)==Error::None);
 auto& m=s.collection[0];m.level=s.level=8;m.xp=xpForLevel(8);m.hp=s.hp=combat::formProfile(m.formId,8).stats.maxHp;
 CHECK(apply(s,Action::Mode,unsigned(mode))==Error::None);CHECK(apply(s,Action::Walk,100)==Error::None);
 s.wildFormId=forms::initialForm(6);s.wildSpecies=Species::Agumon;s.wildLevel=8;s.wildMaxHp=s.wildHp=combat::formProfile(s.wildFormId,8).stats.maxHp;
 CHECK(isValid(s));return s;
}
void reload(State& s){Snapshot b;CHECK(encodeSnapshot(s,b));State out;CHECK(decodeSnapshot(b.bytes,sizeof b.bytes,out)==SnapshotStatus::Ok);CHECK(trade::sameState(s,out));s=out;}
void cards(){
 for(unsigned id=0xD101;id<=0xD104;++id){
  auto s=fixture();if(id==0xD104){--s.hp;s.collection[0].hp=s.hp;}
  const auto before=s;CHECK(apply(s,Action::DigiModify,id)==Error::None);CHECK(s.sequence==before.sequence+1&&s.cardUsed);
  reload(s);const auto charged=s;CHECK(apply(s,Action::DigiModify,id)==Error::CardAlreadyUsed&&trade::sameState(s,charged));
  CHECK(apply(s,Action::Card,1)==Error::CardAlreadyUsed);CHECK(apply(s,Action::Card,2)==Error::CardAlreadyUsed);
  CHECK(apply(s,Action::Retreat)==Error::None);CHECK(!s.digiModifyReady&&!s.cardUsed);reload(s);
  CHECK(apply(s,Action::Walk,100)==Error::None&&!s.digiModifyReady&&!s.cardUsed);
 }
 auto full=fixture(),before=full;CHECK(apply(full,Action::DigiModify,0xD104)==Error::InvalidAction&&trade::sameState(full,before));
 CHECK(apply(full,Action::DigiModify,0xD105)==Error::InvalidValue&&trade::sameState(full,before));
 CHECK(apply(full,Action::DigiModify,0xD107)==Error::InvalidValue&&trade::sameState(full,before));
 auto hurt=fixture();const auto max=combat::formProfile(hurt.collection[0].formId,hurt.level).stats.maxHp;hurt.hp=hurt.collection[0].hp=max-1;
 CHECK(apply(hurt,Action::DigiModify,0xD104)==Error::None&&hurt.hp==max&&hurt.collection[0].hp==max);reload(hurt);
 hurt=fixture();hurt.hp=hurt.collection[0].hp=max/2;const auto hp=hurt.hp;CHECK(apply(hurt,Action::DigiModify,0xD104)==Error::None);CHECK(hurt.hp==hp+max/10&&!hurt.digiModifyReady);
 auto pending=fixture(381,BattleMode::Auto);pending.wildHp=pending.wildMaxHp/2;pending.wildTurn=1;pending.autoCapture=AutoCapture::Awaiting;CHECK(isValid(pending));before=pending;
 CHECK(apply(pending,Action::DigiModify,0xD101)==Error::InvalidAction&&trade::sameState(pending,before));
}
void mechanics(){
 for(unsigned seed=1;seed<=128;++seed){
  auto raw=fixture(seed),power=raw;CHECK(apply(power,Action::DigiModify,0xD102)==Error::None);
  const auto prediction=previewWildAttack(power,combat::Move::Physical);CHECK(prediction.damage>=previewWildAttack(raw,combat::Move::Physical).damage);
  CHECK(apply(raw,Action::Attack)==Error::None&&apply(power,Action::Attack)==Error::None);
  CHECK(raw.rngState==power.rngState&&!power.digiModifyReady);
  if(power.phase==Phase::Encounter)CHECK(power.wildHp==power.wildMaxHp-prediction.damage);
  auto guarded=fixture(seed);raw=guarded;CHECK(apply(guarded,Action::DigiModify,0xD103)==Error::None);
  auto max=combat::formProfile(raw.collection[0].formId,raw.level).stats.maxHp;const auto beforeHp=raw.hp;
  CHECK(apply(raw,Action::Attack)==Error::None&&apply(guarded,Action::Attack)==Error::None);
  CHECK(raw.rngState==guarded.rngState&&raw.wildHp==guarded.wildHp&&!guarded.digiModifyReady);
  if(raw.phase==Phase::Encounter){auto damage=beforeHp-raw.hp;auto reduced=std::min((damage*30+99)/100,(max+4)/5);CHECK(guarded.hp==raw.hp+reduced);}
 }
 for(unsigned id:{0xD101u,0xD102u}){
  auto reflected=fixture();for(unsigned t=0;t<3;++t){reflected.wildTurn=t;if(wildGuard(reflected)==combat::Defense::Counter)break;}
  CHECK(wildGuard(reflected)==combat::Defense::Counter);CHECK(apply(reflected,Action::DigiModify,id)==Error::None);
  const auto pending=reflected.digiModifyReady;CHECK(apply(reflected,Action::Heavy)==Error::None);CHECK(reflected.digiModifyReady==pending);reload(reflected);
 }
 auto guard=fixture();for(unsigned t=0;t<3;++t){guard.wildTurn=t;if(wildGuard(guard)==combat::Defense::Counter)break;}
 CHECK(apply(guard,Action::DigiModify,0xD103)==Error::None);CHECK(apply(guard,Action::Heavy)==Error::None&&!guard.digiModifyReady);
 // Invalid pending enum, absent allowance and off-battle charges never serialize.
 for(unsigned effect=1;effect<=4;++effect){auto s=fixture();s.digiModifyReady=effect;CHECK(!isValid(s));s.cardUsed=true;CHECK(isValid(s)==(effect<=3));}
}
void dungeon(){
 for(auto mode:{BattleMode::Tactical,BattleMode::Auto}){
  auto s=fixture(91,mode);CHECK(apply(s,Action::Retreat)==Error::None);s.dungeonKeys=3;
  CHECK(apply(s,Action::StartExpedition,0)==Error::None);
  for(unsigned tier=1;tier<=3;++tier){
   CHECK(s.expeditionTier==tier&&!s.digiModifyReady&&!s.cardUsed);CHECK(apply(s,Action::DigiModify,0xD100+tier)==Error::None);reload(s);
   // Isolate each real terminal transition; full-health three-fight run is exercised by the simulator.
   s.wildHp=1;CHECK(isValid(s));
   if(mode==BattleMode::Tactical)CHECK(apply(s,Action::Attack)==Error::None);else{autobattle::Trace trace;CHECK(applyAutoStep(s,0,&trace)==Error::None);}
   CHECK(!s.digiModifyReady&&!s.cardUsed);reload(s);
   if(tier<3){CHECK(s.phase==Phase::Expedition);auto prior=s;CHECK(apply(s,Action::DigiModify,0xD101)==Error::WrongPhase&&trade::sameState(s,prior));CHECK(apply(s,Action::NextExpedition)==Error::None);}
  }CHECK(s.phase==Phase::Home&&s.dungeonClears==1&&s.dungeonKeys==2);
 }
}
int main(){cards();mechanics();dungeon();std::printf("Digi effects: %u assertions passed; 128 seeded damage comparisons, durable limits, full/partial healing, reflection, both dungeon modes.\n",checks);}
