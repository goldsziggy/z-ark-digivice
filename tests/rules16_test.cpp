#include "game.hpp"
#include "forms.hpp"
#include "trade.hpp"
#include "capture_ring.hpp"
#include "nearby_match.hpp"
#include "legacy_v15.hpp"
#include <cstdio>
#include <cstring>

using namespace digivice;
namespace {
unsigned checks=0, failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)
void step(State& s, Action a, unsigned value=0){CHECK(apply(s,a,value)==Error::None);CHECK(isValid(s));}
void reject(State& s, Action a, unsigned value, Error e){const auto before=s;CHECK(apply(s,a,value)==e);CHECK(trade::sameState(s,before));}
unsigned nextRandom(unsigned x){x^=x<<13;x^=x>>17;x^=x<<5;return x;}
void forceCapture(State& s){
    unsigned phase=0;
    while(phase<capturering::kCycleMs&&capturering::sample(phase,s.wildFormId).grade!=capturering::Grade::Green)++phase;
    const auto chance=ringCaptureChance(s,phase);CHECK(chance>0&&phase<capturering::kCycleMs);
    if(!chance){std::fprintf(stderr,"capture blocked valid=%d phase=%u screen=%u hp=%u/%u deferred=%u attempts=%u mode=%u count=%u rules=%u form=%u test=%d\n",isValid(s)?1:0,static_cast<unsigned>(s.phase),phase,s.wildHp,s.wildMaxHp,s.captureDeferred,s.captureAttempts,static_cast<unsigned>(s.battleMode),s.collectionCount,s.wildRules,s.wildFormId,needsTestEncounterResolution(s)?1:0);return;}
    for(unsigned guard=0;guard<10000&&nextRandom(s.rngState)%100>=chance;++guard)s.rngState=nextRandom(s.rngState);
    step(s,Action::RingCapture,phase);
}
State ownedBox(unsigned count, unsigned sharedForm){
    auto s=newDevice(7);step(s,Action::Hatch,1);
    s.sequence=s.foregroundSequence=400;
    s.collectionCount=count;s.captures=s.encounters=count-1;s.steps=100*(count-1);s.nextMemberId=count+1;
    for(unsigned i=1;i<count;++i){
        const auto formId=i==1||i==5?sharedForm:20u;
        const auto* form=forms::find(formId);CHECK(form);
        auto& m=s.collection[i];
        m.id=i+1;m.species=static_cast<Species>(form->lineage);m.formId=formId;
        m.level=form->minLevel;m.xp=xpForLevel(m.level);m.bond=form->minBond;
        m.hp=combat::formProfile(formId,m.level).stats.maxHp;
        m.energy=80;m.fullness=70;m.mood=80;m.capturedAtSequence=i;
        s.journal[(formId-1)/32]|=1u<<((formId-1)%32);
    }
    if(!isValid(s))std::fprintf(stderr,"owned box invalid count=%u captures=%u encounters=%u steps=%u next=%u seq=%u walk=%u credit=%u\n",s.collectionCount,s.captures,s.encounters,s.steps,s.nextMemberId,s.sequence,s.walkingEncounters,s.stepCredit);
    CHECK(isValid(s));return s;
}
void weaken(State& s, unsigned formId, unsigned wildLevel){
    step(s,Action::Explore,1000);
    const auto* form=forms::find(formId);CHECK(form);
    if(form&&wildLevel<form->minLevel)wildLevel=form->minLevel;
    s.wildFormId=formId;s.wildSpecies=static_cast<Species>(form->lineage);s.wildLevel=wildLevel;s.wildRules=kRulesVersion;
    s.wildMaxHp=combat::formProfile(formId,wildLevel).stats.maxHp;s.wildHp=s.wildMaxHp/2;
    for(unsigned guard=0;guard<4&&wildGuard(s)==combat::Defense::Counter;++guard)++s.wildTurn;
    if(!isValid(s))std::fprintf(stderr,"weaken invalid phase=%u form=%u hp=%u/%u turn=%u rules=%u encounters=%u walking=%u steps=%u credit=%u\n",static_cast<unsigned>(s.phase),s.wildFormId,s.wildHp,s.wildMaxHp,s.wildTurn,s.wildRules,s.encounters,s.walkingEncounters,s.steps,s.stepCredit);
    CHECK(isValid(s));
}
void levelsAndRoutes(){
    CHECK(kSchemaVersion==24&&kRulesVersion==17&&kMaxLevel==50&&kMaxXp==49000&&kSnapshotSize==3216);
    CHECK(xpForLevel(1)==0&&xpForLevel(20)==7600&&xpForLevel(21)==20u*20u*21u&&xpForLevel(50)==49000);
    CHECK(levelForXp(0)==1&&levelForXp(7600)==20&&levelForXp(8399)==20&&levelForXp(8400)==21&&levelForXp(49000)==50);
    for(unsigned level=1;level<=20;++level)CHECK(xpForLevel(level)==20u*(level-1)*level);
    for(unsigned id=forms::kFirstProductionFormId;id<forms::kFirstProductionFormId+8;++id){
        const auto early=forms::stats(id,20), later=forms::stats(id,50);
        CHECK(later.maxHp>=early.maxHp&&later.attack>=early.attack&&later.maxHp<=2048&&later.attack<=256);
    }
    CHECK(forms::edgeCount()>=166);
    for(std::size_t i=0;i<forms::edgeCount();++i){
        const auto* edge=forms::edgeAt(i);CHECK(edge);
        const auto need=forms::evolutionNeed(*edge);
        CHECK(need.level>=edge->minLevel&&need.level<=50);
        CHECK(need.bond>=edge->minBond&&need.bond<=200);
        CHECK(need.care>=12&&need.care<=100);
        const auto* src=forms::find(edge->from);const auto* dest=forms::find(edge->to);
        CHECK(src&&dest&&need.level>=src->minLevel+1&&need.level<=50);
    }
}
void migrationKeepsProgress(){
    namespace old=legacy_v15;
    auto historical=old::newDevice(31);
    CHECK(old::apply(historical,old::Action::Hatch,2)==old::Error::None);
    historical.level=historical.collection[0].level=20;
    historical.collection[0].xp=old::xpForLevel(20);
    historical.bond=historical.collection[0].bond=48;
    historical.hp=historical.collection[0].hp=combat::formProfile(historical.collection[0].formId,20).stats.maxHp;
    CHECK(old::isValid(historical));
    old::Snapshot bytes;CHECK(old::encodeSnapshot(historical,bytes));
    State migrated;CHECK(decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),migrated)==SnapshotStatus::Migrated);
    CHECK(migrated.level==20&&migrated.collection[0].xp==7600&&migrated.bond==48);
    CHECK(migrated.seed==historical.seed&&migrated.activeCreatureId==1&&migrated.collection[0].formId==historical.collection[0].formId);
    CHECK(!migrated.collection[0].careState&&!migrated.careMinute&&!migrated.lastCritical&&!migrated.captureDeferred);
    CHECK(trade::validMember(migrated.collection[0],migrated.sequence)&&!trade::canOffer(migrated,1));
    auto tradable=migrated;
    tradable.sequence=tradable.foregroundSequence=migrated.sequence+10;
    tradable.collectionCount=2;tradable.captures=tradable.encounters=1;tradable.steps=100;tradable.nextMemberId=3;
    const auto* extra=forms::find(18);
    tradable.collection[1]={2,static_cast<Species>(extra->lineage),combat::formProfile(18,1).stats.maxHp,80,70,80,extra->minBond,1,1,0,18};
    tradable.journal[(18-1)/32]|=1u<<((18-1)%32);
    CHECK(isValid(tradable)&&trade::canOffer(tradable,1));
    Snapshot current;CHECK(encodeSnapshot(migrated,current));
    State again;CHECK(decodeSnapshot(current.bytes,sizeof(current.bytes),again)==SnapshotStatus::Ok&&trade::sameState(migrated,again));
}
void companionsAndBench(){
    auto s=ownedBox(4,19);step(s,Action::PartyAdd,2);step(s,Action::PartyAdd,3);
    const auto firstBond=s.collection[1].bond,secondBond=s.collection[2].bond;
    weaken(s,21,1);s.wildHp=1;step(s,Action::Attack);
    CHECK(s.phase==Phase::Home&&(s.message==Message::Won||s.message==Message::Trained));
    CHECK(s.collection[1].bond==firstBond+8&&s.collection[2].bond==secondBond+8);
    const auto once=s.collection[1].bond;
    reject(s,Action::Attack,0,Error::WrongPhase);
    CHECK(s.collection[1].bond==once);
    const auto* edge=forms::outgoing(s.collection[3].formId,0);
    if(!edge){edge=forms::outgoing(19,0);s.collection[3].formId=19;}
    CHECK(edge);
    const auto need=forms::evolutionNeed(*edge);
    auto& bench=s.collection[3];
    const auto activeForm=s.collection[0].formId;
    bench.formId=edge->from;bench.species=static_cast<Species>(forms::find(edge->from)->lineage);
    bench.level=need.level;bench.xp=xpForLevel(need.level);bench.bond=need.bond;bench.careState=need.care;
    bench.hp=combat::formProfile(bench.formId,bench.level).stats.maxHp;
    s.journal[(bench.formId-1)/32]|=1u<<((bench.formId-1)%32);
    CHECK(isValid(s));
    const auto activeId=s.activeCreatureId;
    step(s,Action::EvolveMember,(bench.id<<16)|edge->to);
    CHECK(s.activeCreatureId==activeId&&s.collection[0].formId==activeForm&&s.collection[3].formId==edge->to);
}
void duplicatesAndAttempts(){
    auto s=ownedBox(3,19);
    const auto oldestXp=s.collection[1].xp,otherXp=s.collection[2].xp,otherForm=s.collection[2].formId;
    weaken(s,19,4);
    const auto bonus=20u+6u*s.wildLevel;
    forceCapture(s);
    CHECK(s.collectionCount==3&&s.collection[1].xp==oldestXp+bonus&&s.collection[2].formId==otherForm&&s.collection[2].xp==otherXp);
    CHECK(s.collection[1].id==2&&s.collection[2].id==3);
    auto full=ownedBox(60,19);
    const auto fullXp=full.collection[1].xp;
    const auto newerXp=full.collection[5].xp;
    weaken(full,19,3);
    const auto fullBonus=20u+6u*full.wildLevel;
    forceCapture(full);
    CHECK(full.collectionCount==60&&full.collection[1].xp==fullXp+fullBonus&&full.collection[5].xp==newerXp);
    CHECK(full.captures==60&&full.nextMemberId==62);
    auto blocked=ownedBox(60,19);
    weaken(blocked,21,1);
    CHECK(!captureChance(blocked));
    reject(blocked,Action::RingCapture,0,Error::CollectionFull);
    auto battle=ownedBox(2,19);
    weaken(battle,18,1);
    for(unsigned attempt=1;attempt<=3;++attempt){
        const auto wildHp=battle.wildHp, hp=battle.hp;
        step(battle,Action::Flick,0);
        CHECK(battle.phase==Phase::Encounter&&battle.wildHp==wildHp&&battle.hp==hp);
        CHECK(battle.captureAttempts==attempt&&battle.captureDeferred==1&&battle.message==Message::CaptureMissed);
        CHECK(!captureChance(battle));
        if(attempt<3){step(battle,Action::Attack);CHECK(battle.phase==Phase::Encounter&&!battle.captureDeferred&&battle.wildHp<wildHp);}
    }
    reject(battle,Action::Flick,0,Error::InvalidAction);
    // Auto continues the same encounter and must not open another capture prompt.
    battle.battleMode=BattleMode::Auto;battle.wildHp=battle.wildMaxHp/2;CHECK(isValid(battle));
    const auto continued=applyAutoFight(battle);
    if(continued!=Error::None)std::fprintf(stderr,"auto continue %u phase %u attempts %u deferred %u valid %d\n",static_cast<unsigned>(continued),static_cast<unsigned>(battle.phase),battle.captureAttempts,battle.captureDeferred,isValid(battle)?1:0);
    CHECK(continued==Error::None);
    CHECK(battle.autoCapture!=AutoCapture::Awaiting&&battle.captures==1);
}
void critsAndCare(){
    auto base=newDevice(9);step(base,Action::Hatch,1);step(base,Action::Explore,1000);
    base.wildFormId=18;base.wildSpecies=static_cast<Species>(forms::find(18)->lineage);
    base.wildLevel=1;base.wildRules=kRulesVersion;
    base.wildMaxHp=base.wildHp=combat::formProfile(18,1).stats.maxHp;
    while(wildGuard(base)==combat::Defense::Counter)++base.wildTurn;
    CHECK(isValid(base));
    unsigned normal=0, critical=0;
    for(unsigned seed=1;seed<4000&&(!normal||!critical);++seed){
        auto s=base;s.rngState=seed;const auto before=s.wildHp;
        CHECK(apply(s,Action::Attack)==Error::None);
        if(s.phase!=Phase::Encounter)continue;
        const auto dealt=before-s.wildHp;
        if(s.lastCritical)critical=dealt;else normal=dealt;
    }
    CHECK(normal&&critical&&critical==normal*3/2);
    nearby::Fighter host{1,11,8}, guest{2,18,8};
    bool synced=false;
    for(unsigned seed=1;seed<4000;++seed){
        nearby::Match a,b;
        CHECK(nearby::begin(host,guest,nearby::Mode::Tactical,seed,a));
        b=a;
        CHECK(nearby::resolve(a,0,nearby::Choice::Physical,nearby::Choice::Brace));
        CHECK(nearby::resolve(b,0,nearby::Choice::Physical,nearby::Choice::Brace));
        CHECK(a.lastCritical==b.lastCritical&&a.hp[0]==b.hp[0]&&a.hp[1]==b.hp[1]);
        std::uint8_t bytes[nearby::kMatchBytes];CHECK(nearby::encode(a,bytes,sizeof(bytes)));
        nearby::Match decoded;CHECK(nearby::decode(bytes,sizeof(bytes),decoded)&&decoded.lastCritical==a.lastCritical);
        if(a.lastCritical)synced=true;
    }
    CHECK(synced);
    auto care=newDevice(3);step(care,Action::Hatch,1);
    const auto fullness=care.fullness, bond=care.bond, xp=care.collection[0].xp;
    reject(care,Action::CareMinute,0,Error::InvalidValue);
    step(care,Action::CareMinute,4); // Gap from zero does not reward or punish.
    CHECK(care.careMinute==4&&!toiletNeed(care.collection[0])&&care.fullness==fullness&&care.bond==bond);
    step(care,Action::CareMinute,5);
    CHECK(toiletNeed(care.collection[0])==8&&care.fullness==fullness-1);
    reject(care,Action::CareMinute,5,Error::InvalidAction);
    step(care,Action::CareMinute,9);CHECK(toiletNeed(care.collection[0])==8&&care.careMinute==9);
    care.bond=care.collection[0].bond=30;care.mood=care.collection[0].mood=80;CHECK(isValid(care));
    while(toiletNeed(care.collection[0])<100)step(care,Action::CareMinute,care.careMinute+1);
    CHECK(careWasMissed(care.collection[0])&&care.bond==28&&care.mood==70);
    const auto missedBond=care.bond;
    step(care,Action::CareMinute,care.careMinute+1);
    CHECK(care.bond==missedBond&&careWasMissed(care.collection[0]));
    step(care,Action::Toilet);
    CHECK(!toiletNeed(care.collection[0])&&!careWasMissed(care.collection[0])&&care.bond>missedBond);
    CHECK(care.collection[0].xp>=xp+2);
    const auto after=care.collection[0].xp;
    step(care,Action::Feed);CHECK(care.collection[0].xp==after+2); // A different care action has its own cooldown.
    const auto fed=care.collection[0].xp;
    step(care,Action::Feed);CHECK(care.collection[0].xp==fed); // The same action does not grant XP again until a minute passes.
}
void encounterLevelBand(){
    bool down=false, flat=false, up=false;
    for(unsigned seed=1;seed<=256;++seed)for(unsigned encounter=1;encounter<=12;++encounter){
        const auto mid=wildEncounterLevel(encounter,seed,9);
        CHECK(mid>=8&&mid<=10);
        if(mid==8)down=true; if(mid==9)flat=true; if(mid==10)up=true;
        const auto floor=wildEncounterLevel(encounter,seed,1);
        const auto cap=wildEncounterLevel(encounter,seed,50);
        CHECK((floor==1||floor==2)&&(cap==49||cap==50));
        CHECK(wildEncounterLevel(encounter,seed,1)==floor&&wildEncounterLevel(encounter,seed,50)==cap);
    }
    CHECK(down&&flat&&up);
    bool sawFloor=false,sawAbove=false,sawCap=false,sawBelow=false;
    for(unsigned seed=1;seed<=96&&!(sawFloor&&sawAbove&&sawCap&&sawBelow);++seed){
        auto left=newDevice(seed), right=newDevice(seed^0x5a5a5a5au);
        step(left,Action::Hatch,1); step(right,Action::Hatch,1);
        step(left,Action::WorldSeed,seed); step(right,Action::WorldSeed,seed);
        const auto leftRng=left.rngState, rightRng=right.rngState;
        step(left,Action::Explore,1000); step(right,Action::Explore,1000);
        CHECK(left.level==1&&right.level==1&&left.wildLevel==right.wildLevel&&left.wildLevel==wildEncounterLevel(1,seed,1));
        CHECK((left.wildLevel==1||left.wildLevel==2)&&left.rngState==leftRng&&right.rngState==rightRng);
        CHECK(combat::validFormProfile(left.wildFormId,left.wildLevel)&&combat::validFormProfile(right.wildFormId,right.wildLevel));
        if(left.wildLevel==1)sawFloor=true; if(left.wildLevel==2)sawAbove=true;
        auto queued=newDevice(seed); step(queued,Action::Hatch,1); step(queued,Action::WorldSeed,seed); step(queued,Action::AccrueSteps,1000);
        CHECK(queued.pendingEncounter.level==left.wildLevel&&queued.pendingEncounter.formId==left.wildFormId);
        auto high=newDevice(seed); step(high,Action::Hatch,1); step(high,Action::WorldSeed,seed);
        high.level=high.collection[0].level=50; high.collection[0].xp=xpForLevel(50);
        high.hp=high.collection[0].hp=combat::formProfile(high.collection[0].formId,50).stats.maxHp;
        CHECK(isValid(high));
        auto peer=high; step(high,Action::Explore,1000); step(peer,Action::Walk,100);
        CHECK(high.level==50&&peer.level==50&&high.wildLevel==peer.wildLevel);
        CHECK(high.wildLevel==49||high.wildLevel==50);
        CHECK(high.wildLevel==wildEncounterLevel(1,seed,50)&&combat::validFormProfile(high.wildFormId,high.wildLevel));
        if(high.wildLevel==49)sawBelow=true; if(high.wildLevel==50)sawCap=true;
    }
    CHECK(sawFloor&&sawAbove&&sawCap&&sawBelow);
}
}
int main(){
    levelsAndRoutes();migrationKeepsProgress();companionsAndBench();duplicatesAndAttempts();critsAndCare();encounterLevelBand();
    std::printf("rules 16: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
