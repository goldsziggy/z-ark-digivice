#include "trade_session.hpp"
#include "forms.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
using namespace digivice;
namespace dt = digivice::devicetrade;
namespace {
unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(false)
struct Care final : storage::Backend {
    storage::Slot slots[2]{}; bool present[2]{}; unsigned writes=0,fail=0; bool landed=false,unreadable=false;
    storage::ReadStatus readSlot(unsigned i,storage::Slot& out) override {
        if(unreadable)return storage::ReadStatus::Unreadable;
        if(!present[i])return storage::ReadStatus::Missing;
        out=slots[i];return storage::ReadStatus::Present;
    }
    bool writeSlot(unsigned i,const Snapshot& bytes) override {
        ++writes;if(writes==fail&&!landed)return false;
        std::memcpy(slots[i].bytes,bytes.bytes,kSnapshotSize);slots[i].length=kSnapshotSize;present[i]=true;return writes!=fail;
    }
};
struct Journal final : dt::Backend {
    dt::Slot slots[2]{}; bool present[2]{}; unsigned writes=0,fail=0; bool landed=false,unreadable=false;
    dt::Read read(unsigned i,dt::Slot& out) override {
        if(unreadable)return dt::Read::Unreadable;
        if(!present[i])return dt::Read::Missing;
        out=slots[i];return dt::Read::Present;
    }
    bool write(unsigned i,const dt::Bytes& bytes) override {
        ++writes;if(writes==fail&&!landed)return false;
        slots[i].bytes=bytes;slots[i].length=dt::kJournalBytes;present[i]=true;return writes!=fail;
    }
};
State fixture(unsigned starter) {
    State s=newDevice(100+starter);CHECK(apply(s,Action::Hatch,starter)==Error::None);
    s.sequence=s.foregroundSequence=100;s.collectionCount=2;s.nextMemberId=3;s.captures=s.encounters=1;s.steps=100;
    const auto* f=forms::find(79);CHECK(f);
    s.collection[1]={2,static_cast<Species>(f->lineage),17,63,47,89,100,5,2,xpForLevel(5)+17,79};
    s.journal[(79-1)/32]|=1u<<((79-1)%32);CHECK(isValid(s));return s;
}
trade::Transcript transcript(const State& a,const State& b) {
    trade::Transcript t;t.peers[0].bytes[0]=2;t.peers[1].bytes[0]=4;t.session=8123;
    t.nonces[0]=75;t.nonces[1]=91;t.revision=1;
    t.sourceSequences[0]=a.sequence;t.sourceSequences[1]=b.sequence;
    t.receivedTrades[0]=a.receivedTrades;t.receivedTrades[1]=b.receivedTrades;
    t.offers[0]=a.collection[1];t.offers[1]=b.collection[1];CHECK(trade::valid(t));return t;
}
struct Device {
    State state=fixture(1);Care care;Journal journal;storage::SaveStore saves{care};dt::Session session{state,saves,journal};
    Device(){CHECK(saves.restore(state)==storage::BootStatus::Empty);state=fixture(1);CHECK(saves.checkpointMirrored(state));CHECK(session.restore());}
};
void walk(Device& d,unsigned steps) {
    if(!d.state.encounterRng)CHECK(apply(d.state,Action::EncounterSeed,777)==Error::None);
    CHECK(apply(d.state,Action::AccrueSteps,steps)==Error::None);CHECK(d.saves.checkpoint(d.state));
}
void restored(Care& care,Journal& journal,bool expectedApplied,bool expectHealthy=true) {
    State state;storage::SaveStore saves(care);CHECK(saves.restore(state)==storage::BootStatus::Loaded);
    dt::Session reboot(state,saves,journal);CHECK(reboot.restore()==expectHealthy);
    if(expectHealthy){CHECK(reboot.record());CHECK(state.receivedTrades==(expectedApplied?1u:0u));
        CHECK((findMember(state,2)==nullptr)==expectedApplied);CHECK(reboot.blocksForeground()==!expectedApplied);
        if(expectedApplied){CHECK(reboot.record()->phase==trade::Phase::Applied);for(unsigned i=0;i<2;++i){State s;CHECK(decodeSnapshot(care.slots[i].bytes,care.slots[i].length,s)==SnapshotStatus::Ok);CHECK(s.receivedTrades==1&&!findMember(s,2));}}
    } else CHECK(reboot.blocksForeground()&&reboot.freezesGameWrites());
}
}
int main(){
    const auto peer=fixture(2);
    // A consent accepted by installed firmware retains its rules13 identity
    // through a new-schema checkpoint, reboot and durable ownership decision.
    for(bool committed:{false,true}) {
        Device d;auto t=transcript(d.state,peer);t.rules=13;
        const auto identity=trade::fingerprint(t);
        CHECK(d.session.prepare(t,0,false));walk(d,44);
        if(committed)CHECK(d.session.commit());
        State state;storage::SaveStore saves(d.care);CHECK(saves.restore(state)==storage::BootStatus::Loaded);
        dt::Session reboot(state,saves,d.journal);CHECK(reboot.restore());
        CHECK(reboot.record()->transcript.rules==13&&trade::fingerprint(reboot.record()->transcript)==identity);
        if(!committed){CHECK(reboot.blocksForeground());CHECK(reboot.commit());CHECK(reboot.applyCommitted());}
        CHECK(!reboot.blocksForeground()&&state.receivedTrades==1&&!findMember(state,2));
        CHECK(state.explorationSteps==44&&trade::fingerprint(reboot.record()->transcript)==identity);
    }
    { Device d;const auto t=transcript(d.state,peer);CHECK(d.session.prepare(t,0,false));CHECK(d.session.blocksForeground()&&!d.session.freezesGameWrites());
      // Current schema mirrored BEFORE any prepared reply is permitted.
      for(const auto& slot:d.care.slots){CHECK(slot.length==kSnapshotSize);State decoded;CHECK(decodeSnapshot(slot.bytes,slot.length,decoded)==SnapshotStatus::Ok);}
      walk(d,64);const auto progress=d.state;CHECK(d.session.commit());CHECK(d.session.freezesGameWrites()&&!d.session.permitsBackground());
      CHECK(trade::sameState(d.state,progress));CHECK(d.session.applyCommitted());CHECK(!d.session.blocksForeground());
      CHECK(d.state.explorationSteps==progress.explorationSteps&&d.state.encounterRng==progress.encounterRng&&d.state.pendingEncounter.formId==progress.pendingEncounter.formId);
      const auto writes=d.journal.writes,careWrites=d.care.writes;for(unsigned i=0;i<100;++i){CHECK(d.session.commit());CHECK(d.session.applyCommitted());}CHECK(d.journal.writes==writes&&d.care.writes==careWrites);
      auto next=transcript(d.state,peer);next.session++;CHECK(!d.session.prepare(next,0,false));CHECK(d.session.prepare(next,0,true));
    }
    { Device d;const auto t=transcript(d.state,peer);CHECK(d.session.prepare(t,0,false));walk(d,77);const auto progress=d.state;
      CHECK(d.session.abort(t,0,false));CHECK(trade::sameState(d.state,progress));CHECK(!d.session.blocksForeground());
      State state;storage::SaveStore saves(d.care);CHECK(saves.restore(state)==storage::BootStatus::Loaded);dt::Session reboot(state,saves,d.journal);CHECK(reboot.restore());CHECK(trade::sameState(state,progress));
      auto next=transcript(state,peer);next.session++;CHECK(!reboot.prepare(next,0,false));CHECK(reboot.prepare(next,0,true));CHECK(reboot.record()->serial==2);
    }
    // Cuts before/after either care write while applying an already committed decision.
    for(unsigned slot=1;slot<=2;++slot)for(bool landed:{false,true}) {
        Device d;const auto t=transcript(d.state,peer);CHECK(d.session.prepare(t,0,false));walk(d,63);CHECK(d.session.commit());
        d.care.fail=d.care.writes+slot;d.care.landed=landed;CHECK(!d.session.applyCommitted());CHECK(d.session.blocksForeground()&&d.session.freezesGameWrites());
        d.care.fail=0;restored(d.care,d.journal,true);
    }
    // Cuts in each Commit/Applied journal mirror. A landed Commit always wins;
    // an unlanded first Commit leaves Prepared locked, never grants ownership.
    for(bool terminal:{false,true})for(unsigned slot=1;slot<=2;++slot)for(bool landed:{false,true}) {
        Device d;const auto t=transcript(d.state,peer);CHECK(d.session.prepare(t,0,false));walk(d,64);
        if(terminal)CHECK(d.session.commit());
        d.journal.fail=d.journal.writes+slot;d.journal.landed=landed;
        CHECK(!(terminal?d.session.applyCommitted():d.session.commit()));CHECK(d.session.blocksForeground());
        d.journal.fail=0;restored(d.care,d.journal,terminal||slot==2||landed);
    }
    // Either acknowledged journal key or care key may disappear independently.
    for(bool applied:{false,true})for(bool careKey:{false,true})for(unsigned missing=0;missing<2;++missing) {
        Device d;const auto t=transcript(d.state,peer);CHECK(d.session.prepare(t,0,false));
        if(applied){CHECK(d.session.commit());CHECK(d.session.applyCommitted());}
        if(careKey)d.care.present[missing]=false;else d.journal.present[missing]=false;
        restored(d.care,d.journal,applied);
    }
    { Device d;const auto t=transcript(d.state,peer);CHECK(d.session.prepare(t,0,false));CHECK(d.session.commit());CHECK(d.session.applyCommitted());
      d.journal.present[0]=d.journal.present[1]=false;restored(d.care,d.journal,true,false); }
    { Device d;const auto t=transcript(d.state,peer);CHECK(d.session.prepare(t,0,false));d.journal.slots[0].bytes.data[30]^=1;restored(d.care,d.journal,false,false); }
    { Device d;const auto t=transcript(d.state,peer);CHECK(d.session.prepare(t,0,false));d.care.present[0]=d.care.present[1]=false;
      State placeholder=newGame();storage::SaveStore saves(d.care);CHECK(saves.restore(placeholder)==storage::BootStatus::Empty);
      saves.requireRecovery("care missing with journal evidence");dt::Session reboot(placeholder,saves,d.journal);const auto writes=d.care.writes;CHECK(!reboot.restore());CHECK(d.care.writes==writes); }
    { Device d;d.state.sequence=d.state.foregroundSequence=UINT32_MAX-1;CHECK(isValid(d.state));CHECK(d.saves.checkpointMirrored(d.state));
      const auto t=transcript(d.state,peer);CHECK(d.session.prepare(t,0,false));CHECK(!d.session.permitsBackground());CHECK(d.session.commit());CHECK(d.session.applyCommitted());CHECK(d.state.sequence==UINT32_MAX); }
    std::printf("%u trade session recovery checks PASS; Session=%zu bytes snapshot=%zu journal=%zu\n",checks,sizeof(dt::Session),kSnapshotSize,dt::kJournalBytes);
}
