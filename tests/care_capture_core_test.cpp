#include "game.hpp"
#include "legacy_v11.hpp"
#include "legacy_v12.hpp"
#include "forms.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace digivice;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)
bool same(const State&a,const State&b){Snapshot x,y;return encodeSnapshot(a,x)&&encodeSnapshot(b,y)&&!std::memcmp(x.bytes,y.bytes,sizeof(x.bytes));}
void step(State&s,Action a,unsigned v=0){CHECK(apply(s,a,v)==Error::None);CHECK(isValid(s));}
void reject(State&s,Action a,unsigned v,Error e){const auto before=s;CHECK(apply(s,a,v)==e);CHECK(same(before,s));}
void restore(State&s){Snapshot bytes;CHECK(encodeSnapshot(s,bytes));State out;CHECK(decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),out)==SnapshotStatus::Ok);CHECK(same(s,out));s=out;}
void sync(State&s){auto&m=*const_cast<CreatureMember*>(activeMember(s));m.hp=s.hp;m.energy=s.energy;m.fullness=s.fullness;m.mood=s.mood;m.bond=s.bond;m.level=s.level;}
State hatched(unsigned seed=1){auto s=newDevice(seed);step(s,Action::Hatch,1);return s;}
State fight(unsigned seed=1){auto s=hatched(seed);step(s,Action::Explore,1000);CHECK(s.wildRules==kRulesVersion);return s;}
void target(State&s,unsigned form,unsigned level){s.wildFormId=form;s.wildSpecies=static_cast<Species>(forms::find(form)->lineage);s.wildLevel=level;s.wildHp=s.wildMaxHp=combat::formProfile(form,level).stats.maxHp;CHECK(isValid(s));}
unsigned formWith(encounters::Rarity rarity){for(unsigned id=forms::kFirstProductionFormId;id<=forms::kFormCount;++id)if(encounters::rarityForForm(id)==rarity&&combat::validFormProfile(id,1))return id;return 0;}
void care(){
 for(unsigned bond: {0u,49u,50u,99u,100u,149u,150u,199u,200u})for(unsigned fullness:{0u,79u,80u,100u})for(unsigned mood:{0u,79u,80u,100u}){
  const auto b=combat::careBonus(bond,fullness,mood);CHECK(b.offense==bond/50+(mood>=80));CHECK(b.protection==bond/50+(fullness>=80));CHECK(combat::validCareBonus(b));
 }
 CHECK(!combat::validCareBonus({6,0})&&!combat::validCareBonus({0,6}));
 auto s=hatched();s.fullness=s.mood=0;s.energy=0;s.bond=0;sync(s);CHECK(memberCare(*activeMember(s)).offense==0&&memberCare(*activeMember(s)).protection==0);
 reject(s,Action::Play,0,Error::LowEnergy);step(s,Action::Rest);step(s,Action::Play);CHECK(s.bond==6&&s.mood==12&&s.energy==20);
 s.mood=s.fullness=100;s.energy=100;sync(s);const auto before=s;
 for(unsigned i=0;i<100;++i){step(s,Action::Play);step(s,Action::Rest);step(s,Action::Feed);}CHECK(s.bond==before.bond&&s.energy==100&&s.collection[0].xp==0&&s.level==1);
 s.energy=0;sync(s);step(s,Action::Play);CHECK(s.energy==0);restore(s);
 s.bond=200;s.energy=100;sync(s);const auto base=combat::formProfile(11,1),effective=memberBattleProfile(s,*activeMember(s));CHECK(effective.stats.attack==base.stats.attack+5&&effective.stats.defense==base.stats.defense+5&&effective.stats.magic==base.stats.magic+5&&effective.stats.resistance==base.stats.resistance+5&&effective.stats.maxHp==base.stats.maxHp);
 // Two identical forms are distinct owned instances; care changes only selection.
 s.captures=s.encounters=1;s.steps=100;s.collectionCount=2;s.nextMemberId=3;s.collection[1]=s.collection[0];s.collection[1].id=2;s.collection[1].capturedAtSequence=1;s.collection[1].bond=0;s.collection[1].mood=s.collection[1].fullness=0;CHECK(isValid(s));
 step(s,Action::Select,2);CHECK(memberCare(*activeMember(s)).offense==0);step(s,Action::Feed);CHECK(s.bond==2&&s.collection[0].bond==200);restore(s);step(s,Action::Select,1);CHECK(s.bond==200&&memberCare(*activeMember(s)).offense==5);
 // Bond alone never bypasses level gates and never adds XP.
 const auto* edge=forms::outgoing(11,0);CHECK(edge&&edge->minLevel>1);reject(s,Action::Evolve,edge->to,Error::EvolutionUnavailable);CHECK(s.collection[0].xp==0);
 for(unsigned level:{1u,5u}){
  const auto p=combat::formProfile(11,level);const auto ordinary=combat::resolveForms(11,level,11,level,combat::Move::Physical,combat::Defense::None);
  const auto boosted=combat::resolveCareForms(11,level,11,level,combat::Move::Physical,combat::Defense::None,{5,5},{});
  const auto protectedHit=combat::resolveCareForms(11,level,11,level,combat::Move::Physical,combat::Defense::None,{}, {5,5});
  CHECK(boosted.damage>=ordinary.damage&&protectedHit.damage<=ordinary.damage);
  std::printf("Impmon Lv%u same-form physical: base ATK%u DEF%u; neutral hit%u, offense+5 hit%u, protection+5 received%u (base HP%u unchanged)\n",level,p.stats.attack,p.stats.defense,ordinary.damage,boosted.damage,protectedHit.damage,p.stats.maxHp);
 }
 // Same form on both sides must not accidentally give wild actor player's Care.
 auto duel=fight(4);target(duel,11,1);duel.bond=200;duel.fullness=duel.mood=100;sync(duel);const auto old=duel;const auto outgoing=combat::resolveCareForms(11,1,11,1,combat::Move::Magic,wildGuard(duel),{5,5},{});const auto incoming=combat::resolveCareForms(11,1,11,1,combat::Move::Physical,combat::Defense::None,{}, {5,5});step(duel,Action::Magic);CHECK(duel.wildHp==old.wildHp-outgoing.damage&&duel.hp==old.hp-incoming.damage);
}
void oddsAndThrows(){
 unsigned ids[]{formWith(encounters::Rarity::Common),formWith(encounters::Rarity::Uncommon),formWith(encounters::Rarity::Rare)};for(auto id:ids)CHECK(id);
 for(unsigned bucket=0;bucket<3;++bucket){
  unsigned previousLevel=100;
  for(unsigned level=1;level<=20;++level){auto s=fight();target(s,ids[bucket],level);s.wildHp=s.wildMaxHp/2;const auto base=50+40*(s.wildMaxHp-2*s.wildHp)/s.wildMaxHp,penalty=bucket*10+5*((level-1)>5?5:level-1);const auto expected=base>penalty+10?base-penalty:10;
   CHECK(captureChance(s)==expected&&captureChance(s)<=90&&captureChance(s)>=10);CHECK(captureChance(s)<=previousLevel+1);previousLevel=captureChance(s);
   auto previous=captureChance(s);while(s.wildHp>1){--s.wildHp;const auto chance=captureChance(s);CHECK(chance>=previous&&chance<=90);previous=chance;}
  }
 }
 for(unsigned level:{1u,2u})for(unsigned index:{0u,2u}){auto s=fight();target(s,ids[index],level);s.wildHp=s.wildMaxHp/2;std::printf("Capture %s Lv%u vs partnerLv1: halfHP%u%%",forms::find(ids[index])->name,level,captureChance(s));s.wildHp=s.wildMaxHp/4;std::printf(" quarterHP%u%%\n",captureChance(s));}
 auto misses=fight();misses.wildHp=misses.wildMaxHp/2;misses.hp=1;sync(misses);const auto rng=misses.rngState,seq=misses.sequence;
 for(unsigned attempt=1;attempt<=3;++attempt){const auto prior=misses;auto replay=misses;restore(replay);step(misses,Action::Flick,0);step(replay,Action::Flick,0);CHECK(same(misses,replay)&&misses.hp==1&&misses.rngState==rng&&misses.lastCapture.result==CaptureResult::Miss&&misses.lastCapture.chance==0&&misses.lastCapture.attempt==attempt&&misses.lastCapture.sequence==seq+attempt);CHECK(misses.lastCapture.targetFormId==prior.wildFormId&&misses.lastCapture.targetLevel==prior.wildLevel);restore(misses);if(attempt<3)CHECK(misses.phase==Phase::Encounter&&misses.wildTurn==0);}
 CHECK(misses.phase==Phase::Home&&misses.message==Message::CaptureEnded&&misses.captures==0&&misses.collection[0].xp==0);reject(misses,Action::Flick,0,Error::WrongPhase);
 unsigned escaped=0,caught=0;
 for(unsigned seed=1;seed<=128;++seed){auto s=fight(seed);s.wildHp=s.wildMaxHp/2;const auto before=s;auto retry=s;const auto chance=captureChance(s);auto expected=s.rngState;expected^=expected<<13;expected^=expected>>17;expected^=expected<<5;step(s,Action::Capture);step(retry,Action::Capture);CHECK(same(s,retry)&&s.rngState==expected&&s.lastCapture.chance==chance&&s.lastCapture.attempt==1);CHECK((s.lastCapture.result==CaptureResult::Captured)==(expected%100<chance));if(s.phase==Phase::Home){++caught;CHECK(s.captures==1&&s.collectionCount==2);}else{++escaped;CHECK(s.hp==before.hp&&s.wildHp==before.wildHp&&s.wildTurn==before.wildTurn);}restore(s);}
 CHECK(escaped&&caught);
}
void autoAndMigration(){
 unsigned three=0,captured=0;
 for(unsigned seed=1;seed<=256;++seed){
  auto s=hatched(seed);step(s,Action::Mode,1);step(s,Action::Explore,1000);const auto before=s;auto retry=s;restore(retry);autobattle::Trace a,b;CHECK(applyAuto(s,&a)==Error::None&&applyAuto(retry,&b)==Error::None&&same(s,retry));CHECK(s.sequence==before.sequence+1&&a.combatRulesVersion==12&&a.playerOffenseBonus==1);
  char x[autobattle::kTraceJsonCapacity],y[autobattle::kTraceJsonCapacity];CHECK(autobattle::writeJson(a,x,sizeof(x))&&autobattle::writeJson(b,y,sizeof(y))&&!std::strcmp(x,y));CHECK(std::strstr(x,"\"playerCare\":{\"offenseBonus\":1"));
  for(unsigned i=0;i<a.count;++i){const auto& f=a.steps[i];if(f.action==autobattle::Move::Capture){CHECK(f.captureAttempt>=1&&f.captureAttempt<=3&&f.captureChance>=10&&f.captureResult>=2&&f.opponentAction==autobattle::Move::None&&f.playerHpAfter==f.playerHpBefore);if(f.captureAttempt==3&&!f.captured){++three;CHECK(i+1==a.count&&s.message==Message::CaptureEnded&&s.hp==f.playerHpBefore&&a.outcome==autobattle::Outcome::Retreated);}captured+=f.captured;}}
  if(s.lastCapture.result!=CaptureResult::None)CHECK(s.lastCapture.sequence==s.sequence);restore(s);
  namespace old=legacy_v11;auto prior=old::newDevice(seed);CHECK(old::apply(prior,old::Action::Hatch,1)==old::Error::None);CHECK(old::apply(prior,old::Action::Mode,1)==old::Error::None);CHECK(old::apply(prior,old::Action::Explore,1000)==old::Error::None);old::Snapshot bytes;CHECK(old::encodeSnapshot(prior,bytes));legacy_v12::State migrated;CHECK(legacy_v12::decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),migrated)==legacy_v12::SnapshotStatus::Migrated&&migrated.wildRules==11&&migrated.lastCapture.result==legacy_v12::CaptureResult::None);CHECK(old::applyAuto(prior,&a)==old::Error::None&&legacy_v12::applyAuto(migrated,&b)==legacy_v12::Error::None);CHECK(autobattle::writeJson(a,x,sizeof(x))&&autobattle::writeJson(b,y,sizeof(y))&&!std::strcmp(x,y));legacy_v12::Snapshot now;CHECK(old::encodeSnapshot(prior,bytes)&&legacy_v12::encodeSnapshot(migrated,now)&&!std::memcmp(bytes.bytes+12,now.bytes+12,sizeof(bytes.bytes)-16));CHECK(!std::strstr(x,"playerCare")&&!std::strstr(x,"\"capture\":{"));
 }
 CHECK(three>0&&captured>0);std::printf("256 new Auto fights: %u third-failure exits, %u captures; 256 frozen11 traces/states identical\n",three,captured);
}
void offers(){
 unsigned seen[277]{};
 for(unsigned seed=1;seed<=128;++seed){auto egg=newDevice(seed);const auto rng=egg.rngState;reject(egg,Action::StarterOfferSeed,0,Error::InvalidValue);step(egg,Action::StarterOfferSeed,seed*4321);CHECK(egg.sequence==1&&egg.rngState==rng&&egg.encounterRng==0);restore(egg);reject(egg,Action::StarterOfferSeed,seed,Error::InvalidAction);reject(egg,Action::Hatch,12,Error::InvalidValue);
  for(unsigned i=0;i<3;++i){const auto id=egg.starterOffers[i];CHECK(id<277&&validStarterOfferForm(id)&&combat::validFormProfile(id,1)&&forms::find(id)->stage==forms::Stage::Rookie&&forms::outgoing(id,0));++seen[id];for(unsigned j=0;j<i;++j)CHECK(id!=egg.starterOffers[j]);for(unsigned fixed=1;fixed<=8;++fixed)CHECK(id!=starterForm(egg,fixed));}
  for(unsigned slot=1;slot<=11;++slot){auto chosen=egg;const auto form=starterForm(chosen,slot);step(chosen,Action::Hatch,slot);CHECK(chosen.collectionCount==1&&chosen.collection[0].formId==form&&chosen.starterId==slot&&chosen.sequence==2&&chosen.collection[0].capturedAtSequence==0);restore(chosen);reject(chosen,Action::StarterOfferSeed,9,Error::WrongPhase);reject(chosen,Action::Hatch,slot,Error::AlreadyHatched);const auto* edge=forms::outgoing(form,0);CHECK(edge);auto&m=chosen.collection[0];m.level=chosen.level=edge->minLevel;m.xp=xpForLevel(m.level);m.bond=chosen.bond=edge->minBond;m.hp=chosen.hp=combat::formProfile(form,m.level).stats.maxHp;CHECK(isValid(chosen));step(chosen,Action::Evolve,edge->to);restore(chosen);}
 }
 unsigned distinct=0;for(unsigned id=1;id<=forms::kFormCount;++id){distinct+=seen[id]>0;CHECK((seen[id]>0)==validStarterOfferForm(id));}CHECK(distinct==32);std::printf("128 one-time offer seeds cover all%u candidates; all11 slots hatch/evolve/restart\n",distinct);
 namespace old=legacy_v11;for(unsigned hatch:{0u,1u}){auto prior=old::newDevice(999);if(hatch)CHECK(old::apply(prior,old::Action::Hatch,8)==old::Error::None);old::Snapshot bytes;CHECK(old::encodeSnapshot(prior,bytes));State s;CHECK(decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),s)==SnapshotStatus::Migrated&&s.starterOfferSeed==0);if(hatch)CHECK(s.starterId==8&&s.collection[0].formId==60);else{step(s,Action::StarterOfferSeed,44);CHECK(s.phase==Phase::Egg&&s.collectionCount==0);}}
}
void malformed(){
 auto s=newDevice(2);step(s,Action::StarterOfferSeed,55);auto bad=s;bad.starterOffers[0]=bad.starterOffers[1];CHECK(!isValid(bad));bad=s;bad.starterOfferSeed=0;CHECK(!isValid(bad));
 step(s,Action::Hatch,9);step(s,Action::Explore,1000);s.wildHp=1;step(s,Action::Flick,0);CHECK(s.lastCapture.result==CaptureResult::Miss);
 Snapshot bytes;CHECK(encodeSnapshot(s,bytes));for(unsigned offset=596;offset<sizeof(bytes.bytes);++offset){auto corrupt=bytes;corrupt.bytes[offset]^=1;auto out=s;CHECK(decodeSnapshot(corrupt.bytes,sizeof(corrupt.bytes),out)!=SnapshotStatus::Ok&&same(s,out));}
 bad=s;bad.lastCapture.sequence=s.sequence+1;CHECK(!isValid(bad));bad=s;bad.lastCapture.chance=1;CHECK(!isValid(bad));bad=s;bad.lastCapture.attempt=4;CHECK(!isValid(bad));bad=s;bad.lastCapture.targetLevel=21;CHECK(!isValid(bad));bad=s;bad.lastCapture.result=static_cast<CaptureResult>(4);CHECK(!isValid(bad));
 char json[kJsonCapacity];CHECK(writeJson(s,json,sizeof(json))&&std::strstr(json,"\"lastCapture\":{")&&std::strstr(json,"\"care\":{")&&std::strstr(json,"\"offers\":["));
 std::printf("Measured State%zu / Snapshot%zu / Trace%zu / Step%zu / JSON capacity%zu\n",sizeof(State),sizeof(Snapshot),sizeof(autobattle::Trace),sizeof(autobattle::Step),kJsonCapacity);
}
}
int main(){care();oddsAndThrows();autoAndMigration();offers();malformed();std::printf("%u Care/capture/offers checks, %u failures\n",checks,failures);return failures?1:0;}
