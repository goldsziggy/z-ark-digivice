#include "battle_presentation.hpp"
#include "capture_ring.hpp"
#include "combat.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <initializer_list>

using namespace digivice;
namespace bp=digivice::battlepresentation;
unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) {std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x); std::exit(1);} } while(false)

autobattle::Trace fixture() {
    autobattle::Trace t;
    t.playerFormId=1;t.playerSpecies=1;t.playerLevel=1;
    t.enemyFormId=4;t.enemySpecies=2;t.enemyLevel=1;
    t.startSequence=10;t.endSequence=11;t.count=2;t.outcome=autobattle::Outcome::Won;
    auto& a=t.steps[0];a.action=autobattle::Move::Physical;a.opponentAction=autobattle::Move::Magic;
    a.playerHpBefore=100;a.playerHpAfter=91;a.enemyHpBefore=88;a.enemyHpAfter=78;
    auto& b=t.steps[1];b.action=autobattle::Move::Magic;
    b.playerHpBefore=b.playerHpAfter=91;b.enemyHpBefore=78;b.enemyHpAfter=0;
    return t;
}
void drain(bp::Sequencer& player,std::uint64_t now) {
    unsigned actors=0;
    for(unsigned i=0;player.locked() && i<2000;++i) {
        now+=100;const auto& v=player.poll(now);
        CHECK(v.playerHp<=v.playerMaxHp && v.enemyHp<=v.enemyMaxHp);
        CHECK(v.progressPermille<=1000);
        const auto cue=player.consumeCue();
        CHECK(player.consumeCue()==bp::Cue::None);
        if(cue==bp::Cue::Attack || cue==bp::Cue::Magic)++actors;
    }
    CHECK(!player.locked());CHECK(actors<=96);
    player.poll(now+100000);CHECK(player.consumeCue()==bp::Cue::None);
}
State encounter(std::uint32_t seed,std::uint32_t starter=2) {
    State s=newDevice(seed);CHECK(apply(s,Action::Hatch,starter)==Error::None);
    CHECK(apply(s,Action::Explore,1000)==Error::None);CHECK(s.phase==digivice::Phase::Encounter);return s;
}
struct CaptureFixture { State before,after; Action action; std::uint32_t value; };
CaptureFixture captureFixture(CaptureResult wanted,unsigned attempt,bool walkingSeeded=true) {
    for(unsigned seed=1;seed<=1024;++seed) {
        auto before=walkingSeeded ? encounter(seed) : newDevice(seed);
        if(!walkingSeeded) {
            CHECK(apply(before,Action::Hatch,2)==Error::None); CHECK(apply(before,Action::Walk,100)==Error::None);
            before.wildRules=kRulesVersion; // Synthetic native foe before the walking RNG's first seed.
        }
        before.wildHp=before.wildMaxHp/2;
        before.captureAttempts=attempt-1;
        auto after=before;
        const auto action=wanted==CaptureResult::Miss?Action::Flick:Action::Capture;
        CHECK(isValid(before)); CHECK(apply(after,action,0)==Error::None);
        if(after.lastCapture.result==wanted) return {before,after,action,0};
    }
    CHECK(false); return {};
}
void captureChecks() {
    bp::Sequencer player;
    for(const auto result:{CaptureResult::Miss,CaptureResult::Escaped,CaptureResult::Captured}) for(unsigned attempt=1;attempt<=3;++attempt) {
        const auto f=captureFixture(result,attempt); const bool miss=result==CaptureResult::Miss, caught=result==CaptureResult::Captured;
        Snapshot beforeBytes,afterBytes;
        CHECK(encodeSnapshot(f.before,beforeBytes) && encodeSnapshot(f.after,afterBytes));
        CHECK(player.startTactical(f.before,f.after,f.action,f.value,0));
        CHECK(player.view().capturePresentation && player.view().captureAttempt==attempt && player.view().captureRemaining==3-attempt);
        CHECK(player.view().captureChance==0 && !player.view().captureMiss && !player.view().captureCaught);
        CHECK(player.consumeCue()==bp::Cue::CaptureThrow && player.consumeCue()==bp::Cue::None);
        CHECK(!player.startSavedCapture(f.after,1)); // Input remains locked.
        player.poll(499);CHECK(player.locked() && player.view().captureElapsedMs==499 && player.view().captureChance==0);
        player.poll(500);CHECK(player.locked() && player.view().captureElapsedMs==500);
        CHECK(player.view().captureChance==f.after.lastCapture.chance && player.view().captureMiss==miss && !player.view().captureCaught);
        CHECK(player.view().playerHp==f.before.hp && player.view().enemyHp==f.before.wildHp);
        CHECK(player.consumeCue()==(miss?bp::Cue::CaptureFail:bp::Cue::None));
        const auto end=miss?2100u:3900u;
        if(!miss) {
            player.poll(2299);CHECK(player.locked() && !player.view().captureCaught && player.consumeCue()==bp::Cue::None);
            player.poll(2300);CHECK(player.locked() && player.view().captureCaught==caught && player.view().captured==caught);
            CHECK(player.consumeCue()==(caught?bp::Cue::CaptureSuccess:bp::Cue::CaptureFail));
        }
        player.poll(end-1);CHECK(player.locked());player.poll(end);CHECK(!player.locked());
        CHECK(player.view().captureElapsedMs==end && player.view().progressPermille==1000 && player.consumeCue()==bp::Cue::None);
        CHECK(player.view().playerHp==f.before.hp && player.view().enemyHp==f.before.wildHp);
        if(attempt==3 && !caught) {
            CHECK(f.after.phase==digivice::Phase::Encounter && f.after.message==Message::CaptureMissed);
            CHECK(f.after.hp==f.before.hp && f.after.wildHp==f.before.wildHp && player.view().outcome==autobattle::Outcome::None);
        }
        // A reboot can replay the latest committed result, without inventing
        // terminal target HP, changing any byte or requesting a new game event.
        CHECK(player.startSavedCapture(f.after,0)); CHECK(player.view().captureAttempt==attempt);
        CHECK(player.view().enemyHp==(f.after.phase==digivice::Phase::Encounter?f.after.wildHp:0));
        player.consumeCue(); drain(player,0);
        CHECK(player.view().captureCaught==caught && player.view().captureMiss==miss);
        Snapshot beforeAgain,afterAgain;
        CHECK(encodeSnapshot(f.before,beforeAgain) && encodeSnapshot(f.after,afterAgain));
        CHECK(!std::memcmp(beforeBytes.bytes,beforeAgain.bytes,sizeof(beforeBytes.bytes)) && !std::memcmp(afterBytes.bytes,afterAgain.bytes,sizeof(afterBytes.bytes)));
    }
    const auto caught=captureFixture(CaptureResult::Captured,1);
    CHECK(player.startSavedCapture(caught.after,0));player.consumeCue();
    player.poll(100000);CHECK(player.locked() && player.view().captureElapsedMs==500 && !player.view().captureCaught);
    CHECK(player.consumeCue()==bp::Cue::None);
    player.poll(100001);CHECK(player.view().captureElapsedMs==501);
    player.poll(123);CHECK(player.view().captureElapsedMs==501 && player.consumeCue()==bp::Cue::None);
    player.poll(101799);CHECK(!player.view().captureCaught);
    player.poll(101800);CHECK(player.view().captureCaught && player.view().captureElapsedMs==2300);
    CHECK(player.consumeCue()==bp::Cue::CaptureSuccess && player.consumeCue()==bp::Cue::None);
    player.poll(103399);CHECK(player.locked());player.poll(103400);CHECK(!player.locked());
    CHECK(player.startSavedCapture(caught.after,0));player.consumeCue();player.poll(500);player.poll(800);
    player.pause(true,800);CHECK(player.view().paused && player.locked());
    player.poll(99999);CHECK(player.view().captureElapsedMs==800 && player.consumeCue()==bp::Cue::None);
    player.pause(false,100000);player.poll(100000);CHECK(player.view().captureElapsedMs==800);
    player.poll(101499);CHECK(!player.view().captureCaught);player.poll(101500);CHECK(player.view().captureCaught);
    CHECK(player.consumeCue()==bp::Cue::CaptureSuccess);player.poll(103099);CHECK(player.locked());player.poll(103100);CHECK(!player.locked());
    const auto miss=captureFixture(CaptureResult::Miss,3);
    CHECK(player.startSavedCapture(miss.after,0));player.consumeCue();
    player.poll(100000);CHECK(player.view().captureMiss && player.view().captureElapsedMs==500);
    CHECK(player.consumeCue()==bp::Cue::CaptureFail);player.poll(101599);CHECK(player.locked());player.poll(101600);CHECK(!player.locked());
    CHECK(player.startSavedCapture(caught.after,0));player.cancel();CHECK(!player.locked() && !player.view().capturePresentation);
    player.poll(100000);CHECK(player.consumeCue()==bp::Cue::None);
    // Background walking can checkpoint mid-reveal without making a saved
    // capture stale. A meaningful later action still invalidates its replay.
    for(const auto result:{CaptureResult::Miss,CaptureResult::Escaped,CaptureResult::Captured}) for(bool seeded:{false,true}) {
        auto background=captureFixture(result,1,seeded).after;
        const auto foreground=background.foregroundSequence;
        if(!seeded) CHECK(apply(background,Action::EncounterSeed,1234)==Error::None);
        CHECK(apply(background,Action::AccrueSteps,64)==Error::None);
        CHECK(background.sequence>foreground && background.foregroundSequence==foreground);
        Snapshot bytes;State reboot;
        CHECK(encodeSnapshot(background,bytes) && decodeSnapshot(bytes.bytes,kSnapshotSize,reboot)==SnapshotStatus::Ok);
        CHECK(player.startSavedCapture(reboot,0)); CHECK(player.view().captureAttempt==1); player.cancel();
        CHECK(apply(reboot,Action::Walk,1)==Error::None);
        CHECK(!player.startSavedCapture(reboot,0));
    }
    auto stale=caught.after;CHECK(apply(stale,Action::Feed)==Error::None);CHECK(!player.startSavedCapture(stale,0));
    auto invalid=caught.after;invalid.lastCapture.sequence=0;CHECK(!player.startSavedCapture(invalid,0));
    invalid=caught.after;invalid.lastCapture.sequence++;CHECK(!player.startSavedCapture(invalid,0));
    invalid=caught.after;invalid.lastCapture.attempt=4;CHECK(!player.startSavedCapture(invalid,0));
    invalid=caught.after;invalid.lastCapture.targetFormId=11;CHECK(!player.startSavedCapture(invalid,0));
    invalid=caught.after;invalid.lastCapture.targetLevel=2;CHECK(!player.startSavedCapture(invalid,0));
    invalid=miss.after;invalid.lastCapture.chance=50;CHECK(!player.startSavedCapture(invalid,0));
    CHECK(!player.startSavedCapture(newDevice(),0));
    // Historical synthetic capture metadata stays intact, but never boots into
    // a test-creature reveal. Pending-only cleanup preserves foreground identity.
    auto historicalTest=miss.after;
    historicalTest.lastCapture.targetFormId=4;
    CHECK(isValid(historicalTest));
    CHECK(!player.startSavedCapture(historicalTest,0));
    CHECK(apply(historicalTest,Action::AccrueSteps,1000)==Error::None);
    historicalTest.pendingEncounter={4,1,12};
    CHECK(isValid(historicalTest));
    const auto historicalRecord=historicalTest.lastCapture;
    const auto historicalForeground=historicalTest.foregroundSequence;
    CHECK(apply(historicalTest,Action::ResolveTestEncounter,0)==Error::None);
    CHECK(historicalTest.foregroundSequence==historicalForeground &&
          !std::memcmp(&historicalRecord,&historicalTest.lastCapture,sizeof(historicalRecord)));
    CHECK(!player.startSavedCapture(historicalTest,0) && !player.locked());
    // Actual Auto capture frames preserve per-throw metadata, HP and the saved
    // third-failure endpoint; they cannot invent a retaliating opponent actor.
    bool sawThird=false,sawCaught=false;
    for(unsigned seed=1;seed<=1024 && (!sawThird || !sawCaught);++seed) {
        auto state=encounter(seed);state.battleMode=BattleMode::Auto;autobattle::Trace trace;
        CHECK(applyAuto(state,&trace)==Error::None && trace.combatRulesVersion==12);
        for(std::size_t i=0;i<trace.count;++i) if(trace.steps[i].action==autobattle::Move::Capture) {
            const auto& step=trace.steps[i];CHECK(step.opponentAction==autobattle::Move::None && step.playerHpBefore==step.playerHpAfter);
            CHECK(step.captureAttempt>=1 && step.captureAttempt<=3 && step.captureChance>=10 && step.captureChance<=90);
        }
        sawCaught=sawCaught || trace.outcome==autobattle::Outcome::Captured;
        for(std::size_t i=0;i<trace.count;++i) if(trace.steps[i].action==autobattle::Move::Capture && trace.steps[i].captureAttempt==3 && !trace.steps[i].captured) {
            sawThird=true;CHECK(trace.steps[i].playerHpAfter==trace.steps[i].playerHpBefore && state.message!=Message::CaptureEnded);
        }
        CHECK(player.startAuto(trace,0));player.consumeCue();drain(player,0);
        auto bad=trace;bad.playerOffenseBonus=6;CHECK(!player.startAuto(bad,0));
        for(std::size_t i=0;i<trace.count;++i) if(trace.steps[i].action==autobattle::Move::Capture) {
            bad=trace;bad.steps[i].captureChance=9;CHECK(!player.startAuto(bad,0));
            bad=trace;bad.steps[i].opponentAction=autobattle::Move::Physical;CHECK(!player.startAuto(bad,0));
            bad=trace;bad.steps[i].captureResult=0;CHECK(!player.startAuto(bad,0));
            bad=trace;bad.steps[i].captureAttempt=4;CHECK(!player.startAuto(bad,0));break;
        }
    }
    CHECK(sawThird && sawCaught);
    // Frozen carried-over policies keep zero capture metadata and their old
    // actor/impact timeline. The new care/capture presentation cannot leak in.
    for(unsigned rules=7;rules<=11;++rules) {
        auto state=encounter(91);state.wildRules=rules;state.battleMode=BattleMode::Auto;
        autobattle::Trace trace;CHECK(isValid(state) && applyAuto(state,&trace)==Error::None);
        CHECK(trace.combatRulesVersion!=12 && !trace.playerOffenseBonus && !trace.playerProtectionBonus);
        CHECK(state.lastCapture.result==CaptureResult::None);
        for(std::size_t i=0;i<trace.count;++i) CHECK(!trace.steps[i].captureAttempt && !trace.steps[i].captureChance && !trace.steps[i].captureResult);
        CHECK(player.startAuto(trace,0));player.consumeCue();
        for(unsigned now=100;player.locked() && now<200000;now+=100) {
            player.poll(now);CHECK(!player.view().capturePresentation);player.consumeCue();
        }
        CHECK(!player.locked());
    }
}
void autoFlickChecks() {
    bp::Sequencer player;
    unsigned pauses=0;
    for(unsigned rules=7;rules<=13;++rules) for(unsigned seed=1;seed<=16;++seed) {
        auto before=encounter(seed);before.battleMode=BattleMode::Auto;before.wildRules=rules;
        CHECK(isValid(before));auto after=before;autobattle::Trace chunk;
        CHECK(applyAutoFight(after,&chunk)==Error::None);
        for(std::size_t i=0;i<chunk.count;++i)CHECK(chunk.steps[i].action!=autobattle::Move::Capture);
        CHECK(player.startAuto(chunk,0));player.consumeCue();drain(player,0);
        if(after.autoCapture!=AutoCapture::Awaiting)continue;
        ++pauses;CHECK(chunk.outcome==autobattle::Outcome::None && after.phase==digivice::Phase::Encounter);
        CHECK(player.view().enemyHp==after.wildHp && player.view().playerHp==after.hp);
        CHECK(after.captureAttempts==0 && after.lastCapture.result==CaptureResult::None);
        // The committed crossing turn has ended before an actual throw exists.
        auto resumed=after;autobattle::Trace rest;CHECK(applyAutoResume(resumed,&rest)==Error::None);
        CHECK(resumed.autoCapture==AutoCapture::None && resumed.phase==digivice::Phase::Home);
        for(std::size_t i=0;i<rest.count;++i)CHECK(rest.steps[i].action!=autobattle::Move::Capture);
        CHECK(player.startAuto(rest,0));player.consumeCue();drain(player,0);
        // Three user misses each have one calm saved reveal, including old
        // frozen enemy profiles; there is no automatic throw or retaliation.
        for(unsigned attempt=1;attempt<=3;++attempt) {
            const auto throwing=after;CHECK(apply(after,Action::Flick,0)==Error::None);
            CHECK(after.lastCapture.result==CaptureResult::Miss && after.lastCapture.attempt==attempt);
            CHECK(after.hp==throwing.hp);
            CHECK(player.startTactical(throwing,after,Action::Flick,0,0));
            CHECK(player.view().capturePresentation && player.consumeCue()==bp::Cue::CaptureThrow);
            drain(player,0);CHECK(player.view().captureMiss);
            CHECK(player.startSavedCapture(after,0));CHECK(player.consumeCue()==bp::Cue::CaptureThrow);drain(player,0);
            CHECK(attempt==3 ? after.phase==digivice::Phase::Home : after.autoCapture==AutoCapture::Awaiting);
        }
    }
    CHECK(pauses>10);
}
std::uint32_t phaseForGrade(const State& state,capturering::Grade grade) {
    for(std::uint32_t phase=0;phase<capturering::kCycleMs;++phase)
        if(capturering::sample(phase,state.wildFormId).grade==grade)return phase;
    CHECK(false);return 0;
}
CaptureFixture ringFixture(CaptureResult wanted,unsigned attempt,capturering::Grade grade,
                           bool automatic,unsigned rules,bool lowChance=false) {
    for(unsigned seed=1;seed<=4096;++seed) {
        auto before=encounter(seed);before.wildRules=rules;
        if(lowChance) {
            before.wildLevel=10;
            before.wildMaxHp=combat::formProfile(before.wildFormId,before.wildLevel).stats.maxHp;
        }
        before.wildHp=before.wildMaxHp/2;before.captureAttempts=attempt-1;
        if(automatic) { before.battleMode=BattleMode::Auto;before.autoCapture=AutoCapture::Awaiting;before.wildTurn=1; }
        const auto phase=phaseForGrade(before,grade);
        CHECK(isValid(before));
        if(lowChance && ringCaptureChance(before,phase)!=1)continue;
        auto after=before;CHECK(apply(after,Action::RingCapture,phase)==Error::None);
        if(after.lastCapture.result==wanted)return {before,after,Action::RingCapture,phase};
    }
    CHECK(false);return {};
}
void ringCaptureChecks() {
    bp::Sequencer player;
    for(const auto grade:{capturering::Grade::Red,capturering::Grade::Orange,capturering::Grade::Green})
    for(const auto result:{CaptureResult::Escaped,CaptureResult::Captured})
    for(unsigned attempt=1;attempt<=3;++attempt)for(bool automatic:{false,true}) {
        const auto f=ringFixture(result,attempt,grade,automatic,kRulesVersion);
        const auto chance=ringCaptureChance(f.before,f.value);
        CHECK(chance>=1 && chance<=captureChance(f.before));
        CHECK(grade!=capturering::Grade::Green || chance==captureChance(f.before));
        CHECK(f.after.rngState!=f.before.rngState && f.after.lastCapture.chance==chance);
        Snapshot committed;CHECK(encodeSnapshot(f.after,committed));
        CHECK(player.startTactical(f.before,f.after,f.action,f.value,0));
        CHECK(player.view().capturePresentation && !player.view().aimMiss && !player.view().captureMiss);
        CHECK(player.view().captureAttempt==attempt && player.view().captureRemaining==3-attempt);
        CHECK(player.consumeCue()==bp::Cue::CaptureThrow);
        CHECK(!player.startTactical(f.before,f.after,f.action,f.value,1));
        player.poll(500);CHECK(player.view().captureChance==chance && !player.view().captureMiss);
        CHECK(player.consumeCue()==bp::Cue::None); // Red connects and has real wiggles.
        player.poll(2299);CHECK(player.locked() && !player.view().captureCaught && player.consumeCue()==bp::Cue::None);
        player.poll(2300);CHECK(player.view().captureCaught==(result==CaptureResult::Captured));
        CHECK(player.consumeCue()==(result==CaptureResult::Captured?bp::Cue::CaptureSuccess:bp::Cue::CaptureFail));
        player.poll(3900);CHECK(!player.locked() && !player.view().captureMiss);
        State reboot;CHECK(decodeSnapshot(committed.bytes,kSnapshotSize,reboot)==SnapshotStatus::Ok);
        CHECK(player.startSavedCapture(reboot,0));player.consumeCue();player.poll(500);
        CHECK(player.view().captureChance==chance && !player.view().aimMiss && !player.view().captureMiss);
        drain(player,500);CHECK(player.view().captureCaught==(result==CaptureResult::Captured));
        Snapshot replayed;CHECK(encodeSnapshot(reboot,replayed));
        CHECK(!std::memcmp(committed.bytes,replayed.bytes,sizeof(committed.bytes)));
        CHECK(!player.startTactical(f.before,f.after,f.action,capturering::kCycleMs,0));
        auto bad=f.after;bad.lastCapture.chance=chance==1?2:chance-1;
        CHECK(isValid(bad));CHECK(!player.startTactical(f.before,bad,f.action,f.value,0));
    }
    // New timing input also gives old saved foes a calm, replayable result.
    // The existing legacy Flick/Auto tests above retain their frozen behavior.
    for(unsigned rules=7;rules<=11;++rules)for(bool automatic:{false,true})
    for(const auto result:{CaptureResult::Escaped,CaptureResult::Captured})for(unsigned attempt:{1u,3u}) {
        const auto f=ringFixture(result,attempt,capturering::Grade::Red,automatic,rules);
        if(result==CaptureResult::Escaped)CHECK(f.after.hp==f.before.hp);
        if(f.after.phase==digivice::Phase::Encounter)CHECK(f.after.wildHp==f.before.wildHp);
        CHECK(player.startTactical(f.before,f.after,f.action,f.value,0));
        player.consumeCue();drain(player,0);
        CHECK(player.view().captureChance==f.after.lastCapture.chance && !player.view().captureMiss);
        Snapshot committed;State reboot;
        CHECK(encodeSnapshot(f.after,committed) && decodeSnapshot(committed.bytes,kSnapshotSize,reboot)==SnapshotStatus::Ok);
        CHECK(player.startSavedCapture(reboot,0));player.consumeCue();drain(player,0);
        CHECK(player.view().captureChance==f.after.lastCapture.chance && !player.view().captureMiss);
    }
    // A 1% red record must survive pause, reboot and background walking without
    // the historical 10% trace floor dropping its reveal or recomputing odds.
    auto low=ringFixture(CaptureResult::Escaped,1,capturering::Grade::Red,false,kRulesVersion,true);
    CHECK(low.after.lastCapture.chance==1);
    CHECK(player.startTactical(low.before,low.after,low.action,low.value,0));
    player.consumeCue();player.poll(500);CHECK(player.view().captureChance==1);
    player.pause(true,500);player.poll(10000);CHECK(player.view().captureChance==1 && player.locked());
    player.pause(false,10000);player.cancel();
    CHECK(apply(low.after,Action::AccrueSteps,7)==Error::None);
    Snapshot committed;State reboot;
    CHECK(encodeSnapshot(low.after,committed) && decodeSnapshot(committed.bytes,kSnapshotSize,reboot)==SnapshotStatus::Ok);
    CHECK(player.startSavedCapture(reboot,0));player.consumeCue();player.poll(500);
    CHECK(player.view().captureChance==1);drain(player,500);
    auto invalid=reboot;invalid.lastCapture.chance=0;CHECK(!player.startSavedCapture(invalid,0));
    invalid=reboot;invalid.lastCapture.chance=91;CHECK(!player.startSavedCapture(invalid,0));
}
void mergedCapturePresentation() {
    bool found=false;
    for(unsigned seed=1; seed<=512 && !found; ++seed) {
        State owned=newDevice(seed);
        if(apply(owned,Action::Hatch,1)!=Error::None) continue;
        const auto partnerForm=activeMember(owned)->formId;
        const auto partnerSpecies=activeMember(owned)->species;
        if(apply(owned,Action::Explore,1000)!=Error::None || owned.phase!=Phase::Encounter || owned.wildFormId==partnerForm) continue;
        owned.wildHp=owned.wildMaxHp/2;
        State first=owned;
        if(apply(first,Action::Capture)!=Error::None || first.lastCapture.result!=CaptureResult::Captured || first.collectionCount!=2) continue;
        const auto newest=first.collection[first.collectionCount-1].formId;
        if(newest==partnerForm || apply(first,Action::Explore,1000)!=Error::None || first.phase!=Phase::Encounter) continue;
        first.wildFormId=partnerForm; first.wildSpecies=partnerSpecies; first.wildLevel=1; first.wildTurn=0; first.captureAttempts=0;
        first.wildRules=kRulesVersion; first.wildMaxHp=combat::formProfile(partnerForm,1).stats.maxHp; first.wildHp=first.wildMaxHp/2;
        if(!isValid(first)) continue;
        State before=first;
        for(unsigned attempt=0; attempt<3 && !found; ++attempt) {
            State after=before;
            if(apply(after,Action::Capture)!=Error::None) break;
            if(after.lastCapture.result!=CaptureResult::Captured) { before=after; continue; }
            CHECK(after.phase==Phase::Home && after.message==Message::Captured && after.collectionCount==2);
            CHECK(after.collection[after.collectionCount-1].formId==newest && after.lastCapture.targetFormId==partnerForm);
            bp::Sequencer player;
            CHECK(player.startTactical(before,after,Action::Capture,0,0));
            CHECK(player.view().enemyFormId==partnerForm && player.view().outcome==autobattle::Outcome::Captured);
            player.consumeCue(); player.poll(500); player.poll(2300);
            CHECK(player.view().captureCaught && player.view().enemyFormId==partnerForm);
            found=true;
        }
    }
    CHECK(found);
}
int main() {
    CHECK(sizeof(bp::Sequencer)<=2048);
    auto t=fixture();const auto original=t;
    bp::Sequencer player;
    CHECK(player.startAuto(t,100));CHECK(player.locked());
    CHECK(player.view().actor==bp::Actor::Player && player.view().enemyHp==88);
    CHECK(!std::strcmp(player.view().moveName,combat::formProfile(1,1).physicalSkill));
    CHECK(player.consumeCue()==bp::Cue::Attack);CHECK(player.consumeCue()==bp::Cue::None);
    CHECK(!player.startAuto(t,101));
    player.poll(449);CHECK(player.view().enemyHp==88 && !player.view().flash);
    player.poll(450);CHECK(player.view().enemyHp==78 && player.view().playerHp==100 && player.view().flash);
    CHECK(player.consumeCue()==bp::Cue::Hit);
    player.poll(1300);CHECK(player.view().actor==bp::Actor::Opponent);
    CHECK(!std::strcmp(player.view().moveName,combat::formProfile(4,1).magicSkill));
    CHECK(player.consumeCue()==bp::Cue::Magic);
    player.poll(1650);CHECK(player.view().playerHp==91);CHECK(player.consumeCue()==bp::Cue::Hit);
    player.poll(2500);CHECK(player.view().actor==bp::Actor::Player && player.view().turn==2);
    CHECK(player.consumeCue()==bp::Cue::Magic);
    player.poll(2850);CHECK(player.view().enemyHp==0);CHECK(player.consumeCue()==bp::Cue::Hit);
    player.poll(3700);CHECK(player.view().phase==bp::Phase::Summary && player.locked());
    CHECK(player.consumeCue()==bp::Cue::Win);
    player.poll(5299);CHECK(player.locked());player.poll(5300);CHECK(!player.locked());
    CHECK(player.consumeCue()==bp::Cue::None);CHECK(!std::memcmp(&t,&original,sizeof(t)));

    // A stalled display shows the current impact, then holds it for 850ms. It
    // cannot emit an entire fight's effects at the next clock sample.
    CHECK(player.startAuto(t,0));player.consumeCue();
    player.poll(100000);CHECK(player.view().actor==bp::Actor::Player);
    CHECK(player.consumeCue()==bp::Cue::Hit);CHECK(player.consumeCue()==bp::Cue::None);
    player.poll(100001);CHECK(player.view().actor==bp::Actor::Player);
    player.poll(100849);CHECK(player.view().actor==bp::Actor::Player);
    player.poll(100850);CHECK(player.view().actor==bp::Actor::Opponent);
    CHECK(player.consumeCue()==bp::Cue::Magic);
    const auto frozen=player.view();player.poll(12);CHECK(player.view().playerHp==frozen.playerHp && player.view().phase==frozen.phase);
    player.pause(true,100900);CHECK(player.view().paused && player.locked());
    player.poll(999999);CHECK(player.view().playerHp==100 && player.consumeCue()==bp::Cue::None);
    player.pause(false,1000000);CHECK(!player.view().paused);
    player.poll(1000299);CHECK(player.view().playerHp==100);
    player.poll(1000300);CHECK(player.view().playerHp==91);CHECK(player.consumeCue()==bp::Cue::Hit);
    player.cancel();CHECK(!player.locked() && player.view().phase==bp::Phase::Idle);
    player.poll(UINT64_MAX);CHECK(player.consumeCue()==bp::Cue::None);

    // Invalid/dead-target traces do not replace a valid completed view.
    auto bad=t;bad.count=49;CHECK(!player.startAuto(bad,0));
    bad=t;bad.steps[1].enemyHpBefore=0;CHECK(!player.startAuto(bad,0));
    bad=t;bad.steps[1].opponentAction=autobattle::Move::Magic;CHECK(!player.startAuto(bad,0));
    bad=t;bad.steps[0].captured=true;CHECK(!player.startAuto(bad,0));
    bad=t;bad.playerFormId=0;CHECK(!player.startAuto(bad,0));
    bad=t;bad.endSequence=bad.startSequence;CHECK(!player.startAuto(bad,0));

    // Counter is a distinct opponent effect, never player damage plus a second
    // invented wild attack. A final capture similarly has no retaliation.
    auto reflected=fixture();reflected.count=1;reflected.outcome=autobattle::Outcome::Retreated;
    auto& r=reflected.steps[0];r.action=autobattle::Move::Heavy;r.guard=autobattle::Move::Counter;
    r.opponentAction=autobattle::Move::Counter;r.reflected=true;r.enemyHpAfter=r.enemyHpBefore;r.playerHpAfter=0;
    CHECK(player.startAuto(reflected,0));CHECK(player.view().damage==0);
    player.consumeCue();player.poll(350);CHECK(player.consumeCue()==bp::Cue::None && player.view().enemyHp==88);
    player.poll(1200);CHECK(player.view().move==autobattle::Move::Counter && player.view().damage==100);
    player.consumeCue();player.poll(1550);CHECK(player.view().playerHp==0 && player.consumeCue()==bp::Cue::Hit);
    player.poll(2400);CHECK(player.view().phase==bp::Phase::Summary && player.consumeCue()==bp::Cue::Retreat);
    player.cancel();
    auto captured=fixture();captured.count=1;captured.outcome=autobattle::Outcome::Captured;
    auto& c=captured.steps[0];c.action=autobattle::Move::Capture;c.opponentAction=autobattle::Move::None;
    c.playerHpAfter=c.playerHpBefore;c.enemyHpAfter=c.enemyHpBefore;c.captured=true;
    CHECK(player.startAuto(captured,0));CHECK(player.consumeCue()==bp::Cue::CaptureThrow);
    player.poll(350);CHECK(player.view().captured && player.consumeCue()==bp::Cue::CaptureSuccess);
    player.poll(1200);CHECK(player.view().phase==bp::Phase::Summary && player.consumeCue()==bp::Cue::None);
    player.cancel();

    // Build real committed native outcomes. Presentation never changes either
    // input snapshot, and every valid native trace can finish without more events.
    for(unsigned starter=1;starter<=8;++starter) for(unsigned seed=1;seed<=8;++seed) {
        State before=encounter(seed,starter);before.battleMode=BattleMode::Auto;
        State after=before;autobattle::Trace actual;
        CHECK(applyAuto(after,&actual)==Error::None);
        Snapshot a,b;CHECK(encodeSnapshot(before,a) && encodeSnapshot(after,b));
        CHECK(player.startAuto(actual,0));player.consumeCue();drain(player,0);
        Snapshot aa,bb;CHECK(encodeSnapshot(before,aa) && encodeSnapshot(after,bb));
        CHECK(!std::memcmp(a.bytes,aa.bytes,sizeof(a.bytes)) && !std::memcmp(b.bytes,bb.bytes,sizeof(b.bytes)));
    }
    for(auto action:{Action::Attack,Action::Magic,Action::Heavy}) {
        auto before=encounter(42);auto after=before;
        CHECK(apply(after,action)==Error::None);
        CHECK(player.startTactical(before,after,action,0,0));
        CHECK(player.view().damage==before.wildHp-after.wildHp);
        player.consumeCue();drain(player,0);
    }
    // Real lethal response: display combat HP zero, not the saved 10% recovery;
    // reconstruct the remaining wild HP with the existing combat resolver.
    auto before=encounter(2);before.hp=before.collection[0].hp=1;
    auto after=before;CHECK(isValid(before));CHECK(apply(after,Action::Attack)==Error::None);
    CHECK(after.phase==digivice::Phase::Home && after.hp>0);
    CHECK(player.startTactical(before,after,Action::Attack,0,0));
    player.consumeCue();player.poll(350);CHECK(player.view().enemyHp>0 && player.view().enemyHp<before.wildHp);
    player.consumeCue();player.poll(1200);player.consumeCue();player.poll(1550);CHECK(player.view().playerHp==0);
    player.cancel();
    // Frozen rules11 missed flick still consumes the historical response.
    before=encounter(9);before.wildRules=11;before.wildHp=before.wildMaxHp/2;after=before;
    CHECK(apply(after,Action::Flick,0)==Error::None);
    CHECK(player.startTactical(before,after,Action::Flick,0,0));CHECK(player.view().aimMiss);
    player.consumeCue();player.poll(350);CHECK(player.consumeCue()==bp::Cue::CaptureFail);
    player.poll(1200);CHECK(player.view().actor==bp::Actor::Opponent);
    player.cancel();
    // A finishing hit followed by XP level-up displays the actual combat HP,
    // not the larger care HP which was already durably awarded afterward.
    before=encounter(17);before.wildHp=1;before.collection[0].xp=39;after=before;
    CHECK(isValid(before));CHECK(apply(after,Action::Attack)==Error::None);
    CHECK(after.phase==digivice::Phase::Home && after.message==Message::Trained && after.level==2);
    CHECK(player.startTactical(before,after,Action::Attack,0,0));
    player.consumeCue();player.poll(350);CHECK(player.view().playerHp==before.hp && player.view().enemyHp==0);
    player.consumeCue();player.poll(1200);CHECK(player.view().phase==bp::Phase::Summary);
    CHECK(player.consumeCue()==bp::Cue::Win);player.cancel();
    // The real core's Heavy/Counter turn becomes precisely one reflected hit.
    before=encounter(12);before.wildTurn=2;after=before;
    CHECK(wildGuard(before)==combat::Defense::Counter);CHECK(apply(after,Action::Heavy)==Error::None);
    CHECK(player.startTactical(before,after,Action::Heavy,0,0));
    player.consumeCue();player.poll(350);CHECK(player.view().enemyHp==before.wildHp && !player.view().damage);
    player.poll(1200);CHECK(player.view().move==autobattle::Move::Counter && player.view().reflected);
    CHECK(player.view().damage==before.hp-after.hp);player.cancel();
    CHECK(!player.startTactical(before,before,Action::Flick,0,0));
    CHECK(!player.startTactical(before,after,Action::Feed,0,0));
    // Terminal attack reconstruction uses the saved rules12 care bonus, while
    // displaying combat HP rather than the already committed retreat healing.
    before=encounter(2);before.hp=before.collection[0].hp=1;
    before.fullness=before.collection[0].fullness=100;before.mood=before.collection[0].mood=100;before.bond=before.collection[0].bond=100;
    const auto* member=activeMember(before);
    const auto expected=combat::resolveCareForms(member->formId,member->level,before.wildFormId,before.wildLevel,combat::Move::Physical,wildGuard(before),memberCare(*member),{}).damage;
    const auto oldDamage=combat::resolveForms(member->formId,member->level,before.wildFormId,before.wildLevel,combat::Move::Physical,wildGuard(before)).damage;
    CHECK(expected>oldDamage);after=before;CHECK(apply(after,Action::Attack)==Error::None && after.message==Message::Retreated);
    CHECK(player.startTactical(before,after,Action::Attack,0,0));CHECK(player.view().damage==expected);
    player.consumeCue();player.poll(350);CHECK(player.view().enemyHp==before.wildHp-expected);player.cancel();
    captureChecks();
    mergedCapturePresentation();
    autoFlickChecks();
    ringCaptureChecks();
    std::printf("Battle presentation: %u checks passed; sequencer %zu bytes\n",checks,sizeof(bp::Sequencer));
}
