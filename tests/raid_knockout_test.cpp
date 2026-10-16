#include "expedition_raid.hpp"
#include "battle_trace.hpp"
#include "expeditions.hpp"
#include "combat.hpp"
#include "forms.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace digivice;
using namespace digivice::nearby;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)
constexpr Fighter host{17,18,10,2,2},guest{23,18,10,2,2};
constexpr Fighter strongHost{17,18,99,5,5},strongGuest{23,25,99,5,5};
void roundTrip(const Raid& r){std::uint8_t bytes[kRaidBytes];Raid copy;CHECK(encodeRaid(r,bytes,sizeof(bytes)));CHECK(decodeRaid(bytes,sizeof(bytes),copy)&&sameRaid(r,copy));}
Choice choices[2];
bool basic(Raid& r,Choice strike=Choice::Physical,Choice guard=Choice::Brace){
 choices[0]=choices[1]=Choice::None;const auto a=raidAttacker(r),d=raidDefender(r);
 if(a>=2)return false;choices[a]=strike;if(d<2)choices[d]=guard;return resolveRaid(r,r.sequence,choices[0],choices[1]);
}
std::uint32_t replyDamage(const Raid& before,unsigned target,combat::Defense guard){
 const auto& p=before.players[target];const auto maximum=combat::formProfile(p.formId,p.level).stats.maxHp;
 const auto hit=combat::resolveCareForms(before.foeForm,before.foeLevel,p.formId,p.level,
  before.sequence%2u?combat::Move::Magic:combat::Move::Physical,guard,{}, {p.offenseBonus,p.protectionBonus},
  std::clamp((maximum+23u)/24u,4u,32u),std::min(16u,before.foeLevel/3u));
 return std::max(1u,hit.damage*(100u+std::min(40u,4u*before.foeLevel))/100u);
}
void skipRolesAndRejectGhostInput(){
 for(unsigned dead=0;dead<2;++dead){
  Raid r;CHECK(beginRaid(strongHost,strongGuest,raidSeed(25,99,0),r));r.hp[dead]=0;r.energy[dead]=0;const auto alive=1u-dead;
  CHECK(validRaid(r)&&raidAttacker(r)==alive&&raidDefender(r)==kNoRaidActor&&!raidEnergyExhausted(r));
  CHECK(raidLastAttacker(r)==kNoRaidActor);
  for(unsigned c=0;c<=6;++c)CHECK(!legalRaidChoice(r,dead,static_cast<Choice>(c)));
  CHECK(legalRaidChoice(r,alive,Choice::Physical)&&legalRaidChoice(r,alive,Choice::Magic)&&!legalRaidChoice(r,alive,Choice::Brace));
  const auto before=r;
  CHECK(!resolveRaid(r,r.sequence,Choice::Physical,Choice::Brace)&&sameRaid(r,before));
  CHECK(!resolveRaid(r,r.sequence,Choice::Brace,Choice::Physical)&&sameRaid(r,before));
  CHECK(!resolveRaid(r,r.sequence,Choice::None,Choice::None)&&sameRaid(r,before));
  CHECK(basic(r));CHECK(r.sequence==before.sequence+1&&r.lastDefense==Choice::None&&raidLastAttacker(r)==alive);
  CHECK(r.hp[dead]==0&&r.energy[dead]==0&&r.energy[alive]==before.energy[alive]-2);
  CHECK(r.foeHp>0&&r.foeTarget==alive&&r.lastPlayerDamage==std::min(before.hp[alive],replyDamage(before,alive,combat::Defense::None)));
  roundTrip(r);
 }
}
void liveSurvivorCarriesEveryTier(){
 for(unsigned alive=0;alive<2;++alive)for(unsigned scenario=0;scenario<6;++scenario){
  Raid r;CHECK(beginRaid(strongHost,strongGuest,raidSeed(68,1,scenario),r));const auto dead=1u-alive;r.hp[dead]=0;r.energy[dead]=0;
  const auto initialHp=r.hp[alive];unsigned count=0;
  while(r.status==RaidStatus::Active&&count++<72){
   const auto before=r;CHECK(raidAttacker(r)==alive&&raidDefender(r)==kNoRaidActor);CHECK(basic(r));
   CHECK(r.lastDefense==Choice::None&&raidLastAttacker(r)==alive&&r.hp[dead]==0&&r.energy[dead]==0);
   CHECK(r.lastTier==before.tier&&r.lastFoeForm==before.foeForm&&r.lastFoeLevel==before.foeLevel);
   CHECK(r.hp[alive]==initialHp&&r.energy[alive]==100-2*r.sequence);
   const auto after=r;CHECK(!resolveRaid(r,before.sequence,choices[0],choices[1])&&sameRaid(r,after));roundTrip(r);
  }
  CHECK(r.status==RaidStatus::Cleared&&r.tier==expeditions::tierCount(scenario)&&count==expeditions::tierCount(scenario));
  // A terminal result with no living partner may never claim a clear.
  auto impossible=r;impossible.hp[0]=impossible.hp[1]=0;CHECK(!validRaid(impossible));
  impossible.status=RaidStatus::Wiped;CHECK(validRaid(impossible));roundTrip(impossible);
 }
}
Raid reflectionSetup(){
 Raid r;CHECK(beginRaid(strongHost,strongGuest,raidSeed(25,99,0),r));
 CHECK(basic(r));CHECK(basic(r));CHECK(r.status==RaidStatus::Active&&r.sequence==2&&r.hp[0]&&r.hp[1]);return r;
}
void reflectionKnockouts(){
 {auto r=reflectionSetup();r.hp[0]=1;CHECK(validRaid(r));const auto before=r;
  CHECK(resolveRaid(r,r.sequence,Choice::Heavy,Choice::Brace));
  CHECK(r.hp[0]==0&&r.hp[1]>0&&r.status==RaidStatus::Active&&r.lastDefense==Choice::Brace);
  CHECK(raidLastAttacker(r)==0&&raidAttacker(r)==1&&raidDefender(r)==kNoRaidActor);
  CHECK(r.foeTarget==1&&r.lastPlayerDamage==std::min(before.hp[1],replyDamage(before,1,combat::Defense::Brace)));
  CHECK(r.hp[1]==before.hp[1]-r.lastPlayerDamage&&r.foeHp==before.foeHp);roundTrip(r);
  CHECK(basic(r));CHECK(r.lastDefense==Choice::None&&raidLastAttacker(r)==1&&r.foeTarget==1);roundTrip(r);
 }
 {auto r=reflectionSetup();r.hp[0]=1;r.hp[1]=0;CHECK(validRaid(r));const auto before=r;
  CHECK(resolveRaid(r,r.sequence,Choice::Heavy,Choice::None));
  CHECK(r.status==RaidStatus::Wiped&&!r.hp[0]&&!r.hp[1]&&r.foeHp==before.foeHp);
  CHECK(r.lastPlayerDamage==1&&r.foeTarget==0&&r.lastDefense==Choice::None&&raidLastAttacker(r)==0);
  CHECK(raidAttacker(r)==kNoRaidActor&&raidDefender(r)==kNoRaidActor&&!raidEnergyExhausted(r));roundTrip(r);
  const auto after=r;CHECK(!resolveAutoRaid(r,r.sequence)&&sameRaid(r,after));
 }
 {auto r=reflectionSetup();r.hp[0]=r.hp[1]=1;CHECK(validRaid(r));
  CHECK(resolveRaid(r,r.sequence,Choice::Heavy,Choice::Brace));
  CHECK(r.status==RaidStatus::Wiped&&!r.hp[0]&&!r.hp[1]&&r.lastDefense==Choice::Brace&&raidLastAttacker(r)==0);roundTrip(r);
 }
}
void survivorEnergy(){
 for(unsigned alive=0;alive<2;++alive){
  Raid r;CHECK(beginRaid(strongHost,strongGuest,raidSeed(68,1,0),r));const auto dead=1u-alive;r.hp[dead]=0;r.energy[dead]=100;r.energy[alive]=2;
  CHECK(validRaid(r)&&!raidEnergyExhausted(r));CHECK(basic(r));
  CHECK(r.tier==2&&r.status==RaidStatus::Wiped&&r.energy[alive]==0&&r.energy[dead]==100&&raidEnergyExhausted(r));
  CHECK(!legalRaidChoice(r,alive,Choice::Physical));roundTrip(r);
  Raid boss;CHECK(beginRaid(strongHost,strongGuest,raidSeed(68,1,3),boss));boss.hp[dead]=0;boss.energy[alive]=2;
  CHECK(basic(boss)&&boss.status==RaidStatus::Cleared&&boss.energy[alive]==0);roundTrip(boss);
 }
}
// Frozen pre-repair Auto policy, only to reconstruct historical regression
// states. Production Auto must never select a strictly dominated basic move.
bool priorAuto(Raid& r) {
 Raid n=r;const auto a=raidAttacker(n),d=raidDefender(n);
 Choice choices[]{Choice::None,Choice::None};
 choices[a]=(autobattle::nextRandom(n.rng)&1u)?Choice::Magic:Choice::Physical;
 if(d<2)choices[d]=static_cast<Choice>(static_cast<unsigned>(Choice::Brace)+autobattle::nextRandom(n.rng)%3u);
 if(!resolveRaid(n,n.sequence,choices[0],choices[1]))return false;
 r=n;return true;
}
void autoDominanceRegression(){
 // Exact reported L99 cell: same foe, seed, full natural HP and care bonuses.
 const Fighter a{17,21,99,5,5},b{23,21,99,5,5};Raid old,current;
 CHECK(beginRaid(a,b,90374197u,old)&&beginRaid(a,b,90374197u,current));
 CHECK(old.foeForm==53&&old.foeLevel==99&&old.scenario==5&&kMaxRaidRounds==24);
 unsigned weakerTurns=0;
 while(old.status==RaidStatus::Active){
  if(current.status==RaidStatus::Active){
   auto replay=current;const auto sequence=current.sequence;
   CHECK(resolveAutoRaid(current,sequence)&&resolveAutoRaid(replay,sequence)&&sameRaid(current,replay));
   CHECK(current.lastAttack==Choice::Physical);
   const auto committed=current;CHECK(!resolveAutoRaid(current,sequence)&&sameRaid(current,committed));roundTrip(current);
  }
  CHECK(priorAuto(old));weakerTurns+=old.lastAttack==Choice::Magic;
  if(current.sequence==old.sequence){CHECK(current.rng==old.rng&&current.lastDefense==old.lastDefense);}
 }
 CHECK(old.status==RaidStatus::Wiped&&old.sequence==24&&old.foeHp==18&&old.hp[0]==136&&old.hp[1]==136);
 CHECK(old.energy[0]==76&&old.energy[1]==76&&weakerTurns==14);
 CHECK(current.status==RaidStatus::Cleared&&current.sequence==15&&!current.foeHp);
 CHECK(current.hp[0]==331&&current.hp[1]==291&&current.energy[0]==84&&current.energy[1]==86);
 CHECK(current.seed==old.seed&&sameFighter(current.players[0],a)&&sameFighter(current.players[1],b));
 std::printf("Exact L99 boss: old Auto timeout24/foe18HP, corrected Auto clear15; guard/RNG draws preserved.\n");
}
void naturalAutoRegression(){
 unsigned survivorTurns=0,koSides[2]{},clears=0,wipes=0,runs=0;
 for(unsigned scenario=0;scenario<6;++scenario)for(unsigned form:{18u,25u,39u,161u})for(unsigned level=std::max(1u,static_cast<unsigned>(forms::find(form)->minLevel));level<=24;++level){
  Raid r;const bool begun=beginRaid(host,guest,raidSeed(form,level,scenario),r);CHECK(begun);if(!begun)continue;++runs;unsigned turns=0;
  while(r.status==RaidStatus::Active&&turns++<72){
   const auto before=r;auto replay=r;const auto actor=raidAttacker(before),defender=raidDefender(before);
   CHECK(actor<2&&before.hp[actor]>0);CHECK(resolveAutoRaid(r,before.sequence)&&resolveAutoRaid(replay,before.sequence)&&sameRaid(r,replay));
   CHECK(r.sequence==before.sequence+1&&raidLastAttacker(r)==actor);
   CHECK(r.energy[actor]==before.energy[actor]-2&&r.energy[1u-actor]==before.energy[1u-actor]);
   if(defender==kNoRaidActor){++survivorTurns;CHECK(r.lastDefense==Choice::None);CHECK(r.hp[1u-actor]==0&&r.energy[1u-actor]==before.energy[1u-actor]);}
   else CHECK(r.lastDefense==Choice::Brace||r.lastDefense==Choice::Ward||r.lastDefense==Choice::Counter);
   for(unsigned side=0;side<2;++side)if(before.hp[side]&&!r.hp[side])++koSides[side];
   roundTrip(r);const auto committed=r;CHECK(!resolveAutoRaid(r,before.sequence)&&sameRaid(r,committed));
  }
  CHECK(r.status!=RaidStatus::Active&&turns<=72);clears+=r.status==RaidStatus::Cleared;wipes+=r.status==RaidStatus::Wiped;
 }
 CHECK(survivorTurns&&koSides[0]&&koSides[1]&&clears&&wipes);
 std::printf("%u natural Auto runs: %u survivor turns, host KOs%u, guest KOs%u, clears%u, wipes%u\n",runs,survivorTurns,koSides[0],koSides[1],clears,wipes);
 // The exact balance-study bug: guest was at0HP before round20 and attacked.
 Raid r;CHECK(beginRaid(host,guest,327841,r));while(r.sequence<19&&r.status==RaidStatus::Active)CHECK(priorAuto(r));
 CHECK(r.sequence==19&&r.hp[0]&&r.hp[1]==0&&raidAttacker(r)==0);
 const auto before=r;CHECK(resolveAutoRaid(r,r.sequence));CHECK(raidLastAttacker(r)==0&&r.lastDefense==Choice::None);
 CHECK(r.energy[0]==before.energy[0]-2&&r.energy[1]==before.energy[1]&&r.hp[1]==0);roundTrip(r);
}
void malformedFeedback(){
 Raid r;CHECK(beginRaid(host,guest,raidSeed(25,10,0),r));CHECK(basic(r));CHECK(r.hp[0]&&r.hp[1]);
 std::uint8_t bytes[kRaidBytes];CHECK(encodeRaid(r,bytes,sizeof(bytes)));bytes[23*4+3]=static_cast<std::uint8_t>(Choice::None);
 Raid unchanged=r;CHECK(!decodeRaid(bytes,sizeof(bytes),unchanged)&&sameRaid(r,unchanged));
}
}
int main(){skipRolesAndRejectGhostInput();liveSurvivorCarriesEveryTier();reflectionKnockouts();survivorEnergy();naturalAutoRegression();autoDominanceRegression();malformedFeedback();std::printf("%u knockout checks, %u failures\n",checks,failures);return failures?1:0;}
