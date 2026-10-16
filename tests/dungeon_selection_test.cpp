#include "game.hpp"
#include "forms.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace digivice;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)
State ready(unsigned form,unsigned level,unsigned seed) {
 const auto lineage=forms::find(form)->lineage;
 auto s=newDevice(seed);CHECK(apply(s,Action::Hatch,lineage-4)==Error::None);
 auto& m=s.collection[0];m.formId=form;m.species=static_cast<Species>(lineage);
 m.level=s.level=level;m.xp=xpForLevel(level);m.hp=s.hp=combat::formProfile(form,level).stats.maxHp;
 m.energy=s.energy=100;m.fullness=s.fullness=m.mood=s.mood=80;
 m.bond=s.bond=level==10?64:level==20?96:level==40?140:180;m.careState=60;
 s.journal[(form-1)/32]|=1u<<((form-1)%32);s.dungeonKeys=3;s.bossSigils=1;s.battleMode=BattleMode::Auto;
 CHECK(isValid(s));return s;
}
bool finish(State& s,unsigned scenario,unsigned& attacks) {
 CHECK(apply(s,Action::StartExpedition,scenario)==Error::None);
 unsigned actions=0;
 while((s.phase==Phase::Encounter||s.phase==Phase::Expedition)&&actions++<256){
  if(s.phase==Phase::Expedition){const auto hp=s.hp,energy=s.energy;
   CHECK(apply(s,Action::NextExpedition)==Error::None);CHECK(s.hp==hp&&s.energy==energy);continue;}
  autobattle::Trace trace;Error e;
  if(s.autoCapture==AutoCapture::FocusStrike||s.autoCapture==AutoCapture::FocusBlock)e=applyFocus(s,kFocusNoTap,&trace);
  else if(s.autoCapture==AutoCapture::Awaiting)e=applyAutoResume(s,&trace);
  else e=applyAutoFight(s,&trace);
  CHECK(e==Error::None);if(e!=Error::None)break;
  attacks+=trace.count;CHECK(isValid(s));
 }
 CHECK(s.phase==Phase::Home);CHECK(s.dungeonKeys==2);CHECK(s.captures==0);
 CHECK(s.dungeonWins==s.dungeonClears);CHECK(s.dungeonClears<=1);
 return s.dungeonClears==1;
}
void publicPreviewDoesNotMutate(){
 for(const auto form:{18u,46u,53u,60u})for(unsigned scenario=0;scenario<6;++scenario){
  auto s=ready(form,10,91);Snapshot before,after;CHECK(encodeSnapshot(s,before));
  unsigned id=0,level=0,id2=0,level2=0;
  CHECK(previewExpeditionFoe(s,scenario,id,level));CHECK(previewExpeditionFoe(s,scenario,id2,level2));
  CHECK(id==id2&&level==level2);CHECK(encodeSnapshot(s,after));
  CHECK(std::memcmp(before.bytes,after.bytes,sizeof(before.bytes))==0);
  CHECK(forms::productionForm(id)&&forms::hasInstalledArt(id)&&combat::validFormProfile(id,level));
 }
}
void fullRunBudgetRegression(){
 struct Case{unsigned form,level,scenario;};
 const Case cases[]{{53,10,0},{54,20,0},{20,40,2}};
 for(const auto& c:cases){
  unsigned wins=0,trivial=0;
  for(unsigned seed=1;seed<=256;++seed){auto s=ready(c.form,c.level,seed);unsigned attacks=0;
   if(finish(s,c.scenario,attacks)){++wins;if(attacks<=3)++trivial;}}
  // These three formerly had zero no-Focus/capture clears. Acceptance concerns
  // actual native runs, not a golden implementation-specific score or foe ID.
  CHECK(wins>=192);CHECK(trivial==0);
  std::printf("form=%u level=%u scenario=%u no-focus wins=%u/256 trivial=%u\n",c.form,c.level,c.scenario,wins,trivial);
 }
}
void bossesRetainTheirSelection(){
 // Frozen final-Auto preview outputs; changing dungeon budgets must not rebalance bosses.
 const unsigned golden[][6]{
  {20,40,1,3,216,41},{20,40,127,3,216,41},{20,40,1,4,274,43},{20,40,127,4,274,43},
  {20,40,1,5,22,41},{20,40,127,5,22,41},{53,10,1,3,144,5},{53,10,127,3,144,5},
  {53,10,1,4,141,5},{53,10,127,4,141,5},{53,10,1,5,268,5},{53,10,127,5,139,5},
  {54,20,1,3,189,10},{54,20,127,3,203,10},{54,20,1,4,195,10},{54,20,127,4,195,10},
  {54,20,1,5,16,10},{54,20,127,5,16,10}};
 for(const auto& c:golden){auto s=ready(c[0],c[1],c[2]);unsigned id=0,level=0;
  CHECK(previewExpeditionFoe(s,c[3],id,level));CHECK(id==c[4]&&level==c[5]);}
}
}
int main(){publicPreviewDoesNotMutate();fullRunBudgetRegression();bossesRetainTheirSelection();
 std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;}
