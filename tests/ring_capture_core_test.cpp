#include "game.hpp"
#include "snapshot_test_helpers.hpp"
#include "capture_ring.hpp"
#include "forms.hpp"
#include "legacy_combat_v8.hpp"
#include "legacy_v13.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace digivice;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); } } while(false)
void step(State& s,Action action,unsigned value=0){CHECK(apply(s,action,value)==Error::None);CHECK(isValid(s));}
bool same(const State& a,const State& b){Snapshot x,y;return encodeSnapshot(a,x)&&encodeSnapshot(b,y)&&!std::memcmp(x.bytes,y.bytes,kSnapshotSize);}
void restore(State& s){Snapshot bytes;CHECK(encodeSnapshot(s,bytes));State out;CHECK(decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),out)==SnapshotStatus::Ok);CHECK(same(s,out));s=out;}
void reject(State& s,unsigned phase,Error expected){const auto before=s;CHECK(apply(s,Action::RingCapture,phase)==expected);CHECK(same(s,before));}
unsigned nextRng(unsigned x){x^=x<<13;x^=x>>17;x^=x<<5;return x;}
unsigned crc(unsigned value,const unsigned char* bytes,unsigned size){for(unsigned i=0;i<size;++i){value^=bytes[i];for(unsigned j=0;j<8;++j)value=(value>>1)^((value&1)?0xedb88320u:0);}return value;}
State fight(unsigned seed=1){auto s=newDevice(seed);step(s,Action::Hatch,1);step(s,Action::WorldSeed,4567);step(s,Action::Explore,1000);s.wildHp=s.wildMaxHp/2;return s;}
void target(State& s,unsigned id,unsigned level=1){s.wildFormId=id;s.wildSpecies=static_cast<Species>(forms::find(id)->lineage);s.wildLevel=level;s.wildMaxHp=combat::formProfile(id,level).stats.maxHp;s.wildHp=s.wildMaxHp/2;CHECK(isValid(s));}
unsigned formWith(encounters::Rarity rarity){for(unsigned id=forms::kFirstProductionFormId;id<=forms::kFormCount;++id)if(encounters::rarityForForm(id)==rarity&&combat::validFormProfile(id,1))return id;return 0;}
unsigned phaseFor(const State& s,capturering::Grade grade){for(unsigned phase=0;phase<capturering::kCycleMs;++phase)if(capturering::sample(phase,s.wildFormId).grade==grade)return phase;CHECK(false);return 0;}
void contract(){
    CHECK(kSchemaVersion==22&&kRulesVersion==15&&kSnapshotSize==2964);
    CHECK(static_cast<unsigned>(Action::RingCapture)==static_cast<unsigned>(Action::WorldSeed)+1);
    Action parsed;CHECK(parseAction("ring-capture",parsed)&&parsed==Action::RingCapture);
    auto s=fight();reject(s,2400,Error::InvalidValue);reject(s,UINT32_MAX,Error::InvalidValue);
    CHECK(!ringCaptureChance(s,2400)&&!ringCaptureChance(s,UINT32_MAX));
    for(unsigned phase:{0u,2399u}){auto edge=s;const auto chance=ringCaptureChance(edge,phase);step(edge,Action::RingCapture,phase);CHECK(edge.rngState==nextRng(s.rngState)&&edge.lastCapture.chance==chance);}
    auto removed=s;removed.wildRules=12;target(removed,4);CHECK(captureChance(removed)>0&&!ringCaptureChance(removed,0));reject(removed,0,Error::InvalidAction);
    auto egg=newDevice();reject(egg,0,Error::WrongPhase);CHECK(!ringCaptureChance(egg,0));
    auto home=newDevice();step(home,Action::Hatch,1);reject(home,0,Error::WrongPhase);CHECK(!ringCaptureChance(home,0));
    auto strong=s;strong.wildHp=strong.wildMaxHp;reject(strong,0,Error::WildTooStrong);CHECK(!ringCaptureChance(strong,0));
    auto exhausted=s;exhausted.sequence=UINT32_MAX;reject(exhausted,0,Error::CounterOverflow);CHECK(!ringCaptureChance(exhausted,0));
    auto automatic=newDevice();step(automatic,Action::Hatch,1);step(automatic,Action::Mode,1);step(automatic,Action::Explore,1000);
    reject(automatic,0,Error::WrongMode);CHECK(!ringCaptureChance(automatic,0));
    // Collection capacity blocks before a draw; no fixture member or reward is lost.
    auto full=s;full.sequence=full.foregroundSequence=100;full.collectionCount=60;full.captures=59;full.encounters=60;full.steps=5900;full.nextMemberId=61;
    for(unsigned i=1;i<60;++i){full.collection[i]=full.collection[0];full.collection[i].id=i+1;full.collection[i].capturedAtSequence=i+1;}
    CHECK(isValid(full));reject(full,0,Error::CollectionFull);CHECK(!ringCaptureChance(full,0));
}
void fullRoster(){
    // Make the last free slot observable: existing members have distinct care
    // values and stable IDs, and the final capture must append without replacing.
    auto s=fight();s.sequence=s.foregroundSequence=100;s.collectionCount=59;
    s.captures=58;s.encounters=59;s.steps=5800;s.nextMemberId=60;
    for(unsigned i=1;i<59;++i){s.collection[i]=s.collection[0];auto& member=s.collection[i];
        member.id=i+1;member.capturedAtSequence=i+1;member.energy=30+i;member.mood=40+i;}
    CHECK(isValid(s));const auto phase=phaseFor(s,capturering::Grade::Green),chance=ringCaptureChance(s,phase);
    CHECK(chance>0);
    while(nextRng(s.rngState)%100>=chance)s.rngState=nextRng(s.rngState);
    const auto beforeLastCapture=s;step(s,Action::RingCapture,phase);
    CHECK(s.phase==Phase::Home&&s.collectionCount==kCollectionCapacity&&s.captures==59&&s.nextMemberId==61);
    CHECK(s.activeCreatureId==beforeLastCapture.activeCreatureId&&s.collection[0].id==beforeLastCapture.collection[0].id&&
        s.collection[0].formId==beforeLastCapture.collection[0].formId&&s.collection[0].capturedAtSequence==0);
    // Only the active partner earns the normal battle XP/bond reward.
    CHECK(s.collection[0].xp>beforeLastCapture.collection[0].xp);
    for(unsigned i=1;i<59;++i)CHECK(!std::memcmp(&s.collection[i],&beforeLastCapture.collection[i],sizeof(CreatureMember)));
    CHECK(s.collection[59].id==60&&s.collection[59].formId==beforeLastCapture.wildFormId&&
        s.collection[59].capturedAtSequence==beforeLastCapture.sequence+1&&s.lastCapture.result==CaptureResult::Captured);
    restore(s);const auto home=s;CHECK(apply(s,Action::Hatch,2)==Error::AlreadyHatched);CHECK(same(s,home));
    Snapshot forged;CHECK(encodeSnapshot(s,forged));
    const auto put=[&](unsigned offset,unsigned value){for(unsigned i=0;i<4;++i)forged.bytes[offset+i]=static_cast<unsigned char>(value>>(i*8));};
    put(104,61); // Schema21 collectionCount field; retain an otherwise valid save.
    put(kSnapshotSize-4,~crc(~0u,forged.bytes,kSnapshotSize-4));
    auto destination=newDevice(42);const auto untouched=destination;
    CHECK(decodeSnapshot(forged.bytes,kSnapshotSize,destination)==SnapshotStatus::InvalidState);
    CHECK(same(destination,untouched));
    step(s,Action::Explore,1000);s.wildHp=s.wildMaxHp/2;
    // Reserve a deterministic successful draw for after Make room; every full
    // rejection and the release must leave that exact draw available.
    while(nextRng(s.rngState)%100>=10)s.rngState=nextRng(s.rngState);
    CHECK(isValid(s));restore(s);
    const auto full=s;
    // Legacy direct capture, missed/on-target flicks and every timing grade all
    // reject before attempts, RNG, rewards or any serialized byte can change.
    for(auto action:{Action::Capture,Action::Flick,Action::RingCapture}){
        const unsigned value=action==Action::Flick?capturering::kOnTargetFlickValue:0;
        CHECK(apply(s,action,value)==Error::CollectionFull);CHECK(same(s,full));
    }
    CHECK(apply(s,Action::Flick,0)==Error::CollectionFull);CHECK(same(s,full));
    for(auto grade:{capturering::Grade::Red,capturering::Grade::Orange,capturering::Grade::Green}){
        const auto timing=phaseFor(s,grade);CHECK(!ringCaptureChance(s,timing));reject(s,timing,Error::CollectionFull);
    }
    CHECK(!captureChance(s)&&s.captureAttempts==0&&s.rngState==full.rngState);
    CHECK(apply(s,Action::Release,s.activeCreatureId)==Error::ActiveMemberRelease);CHECK(same(s,full));
    step(s,Action::Release,4);CHECK(s.collectionCount==59&&s.nextMemberId==61&&!findMember(s,4));
    CHECK(s.phase==full.phase&&s.wildFormId==full.wildFormId&&s.wildHp==full.wildHp&&s.wildTurn==full.wildTurn&&
        s.rngState==full.rngState&&s.captureAttempts==full.captureAttempts&&s.captures==full.captures);
    CHECK(!std::memcmp(s.journal,full.journal,sizeof(s.journal))&&s.activeCreatureId==full.activeCreatureId);
    for(unsigned i=0,j=0;i<full.collectionCount;++i)if(full.collection[i].id!=4){
        CHECK(!std::memcmp(&s.collection[j],&full.collection[i],sizeof(CreatureMember)));++j;
    }
    restore(s);const auto renewed=phaseFor(s,capturering::Grade::Green);
    CHECK(captureChance(s)>0&&ringCaptureChance(s,renewed)>0);
    const auto room=s;step(s,Action::RingCapture,renewed);
    CHECK(s.rngState==nextRng(room.rngState)&&s.lastCapture.attempt==1&&s.lastCapture.result==CaptureResult::Captured);
    CHECK(s.collectionCount==60&&s.nextMemberId==62&&s.collection[59].id==61&&!findMember(s,4));
    CHECK(s.activeCreatureId==room.activeCreatureId&&s.collection[0].id==room.collection[0].id);
    for(unsigned i=1;i<59;++i)CHECK(!std::memcmp(&s.collection[i],&room.collection[i],sizeof(CreatureMember)));
    restore(s);
}
void installedEightMigration(){
    namespace old=legacy_v13;
    for(unsigned phase=0;phase<4;++phase){
        auto prior=old::newDevice(117);
        CHECK(old::apply(prior,old::Action::StarterOfferSeed,991)==old::Error::None);
        if(phase){
            CHECK(old::apply(prior,old::Action::Hatch,1)==old::Error::None);
            CHECK(old::apply(prior,old::Action::WorldSeed,817)==old::Error::None);
            prior.sequence=prior.foregroundSequence=100;prior.collectionCount=8;
            prior.captures=7;prior.encounters=7;prior.steps=700;prior.nextMemberId=9;
            for(unsigned i=1;i<8;++i){prior.collection[i]=prior.collection[0];auto& m=prior.collection[i];
                m.id=i+1;m.capturedAtSequence=i+1;m.hp=10+i;m.energy=40+i;m.fullness=50+i;m.mood=60+i;m.bond=20+i;}
            CHECK(old::apply(prior,old::Action::Select,8)==old::Error::None);
            prior.lastCapture={prior.sequence,18,1,1,1,old::CaptureResult::Escaped};
            if(phase>1)CHECK(old::apply(prior,old::Action::AccrueSteps,1000)==old::Error::None);
            if(phase>2)CHECK(old::apply(prior,old::Action::PresentEncounter)==old::Error::None);
        }
        CHECK(old::isValid(prior));old::Snapshot saved;CHECK(old::encodeSnapshot(prior,saved));
        const auto original=saved;State restored;
        CHECK(decodeSnapshot(saved.bytes,sizeof(saved.bytes),restored)==SnapshotStatus::Migrated);
        CHECK(!std::memcmp(saved.bytes,original.bytes,sizeof(saved.bytes)));
        Snapshot current;CHECK(encodeSnapshot(restored,current));
        CHECK(snapshot_test::sameOldPayload(saved.bytes,current.bytes,sizeof(saved.bytes)));
        CHECK(restored.collectionCount==prior.collectionCount&&restored.activeCreatureId==prior.activeCreatureId);
        for(unsigned i=0;i<8;++i){const auto& a=prior.collection[i];const auto& b=restored.collection[i];
            CHECK(a.id==b.id&&static_cast<unsigned>(a.species)==static_cast<unsigned>(b.species)&&a.hp==b.hp&&a.energy==b.energy&&a.fullness==b.fullness&&a.mood==b.mood&&a.bond==b.bond&&a.level==b.level&&a.capturedAtSequence==b.capturedAtSequence&&a.xp==b.xp&&a.formId==b.formId);}
        for(unsigned i=8;i<60;++i){const auto& m=restored.collection[i];CHECK(!m.id&&m.species==Species::None&&!m.hp&&!m.energy&&!m.fullness&&!m.mood&&!m.bond&&!m.level&&!m.capturedAtSequence&&!m.xp&&!m.formId);}
        restore(restored);
        // Older firmware cannot decode the larger format, even with <=8 members.
        old::State rollback=prior;
        CHECK(old::decodeSnapshot(current.bytes,sizeof(current.bytes),rollback)!=old::SnapshotStatus::Ok);
        old::Snapshot rollbackBytes;CHECK(old::encodeSnapshot(rollback,rollbackBytes));
        CHECK(!std::memcmp(rollbackBytes.bytes,original.bytes,sizeof(original.bytes)));
        if(phase){
            auto invalid=saved;for(unsigned i=0;i<4;++i)invalid.bytes[104+i]=static_cast<unsigned char>(9u>>(8*i));
            const auto checksum=~crc(~0u,invalid.bytes,sizeof(invalid.bytes)-4);
            for(unsigned i=0;i<4;++i)invalid.bytes[sizeof(invalid.bytes)-4+i]=static_cast<unsigned char>(checksum>>(8*i));
            const auto untouched=restored;
            CHECK(decodeSnapshot(invalid.bytes,sizeof(invalid.bytes),restored)==SnapshotStatus::InvalidState&&same(restored,untouched));
        }
    }
}
void fullRosterPartnerEvolutionAndBudget(){
    auto s=newDevice();step(s,Action::Hatch,1);
    s.sequence=s.foregroundSequence=100;s.collectionCount=60;s.captures=s.encounters=59;s.steps=5900;s.nextMemberId=61;
    for(unsigned i=1;i<60;++i){s.collection[i]=s.collection[0];s.collection[i].id=i+1;s.collection[i].capturedAtSequence=i+1;s.collection[i].mood=30+i;}
    CHECK(isValid(s));const auto full=s;step(s,Action::Select,60);
    CHECK(s.activeCreatureId==60&&s.collectionCount==60&&s.mood==89);
    for(unsigned i=0;i<60;++i)CHECK(!std::memcmp(&s.collection[i],&full.collection[i],sizeof(CreatureMember)));
    restore(s);const auto partner=s;
    CHECK(apply(s,Action::Release,60)==Error::ActiveMemberRelease&&same(s,partner));
    step(s,Action::Select,1);s.level=s.collection[0].level=5;s.collection[0].xp=xpForLevel(5);s.bond=s.collection[0].bond=200;
    const auto before=s;const auto* edge=forms::outgoing(11,0);CHECK(edge);if(edge)step(s,Action::Evolve,edge->to);
    CHECK(s.collectionCount==60&&s.activeCreatureId==1&&s.nextMemberId==61&&s.collection[0].id==1&&s.collection[0].capturedAtSequence==0);
    for(unsigned i=1;i<60;++i)CHECK(!std::memcmp(&s.collection[i],&before.collection[i],sizeof(CreatureMember)));
    restore(s);
    // Maximum-width form from the independent3641-form/level sweep: all60
    // members, full journal, 10-digit identities/counters and active encounter.
    s=newDevice(UINT32_MAX);step(s,Action::Hatch,1);
    s.sequence=s.foregroundSequence=UINT32_MAX-1;s.steps=4294967200u;s.encounters=s.steps/100;s.captures=59;
    s.receivedTrades=UINT32_MAX-65;s.nextMemberId=s.captures+s.receivedTrades+2;s.collectionCount=60;s.activeCreatureId=s.nextMemberId-60;
    s.worldSeed=s.rngState=UINT32_MAX;s.phase=Phase::Encounter;s.wildFormId=18;s.wildSpecies=Species::Agumon;s.wildRules=kRulesVersion;s.wildLevel=20;s.wildTurn=1000;
    s.wildMaxHp=s.wildHp=combat::formProfile(18,20).stats.maxHp;s.captureAttempts=2;s.lastCapture={s.sequence,18,90,2,20,CaptureResult::Escaped};
    for(unsigned id=1;id<=forms::kFormCount;++id)s.journal[(id-1)/32]|=1u<<((id-1)%32);
    for(unsigned i=0;i<60;++i){auto& m=s.collection[i];m={s.activeCreatureId+i,static_cast<Species>(forms::find(245)->lineage),combat::formProfile(245,15).stats.maxHp,100,100,100,200,15,s.sequence-60+i,xpForLevel(15),245};}
    const auto& active=s.collection[0];s.hp=active.hp;s.energy=active.energy;s.fullness=active.fullness;s.mood=active.mood;s.bond=active.bond;s.level=active.level;
    for(unsigned i=0;i<kPartyCapacity;++i)s.partyMemberIds[i]=s.collection[i+1].id;
    CHECK(isValid(s));static char json[kJsonCapacity];const auto n=writeJson(s,json,sizeof(json));
    CHECK(n>38000&&n<kJsonCapacity&&sizeof(State)==2936&&sizeof(Snapshot)==2964&&kCollectionCapacity==60);
    CHECK(std::strstr(json,"\"collectionCapacity\":60"));
    char shortJson[32];CHECK(!writeJson(s,shortJson,sizeof(shortJson))&&!shortJson[0]);restore(s);
    std::printf("60-member wide JSON fixture: %zu/%zu bytes; State%zu Snapshot%zu\n",n,kJsonCapacity,sizeof(State),sizeof(Snapshot));
}
void odds(){
    using namespace capturering;
    for(unsigned base=0;base<=90;++base)for(auto grade:{Grade::Red,Grade::Orange,Grade::Green}){
        const unsigned factor=grade==Grade::Red?10:grade==Grade::Orange?50:100;
        const unsigned scaled=base*factor/100,expected=base?(scaled?scaled:1):0;
        CHECK(chanceForGrade(base,grade)==expected&&expected<=base);
    }
    unsigned ids[]{formWith(encounters::Rarity::Common),formWith(encounters::Rarity::Uncommon),formWith(encounters::Rarity::Rare)};
    for(unsigned bucket=0;bucket<3;++bucket)for(unsigned level:{1u,2u,6u,20u})for(unsigned hpDivisor:{2u,4u,10000u}){
        auto s=fight();target(s,ids[bucket],level);s.wildHp=s.wildMaxHp/hpDivisor;if(!s.wildHp)s.wildHp=1;
        const auto hpBase=50+40*(s.wildMaxHp-2*s.wildHp)/s.wildMaxHp;
        const auto diff=level-1,penalty=bucket*10+5*(diff>5?5:diff);
        const auto base=hpBase>penalty+10?hpBase-penalty:10;
        CHECK(captureChance(s)==base);
        for(unsigned care:{0u,200u}){
            s.bond=s.collection[0].bond=care;s.fullness=s.collection[0].fullness=care?100:0;s.mood=s.collection[0].mood=care?100:0;
            CHECK(isValid(s)&&captureChance(s)==base); // Care changes battle damage, never a second capture multiplier.
            for(auto grade:{Grade::Red,Grade::Orange,Grade::Green}){
                const auto phase=phaseFor(s,grade),chance=ringCaptureChance(s,phase);
                CHECK(chance==chanceForGrade(base,grade)&&chance>=1&&chance<=base);
                if(grade==Grade::Green)CHECK(chance==base&&chance<100);
            }
        }
    }
    auto minimum=fight();target(minimum,ids[2],20);
    CHECK(captureChance(minimum)==10&&ringCaptureChance(minimum,phaseFor(minimum,Grade::Red))==1);
}
void drawsAndReplay(){
    using namespace capturering;
    unsigned caught[3]{},escaped[3]{};
    for(unsigned seed=1;seed<=128;++seed)for(unsigned form:{18u,39u,60u,81u})for(auto grade:{Grade::Red,Grade::Orange,Grade::Green}){
        auto s=fight(seed);target(s,form);const auto before=s;const auto phase=phaseFor(s,grade),chance=ringCaptureChance(s,phase);
        auto replay=s;restore(replay);step(s,Action::RingCapture,phase);step(replay,Action::RingCapture,phase);CHECK(same(s,replay));
        const auto expectedRng=nextRng(before.rngState);const bool captures=expectedRng%100<chance;
        CHECK(s.rngState==expectedRng&&s.worldSeed==before.worldSeed&&s.encounterRng==before.encounterRng);
        CHECK(s.lastCapture.chance==chance&&s.lastCapture.attempt==1&&s.lastCapture.targetFormId==form);
        CHECK((s.lastCapture.result==CaptureResult::Captured)==captures&&s.lastCapture.result!=CaptureResult::Miss);
        CHECK(s.sequence==before.sequence+1&&s.lastCapture.sequence==s.foregroundSequence);
        if(captures)++caught[static_cast<unsigned>(grade)];
        else{++escaped[static_cast<unsigned>(grade)];CHECK(s.hp==before.hp&&s.wildHp==before.wildHp&&s.wildTurn==before.wildTurn);}
        restore(s);
        // A green attempt is exactly the existing full-odds aimed legacy event.
        if(grade==Grade::Green){auto old=before;step(old,Action::Flick,kOnTargetFlickValue);CHECK(same(s,old));}
    }
    for(unsigned i=0;i<3;++i){CHECK(caught[i]&&escaped[i]);std::printf("grade%u: %u captured/%u escaped across deterministic draws\n",i,caught[i],escaped[i]);}
    // Legacy misses still use no RNG and retain their zero-chance record.
    auto old=fight();const auto rng=old.rngState;step(old,Action::Flick,0);CHECK(old.rngState==rng&&old.lastCapture.chance==0&&old.lastCapture.result==CaptureResult::Miss);restore(old);
    // Select a deterministic seed whose first three draws all escape at1%.
    auto low=fight(1);target(low,formWith(encounters::Rarity::Rare),20);const auto phase=phaseFor(low,Grade::Red);
    CHECK(ringCaptureChance(low,phase)==1);const auto hp=low.hp,turn=low.wildTurn;
    for(unsigned attempt=1;attempt<=3;++attempt){CHECK(nextRng(low.rngState)%100>=1);step(low,Action::RingCapture,phase);CHECK(low.lastCapture.chance==1&&low.lastCapture.attempt==attempt&&low.lastCapture.result==CaptureResult::Escaped&&low.hp==hp);restore(low);if(attempt<3)CHECK(low.phase==Phase::Encounter&&low.wildTurn==turn);}
    CHECK(low.phase==Phase::Home&&low.message==Message::CaptureEnded);reject(low,phase,Error::WrongPhase);
    auto bad=low;bad.lastCapture.chance=0;CHECK(!isValid(bad));bad=low;bad.lastCapture.chance=91;CHECK(!isValid(bad));
    char json[kJsonCapacity];CHECK(writeJson(low,json,sizeof(json))>0&&std::strstr(json,"\"chance\":1"));
}
void modesAndLegacy(){
    using namespace capturering;
    for(unsigned rules:{5u,8u,9u,11u,12u,13u})for(bool automatic:{false,true}){
        auto s=newDevice(1);step(s,Action::Hatch,1);if(automatic)step(s,Action::Mode,1);step(s,Action::Explore,1000);
        s.wildFormId=18;s.wildSpecies=Species::Agumon;s.wildLevel=1;s.wildRules=rules;
        s.wildMaxHp=s.wildHp=rules<9?legacy_v8::combat::formProfile(18,1).stats.maxHp:combat::formProfile(18,1).stats.maxHp;
        s.hp=s.collection[0].hp=rules<9?legacy_v8::combat::formProfile(11,1).stats.maxHp:combat::formProfile(11,1).stats.maxHp;
        if(automatic){CHECK(isValid(s));step(s,Action::AutoFight);CHECK(s.autoCapture==AutoCapture::Awaiting);}
        else s.wildHp=s.wildMaxHp/2;
        const auto before=s;const auto base=captureChance(s);const auto phase=phaseFor(s,Grade::Red),chance=ringCaptureChance(s,phase);
        CHECK(chance==chanceForGrade(base,Grade::Red));step(s,Action::RingCapture,phase);
        CHECK(s.lastCapture.chance==chance&&s.lastCapture.result==CaptureResult::Escaped&&s.hp==before.hp&&s.wildTurn==before.wildTurn);
        CHECK(s.rngState==nextRng(before.rngState));restore(s);
    }
}
void frozenLegacyHistory(){
    namespace old=legacy_v13;
    unsigned hash=~0u,count=0;
    for(unsigned starter=1;starter<=8;++starter)for(unsigned seed=1;seed<=64;++seed)for(unsigned mode=0;mode<3;++mode){
        auto s=old::newDevice(seed);CHECK(old::apply(s,old::Action::Hatch,starter)==old::Error::None);CHECK(old::apply(s,old::Action::Explore,1000)==old::Error::None);s.wildHp=s.wildMaxHp/2;
        for(unsigned turn=0;turn<3&&s.phase==old::Phase::Encounter;++turn){
            CHECK(old::apply(s,mode==0?old::Action::Capture:old::Action::Flick,mode==1?41140u:0u)==old::Error::None);
            old::Snapshot bytes;CHECK(old::encodeSnapshot(s,bytes));hash=crc(hash,bytes.bytes,sizeof(bytes.bytes));++count;
        }
    }
    // Independently compiled from git110212b game.cpp/.hpp before this event was
    // added. Covers every canonical byte, including RNG, rewards, tries and CRC.
    CHECK(count==3408&&~hash==0xefe78ad1u);
    std::printf("%u legacy Capture/Flick snapshots unchanged: CRC%08x\n",count,~hash);
}
}
int main(){contract();fullRoster();installedEightMigration();fullRosterPartnerEvolutionAndBudget();odds();drawsAndReplay();modesAndLegacy();frozenLegacyHistory();std::printf("Ring capture core: %u checks, %u failures; schema22/rules15; frozen13 capture histories retained\n",checks,failures);return failures?1:0;}
