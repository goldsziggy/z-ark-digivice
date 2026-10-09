#include "practice_session.hpp"
#include "combat.hpp"
#include "forms.hpp"
#include "legacy_practice_v2.hpp"
#include "legacy_practice_v3.hpp"
#include "legacy_practice_v5.hpp"
#include "legacy_practice_v6.hpp"
#include "catalog_fixture.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace d = digivice::devicepractice;
namespace p = digivice::practice;
using digivice::State;
unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %u: %s\n",__LINE__,#x); std::exit(1); } } while(false)
class Memory final : public d::Backend {
public:
    d::Slot slots[2]; bool present[2]{}; bool unreadable = false, failBefore = false, failAfter = false, corrupt = false, readback = false;
    unsigned writes = 0;
    d::Read read(unsigned i, d::Slot& out) override {
        if (unreadable || (readback && writes)) return d::Read::Unreadable;
        if (!present[i]) return d::Read::Missing;
        out = slots[i]; return d::Read::Present;
    }
    bool write(unsigned i, const d::Bytes& bytes) override {
        ++writes;
        if (failBefore) return false;
        slots[i] = {bytes, d::kRecordBytes}; present[i] = true;
        if (corrupt) slots[i].bytes.data[50] ^= 1;
        return !failAfter;
    }
};
bool sameCare(const State& a,const State& b) {
    digivice::Snapshot x,y;
    return digivice::encodeSnapshot(a,x) && digivice::encodeSnapshot(b,y) && !std::memcmp(x.bytes,y.bytes,sizeof(x.bytes));
}
bool sameBattle(const p::State& a,const p::State& b) {
    p::Snapshot x,y;
    return p::encodeSnapshot(a,x) && p::encodeSnapshot(b,y) && !std::memcmp(x.bytes,y.bytes,sizeof(x.bytes));
}
d::Reply command(d::PracticeSession& session,const char* text,const State& care,std::uint32_t seed=12345,bool writable=true) {
    const auto before = care;
    auto reply = session.command(text,care,writable,seed);
    CHECK(sameCare(before,care));
    return reply;
}
State productionCare() {
    auto state=digivice::newDevice();
    CHECK(digivice::apply(state,digivice::Action::Hatch,1)==digivice::Error::None);
    return state;
}
void basicAndRetry() {
    Memory memory; d::PracticeSession s(memory); auto care = productionCare();
    CHECK(s.restore()==d::Result::Empty && s.writable() && !s.hasBattle());
    CHECK(!s.blocksPartner() && s.allowsCareAction(digivice::Action::Select));
    CHECK(s.allowsCareAction(digivice::Action::Explore));
    CHECK(s.allowsCareAction(digivice::Action::PresentEncounter));
    CHECK(command(s,"practice start 1 0 auto",care).result==d::Result::Applied);
    CHECK(s.revision()==1 && s.lastCommandId()==1 && s.mode()==d::Mode::Auto && s.companionId()==1);
    CHECK(s.battle() && s.battle()->status!=p::Status::Active && !s.blocksPartner());
    CHECK(s.allowsCareAction(digivice::Action::Explore));
    const auto terminal=*s.battle(); const auto writes=memory.writes;
    auto egg=digivice::newDevice();
    CHECK(command(s,"practice start 1 0 auto",egg,987,false).result==d::Result::Duplicate);
    CHECK(memory.writes==writes && sameBattle(*s.battle(),terminal));
    CHECK(command(s,"practice start 1 0 tactical",care).result==d::Result::Conflict);
    CHECK(command(s,"practice start 1 1 auto",care).result==d::Result::Conflict);
    CHECK(command(s,"practice start 2 0 auto",care).result==d::Result::Stale);
    CHECK(command(s,"practice act 2 1 physical",care).result==d::Result::AutoComplete);
    digivice::autobattle::Trace a,b;
    CHECK(s.replay(a) && s.replay(b) && a.count==b.count && memory.writes==writes);
    CHECK(a.playerSpecies==static_cast<unsigned>(care.collection[0].species) && a.playerLevel==1 && a.outcome!=digivice::autobattle::Outcome::None);
    d::PracticeSession restarted(memory);
    CHECK(restarted.restore()==d::Result::Ready && sameBattle(*restarted.battle(),terminal));
    CHECK(command(restarted,"practice start 1 0 auto",egg,4,false).result==d::Result::Duplicate);
    CHECK(restarted.replay(b) && b.count==a.count);
    CHECK(command(restarted,"practice start 2 1 tactical",care).result==d::Result::Applied);
    CHECK(restarted.mode()==d::Mode::Tactical && restarted.battle()->sequence==0 && restarted.revision()==2);
    CHECK(restarted.blocksPartner() && !restarted.allowsCareAction(digivice::Action::Select) && !restarted.allowsCareAction(digivice::Action::Walk));
    CHECK(!restarted.allowsCareAction(digivice::Action::Explore));
    CHECK(!restarted.allowsCareAction(digivice::Action::PresentEncounter));
    CHECK(restarted.allowsCareAction(digivice::Action::AccrueSteps) && restarted.allowsCareAction(digivice::Action::EncounterSeed));
    {
        d::PracticeSession restoredActive(memory);
        CHECK(restoredActive.restore()==d::Result::Ready && !restoredActive.allowsCareAction(digivice::Action::Explore));
    }
    for(auto action:{digivice::Action::Feed,digivice::Action::Play,digivice::Action::Rest}) CHECK(restarted.allowsCareAction(action));
    CHECK(command(restarted,"practice start 1 0 auto",care).result==d::Result::Stale);
    CHECK(command(restarted,"practice start 3 2 auto",care).result==d::Result::ActiveBattle);
    CHECK(command(restarted,"practice act 3 2 brace",care).result==d::Result::CoreRejected);
    CHECK(restarted.revision()==2);
    const auto frozen=*restarted.battle();
    for(unsigned i=0;i<8;i++) CHECK(digivice::apply(care,digivice::Action::Play)==digivice::Error::None);
    CHECK(care.level==1); // Care grows bond, not XP, under RPG rules.
    care.collection[0].xp=digivice::xpForLevel(2); care.collection[0].level=care.level=2;
    CHECK(digivice::isValid(care) && restarted.battle()->playerLevel==frozen.playerLevel);
    CHECK(command(restarted,"practice act 3 2 card 1",care).result==d::Result::Applied);
    CHECK(restarted.revision()==3 && restarted.battle()->cardUsed);
    CHECK(command(restarted,"practice act 3 2 card 1",egg,0,false).result==d::Result::Duplicate);
    CHECK(command(restarted,"practice act 3 2 card 2",care).result==d::Result::Conflict);
    CHECK(command(restarted,"practice act 4 3 physical",care).result==d::Result::Applied);
    CHECK(restarted.battle()->playerLevel==1 && restarted.battle()->phase==p::Phase::Defend);
    CHECK(command(restarted,"practice act 5 4 retreat",care).result==d::Result::Applied);
    CHECK(!restarted.blocksPartner() && restarted.battle()->status==p::Status::Retreated);
    CHECK(restarted.allowsCareAction(digivice::Action::Explore));
    CHECK(command(restarted,"practice start 6 5 auto",care).result==d::Result::Applied);
    CHECK(restarted.battle()->playerLevel==2 && restarted.revision()==6);
}
void failureCases() {
    for(unsigned failure=0;failure<4;failure++) {
        Memory m; d::PracticeSession s(m); auto care=productionCare();
        CHECK(s.restore()==d::Result::Empty);
        m.failBefore=failure==0; m.failAfter=failure==1; m.corrupt=failure==2; m.readback=failure==3;
        CHECK(command(s,"practice start 1 0 auto",care).result==d::Result::RecoveryRequired);
        CHECK(!s.writable() && !s.hasBattle() && s.revision()==0 && s.blocksPartner());
        CHECK(!s.allowsCareAction(digivice::Action::Explore));
        CHECK(!s.allowsCareAction(digivice::Action::PresentEncounter));
        CHECK(s.allowsCareAction(digivice::Action::AccrueSteps) && s.allowsCareAction(digivice::Action::EncounterSeed));
        CHECK(s.allowsCareAction(digivice::Action::Feed));
        const auto writes=m.writes;
        CHECK(command(s,"practice start 1 0 auto",care).result==d::Result::RecoveryRequired && m.writes==writes);
        m.failBefore=m.failAfter=m.corrupt=m.readback=false;
        d::PracticeSession reboot(m); const auto restored=reboot.restore();
        if(failure==0) CHECK(restored==d::Result::Empty);
        else if(failure==2) CHECK(restored==d::Result::RecoveryRequired && !reboot.writable());
        else {
            CHECK(restored==d::Result::Ready && reboot.revision()==1);
            CHECK(command(reboot,"practice start 1 0 auto",care,999).result==d::Result::Duplicate);
            CHECK(m.writes==writes);
        }
        // A verified empty/completed restore releases the gate; corrupt storage does not.
        CHECK(reboot.allowsCareAction(digivice::Action::Explore)==(restored!=d::Result::RecoveryRequired));
    }
    // A lost acknowledgment on a Tactical action restores only that one turn.
    Memory m; d::PracticeSession s(m); auto care=productionCare();
    CHECK(s.restore()==d::Result::Empty);
    CHECK(command(s,"practice start 1 0 tactical",care).result==d::Result::Applied);
    const auto old=*s.battle(); m.failAfter=true;
    CHECK(command(s,"practice act 2 1 physical",care).result==d::Result::RecoveryRequired);
    CHECK(sameBattle(old,*s.battle()) && s.revision()==1);
    CHECK(!s.allowsCareAction(digivice::Action::Explore));
    m.failAfter=false; d::PracticeSession restarted(m);
    CHECK(restarted.restore()==d::Result::Ready && restarted.revision()==2 && restarted.battle()->exchanges==1);
    const auto writes=m.writes;
    CHECK(command(restarted,"practice act 2 1 physical",care).result==d::Result::Duplicate && m.writes==writes);
    // Every corrupt byte is detected and neither slot is rewritten on boot.
    const auto saved=m.slots[1];
    for(std::size_t i=0;i<d::kRecordBytes;i++) {
        m.slots[1]=saved; m.slots[1].bytes.data[i]^=0x40;
        d::PracticeSession bad(m); CHECK(bad.restore()==d::Result::RecoveryRequired);
        CHECK(!bad.writable() && bad.blocksPartner() && m.writes==writes);
    }
    m.slots[1]=saved; m.slots[1].length=d::kRecordBytes+1;
    d::PracticeSession future(m); CHECK(future.restore()==d::Result::RecoveryRequired && m.writes==writes);
    m.unreadable=true; d::PracticeSession unreadable(m);
    CHECK(unreadable.restore()==d::Result::RecoveryRequired && !unreadable.writable());
    CHECK(!unreadable.allowsCareAction(digivice::Action::Explore));
}
void parsingAndStarts() {
    Memory m; d::PracticeSession s(m); auto care=productionCare();
    CHECK(s.restore()==d::Result::Empty);
    for(const char* invalid:{"", "practice", "practice start", "practice start 0 0 auto", "practice start -1 0 auto", "practice start 4294967296 0 auto", "practice start 1 -1 auto", "practice start 1 0 AUTO", "practice start 1 0 auto extra", "practice act 1 0 card", "practice act 1 0 physical 0", "practice act 1 0 unknown", "other start 1 0 auto"}) {
        CHECK(command(s,invalid,care).result==d::Result::InvalidCommand && m.writes==0);
    }
    char longLine[180]; std::memset(longLine,'a',179); longLine[179]=0;
    CHECK(command(s,longLine,care).result==d::Result::InvalidCommand && m.writes==0);
    CHECK(command(s,"practice status",care).result==d::Result::Status);
    CHECK(command(s,"practice help",care).result==d::Result::Help);
    CHECK(command(s,"practice replay",care).result==d::Result::Replay);
    const auto egg=digivice::newDevice();
    const auto testPartner=digivice::newGame();
    CHECK(command(s,"practice start 1 0 auto",testPartner).result==d::Result::InvalidStart && m.writes==0);
    CHECK(command(s,"practice start 1 0 auto",egg).result==d::Result::InvalidStart);
    CHECK(command(s,"practice start 1 0 auto",care,1,false).result==d::Result::InvalidStart);
    CHECK(digivice::apply(care,digivice::Action::Walk,100)==digivice::Error::None);
    CHECK(command(s,"practice start 1 0 auto",care).result==d::Result::InvalidStart && m.writes==0);
    care=productionCare();
    CHECK(command(s,"practice start 4294967295 0 auto",care).result==d::Result::Applied);
    const auto writes=m.writes;
    CHECK(command(s,"practice start 4294967295 0 auto",care).result==d::Result::Duplicate);
    CHECK(command(s,"practice start 1 1 auto",care).result==d::Result::Stale && m.writes==writes);
}
void setWord(d::Slot& slot,std::size_t offset,std::uint32_t value) {
    for(unsigned i=0;i<4;i++) slot.bytes.data[offset+i]=static_cast<std::uint8_t>(value>>(8*i));
}
void reseal(d::Slot& slot) {
    std::uint32_t crc=0xffffffffu;
    for(std::size_t i=0;i<slot.length-4;i++) {
        crc^=slot.bytes.data[i];
        for(unsigned bit=0;bit<8;bit++) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));
    }
    setWord(slot,slot.length-4,~crc);
}
void envelopesAndLimits() {
    Memory m; d::PracticeSession s(m); auto care=productionCare();
    CHECK(s.restore()==d::Result::Empty);
    CHECK(command(s,"practice start 1 0 auto",care).result==d::Result::Applied);
    const auto original=m.slots[0]; const auto writes=m.writes;
    // Valid checksums do not make unknown versions, enum values or reserved
    // bytes acceptable, and boot never repairs or replaces these records.
    for(const auto offset:{4u,12u,28u,32u,36u,284u}) {
        m.slots[0]=original; setWord(m.slots[0],offset,99); reseal(m.slots[0]);
        d::PracticeSession incompatible(m);
        CHECK(incompatible.restore()==d::Result::RecoveryRequired && m.writes==writes);
    }
    m.slots[0]=original; m.slots[1]=original; m.present[1]=true;
    d::PracticeSession identical(m);
    CHECK(identical.restore()==d::Result::Ready && identical.revision()==1);
    // Two otherwise valid records with the same revision and different receipts
    // are ambiguous; preserve the bytes and disable practice writes.
    setWord(m.slots[1],20,2); reseal(m.slots[1]);
    d::PracticeSession conflict(m);
    CHECK(conflict.restore()==d::Result::RecoveryRequired && conflict.hasBattle());
    CHECK(command(conflict,"practice start 3 1 auto",care).result==d::Result::RecoveryRequired && m.writes==writes);
    m.present[1]=false; m.slots[0]=original;
    setWord(m.slots[0],8,UINT32_MAX); setWord(m.slots[0],24,UINT32_MAX-1); reseal(m.slots[0]);
    d::PracticeSession exhausted(m);
    CHECK(exhausted.restore()==d::Result::Ready && exhausted.revision()==UINT32_MAX);
    CHECK(command(exhausted,"practice start 1 4294967294 auto",care).result==d::Result::Duplicate);
    CHECK(command(exhausted,"practice start 2 4294967295 auto",care).result==d::Result::Limit && m.writes==writes);
}
void copyHex(std::uint8_t* out,const char* hex) {
    const auto nibble=[](char c){return c<='9' ? c-'0' : c-'a'+10;};
    for(std::size_t i=0;hex[i*2];i++) out[i]=static_cast<std::uint8_t>(nibble(hex[i*2])*16+nibble(hex[i*2+1]));
}
void legacyRecords() {
    for(bool automatic:{false,true}) {
        Memory m; auto& slot=m.slots[0]; m.present[0]=true; slot.length=d::kLegacyRecordBytes;
        std::memcpy(slot.bytes.data,"DVPR",4); setWord(slot,4,1);
        const std::uint32_t revision=automatic ? 1 : 3;
        const std::uint32_t words[]={revision,automatic ? 1u : 0u,1,revision,revision-1,
            automatic ? 1u : 2u,automatic ? 1u : 0u,automatic ? 0u : 2u,0};
        for(unsigned i=0;i<9;i++) setWord(slot,8+i*4,words[i]);
        copyHex(slot.bytes.data+44,kPracticeV2InitialHex);
        copyHex(slot.bytes.data+156,automatic ? kPracticeV2AutoHex : kPracticeV2TurnHex); reseal(slot);
        const auto old=slot;
        d::PracticeSession session(m); auto care=productionCare();
        CHECK(session.restore()==d::Result::Ready && m.writes==0);
        CHECK(session.battle()->rulesVersion==2 && session.battle()->playerFormId==0);
        CHECK(std::memcmp(old.bytes.data,slot.bytes.data,d::kLegacyRecordBytes)==0);
        if(automatic) {
            digivice::autobattle::Trace trace; CHECK(session.replay(trace) && trace.combatRulesVersion==3);
            CHECK(command(session,"practice start 1 0 auto",digivice::newDevice(),9,false).result==d::Result::Duplicate);
            CHECK(m.writes==0);
            CHECK(command(session,"practice start 2 1 auto",care).result==d::Result::Applied);
            CHECK(session.battle()->rulesVersion==7 && session.battle()->playerFormId==care.collection[0].formId);
        } else {
            CHECK(command(session,"practice act 3 2 heavy",care).result==d::Result::Duplicate && m.writes==0);
            CHECK(command(session,"practice act 4 3 brace",care).result==d::Result::Applied);
            CHECK(session.battle()->rulesVersion==2); // Finishes using frozen combat, even in new envelope.
        }
        CHECK(m.slots[0].length==288 && m.slots[1].length==304 && m.writes==1);
        d::PracticeSession restarted(m); CHECK(restarted.restore()==d::Result::Ready && m.writes==1);
        CHECK(sameBattle(*session.battle(),*restarted.battle()));
    }
    // A new practice freezes an evolved form and level; its save is independent.
    Memory m; d::PracticeSession s(m); auto care=productionCare();
    auto& member=care.collection[0]; member.formId=12; // Named evolved Impmon route: Wizardmon.
    member.level=care.level=20; member.xp=digivice::xpForLevel(20);
    member.bond=care.bond=digivice::forms::find(member.formId)->minBond;
    care.journal[(member.formId-1)/32] |= 1u << ((member.formId-1)%32);
    CHECK(digivice::isValid(care) && s.restore()==d::Result::Empty);
    CHECK(command(s,"practice start 1 0 tactical",care).result==d::Result::Applied);
    CHECK(s.battle()->playerFormId==member.formId && s.battle()->playerLevel==20);
    CHECK(s.battle()->enemyLevel==20); // Newly started RPG practice matches level.
    CHECK(!s.allowsCareAction(digivice::Action::Evolve));
}
void stableIdsAndCatalog() {
    Memory memory; d::PracticeSession session(memory);
    auto care = stableMemberFixture(67);
    CHECK(digivice::isValid(care) && session.restore() == d::Result::Empty);
    CHECK(digivice::apply(care, digivice::Action::Release, 1) == digivice::Error::None);
    CHECK(care.collection[0].id == 19 && care.activeCreatureId == 19);
    CHECK(command(session, "practice start 1 0 tactical", care).result == d::Result::Applied);
    CHECK(session.companionId() == 19 && session.battle()->playerFormId == 67 && session.battle()->rulesVersion == 7);
    CHECK(!session.allowsCareAction(digivice::Action::Release));
    const auto writes = memory.writes; d::PracticeSession restarted(memory);
    CHECK(restarted.restore() == d::Result::Ready && restarted.companionId() == 19 && memory.writes == writes);
    CHECK(command(restarted, "practice start 1 0 tactical", digivice::newDevice(), 0, false).result == d::Result::Duplicate);
    CHECK(command(restarted, "practice act 2 1 retreat", care).result == d::Result::Applied);
    CHECK(restarted.allowsCareAction(digivice::Action::Release));
    CHECK(command(restarted, "practice start 3 2 auto", care).result == d::Result::Applied);
    digivice::autobattle::Trace trace;
    CHECK(restarted.replay(trace) && trace.playerFormId == 67 && trace.playerSpecies == static_cast<unsigned>(digivice::activeMember(care)->species));
}
void preservedPractice3And5And6() {
  for(unsigned version:{3u,5u,6u}) {
    for (bool automatic : {false, true}) {
        Memory memory; auto& slot = memory.slots[0]; memory.present[0] = true; slot.length = d::kRecordBytes;
        std::memcpy(slot.bytes.data,"DVPR",4); setWord(slot,4,2);
        const auto revision = automatic ? 1u : 3u;
        const std::uint32_t words[]{revision,automatic?1u:0u,1,revision,revision-1,
            automatic?1u:2u,automatic?1u:0u,automatic?0u:2u,0};
        for (unsigned i=0;i<9;++i) setWord(slot,8+i*4,words[i]);
        copyHex(slot.bytes.data+44,version==3?kPracticeV3InitialHex:version==5?kPracticeV5InitialHex:kPracticeV6InitialHex);
        copyHex(slot.bytes.data+164,version==3?(automatic?kPracticeV3AutoHex:kPracticeV3TurnHex):version==5?(automatic?kPracticeV5AutoHex:kPracticeV5TurnHex):(automatic?kPracticeV6AutoHex:kPracticeV6TurnHex)); reseal(slot);
        const auto original=slot;
        d::PracticeSession session(memory); auto care=productionCare();
        CHECK(session.restore()==d::Result::Ready && session.battle()->rulesVersion==version && memory.writes==0);
        CHECK(!std::memcmp(original.bytes.data,slot.bytes.data,d::kRecordBytes));
        if (automatic) {
            digivice::autobattle::Trace trace;
            CHECK(session.replay(trace));
            CHECK(command(session,"practice start 1 0 auto",digivice::newDevice(),999,false).result==d::Result::Duplicate && memory.writes==0);
            CHECK(command(session,"practice start 2 1 auto",care).result==d::Result::Applied && session.battle()->rulesVersion==7);
        } else {
            CHECK(command(session,"practice act 3 2 heavy",care).result==d::Result::Duplicate && memory.writes==0);
            CHECK(command(session,"practice act 4 3 brace",care).result==d::Result::Applied && session.battle()->rulesVersion==version);
        }
        d::PracticeSession restart(memory);
        CHECK(restart.restore()==d::Result::Ready && sameBattle(*restart.battle(),*session.battle()));
    }
  }
}

int main() {
    basicAndRetry(); failureCases(); parsingAndStarts(); envelopesAndLimits(); legacyRecords(); stableIdsAndCatalog(); preservedPractice3And5And6();
    std::printf("PASS practice session: %u checks; Session=%zuB record=%zuB; synthetic storage faults, no hardware\n",checks,sizeof(d::PracticeSession),d::kRecordBytes);
}
