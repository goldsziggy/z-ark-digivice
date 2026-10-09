// Host-only clock/interaction model. Every gameplay transition uses native core.
#include "game.hpp"
#include "forms.hpp"
#include "combat.hpp"
#ifdef PARK_CURRENT_RARITY
#include "encounters.hpp"
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>
namespace g=digivice;
namespace f=digivice::forms;
namespace c=digivice::combat;
#ifndef PARK_RECOVERY_MODE
#define PARK_RECOVERY_MODE 0
#endif
// 0: original manual HP/full + energy80; 1: manual full100; 2: one-confirm full100.
constexpr unsigned recoveryMode=PARK_RECOVERY_MODE;
struct Input {unsigned id,starter,seed,seedIndex,minutes,rate,automatic,release,pace;};
Input input{};
void require(bool value,const char* message){if(!value){std::fprintf(stderr,"park case %u: %s\n",input.id,message);std::exit(1);}}
g::Action publicChoice(const g::State& s){
 if(g::captureChance(s))return g::Action::Capture;
 auto best=g::Action::Attack;int score=-100000;
 for(auto action:{g::Action::Attack,g::Action::Magic,g::Action::Heavy}){
  if(action==g::Action::Heavy&&s.energy<6)continue;
  auto move=action==g::Action::Attack?c::Move::Physical:action==g::Action::Magic?c::Move::Magic:c::Move::Heavy;
  auto hit=c::resolveForms(g::activeMember(s)->formId,s.level,s.wildFormId,s.wildLevel,move,g::wildGuard(s));
  const int value=static_cast<int>(std::min(hit.damage,hit.reflected?s.hp:s.wildHp))*(hit.reflected?-1:1);
  if(value>score){score=value;best=action;}
 }
 return best;
}
struct Encounter {unsigned form,level,attempts=0,turns=0;const char* outcome="pending";double atMs;bool previouslyObtained=false,newCapture=false;};
void run(){
 auto s=g::newDevice(input.seed);
 require(g::apply(s,g::Action::Hatch,input.starter)==g::Error::None,"hatch");
 require(g::apply(s,g::Action::Mode,input.automatic)==g::Error::None,"mode");
 const double budget=input.minutes*60000.0,pace=input.pace/100.0;
 double now=0,walking=0,interaction=0,autoplay=0,fullAt=-1;
 double firstEvolutionAt=-1,firstEvolutionWalk=0,firstEvolutionStopped=0;
 unsigned firstEvolutionSteps=0,firstEvolutionForm=0,firstEvolutionLevel=0,newCaptureForms=0;
 unsigned actions=0,rests=0,recoveryConfirmations=0,recoveryVisits=0,releases=0,evolutions=0,battleActions=0,failed=0,wins=0,retreats=0,fullEncounters=0,automaticCommits=0;
 bool presentationPending=false;
 std::array<bool,513> seen{},captured{};
 std::vector<Encounter> encounters;
 auto spend=[&](double ms,bool presentation=false){
  if(now+ms>budget){if(presentation){const auto used=budget-now;autoplay+=used;now=budget;presentationPending=true;}return false;}
  now+=ms;if(presentation)autoplay+=ms;else interaction+=ms;return true;
 };
 auto command=[&](g::Action a,unsigned value=0){require(g::apply(s,a,value)==g::Error::None,"native action");++actions;};
 bool stop=false;
 while(now<budget&&!stop){
  require(g::isValid(s),"loop state");
  if(s.phase==g::Phase::Home){
   if(input.release&&s.collectionCount==g::kCollectionCapacity){
    if(!spend(10000*pace))break;
    unsigned id=0;for(std::size_t i=0;i<s.collectionCount;++i)if(s.collection[i].id!=s.activeCreatureId){id=s.collection[i].id;break;}
    require(id!=0,"release candidate");command(g::Action::Release,id);++releases;
   }
   const auto* edge=f::outgoing(g::activeMember(s)->formId,0);
   if(edge&&s.level>=edge->minLevel&&s.bond>=edge->minBond){
    if(!spend(10000*pace))break;
    command(g::Action::Evolve,edge->to);++evolutions;
    if(firstEvolutionAt<0){firstEvolutionAt=now;firstEvolutionSteps=s.steps;firstEvolutionForm=g::activeMember(s)->formId;firstEvolutionLevel=s.level;firstEvolutionWalk=walking;firstEvolutionStopped=interaction+autoplay;}
   }
   const auto maximum=f::stats(g::activeMember(s)->formId,s.level).maxHp;
   const unsigned energyGoal=recoveryMode?100:80;
   if(s.hp<maximum||s.energy<energyGoal){
    if(!spend(4000*pace))break;
    ++recoveryVisits;
    if(recoveryMode==2){
     if(!spend(2000*pace))break;
     ++recoveryConfirmations;++actions;
#ifdef PARK_CURRENT_RARITY
     const auto expectedRestCount=g::recoveryRestCount(s),beforeRests=rests;
#endif
     while(s.hp<maximum||s.energy<energyGoal){require(g::apply(s,g::Action::Rest)==g::Error::None,"batch native Rest");++rests;require(rests<1000,"rest bound");}
#ifdef PARK_CURRENT_RARITY
     require(rests-beforeRests==expectedRestCount,"native recovery count mismatch");
#endif
    }else{
     while(s.hp<maximum||s.energy<energyGoal){if(!spend(2000*pace)){stop=true;break;}command(g::Action::Rest);++rests;++recoveryConfirmations;}
    }
    if(stop)break;
   }
   const unsigned threshold=100; // Baseline contractual interval; candidate may replace with native constant.
   const unsigned need=s.stepCredit>=threshold?1:threshold-s.stepCredit;
   const double stepMs=60000.0/input.rate;
   unsigned steps=need;
   if(now+steps*stepMs>budget)steps=static_cast<unsigned>((budget-now)/stepMs);
   if(!steps)break;
   require(g::apply(s,g::Action::Walk,steps)==g::Error::None,"walk");
   walking+=steps*stepMs;now+=steps*stepMs;
   if(s.phase==g::Phase::Home)break;
   seen[s.wildFormId]=true;encounters.push_back({s.wildFormId,s.wildLevel,0,0,"pending",now,g::hasObtained(s,s.wildFormId),false});
   if(s.collectionCount==g::kCollectionCapacity)++fullEncounters;
   if(!spend(3000*pace))break;
  }
  require(s.phase==g::Phase::Encounter,"battle phase");
  auto& event=encounters.back();const auto capturesBefore=s.captures;
  if(input.automatic){
   if(!spend(4000*pace))break;
   g::autobattle::Trace trace;
   require(g::applyAuto(s,&trace)==g::Error::None,"auto");++actions;++automaticCommits;
   event.turns=static_cast<unsigned>(trace.count);battleActions+=event.turns;
   for(std::size_t i=0;i<trace.count;++i)if(trace.steps[i].action==g::autobattle::Move::Capture){++event.attempts;if(!trace.steps[i].captured)++failed;}
   if(!spend(trace.count*650.0,true))stop=true;
  }else{
   while(s.phase==g::Phase::Encounter){
    auto action=publicChoice(s);
    const double ms=4000*pace+(action==g::Action::Capture?1200:0);
    if(!spend(ms)){stop=true;break;}
    const auto captureCount=s.captures;
    command(action);++battleActions;++event.turns;
    if(action==g::Action::Capture){++event.attempts;if(s.captures==captureCount)++failed;}
    require(event.turns<=512,"bounded tactical encounter");
   }
  }
  if(s.phase==g::Phase::Home){
   if(s.captures>capturesBefore){event.outcome="captured";captured[event.form]=true;event.newCapture=!event.previouslyObtained;if(event.newCapture)++newCaptureForms;}
   else if(s.message==g::Message::Retreated){event.outcome="retreated";++retreats;}
   else{event.outcome="won";++wins;}
   if(s.collectionCount==g::kCollectionCapacity&&fullAt<0)fullAt=now;
   if(!stop&&!spend(2000*pace))stop=true;
  }
 }
 require(g::isValid(s),"final state");
 g::Snapshot save;require(g::encodeSnapshot(s,save),"final save encode");g::State restored;
 require(g::decodeSnapshot(save.bytes,sizeof(save.bytes),restored)==g::SnapshotStatus::Ok,"final save decode");
 require(restored.sequence==s.sequence&&restored.captures==s.captures&&restored.steps==s.steps,"save values");
 unsigned uniqueSeen=0,uniqueCaptured=0;for(unsigned i=1;i<513;++i){uniqueSeen+=seen[i];uniqueCaptured+=captured[i];}
 std::printf("{\"case\":%u,\"starter\":%u,\"seed\":%u,\"seedIndex\":%u,\"minutes\":%u,\"stepsPerMinute\":%u,\"mode\":\"%s\",\"rosterPolicy\":\"%s\",\"interactionPacePercent\":%u,\"level\":%u,\"xp\":%u,\"form\":%u,\"encounters\":%u,\"wins\":%u,\"captures\":%u,\"retreats\":%u,\"failedCaptureAttempts\":%u,\"distinctSeen\":%u,\"distinctCaptured\":%u,\"collectionCount\":%u,\"fullEncounters\":%u,\"fullAtMs\":%.0f,\"releases\":%u,\"evolutions\":%u,\"rests\":%u,\"commandActions\":%u,\"battleTurns\":%u,\"autoCommits\":%u,\"steps\":%u,\"walkMs\":%.0f,\"interactionMs\":%.0f,\"autoplayMs\":%.0f,\"unspentMs\":%.0f,\"endsInEncounter\":%s,\"presentationPending\":%s,\"events\":[",
 input.id,input.starter,input.seed,input.seedIndex,input.minutes,input.rate,input.automatic?"auto":"tactical",input.release?"release-oldest-nonactive":"keep-eight",input.pace,s.level,g::activeMember(s)->xp,g::activeMember(s)->formId,s.encounters,wins,s.captures,retreats,failed,uniqueSeen,uniqueCaptured,s.collectionCount,fullEncounters,fullAt,releases,evolutions,rests,actions,battleActions,automaticCommits,s.steps,walking,interaction,autoplay,budget-now,s.phase==g::Phase::Encounter?"true":"false",presentationPending?"true":"false");
 for(std::size_t i=0;i<encounters.size();++i){const auto& e=encounters[i];
  const char* rarity=nullptr;
#ifdef PARK_CURRENT_RARITY
  rarity=g::encounters::rarityName(g::encounters::rarityForForm(e.form));require(rarity!=nullptr,"missing rarity");
#endif
  std::printf("%s{\"form\":%u,\"level\":%u,\"outcome\":\"%s\",\"attempts\":%u,\"turns\":%u,\"atMs\":%.0f,\"rarity\":",i?",":"",e.form,e.level,e.outcome,e.attempts,e.turns,e.atMs);
  if(rarity)std::printf("\"%s\"",rarity);else std::printf("null");
  std::printf(",\"previouslyObtained\":%s,\"newCapture\":%s}",e.previouslyObtained?"true":"false",e.newCapture?"true":"false");
 }
 std::printf("],\"recoveryMode\":%u,\"recoveryConfirmations\":%u,\"recoveryVisits\":%u,\"nativeGameEvents\":%u,\"newCaptureForms\":%u,\"firstEvolutionAtMs\":%.0f,\"firstEvolutionSteps\":%u,\"firstEvolutionForm\":%u,\"firstEvolutionLevel\":%u,\"firstEvolutionWalkMs\":%.0f,\"firstEvolutionStoppedMs\":%.0f}\n",recoveryMode,recoveryConfirmations,recoveryVisits,s.sequence-2,newCaptureForms,firstEvolutionAt,firstEvolutionSteps,firstEvolutionForm,firstEvolutionLevel,firstEvolutionWalk,firstEvolutionStopped);
}
int main(){while(std::cin>>input.id>>input.starter>>input.seed>>input.seedIndex>>input.minutes>>input.rate>>input.automatic>>input.release>>input.pace)run();require(std::cin.eof(),"input parse");}
