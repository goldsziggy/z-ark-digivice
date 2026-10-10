#include "snapshot_test_helpers.hpp"
// Independent durability regression: actual pure exchange, trade journal and
// two-slot care store. Synthetic fixtures only; no device, radio or private save.
// Protocol-to-runtime consent/recovery integration is tested separately.
#include "trade.hpp"
#include "forms.hpp"
#include "trade_store.hpp"
#include "trade_session.hpp"
#include "save_store.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

using namespace digivice;
namespace t=digivice::trade;
namespace dt=digivice::devicetrade;
namespace {
unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);std::exit(1); } } while(false)
void put32(std::uint8_t* p,std::uint32_t v){for(unsigned i=0;i<4;++i)p[i]=static_cast<std::uint8_t>(v>>(8*i));}
std::uint32_t crc(const std::uint8_t* p,std::size_t n){std::uint32_t v=~0u;for(std::size_t i=0;i<n;++i){v^=p[i];for(unsigned j=0;j<8;++j)v=(v>>1)^((v&1)?0xedb88320u:0);}return ~v;}
void projectSchema22(const std::uint8_t* current,std::uint8_t* old){
 constexpr unsigned size=2964;
 std::uint8_t schema26[kSchema26SnapshotSize];snapshot_test::schema26Image(current,schema26);
 snapshot_test::rules15Image(schema26,old);
 old[4]=22;old[5]=0;old[6]=(size-12)&255;old[7]=(size-12)>>8;put32(old+8,15);put32(old+size-4,crc(old,size-4));
}
void oldSnapshot(const std::uint8_t* current,std::uint8_t* old,unsigned version=19){
 CHECK(version==19||version==20||version==21||version==22);
 if(version==22){projectSchema22(current,old);return;}
 if(version==21){
  std::uint8_t schema26[kSchema26SnapshotSize];snapshot_test::schema26Image(current,schema26);
  std::uint8_t image[2964];snapshot_test::rules15Image(schema26,image);
  constexpr unsigned size=2952;std::memcpy(old,image,size-4);put32(old+8,14);
  old[4]=21;old[5]=0;old[6]=(size-12)&255;old[7]=(size-12)>>8;put32(old+size-4,crc(old,size-4));return;
 }
 const unsigned size=version==19?660:664;
 const auto projected=snapshot_test::eightSlotBytes(current);std::memcpy(old,projected.data(),size-4);put32(old+8,13);
 old[4]=version;old[5]=0;old[6]=(size-12)&255;old[7]=(size-12)>>8;put32(old+size-4,crc(old,size-4));
}
dt::Slot journalSlot(const t::Record& record,unsigned revision,unsigned version){
 std::array<std::uint8_t,t::kRecordBytes> bytes{};CHECK(t::encodeRecord(record,bytes.data(),bytes.size()));
 const bool legacy=version!=23;const unsigned snapshotSize=version==19?660:version==20?664:version==21?2952:2964;
 dt::Slot slot;slot.length=legacy?20+168+2*snapshotSize+4:dt::kJournalBytes;auto* out=slot.bytes.data;
 std::memcpy(out,"DVTJ",4);put32(out+4,1);put32(out+8,revision);
 if(legacy){
  CHECK(record.transcript.rules==(version==22?15:version==21?14:13));
  if(version==19)CHECK(record.before.worldSeed==0&&record.after.worldSeed==0);
  std::memcpy(out+16,bytes.data(),168);
  oldSnapshot(bytes.data()+168,out+16+168,version);oldSnapshot(bytes.data()+168+kSnapshotSize,out+16+168+snapshotSize,version);
  const unsigned length=168+2*snapshotSize;put32(out+16+length,crc(out+16,length));
 }else std::memcpy(out+16,bytes.data(),bytes.size());
 put32(out+slot.length-4,crc(out,slot.length-4));return slot;
}

bool sameRecord(const t::Record& a,const t::Record& b){
 return a.serial==b.serial&&a.phase==b.phase&&a.localSide==b.localSide&&t::sameTranscript(a.transcript,b.transcript)&&t::sameState(a.before,b.before)&&t::sameState(a.after,b.after);
}
void step(State& s,Action action,unsigned value=0) {
 CHECK(apply(s,action,value)==Error::None);CHECK(isValid(s));
}
State fixture(unsigned starter=1,unsigned count=2) {
 auto s=newDevice(100+starter);step(s,Action::Hatch,starter);
 s.sequence=s.foregroundSequence=100;s.collectionCount=count;s.nextMemberId=count+1;
 s.captures=s.encounters=count-1;s.steps=100*(count-1);
 const auto* form=forms::find(79);
 for(unsigned i=1;i<count;++i)s.collection[i]={i+1,static_cast<Species>(form->lineage),17,63,47,30+i,100,5,i+1,xpForLevel(5)+17,79};
 s.journal[(79-1)/32]|=1u<<((79-1)%32);s.dungeonKeys=3;CHECK(isValid(s));return s;
}
t::Transcript transcript(const State& a,const State& b,unsigned aId=2,unsigned bId=2) {
 t::Transcript x;x.peers[0].bytes[0]=2;x.peers[1].bytes[0]=4;x.session=0xaabbccdd12345678ull;
 x.nonces[0]=197;x.nonces[1]=12345;x.revision=1;x.sourceSequences[0]=a.sequence;x.sourceSequences[1]=b.sequence;
 x.receivedTrades[0]=a.receivedTrades;x.receivedTrades[1]=b.receivedTrades;
 x.offers[0]=*findMember(a,aId);x.offers[1]=*findMember(b,bId);CHECK(t::valid(x));return x;
}
void roundtrip(const t::Record& record) {
 std::array<std::uint8_t,t::kRecordBytes+2> bytes;bytes.fill(0xab);
 CHECK(t::encodeRecord(record,bytes.data()+1,t::kRecordBytes));CHECK(bytes.front()==0xab&&bytes.back()==0xab);
 t::Record decoded;CHECK(t::decodeRecord(bytes.data()+1,t::kRecordBytes,decoded));
 CHECK(decoded.serial==record.serial&&decoded.phase==record.phase&&decoded.localSide==record.localSide);
 CHECK(t::sameTranscript(decoded.transcript,record.transcript)&&t::sameState(decoded.before,record.before)&&t::sameState(decoded.after,record.after));
}
struct Journal final:dt::Backend {
 dt::Slot slots[2]{}; bool present[2]{};unsigned writes=0, fail=0;bool landed=false;
 dt::Read read(unsigned i,dt::Slot& out)override {if(!present[i])return dt::Read::Missing;out=slots[i];return dt::Read::Present;}
 bool write(unsigned i,const dt::Bytes& bytes)override {++writes;if(writes==fail&&!landed)return false;slots[i].bytes=bytes;slots[i].length=dt::kJournalBytes;present[i]=true;return writes!=fail;}
};
struct Care final:digivice::storage::Backend {
 digivice::storage::Slot slots[2]{};bool present[2]{};unsigned writes=0,fail=0;bool landed=false;
 digivice::storage::ReadStatus readSlot(unsigned i,digivice::storage::Slot& out)override{if(!present[i])return digivice::storage::ReadStatus::Missing;out=slots[i];return digivice::storage::ReadStatus::Present;}
 bool writeSlot(unsigned i,const Snapshot& s)override{++writes;if(writes==fail&&!landed)return false;std::memcpy(slots[i].bytes,s.bytes,kSnapshotSize);slots[i].length=kSnapshotSize;present[i]=true;return writes!=fail;}
};
void installedJournalRecovery(){
 CHECK(dt::kJournalBytes==13912&&dt::kV26JournalBytes==6624&&dt::kV22JournalBytes==6120&&dt::kV21JournalBytes==6096&&dt::kV20JournalBytes==1520&&dt::kV19JournalBytes==1512);
 CHECK(dt::supportedJournalSize(1512)&&dt::supportedJournalSize(1520)&&dt::supportedJournalSize(6096)&&dt::supportedJournalSize(6120)&&dt::supportedJournalSize(6624)&&dt::supportedJournalSize(13912));
 for(const auto size:{0u,1u,1511u,1513u,1519u,1521u,6095u,6097u,6119u,6121u,6623u,6625u,UINT32_MAX})CHECK(!dt::supportedJournalSize(size));
 auto a=fixture(),b=fixture(2);auto x=transcript(a,b);x.rules=13;
 for(unsigned version:{19u,20u,21u,22u})for(unsigned side=0;side<2;++side){
  x.rules=version==22?15:version==21?14:13;
  const auto& before=side?b:a;auto walked=before;step(walked,Action::EncounterSeed,777+side);step(walked,Action::AccrueSteps,1000);walked.pendingEncounter.rules=x.rules;
  t::Record prepared,committed,applied,aborted;CHECK(t::prepare(before,x,side,7,prepared));CHECK(t::commit(prepared,walked,committed));
  CHECK(t::applied(committed,committed.after,applied));CHECK(t::abort(prepared,aborted));
  for(const auto& phase:{prepared,committed,applied,aborted}){
   const auto old=journalSlot(phase,19,version),current=journalSlot(phase,19,23);
   // Identical installed journals are decoded in RAM without any storage write.
   Journal untouched;untouched.present[0]=untouched.present[1]=true;untouched.slots[0]=untouched.slots[1]=old;
   dt::Store loaded(untouched);CHECK(loaded.restore()==dt::Boot::Ready&&loaded.mirrored());CHECK(sameRecord(*loaded.record(),phase));
   CHECK(untouched.writes==0&&loaded.record()->before.worldSeed==0&&loaded.record()->after.worldSeed==0);
   CHECK(loaded.checkpoint(phase)&&untouched.writes==0);
   for(unsigned first=0;first<2;++first){
    // A cut after one old-to-new rewrite produces the same revision in two
    // representations. It remains locked until the verified pair is repaired.
    Journal mixed=untouched;mixed.slots[first]=current;
    dt::Store reboot(mixed);CHECK(reboot.restore()==dt::Boot::NeedsMirror&&!reboot.mirrored());CHECK(sameRecord(*reboot.record(),phase)&&mixed.writes==0);
    CHECK(reboot.repairMirror()&&reboot.mirrored()&&mixed.writes==2);
    for(const auto& slot:mixed.slots)CHECK(slot.length==dt::kJournalBytes&&!std::memcmp(slot.bytes.data,current.bytes.data,dt::kJournalBytes));
    dt::Store again(mixed);CHECK(again.restore()==dt::Boot::Ready&&sameRecord(*again.record(),phase));
    // A semantic conflict remains a fault even when both CRCs are valid.
    auto changed=phase;changed.before.worldSeed=changed.after.worldSeed=42;CHECK(t::valid(changed));
    Journal conflict=untouched;conflict.slots[first]=journalSlot(changed,19,23);
    dt::Store rejected(conflict);CHECK(rejected.restore()==dt::Boot::RecoveryRequired&&!rejected.writable()&&conflict.writes==0);
   }
   // Missing mirror repairs and every possible uncertain write in that repair
   // keep the original phase/transcript. Neither a reboot nor a cut seeds it.
   for(unsigned missing=0;missing<2;++missing)for(unsigned cut=1;cut<=2;++cut)for(bool landed:{false,true}){
    Journal backend=untouched;backend.present[missing]=false;backend.fail=cut;backend.landed=landed;
    dt::Store repairing(backend);CHECK(repairing.restore()==dt::Boot::NeedsMirror);CHECK(!repairing.repairMirror()&&!repairing.writable());
    backend.fail=0;dt::Store reboot(backend);const auto status=reboot.restore();CHECK(status==dt::Boot::Ready||status==dt::Boot::NeedsMirror);
    CHECK(sameRecord(*reboot.record(),phase));CHECK(reboot.repairMirror()&&reboot.mirrored());CHECK(sameRecord(*reboot.record(),phase));
   }
   // Exercise the real Session against migrated care snapshots as well: only a
   // durable Committed decision may finish ownership; Prepared stays locked.
   for(bool alreadyApplied:{false,true}){
    if(alreadyApplied&&phase.phase!=t::Phase::Committed)continue;
    const bool terminalOwnership=phase.phase==t::Phase::Committed||phase.phase==t::Phase::Applied;
    const auto& saved=(alreadyApplied||phase.phase==t::Phase::Applied)?committed.after:walked;
    Care care;Snapshot encoded;CHECK(encodeSnapshot(saved,encoded));
    for(unsigned slot=0;slot<2;++slot){care.present[slot]=true;care.slots[slot].length=version==19?660:version==20?664:version==21?2952:2964;oldSnapshot(encoded.bytes,care.slots[slot].bytes,version);}
    State state;storage::SaveStore saves(care);CHECK(saves.restore(state)==storage::BootStatus::Migrated&&care.writes==0);
    Journal journal=untouched;dt::Session session(state,saves,journal);CHECK(session.restore());
    CHECK(t::sameState(state,terminalOwnership?committed.after:walked));CHECK(state.worldSeed==0);
    CHECK(session.record()->serial==phase.serial&&session.record()->localSide==side&&t::sameTranscript(session.record()->transcript,phase.transcript));
    CHECK(session.record()->phase==(terminalOwnership?t::Phase::Applied:phase.phase));
    CHECK(session.blocksForeground()==(phase.phase==t::Phase::Prepared));
    CHECK(t::sameState(session.record()->before,phase.before)&&t::sameState(session.record()->after,phase.after));
   }
  }
  // Old Prepared -> new Committed and old Committed -> new Applied must also
  // survive interruption between the two writes without losing the decision.
  for(bool finish:{false,true})for(unsigned cut=1;cut<=2;++cut)for(bool landed:{false,true}){
   const auto& prior=finish?committed:prepared;const auto& next=finish?applied:committed;
   Journal backend;backend.present[0]=backend.present[1]=true;backend.slots[0]=backend.slots[1]=journalSlot(prior,19,version);
   dt::Store store(backend);CHECK(store.restore()==dt::Boot::Ready);backend.fail=cut;backend.landed=landed;
   CHECK(!store.checkpoint(next)&&!store.writable());backend.fail=0;
   dt::Store reboot(backend);const auto status=reboot.restore();CHECK(status==dt::Boot::Ready||status==dt::Boot::NeedsMirror);
   CHECK(sameRecord(*reboot.record(),cut==1&&!landed?prior:next));CHECK(reboot.repairMirror());
  }
 }
}
} // namespace

int main(){
 installedJournalRecovery();
 auto a=fixture(1,60),b=fixture(2,60);t::Record ancient[2];const auto captures=a.captures;const auto oldsteps=a.steps;
 for(unsigned turn=1;turn<=64;++turn){
  auto x=transcript(a,b,a.collection[1].id,b.collection[1].id);x.session+=turn;x.revision=turn;
  t::Record p[2],c[2],done[2];CHECK(t::prepare(a,x,0,turn,p[0]));CHECK(t::prepare(b,x,1,turn,p[1]));
  if(turn==1){ancient[0]=p[0];ancient[1]=p[1];}
  for(unsigned side=0;side<2;++side){auto& s=side?b:a;if(!s.encounterRng)step(s,Action::EncounterSeed,777+side);step(s,Action::AccrueSteps,64);
   const auto walk=s.explorationSteps;const auto pending=s.pendingEncounter;
   CHECK(t::commit(p[side],s,c[side]));s=c[side].after;CHECK(t::applied(c[side],s,done[side]));roundtrip(done[side]);
   CHECK(s.receivedTrades==turn&&s.captures==captures&&s.steps==oldsteps&&s.collectionCount==60);
   CHECK(s.explorationSteps==walk&&s.pendingEncounter.formId==pending.formId);
   t::Record no;CHECK(!t::commit(ancient[side],s,no));
  }
 }
 auto first=fixture(1,60),second=fixture(2,60);auto x=transcript(first,second);t::Record prepared,committed,applied;
 CHECK(t::prepare(first,x,0,1,prepared));CHECK(t::commit(prepared,first,committed));CHECK(t::applied(committed,committed.after,applied));
 // Interrupted first-ever Prepare has no externally acknowledged transaction.
 // A landed single slot must repair; a write that never landed may remain Empty.
 for(unsigned position=1;position<=2;++position)for(bool landed:{false,true}) {
  Journal backend;dt::Store initial(backend);CHECK(initial.restore()==dt::Boot::Empty);
  backend.fail=position;backend.landed=landed;CHECK(!initial.checkpoint(prepared)&&!initial.writable());
  backend.fail=0;dt::Store reboot(backend);const auto boot=reboot.restore();
  if(position==1&&!landed) CHECK(boot==dt::Boot::Empty&&!reboot.record());
  else {CHECK(boot==dt::Boot::Ready||boot==dt::Boot::NeedsMirror);CHECK(reboot.record()->phase==t::Phase::Prepared);CHECK(reboot.repairMirror()&&reboot.mirrored());}
 }
 for(const auto& transition:{committed,applied})for(unsigned position=1;position<=2;++position)for(bool landed:{false,true}){
  Journal backend;dt::Store initial(backend);CHECK(initial.restore()==dt::Boot::Empty);CHECK(initial.checkpoint(prepared));if(transition.phase==t::Phase::Applied)CHECK(initial.checkpoint(committed));
  backend.fail=backend.writes+position;backend.landed=landed;CHECK(!initial.checkpoint(transition)&&!initial.writable());
  backend.fail=0;dt::Store reboot(backend);auto boot=reboot.restore();CHECK(boot==dt::Boot::Ready||boot==dt::Boot::NeedsMirror);CHECK(reboot.record()!=nullptr);
  CHECK(reboot.record()->phase==(position==1&&!landed?(transition.phase==t::Phase::Applied?t::Phase::Committed:t::Phase::Prepared):transition.phase));
  CHECK(reboot.repairMirror()&&reboot.mirrored());CHECK(!std::memcmp(backend.slots[0].bytes.data,backend.slots[1].bytes.data,dt::kJournalBytes));
 }
 for(const auto& phase:{prepared,committed,applied})for(unsigned missing=0;missing<2;++missing){
  Journal backend;dt::Store initial(backend);CHECK(initial.restore()==dt::Boot::Empty);CHECK(initial.checkpoint(prepared));if(phase.phase!=t::Phase::Prepared)CHECK(initial.checkpoint(committed));if(phase.phase==t::Phase::Applied)CHECK(initial.checkpoint(applied));
  backend.present[missing]=false;dt::Store reboot(backend);CHECK(reboot.restore()==dt::Boot::NeedsMirror);CHECK(reboot.record()->phase==phase.phase);CHECK(reboot.repairMirror()&&reboot.mirrored());
  const auto writes=backend.writes;for(unsigned i=0;i<100;++i)CHECK(reboot.checkpoint(*reboot.record()));CHECK(backend.writes==writes);
  backend.slots[missing].bytes.data[17]^=1;dt::Store corrupt(backend);CHECK(corrupt.restore()==dt::Boot::RecoveryRequired&&!corrupt.writable());
 }
 for(unsigned position=1;position<=2;++position)for(bool landed:{false,true}){
  Care flash;digivice::storage::SaveStore store(flash);State state;CHECK(store.restore(state)==digivice::storage::BootStatus::Empty);CHECK(store.checkpointMirrored(first));
  flash.fail=flash.writes+position;flash.landed=landed;CHECK(!store.checkpointMirrored(committed.after)&&!store.writable());flash.fail=0;
  digivice::storage::SaveStore reboot(flash);CHECK(reboot.restore(state)==digivice::storage::BootStatus::Loaded);CHECK(t::sameState(state,(position==1&&!landed)?first:committed.after));
  CHECK(reboot.checkpointMirrored(committed.after));for(unsigned slot=0;slot<2;++slot){State decoded;CHECK(decodeSnapshot(flash.slots[slot].bytes,flash.slots[slot].length,decoded)==SnapshotStatus::Ok&&decoded.receivedTrades==1&&t::sameState(decoded,committed.after));}
 }
 // Every byte of a full60 current care record is protected by the checksum;
 // even when the other mirror is sound, recovery never silently overwrites it.
 Care intact;storage::SaveStore persisted(intact);State state;
 CHECK(persisted.restore(state)==storage::BootStatus::Empty&&persisted.checkpointMirrored(first));
 for(unsigned offset=0;offset<kSnapshotSize;++offset){
  auto damaged=intact;damaged.slots[0].bytes[offset]^=1;const auto preserved=damaged.slots[0];
  storage::SaveStore reboot(damaged);CHECK(reboot.restore(state)==storage::BootStatus::RecoveryRequired&&!reboot.writable());
  CHECK(t::sameState(state,first));CHECK(!reboot.checkpoint(first)&&damaged.writes==intact.writes);
  CHECK(!std::memcmp(preserved.bytes,damaged.slots[0].bytes,kSnapshotSize));
 }
 std::printf("%u independent trade chain/mirror checks PASS; Store=%zu Record=%zu State=%zu Journal=%zu bytes\n",checks,sizeof(dt::Store),sizeof(t::Record),sizeof(State),dt::kJournalBytes);return 0;
}
