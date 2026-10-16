#include "digi_modify.hpp"
#include "live_battle.hpp"
#include "battle_presentation.hpp"
#include "forms.hpp"
#include "trade.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace digivice;
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
unsigned form(const char* type){for(unsigned i=1;i<=forms::kFormCount;++i){auto* f=forms::find(i);if(f&&forms::productionForm(i)&&!std::strcmp(f->type,type)&&f->minLevel<=8)return i;}std::abort();}
State fixture(BattleMode mode=BattleMode::Tactical,unsigned seed=381,const char* target="ember"){
 auto s=newDevice(seed);CHECK(apply(s,Action::Hatch,1)==Error::None);CHECK(apply(s,Action::WorldSeed,171)==Error::None);
 auto& m=s.collection[0];m.level=s.level=8;m.xp=xpForLevel(8);m.hp=s.hp=combat::formProfile(m.formId,8).stats.maxHp;
 CHECK(apply(s,Action::Mode,unsigned(mode))==Error::None);CHECK(apply(s,Action::Walk,100)==Error::None);
 s.wildFormId=form(target);s.wildSpecies=static_cast<Species>(forms::find(s.wildFormId)->lineage);s.wildLevel=8;s.wildMaxHp=s.wildHp=combat::formProfile(s.wildFormId,8).stats.maxHp;CHECK(isValid(s));return s;
}
void reload(State& s){Snapshot bytes;CHECK(encodeSnapshot(s,bytes));CHECK(bytes.bytes[4]==32);State restored;CHECK(decodeSnapshot(bytes.bytes,sizeof bytes.bytes,restored)==SnapshotStatus::Ok);CHECK(trade::sameState(s,restored));s=restored;}
unsigned crc(const std::uint8_t* p,std::size_t n){unsigned c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
void seal(Snapshot& b){auto c=crc(b.bytes,sizeof b.bytes-4);for(unsigned i=0;i<4;++i)b.bytes[sizeof b.bytes-4+i]=c>>(8*i);}
void chart(){
 for(const char* type:{"grove","tide","ember","neutral"}){
  auto defender=form(type);auto element=combat::counterElement(defender);CHECK((element==combat::AttackElement::FromForm)==(!std::strcmp(type,"neutral")));
  for(auto move:{combat::Move::Physical,combat::Move::Heavy,combat::Move::Magic})for(auto guard:{combat::Defense::None,combat::Defense::Brace,combat::Defense::Counter,combat::Defense::Ward}){
   auto a=form("ember");auto normal=combat::resolveCareForms(a,8,defender,8,move,guard,{},{},4,2);
   auto hit=combat::resolveCareForms(a,8,defender,8,move,guard,{},{},4,2,element);
   CHECK(normal.reflected==hit.reflected);
   if(hit.reflected||element==combat::AttackElement::FromForm)CHECK(normal.damage==hit.damage&&normal.typePercent==hit.typePercent);
   else {
    CHECK(hit.typePercent==125);auto as=combat::formProfile(a,8).stats,ds=combat::formProfile(defender,8).stats;
    int raw=int(move==combat::Move::Magic?as.magic:as.attack)+int(move==combat::Move::Heavy?20:10)-int(move==combat::Move::Magic?ds.resistance:ds.defense);
    auto expected=unsigned(std::max(raw,4))*125/100;
    if((move==combat::Move::Physical&&guard==combat::Defense::Brace)||(move==combat::Move::Magic&&guard==combat::Defense::Ward))expected/=2;
    CHECK(hit.damage==std::max(1u,expected));
   }
  }
 }
 CHECK(combat::resolveCareForms(form("ember"),8,form("grove"),8,combat::Move::Magic,combat::Defense::None,{},{},4,0,static_cast<combat::AttackElement>(4)).damage==0);
}
void savesAndDamage(){
 for(unsigned seed=1;seed<=64;++seed){auto s=fixture(BattleMode::Tactical,seed),plain=s;const auto own=activeMember(s)->formId;auto ownType=combat::formProfile(own,8).type;
  CHECK(apply(s,Action::DigiModify,0xD106)==Error::None&&s.digiModifyReady==6&&s.cardUsed);reload(s);
  auto charged=s;CHECK(apply(s,Action::DigiModify,0xD102)==Error::CardAlreadyUsed&&trade::sameState(s,charged));
  const auto expected=previewWildAttack(s,combat::Move::Magic);const auto normal=previewWildAttack(plain,combat::Move::Magic);CHECK(expected.typePercent==125&&expected.damage>=normal.damage);
  CHECK(apply(s,Action::Magic)==Error::None&&!s.digiModifyReady);CHECK(apply(plain,Action::Magic)==Error::None&&plain.rngState==s.rngState);
  CHECK(activeMember(s)->formId==own&&!std::strcmp(combat::formProfile(own,8).type,ownType));
  battlepresentation::Sequencer play;CHECK(play.startTactical(charged,s,Action::Magic,0,0));CHECK(play.view().element==2);play.poll(350);CHECK(play.view().element==2);play.poll(1200);CHECK(!play.view().element);
  if(s.phase==Phase::Encounter)CHECK(charged.wildHp-s.wildHp==expected.damage);
 }
 auto s=fixture();CHECK(apply(s,Action::DigiModify,0xD106)==Error::None);Snapshot b;CHECK(encodeSnapshot(s,b));State out=fixture(),prior=out;
 for(unsigned version:{30u,31u}){b.bytes[4]=version;seal(b);CHECK(decodeSnapshot(b.bytes,sizeof b.bytes,out)==SnapshotStatus::InvalidState&&trade::sameState(out,prior));}
 auto neutral=fixture(BattleMode::Tactical,381,"neutral");prior=neutral;CHECK(apply(neutral,Action::DigiModify,0xD106)==Error::InvalidAction&&trade::sameState(neutral,prior));
 auto advantaged=fixture(BattleMode::Tactical,381,"grove");prior=advantaged;CHECK(combat::formTypePercent(activeMember(advantaged)->formId,advantaged.wildFormId)==125);CHECK(apply(advantaged,Action::DigiModify,0xD106)==Error::InvalidAction&&trade::sameState(advantaged,prior));
}
void boundaries(){
 auto s=fixture();CHECK(apply(s,Action::DigiModify,0xD106)==Error::None);s.energy=s.collection[0].energy=0;auto old=s;CHECK(apply(s,Action::Heavy)==Error::LowEnergy&&trade::sameState(s,old));s.energy=s.collection[0].energy=80;
 for(unsigned t=0;t<3;++t){s.wildTurn=t;if(wildGuard(s)==combat::Defense::Counter)break;}
 old=s;auto plain=s;plain.digiModifyReady=0;plain.message=Message::Encounter;CHECK(isValid(plain));CHECK(previewWildAttack(s,combat::Move::Heavy).damage==previewWildAttack(plain,combat::Move::Heavy).damage);
 CHECK(apply(s,Action::Heavy)==Error::None&&s.digiModifyReady==6);battlepresentation::Sequencer reflect;CHECK(reflect.startTactical(old,s,Action::Heavy,0,0)&&!reflect.view().element);reload(s);
 s.wildHp=s.wildMaxHp/2;CHECK(apply(s,Action::Flick,0)==Error::None&&s.digiModifyReady==6);CHECK(apply(s,Action::Retreat)==Error::None&&!s.digiModifyReady&&!s.cardUsed);
 auto automatic=fixture(BattleMode::Auto);CHECK(apply(automatic,Action::DigiModify,0xD106)==Error::None);old=automatic;autobattle::Trace trace;CHECK(applyAutoStep(automatic,0,&trace)==Error::None);CHECK(trace.count==1&&trace.steps[0].element==2&&!automatic.digiModifyReady);
 battlepresentation::Sequencer autoPlay;CHECK(autoPlay.startAuto(trace,0,&old)&&autoPlay.view().element==2);trace.steps[0].element=4;battlepresentation::Sequencer bad;CHECK(!bad.startAuto(trace,0));
 // Authority policy remains symmetric: no host-local or guest-local effect.
 for(auto authority:{digimodify::Authority::NearbyHost,digimodify::Authority::NearbyGuest,digimodify::Authority::Practice}){State candidate;CHECK(digimodify::prepare(old,authority,true,0xD106,candidate)==digimodify::Result::Unavailable);}
 bool found=false;for(unsigned seed=1;seed<32&&!found;++seed){auto run=fixture(BattleMode::Tactical,seed);CHECK(apply(run,Action::Retreat)==Error::None);run.dungeonKeys=3;CHECK(apply(run,Action::StartExpedition,1)==Error::None);if(apply(run,Action::DigiModify,0xD106)!=Error::None)continue;found=true;reload(run);run.wildHp=1;CHECK(apply(run,Action::Attack)==Error::None&&run.phase==Phase::Expedition&&!run.digiModifyReady&&!run.cardUsed);CHECK(apply(run,Action::NextExpedition)==Error::None&&!run.digiModifyReady);}
 CHECK(found);
}
int main(){chart();savesAndDamage();boundaries();std::printf("Element Shift: %u checks passed; existing chart, guarded rounding, snapshots, 64 seeds, reflection, capture, Auto, authority and dungeon boundaries.\n",checks);}
