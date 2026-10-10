#include "game.hpp"
#include "snapshot_test_helpers.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace digivice;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); } } while(false)
void step(State& state,Action action,std::uint32_t value=0) {
    CHECK(apply(state,action,value)==Error::None); CHECK(isValid(state));
}
bool same(const State& a,const State& b) {
    Snapshot x,y;
    return encodeSnapshot(a,x)&&encodeSnapshot(b,y)&&!std::memcmp(x.bytes,y.bytes,kSnapshotSize);
}
void reject(State& state,Action action,std::uint32_t value,Error expected) {
    const auto before=state;
    CHECK(apply(state,action,value)==expected); CHECK(same(state,before));
}
State home(std::uint32_t seed=12345) {
    auto state=newDevice(seed); step(state,Action::Hatch,1); return state;
}
State waiting(std::uint32_t seed=1) {
    auto state=home(seed); step(state,Action::Mode,1); step(state,Action::Explore,1000);
    state.wildFormId=18; state.wildSpecies=Species::Agumon; state.wildLevel=1;
    state.wildMaxHp=combat::formProfile(18,1).stats.maxHp;
    state.wildHp=state.wildMaxHp/2+1;
    step(state,Action::AutoFight);
    CHECK(state.autoCapture==AutoCapture::Awaiting); return state;
}
std::uint32_t crc(const std::uint8_t* bytes,std::size_t size) {
    std::uint32_t value=~0u;
    for(std::size_t i=0;i<size;++i) { value^=bytes[i]; for(unsigned j=0;j<8;++j)value=(value>>1)^((value&1)?0xedb88320u:0u); }
    return ~value;
}
void put(std::uint8_t* bytes,std::uint32_t value) {
    for(unsigned i=0;i<4;++i)bytes[i]=static_cast<std::uint8_t>(value>>(8*i));
}
void roundtrip(const State& state) {
    Snapshot bytes; CHECK(encodeSnapshot(state,bytes)); State restored;
    CHECK(decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),restored)==SnapshotStatus::Ok);
    CHECK(same(state,restored));
}
void setup(State& state,std::uint32_t seed) {
    const auto before=state;
    step(state,Action::WorldSeed,seed);
    CHECK(state.worldSeed==seed&&worldSelectionSeed(state)==seed);
    CHECK(state.sequence==before.sequence+1&&state.foregroundSequence==before.foregroundSequence);
    // Normalizing exactly the two allowed fields preserves the entire canonical
    // state, including active/pending foe, capture reveal, RNG, gap and message.
    auto normalized=state; normalized.sequence=before.sequence; normalized.worldSeed=0;
    CHECK(same(normalized,before)); roundtrip(state);
    reject(state,Action::WorldSeed,seed,Error::InvalidAction);
    reject(state,Action::WorldSeed,seed==1?2:1,Error::InvalidAction);
}
void contracts() {
    CHECK(kSchemaVersion==24&&kRulesVersion==17&&kSnapshotSize==3216&&kV19SnapshotSize==660);
    Action parsed; CHECK(parseAction("world-seed",parsed)&&parsed==Action::WorldSeed);
    auto egg=newDevice(); reject(egg,Action::WorldSeed,17,Error::WrongPhase);
    auto state=home(); CHECK(worldSelectionSeed(state)==state.seed&&state.worldSeed==0);
    reject(state,Action::WorldSeed,0,Error::InvalidValue);
    reject(state,static_cast<Action>(static_cast<unsigned>(Action::PartyRemove)+1),0,Error::InvalidAction);
    for(auto seed:{1u,0xffffffffu}) { auto copy=state; setup(copy,seed); }
    state.sequence=UINT32_MAX; reject(state,Action::WorldSeed,17,Error::CounterOverflow);
    state=home(); step(state,Action::EncounterRate,0); setup(state,23);
    auto queued=home(); step(queued,Action::AccrueSteps,1000); CHECK(queued.pendingEncounter.formId); setup(queued,29);
    const auto original=queued.pendingEncounter; step(queued,Action::PresentEncounter);
    CHECK(queued.wildFormId==original.formId&&queued.wildLevel==original.level&&queued.wildRules==original.rules);
    auto active=home(); step(active,Action::Explore,1000); step(active,Action::AccrueSteps,1000); setup(active,31);
    auto automatic=home(); step(automatic,Action::Mode,1); step(automatic,Action::Explore,1000); setup(automatic,37);
    auto paused=waiting(); step(paused,Action::Flick,0); setup(paused,41);
    char json[kJsonCapacity]; CHECK(writeJson(paused,json,sizeof(json))>0);
    CHECK(std::strstr(json,"\"worldSeed\":41")&&std::strstr(json,"\"schemaVersion\":24"));
}
void futureSelection() {
    unsigned different=0;
    for(unsigned seed=1;seed<=128;++seed) {
        const auto base=home(12345);
        for(auto action:{Action::Walk,Action::Explore,Action::AccrueSteps}) {
            auto historical=base,seeded=base,replay=base;
            step(seeded,Action::WorldSeed,seed); step(replay,Action::WorldSeed,seed);
            const auto value=action==Action::Walk?100u:1000u;
            step(historical,action,value); step(seeded,action,value); step(replay,action,value);
            CHECK(same(seeded,replay)); roundtrip(seeded);
            const auto form=action==Action::AccrueSteps?seeded.pendingEncounter.formId:seeded.wildFormId;
            const auto old=action==Action::AccrueSteps?historical.pendingEncounter.formId:historical.wildFormId;
            const auto seededLevel=action==Action::AccrueSteps?seeded.pendingEncounter.level:seeded.wildLevel;
            const auto oldLevel=action==Action::AccrueSteps?historical.pendingEncounter.level:historical.wildLevel;
            CHECK(seededLevel>=1&&seededLevel<=kMaxLevel&&oldLevel>=1&&oldLevel<=kMaxLevel);
            CHECK(seededLevel+1>=base.level&&seededLevel<=base.level+1&&oldLevel+1>=base.level&&oldLevel<=base.level+1);
            CHECK(form==selectWildForm(1,seed,activeMember(base)->formId,seededLevel));
            CHECK(old==selectWildForm(1,base.seed,activeMember(base)->formId,oldLevel));
            CHECK(seeded.rngState==historical.rngState&&seeded.encounterRng==historical.encounterRng);
            CHECK(seeded.encounterTarget==historical.encounterTarget&&seeded.encounterProgress==historical.encounterProgress);
            different+=form!=old;
        }
    }
    CHECK(different>0);
    std::printf("384 seeded future selections checked; %u differ from fixed historical roster\n",different);
}
void unchangedCurrentFight() {
    for(unsigned seed=1;seed<=128;++seed) {
        auto original=waiting(seed),seeded=original;
        setup(seeded,seed+1000);
        CHECK(captureChance(original)==captureChance(seeded));
        step(original,Action::Flick,41140); step(seeded,Action::Flick,41140);
        CHECK(original.lastCapture.result==seeded.lastCapture.result&&original.lastCapture.chance==seeded.lastCapture.chance);
        CHECK(original.rngState==seeded.rngState&&original.captures==seeded.captures&&original.collectionCount==seeded.collectionCount);
        CHECK(original.hp==seeded.hp&&original.energy==seeded.energy&&original.wildHp==seeded.wildHp);
        original=home(seed); step(original,Action::Mode,1); step(original,Action::Explore,1000); seeded=original; setup(seeded,seed+2000);
        autobattle::Trace a,b; CHECK(applyAutoFight(original,&a)==Error::None&&applyAutoFight(seeded,&b)==Error::None);
        b.startSequence=a.startSequence; b.endSequence=a.endSequence;
        char x[autobattle::kTraceJsonCapacity],y[autobattle::kTraceJsonCapacity];
        CHECK(autobattle::writeJson(a,x,sizeof(x))&&autobattle::writeJson(b,y,sizeof(y))&&!std::strcmp(x,y));
        CHECK(original.rngState==seeded.rngState&&original.autoCapture==seeded.autoCapture);
    }
}
void migration() {
    for(unsigned phase=0;phase<5;++phase) {
        auto original=phase==0?newDevice():phase==4?waiting():home();
        if(phase==2)step(original,Action::AccrueSteps,1000);
        if(phase==3)step(original,Action::Explore,1000);
        if(original.wildRules)original.wildRules=13;if(original.pendingEncounter.rules)original.pendingEncounter.rules=13;
        Snapshot current; CHECK(encodeSnapshot(original,current));
        std::array<std::uint8_t,kV19SnapshotSize> old{};
        const auto projected=snapshot_test::eightSlotBytes(current.bytes);std::memcpy(old.data(),projected.data(),old.size()); old[4]=19; put(old.data()+8,13); old[6]=136; old[7]=2;
        put(old.data()+old.size()-4,crc(old.data(),old.size()-4));
        State restored; CHECK(decodeSnapshot(old.data(),old.size(),restored)==SnapshotStatus::Migrated);
        CHECK(!restored.worldSeed&&same(original,restored));
        CHECK(encodeSnapshot(restored,current)&&snapshot_test::sameOldPayload(old.data(),current.bytes,old.size()));
        roundtrip(restored);
        auto invalid=old; invalid.back()^=1; const auto before=restored;
        CHECK(decodeSnapshot(invalid.data(),invalid.size(),restored)==SnapshotStatus::BadChecksum&&same(restored,before));
    }
    auto egg=newDevice(); Snapshot bytes; CHECK(encodeSnapshot(egg,bytes));
    put(bytes.bytes+snapshot_test::currentOffset(656),123); put(bytes.bytes+kSnapshotSize-4,crc(bytes.bytes,kSnapshotSize-4));
    const auto before=egg;
    CHECK(decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),egg)==SnapshotStatus::InvalidState&&same(egg,before));
}
}
int main() {
    contracts(); futureSelection(); unchangedCurrentFight(); migration();
    std::printf("%u world seed checks, %u failures; State=%zu snapshot=%zu\n",checks,failures,sizeof(State),kSnapshotSize);
    return failures?1:0;
}
