#include "snapshot_test_helpers.hpp"
#include "trade.hpp"
#include "forms.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

using namespace digivice;
namespace t=digivice::trade;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);}}while(false)
void put32(std::uint8_t* p,std::uint32_t v){for(unsigned i=0;i<4;++i)p[i]=static_cast<std::uint8_t>(v>>(8*i));}
std::uint32_t crc(const std::uint8_t* p,std::size_t n){std::uint32_t v=~0u;for(std::size_t i=0;i<n;++i){v^=p[i];for(unsigned j=0;j<8;++j)v=(v>>1)^((v&1)?0xedb88320u:0);}return ~v;}
void step(State& s,Action a,unsigned value=0){CHECK(apply(s,a,value)==Error::None);CHECK(isValid(s));}
State fixture(unsigned starter=1,unsigned count=2){
    auto s=newDevice(100+starter);step(s,Action::Hatch,starter);
    s.sequence=s.foregroundSequence=100;s.collectionCount=count;s.nextMemberId=count+1;s.captures=s.encounters=count-1;s.steps=100*(count-1);
    for(unsigned i=1;i<count;++i){const auto id=78+i;const auto* f=forms::find(id);s.collection[i]={i+1,static_cast<Species>(f->lineage),17,63,47,89,100,5,i+1,xpForLevel(5)+17,id};s.journal[(id-1)/32]|=1u<<((id-1)%32);}
    CHECK(isValid(s));return s;
}
t::Transcript transcript(const State& a,const State& b,unsigned aId=2,unsigned bId=2){
    t::Transcript x;x.peers[0].bytes[0]=2;x.peers[1].bytes[0]=4;x.session=0xaabbccdd12345678ull;
    x.nonces[0]=197;x.nonces[1]=12345;x.revision=1;x.sourceSequences[0]=a.sequence;x.sourceSequences[1]=b.sequence;
    x.offers[0]=*findMember(a,aId);x.offers[1]=*findMember(b,bId);x.receivedTrades[0]=a.receivedTrades;x.receivedTrades[1]=b.receivedTrades;CHECK(t::valid(x));return x;
}
void roundtrip(const t::Record& record){
    std::array<std::uint8_t,t::kRecordBytes+2> bytes;bytes.fill(0xab);
    CHECK(t::valid(record));CHECK(t::encodeRecord(record,bytes.data()+1,t::kRecordBytes));CHECK(bytes.front()==0xab&&bytes.back()==0xab);
    t::Record decoded;CHECK(t::decodeRecord(bytes.data()+1,t::kRecordBytes,decoded));
    CHECK(decoded.serial==record.serial&&decoded.phase==record.phase&&decoded.localSide==record.localSide&&t::sameTranscript(decoded.transcript,record.transcript));
    CHECK(t::sameState(decoded.before,record.before)&&t::sameState(decoded.after,record.after));
}
std::vector<std::uint8_t> installedRecord(const t::Record& record,unsigned version){
    // Frozen installed layouts retain8 member slots and the original consent.
    CHECK(version==19||version==20);CHECK(record.transcript.rules==13);
    if(version==19)CHECK(record.before.worldSeed==0&&record.after.worldSeed==0);
    std::array<std::uint8_t,t::kRecordBytes> current{};
    CHECK(t::encodeRecord(record,current.data(),current.size()));
    const unsigned snapshotSize=version==19?660:664;
    std::vector<std::uint8_t> old(168+2*snapshotSize+4);std::memcpy(old.data(),current.data(),168);
    for(unsigned side=0;side<2;++side){
        auto* snapshot=old.data()+168+side*snapshotSize;
        const auto projected=snapshot_test::eightSlotBytes(current.data()+168+side*kSnapshotSize);
        std::memcpy(snapshot,projected.data(),snapshotSize-4);put32(snapshot+8,13);
        snapshot[4]=version;snapshot[5]=0;snapshot[6]=(snapshotSize-12)&255;snapshot[7]=(snapshotSize-12)>>8;
        put32(snapshot+snapshotSize-4,crc(snapshot,snapshotSize-4));
    }
    put32(old.data()+old.size()-4,crc(old.data(),old.size()-4));return old;
}

void installedJournalMigration(){
    auto a=fixture(),b=fixture(2);auto x=transcript(a,b);x.rules=13;
    for(unsigned version:{19u,20u})for(unsigned side=0;side<2;++side){
        const unsigned snapshotSize=version==19?660:664;
        const auto& state=side?b:a;
        t::Record prepared,committed,done,aborted;
        CHECK(t::prepare(state,x,side,7,prepared));
        auto walked=state;step(walked,Action::EncounterSeed,777+side);step(walked,Action::AccrueSteps,1000);
        CHECK(walked.pendingEncounter.formId!=0);walked.pendingEncounter.rules=13;
        CHECK(t::commit(prepared,walked,committed));CHECK(t::applied(committed,committed.after,done));CHECK(t::abort(prepared,aborted));
        for(const auto& record:{prepared,committed,done,aborted}){
            const auto old=installedRecord(record,version);const auto original=old;
            t::Record decoded;CHECK(t::decodeRecord(old.data(),old.size(),decoded));
            CHECK(decoded.serial==record.serial&&decoded.localSide==side&&decoded.phase==record.phase);
            CHECK(t::sameTranscript(decoded.transcript,record.transcript));
            CHECK(t::sameState(decoded.before,record.before)&&t::sameState(decoded.after,record.after));
            CHECK(decoded.before.worldSeed==0&&decoded.after.worldSeed==0&&old==original);
            roundtrip(decoded);
            auto corrupt=old;corrupt[168+snapshotSize+4]=21;
            put32(corrupt.data()+168+2*snapshotSize-4,crc(corrupt.data()+168+snapshotSize,snapshotSize-4));
            put32(corrupt.data()+corrupt.size()-4,crc(corrupt.data(),corrupt.size()-4));
            CHECK(!t::decodeRecord(corrupt.data(),corrupt.size(),decoded));
            CHECK(t::sameState(decoded.after,record.after)&&decoded.phase==record.phase);
        }
        const auto old=installedRecord(committed,version);
        for(std::size_t i=0;i<old.size();++i){
            auto corrupt=old;corrupt[i]^=1;auto destination=prepared;
            CHECK(!t::decodeRecord(corrupt.data(),corrupt.size(),destination));
            CHECK(destination.phase==prepared.phase&&t::sameState(destination.after,prepared.after));
        }
    }
    // Each device keeps its own world seed through an exchange. It is not a
    // background field that can silently change underneath a Prepared record.
    a.worldSeed=123456789;b.worldSeed=987654321;auto seeded=transcript(a,b);seeded.rules=13;
    for(unsigned side=0;side<2;++side){
        const auto& state=side?b:a;t::Record prepared,committed;
        CHECK(t::prepare(state,seeded,side,8,prepared));CHECK(t::commit(prepared,state,committed));
        CHECK(committed.after.worldSeed==state.worldSeed);roundtrip(committed);
        const auto old20=installedRecord(committed,20);t::Record restored;CHECK(t::decodeRecord(old20.data(),old20.size(),restored));
        CHECK(t::sameState(restored.before,committed.before)&&t::sameState(restored.after,committed.after)&&t::sameTranscript(restored.transcript,committed.transcript));
        auto changed=state;++changed.sequence;++changed.worldSeed;
        CHECK(isValid(changed)&&!t::backgroundOnly(state,changed));
        CHECK(!t::commit(prepared,changed,committed));CHECK(committed.after.worldSeed==state.worldSeed);
    }
}
void exchangeProgression(){
    unsigned formsChecked=0;
    for(unsigned formId=11;formId<=forms::kFormCount;++formId){
        auto a=fixture(1),b=fixture(2);const auto* f=forms::find(formId);
        auto& incoming=b.collection[1];incoming.formId=formId;incoming.species=static_cast<Species>(f->lineage);
        incoming.level=20;incoming.xp=kMaxXp;incoming.bond=177;incoming.hp=13;incoming.energy=21;incoming.fullness=39;incoming.mood=57;
        b.journal[(formId-1)/32]|=1u<<((formId-1)%32);CHECK(isValid(b));
        const auto x=transcript(a,b);t::Record records[2],committed[2],done[2];
        CHECK(t::prepare(a,x,0,1,records[0]));CHECK(t::prepare(b,x,1,1,records[1]));
        for(unsigned side=0;side<2;++side){const auto& before=side?b:a;
            CHECK(t::commit(records[side],before,committed[side]));CHECK(t::valid(committed[side]));const auto& after=committed[side].after;
            auto expected=x.offers[1-side];expected.id=before.nextMemberId;expected.capturedAtSequence=before.sequence+1;
            CHECK(t::sameMember(*findMember(after,expected.id),expected));CHECK(!findMember(after,x.offers[side].id));
            CHECK(after.collectionCount==before.collectionCount&&after.activeCreatureId==before.activeCreatureId&&t::sameMember(after.collection[0],before.collection[0]));
            CHECK(after.receivedTrades==1&&after.captures==before.captures&&after.encounters==before.encounters&&after.rngState==before.rngState&&after.encounterRng==before.encounterRng);
            CHECK(after.nextMemberId==before.nextMemberId+1&&after.sequence==before.sequence+1&&after.foregroundSequence==after.sequence);
            CHECK(hasObtained(after,x.offers[side].formId)&&hasObtained(after,expected.formId));
            CHECK(t::applied(committed[side],after,done[side]));roundtrip(done[side]);
            t::Record ignored;CHECK(!t::commit(done[side],after,ignored)&&!t::abort(committed[side],ignored)&&!t::applied(done[side],after,ignored));
        }
        ++formsChecked;
    }
    CHECK(formsChecked==266);
    // Trading the founder is safe: incoming never inherits local founder ID1.
    for(unsigned count:{2u,8u,60u})for(unsigned active:{1u,2u}){
        auto a=fixture(1,count),b=fixture(2,count);step(a,Action::Select,active);const auto x=transcript(a,b,1,2);
        t::Record prepared,committed;CHECK(t::prepare(a,x,0,3,prepared));CHECK(t::commit(prepared,a,committed));const auto& after=committed.after;
        CHECK(!findMember(after,1)&&after.activeCreatureId==2&&after.collectionCount==count&&after.collection[count-1].id==a.nextMemberId);
        CHECK(after.hp==after.collection[0].hp&&after.bond==after.collection[0].bond&&after.collection[count-1].capturedAtSequence==after.sequence);
        roundtrip(committed);
    }
}
void fullCollectionsExchange(){
    auto a=fixture(1,60),b=fixture(2,60);step(a,Action::Select,60);step(b,Action::Select,59);
    const auto x=transcript(a,b,2,3);
    for(unsigned side=0;side<2;++side){
        const auto& before=side?b:a;CHECK(t::canOffer(before,x.offers[side].id));t::Record prepared,committed;
        CHECK(t::prepare(before,x,side,31,prepared));CHECK(t::commit(prepared,before,committed));const auto& after=committed.after;
        CHECK(after.collectionCount==60&&after.nextMemberId==62&&after.activeCreatureId==before.activeCreatureId);
        CHECK(after.captures==before.captures&&after.rngState==before.rngState&&after.receivedTrades==before.receivedTrades+1);
        for(unsigned i=0;i<60;++i){const auto& member=before.collection[i];if(member.id==x.offers[side].id)CHECK(!findMember(after,member.id));
            else {const auto* retained=findMember(after,member.id);CHECK(retained&&t::sameMember(*retained,member));}}
        auto incoming=x.offers[1-side];incoming.id=61;incoming.capturedAtSequence=before.sequence+1;
        CHECK(t::sameMember(after.collection[59],incoming));roundtrip(committed);
    }
}

void restrictionsAndConsent(){
    auto a=fixture(),b=fixture(2);const auto x=transcript(a,b);
    auto lone=fixture(1,1);CHECK(!t::canOffer(lone,1));CHECK(!t::canOffer(newDevice(),1));CHECK(!t::canOffer(a,99));
    auto oldPartner=a;oldPartner.collection[1].formId=4;oldPartner.collection[1].species=Species::Flicker;oldPartner.journal[0]|=1u<<3;CHECK(isValid(oldPartner));
    CHECK(!t::canOffer(oldPartner,1)&&!t::canOffer(oldPartner,2));
    auto fight=a;step(fight,Action::Explore,1000);CHECK(fight.phase==Phase::Encounter&&!t::canOffer(fight,2));
    t::Record record,sentinel;CHECK(t::prepare(a,x,0,17,record));sentinel=record;
    CHECK(!t::prepare(a,x,2,1,sentinel)&&sentinel.serial==17);CHECK(!t::prepare(a,x,0,0,sentinel)&&sentinel.serial==17);
    auto changed=a;changed.collection[1].mood--;CHECK(isValid(changed));CHECK(!t::prepare(changed,x,0,1,sentinel));
    changed=a;step(changed,Action::Release,2);CHECK(!t::prepare(changed,x,0,1,sentinel));
    // Offer edits change the exact transcript and invalidate prior consent.
    auto revision=x;++revision.revision;CHECK(t::valid(revision)&&!t::sameTranscript(x,revision)&&t::fingerprint(x)!=t::fingerprint(revision));
    revision=x;revision.offers[1].mood--;CHECK(!t::sameTranscript(x,revision)&&t::fingerprint(x)!=t::fingerprint(revision));
    revision=x;revision.peers[0].bytes[0]=1;CHECK(!t::valid(revision));
    revision=x;revision.peers[1]=revision.peers[0];CHECK(!t::valid(revision));
    revision=x;revision.offers[1].bond=201;CHECK(!t::valid(revision));
    revision=x;revision.offers[1].xp++;revision.offers[1].level=4;CHECK(!t::valid(revision));
    revision=x;revision.offers[1].capturedAtSequence=b.sequence+1;CHECK(!t::valid(revision));
    revision=x;revision.offers[1].species=static_cast<Species>(65535);CHECK(!t::valid(revision));
    revision=x;revision.nonces[0]=0;CHECK(!t::valid(revision));revision=x;revision.session=0;CHECK(!t::valid(revision));
    revision=x;revision.receivedTrades[0]=a.sequence+1;CHECK(!t::valid(revision));
    revision=x;revision.receivedTrades[0]=1;CHECK(t::valid(revision)&&!t::prepare(a,revision,0,1,sentinel));
}
void walkingAndInterruptedDecisions(){
    auto a=fixture(),b=fixture(2);const auto x=transcript(a,b);t::Record prepared,committed,aborted;
    CHECK(t::prepare(a,x,0,1,prepared));roundtrip(prepared);
    auto walked=a;step(walked,Action::EncounterSeed,777);step(walked,Action::AccrueSteps,1000);step(walked,Action::AccrueSteps,63);
    CHECK(t::backgroundOnly(a,walked)&&walked.pendingEncounter.formId);
    CHECK(t::commit(prepared,walked,committed));roundtrip(committed);
    CHECK(committed.after.explorationSteps==walked.explorationSteps&&committed.after.encounterRng==walked.encounterRng&&committed.after.pendingEncounter.formId==walked.pendingEncounter.formId);
    CHECK(committed.after.sequence==walked.sequence+1&&committed.after.collection[kCollectionCapacity-1].id==0);
    t::Record renewed;CHECK(t::prepare(walked,x,0,2,renewed));CHECK(t::sameState(renewed.before,walked));
    CHECK(t::abort(prepared,aborted));roundtrip(aborted);CHECK(t::sameState(aborted.before,a)&&t::sameState(aborted.after,a));
    // Abort is a decision record only; callers retain the newer walking save.
    CHECK(t::backgroundOnly(aborted.before,walked));t::Record ignored;CHECK(!t::commit(aborted,walked,ignored));
    auto changed=a;step(changed,Action::Feed);CHECK(!t::backgroundOnly(a,changed)&&!t::commit(prepared,changed,ignored));
    changed=a;step(changed,Action::Select,2);CHECK(!t::commit(prepared,changed,ignored));
    changed=a;step(changed,Action::EncounterRate,0);CHECK(!t::commit(prepared,changed,ignored));
    changed=walked;step(changed,Action::PresentEncounter);CHECK(!t::backgroundOnly(a,changed)&&!t::commit(prepared,changed,ignored));
    auto alreadyQueued=walked;CHECK(t::prepare(alreadyQueued,x,0,2,renewed));auto cleared=alreadyQueued;cleared.pendingEncounter={};CHECK(isValid(cleared));CHECK(!t::backgroundOnly(alreadyQueued,cleared));
    // Lost participant review can safely be rejected by a coordinator whose old
    // offer no longer exists, with no rollback or fabricated ownership change.
    auto released=a;step(released,Action::Release,2);CHECK(t::abortUnprepared(released,x,0,3,aborted));roundtrip(aborted);
    CHECK(t::sameState(aborted.after,released));CHECK(!t::abortUnprepared(released,x,1,3,ignored));
    CHECK(!t::applied(committed,walked,ignored));CHECK(t::applied(committed,committed.after,ignored));
    // A replay cannot apply the same trade twice to its own committed output.
    CHECK(!t::commit(prepared,committed.after,ignored));
    // Reserve the final revision for ownership commit; runtime must stop game
    // walking checkpoints at MAX-1 while Prepared (lifetime pedometer continues).
    auto almost=a;almost.sequence=UINT32_MAX-1;auto near=transcript(almost,b);CHECK(t::prepare(almost,near,0,4,prepared));
    CHECK(t::commit(prepared,almost,committed)&&committed.after.sequence==UINT32_MAX);roundtrip(committed);
    step(almost,Action::AccrueSteps,1);CHECK(!t::commit(prepared,almost,ignored));
    auto exhausted=a;exhausted.sequence=UINT32_MAX;CHECK(!t::canOffer(exhausted,2));
    exhausted=a;exhausted.sequence=UINT32_MAX-1;exhausted.receivedTrades=UINT32_MAX-3;exhausted.nextMemberId=UINT32_MAX;CHECK(isValid(exhausted)&&!t::canOffer(exhausted,2));
}
void encodingAndMigration(){
    auto a=fixture(),b=fixture(2);const auto x=transcript(a,b);t::Record prepared;CHECK(t::prepare(a,x,0,1,prepared));
    std::array<std::uint8_t,t::kTranscriptBytes> bytes;CHECK(t::encodeTranscript(x,bytes.data(),bytes.size()));
    for(std::size_t n=0;n<bytes.size();++n){auto destination=x;CHECK(!t::decodeTranscript(bytes.data(),n,destination)&&t::sameTranscript(x,destination));}
    for(unsigned i=0;i<bytes.size();++i){auto damaged=bytes;damaged[i]^=1;auto destination=x;CHECK(!t::decodeTranscript(damaged.data(),damaged.size(),destination)&&t::sameTranscript(x,destination));}
    for(unsigned offset:{4u,8u,44u,52u,56u,60u,76u,140u,144u}){auto damaged=bytes;put32(damaged.data()+offset,UINT32_MAX);put32(damaged.data()+bytes.size()-4,crc(damaged.data(),bytes.size()-4));auto destination=x;CHECK(!t::decodeTranscript(damaged.data(),damaged.size(),destination));}
    std::array<std::uint8_t,t::kRecordBytes> recordBytes;CHECK(t::encodeRecord(prepared,recordBytes.data(),recordBytes.size()));
    for(unsigned i=0;i<recordBytes.size();++i){auto damaged=recordBytes;damaged[i]^=1;auto destination=prepared;CHECK(!t::decodeRecord(damaged.data(),damaged.size(),destination)&&t::sameState(destination.before,prepared.before));}
    for(unsigned offset:{12u,13u,14u,15u}){auto damaged=recordBytes;damaged[offset]=255;put32(damaged.data()+damaged.size()-4,crc(damaged.data(),damaged.size()-4));t::Record destination;CHECK(!t::decodeRecord(damaged.data(),damaged.size(),destination));}
    CHECK(!t::encodeRecord(prepared,recordBytes.data(),recordBytes.size()-1));CHECK(!t::decodeRecord(nullptr,recordBytes.size(),prepared));
    // Schema17 migration preserves every old payload byte and starts trade count0.
    Snapshot current;CHECK(encodeSnapshot(a,current));std::array<std::uint8_t,kV17SnapshotSize> prior;
    const auto projected=snapshot_test::eightSlotBytes(current.bytes);std::memcpy(prior.data(),projected.data(),prior.size());prior[4]=17;put32(prior.data()+8,13);prior[6]=128;prior[7]=2;put32(prior.data()+prior.size()-4,crc(prior.data(),prior.size()-4));
    State migrated;CHECK(decodeSnapshot(prior.data(),prior.size(),migrated)==SnapshotStatus::Migrated&&migrated.receivedTrades==0&&t::sameState(a,migrated));
    CHECK(encodeSnapshot(migrated,current)&&snapshot_test::sameOldPayload(prior.data(),current.bytes,prior.size()));
    put32(current.bytes+snapshot_test::currentOffset(648),1);put32(current.bytes+kSnapshotSize-4,crc(current.bytes,kSnapshotSize-4));CHECK(decodeSnapshot(current.bytes,sizeof(current.bytes),migrated)==SnapshotStatus::InvalidState);
    CHECK(kSchemaVersion==22&&kRulesVersion==15&&kSnapshotSize==2964&&kV19SnapshotSize==660&&t::kTranscriptBytes==152&&t::kRecordBytes==6100&&t::kV19RecordBytes==1492);
}
}
int main(){exchangeProgression();fullCollectionsExchange();restrictionsAndConsent();walkingAndInterruptedDecisions();encodingAndMigration();installedJournalMigration();
    std::printf("%u trade core checks, %u failures; State=%zu Transcript=%zu Record=%zu serialized=%zu/%zu bytes\n",checks,failures,sizeof(State),sizeof(t::Transcript),sizeof(t::Record),t::kTranscriptBytes,t::kRecordBytes);return failures?1:0;}
