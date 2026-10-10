#include "trade.hpp"
#include "forms.hpp"
#include <cstring>
#include <limits>

namespace digivice::trade {
namespace {
constexpr auto kMax = std::numeric_limits<std::uint32_t>::max();
void put32(std::uint8_t* p, std::uint32_t v) { for (unsigned i=0;i<4;++i) p[i]=static_cast<std::uint8_t>(v>>(8*i)); }
std::uint32_t get32(const std::uint8_t* p) { std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(p[i])<<(8*i);return v; }
std::uint32_t crc(const std::uint8_t* p,std::size_t n) {
    std::uint32_t v=0xffffffffu;
    for(std::size_t i=0;i<n;++i){v^=p[i];for(unsigned j=0;j<8;++j)v=(v>>1)^((v&1)?0xedb88320u:0u);}
    return ~v;
}
bool validIdentity(const Identity& id) {
    bool nonzero=false,notBroadcast=false;
    for(const auto b:id.bytes){nonzero|=b!=0;notBroadcast|=b!=255;}
    return nonzero&&notBroadcast&&!(id.bytes[0]&1u);
}
void memberBytes(const CreatureMember& m,std::uint8_t* p) {
    const std::uint32_t values[]{m.id,static_cast<std::uint32_t>(m.species),m.hp,m.energy,m.fullness,m.mood,m.bond,m.level,m.capturedAtSequence,m.xp,m.formId};
    for(unsigned i=0;i<11;++i)put32(p+4*i,values[i]);
}
bool readMember(const std::uint8_t* p,CreatureMember& out) {
    if(get32(p+4)>65535u)return false;
    out={get32(p),static_cast<Species>(get32(p+4)),get32(p+8),get32(p+12),get32(p+16),get32(p+20),get32(p+24),get32(p+28),get32(p+32),get32(p+36),get32(p+40)};
    return true;
}
void copyBackground(const State& from,State& to) {
    to.sequence=from.sequence;to.explorationSteps=from.explorationSteps;
    to.encounterRng=from.encounterRng;to.encounterTarget=from.encounterTarget;
    to.encounterProgress=from.encounterProgress;to.pendingEncounter=from.pendingEncounter;
}
// Consent binds source ownership/care, while monotonically newer background
// checkpoints can advance the containing save without changing that offer.
bool boundOffer(const State& state,const Transcript& t,unsigned side) {
    if(side>1||!valid(t)||state.sequence<t.sourceSequences[side]||state.receivedTrades!=t.receivedTrades[side]||!canOffer(state,t.offers[side].id))return false;
    return sameMember(*findMember(state,t.offers[side].id),t.offers[side]);
}
bool exchange(const State& state,const Transcript& t,unsigned side,State& out) {
    if(!boundOffer(state,t,side)||state.sequence==kMax||state.nextMemberId==kMax||state.receivedTrades==kMax)return false;
    auto next=state;
    const auto outgoing=t.offers[side].id;
    std::size_t index=0;while(index<next.collectionCount&&next.collection[index].id!=outgoing)++index;
    if(index==next.collectionCount)return false;
    for(;index+1<next.collectionCount;++index)next.collection[index]=next.collection[index+1];
    next.collection[next.collectionCount-1]={};
    if(next.activeCreatureId==outgoing){
        next.activeCreatureId=0;
        for(unsigned i=0;i+1<next.collectionCount;++i)if(forms::productionForm(next.collection[i].formId)){next.activeCreatureId=next.collection[i].id;break;}
        if(!next.activeCreatureId)return false;
    }
    auto received=t.offers[1-side];
    received.id=next.nextMemberId++;received.capturedAtSequence=++next.sequence;
    ++next.receivedTrades;
    next.collection[next.collectionCount-1]=received;
    next.journal[(received.formId-1)/32]|=1u<<((received.formId-1)%32);
    reconcileParty(next);
    next.foregroundSequence=next.sequence;
    // Trade UI supplies its own result; stale capture/Auto presentations cannot
    // present a newly received member as a capture or repeat an old battle.
    next.lastCapture={};next.lastAutoOutcome=autobattle::Outcome::None;
    next.lastAutoSequence=0;next.lastAutoTurns=0;next.message=Message::Selected;
    const auto* active=activeMember(next);
    next.hp=active->hp;next.energy=active->energy;next.fullness=active->fullness;
    next.mood=active->mood;next.bond=active->bond;next.level=active->level;
    if(!isValid(next))return false;
    out=next;return true;
}
bool validBase(const Record& record) {
    return record.serial&&record.localSide<2&&static_cast<unsigned>(record.phase)<=static_cast<unsigned>(Phase::Applied)&&
        record.before.sequence>=record.transcript.sourceSequences[record.localSide]&&boundOffer(record.before,record.transcript,record.localSide);
}
}
bool sameIdentity(const Identity& a,const Identity& b){return !std::memcmp(a.bytes,b.bytes,sizeof(a.bytes));}
bool sameMember(const CreatureMember& a,const CreatureMember& b){std::uint8_t x[kMemberBytes],y[kMemberBytes];memberBytes(a,x);memberBytes(b,y);return !std::memcmp(x,y,sizeof(x));}
bool validMember(const CreatureMember& m,std::uint32_t sequence){
    const auto* f=forms::find(m.formId);
    return m.id&&m.id!=kMax&&f&&forms::productionForm(m.formId)&&forms::validForLineage(m.formId,static_cast<unsigned>(m.species))&&
        combat::validFormProfile(m.formId,m.level)&&m.xp<=kMaxXp&&m.level==levelForXp(m.xp)&&m.bond>=f->minBond&&m.bond<=200&&
        m.hp&&m.hp<=combat::formProfile(m.formId,m.level).stats.maxHp&&m.energy<=100&&m.fullness<=100&&m.mood<=100&&
        m.capturedAtSequence<=sequence&&(m.id==1?m.capturedAtSequence==0:m.capturedAtSequence!=0);
}
bool canOffer(const State& s,std::uint32_t id){
    if(!isValid(s)||!s.onboardingComplete||s.phase!=digivice::Phase::Home||needsTestEncounterResolution(s)||s.sequence==kMax||s.nextMemberId==kMax||s.receivedTrades==kMax)return false;
    const auto* offered=findMember(s,id);
    if(!offered||!validMember(*offered,s.sequence))return false;
    for(unsigned i=0;i<s.collectionCount;++i)if(s.collection[i].id!=id&&forms::productionForm(s.collection[i].formId))return true;
    return false;
}
bool valid(const Transcript& t){
    return (t.rules==13||t.rules==14||t.rules==15||t.rules==16||t.rules==17||t.rules==18||t.rules==kRulesVersion)&&validIdentity(t.peers[0])&&validIdentity(t.peers[1])&&std::memcmp(t.peers[0].bytes,t.peers[1].bytes,6)<0&&t.session&&t.nonces[0]&&t.nonces[1]&&t.revision&&
        t.sourceSequences[0]!=kMax&&t.sourceSequences[1]!=kMax&&t.receivedTrades[0]<=t.sourceSequences[0]&&t.receivedTrades[1]<=t.sourceSequences[1]&&validMember(t.offers[0],t.sourceSequences[0])&&validMember(t.offers[1],t.sourceSequences[1]);
}
bool encodeTranscript(const Transcript& t,std::uint8_t* bytes,std::size_t capacity){
    if(!bytes||capacity<kTranscriptBytes||!valid(t))return false;
    std::uint8_t result[kTranscriptBytes]{};
    std::memcpy(result,"DGTX",4);put32(result+4,kVersion);put32(result+8,t.rules);
    std::memcpy(result+12,t.peers[0].bytes,6);std::memcpy(result+18,t.peers[1].bytes,6);
    put32(result+24,static_cast<std::uint32_t>(t.session));put32(result+28,static_cast<std::uint32_t>(t.session>>32));
    put32(result+32,t.nonces[0]);put32(result+36,t.nonces[1]);put32(result+40,t.revision);
    put32(result+44,t.sourceSequences[0]);put32(result+48,t.sourceSequences[1]);
    memberBytes(t.offers[0],result+52);memberBytes(t.offers[1],result+96);
    put32(result+140,t.receivedTrades[0]);put32(result+144,t.receivedTrades[1]);
    put32(result+148,crc(result,148));std::memcpy(bytes,result,sizeof(result));return true;
}
bool decodeTranscript(const std::uint8_t* bytes,std::size_t length,Transcript& out){
    if(!bytes||length!=kTranscriptBytes||std::memcmp(bytes,"DGTX",4)||get32(bytes+4)!=kVersion||get32(bytes+148)!=crc(bytes,148))return false;
    Transcript t;t.rules=get32(bytes+8);
    std::memcpy(t.peers[0].bytes,bytes+12,6);std::memcpy(t.peers[1].bytes,bytes+18,6);
    t.session=get32(bytes+24)|(std::uint64_t(get32(bytes+28))<<32);
    t.nonces[0]=get32(bytes+32);t.nonces[1]=get32(bytes+36);t.revision=get32(bytes+40);
    t.sourceSequences[0]=get32(bytes+44);t.sourceSequences[1]=get32(bytes+48);
    t.receivedTrades[0]=get32(bytes+140);t.receivedTrades[1]=get32(bytes+144);
    if(!readMember(bytes+52,t.offers[0])||!readMember(bytes+96,t.offers[1])||!valid(t))return false;
    out=t;return true;
}
bool sameTranscript(const Transcript& a,const Transcript& b){std::uint8_t x[kTranscriptBytes],y[kTranscriptBytes];return encodeTranscript(a,x,sizeof(x))&&encodeTranscript(b,y,sizeof(y))&&!std::memcmp(x,y,sizeof(x));}
std::uint32_t fingerprint(const Transcript& t){std::uint8_t bytes[kTranscriptBytes];return encodeTranscript(t,bytes,sizeof(bytes))?get32(bytes+148):0;}
bool sameState(const State& a,const State& b){Snapshot x,y;return encodeSnapshot(a,x)&&encodeSnapshot(b,y)&&!std::memcmp(x.bytes,y.bytes,kSnapshotSize);}
bool backgroundOnly(const State& before,const State& current){
    if(!isValid(before)||!isValid(current)||current.sequence<before.sequence||current.explorationSteps<before.explorationSteps)return false;
    if(current.sequence==before.sequence)return sameState(before,current);
    if(before.pendingEncounter.formId&&(before.pendingEncounter.formId!=current.pendingEncounter.formId||before.pendingEncounter.level!=current.pendingEncounter.level||before.pendingEncounter.rules!=current.pendingEncounter.rules))return false;
    if(before.encounterRng&&!current.encounterRng)return false;
    auto normalized=current;copyBackground(before,normalized);
    return sameState(before,normalized);
}
bool prepare(const State& s,const Transcript& t,unsigned side,std::uint32_t serial,Record& out){
    if(!serial||side>1||!boundOffer(s,t,side))return false;
    Record record;record.serial=serial;record.localSide=static_cast<std::uint8_t>(side);record.transcript=t;record.before=s;record.after=s;
    out=record;return true;
}
bool commit(const Record& prepared,const State& current,Record& out){
    if(!valid(prepared)||prepared.phase!=Phase::Prepared||!backgroundOnly(prepared.before,current))return false;
    State after;
    if(!exchange(current,prepared.transcript,prepared.localSide,after))return false;
    out=prepared;out.after=after;out.phase=Phase::Committed;return true;
}
bool abort(const Record& prepared,Record& out){if(!valid(prepared)||prepared.phase!=Phase::Prepared)return false;out=prepared;out.phase=Phase::Aborted;return true;}
bool abortUnprepared(const State& current,const Transcript& t,unsigned side,std::uint32_t serial,Record& out){
    if(side!=0||!serial||!isValid(current)||!valid(t))return false;
    Record result;result.serial=serial;result.phase=Phase::Aborted;result.transcript=t;result.before=current;result.after=current;
    out=result;return true;
}
bool applied(const Record& committed,const State& current,Record& out){
    if(!valid(committed)||committed.phase!=Phase::Committed||!sameState(committed.after,current))return false;
    out=committed;out.phase=Phase::Applied;return true;
}
bool valid(const Record& record){
    if(record.phase==Phase::Aborted&&record.localSide==0)
        return record.serial&&valid(record.transcript)&&sameState(record.before,record.after);
    if(!validBase(record))return false;
    if(record.phase==Phase::Prepared||record.phase==Phase::Aborted)return sameState(record.before,record.after);
    if(!record.after.sequence)return false;
    auto current=record.before;copyBackground(record.after,current);--current.sequence;
    if(!backgroundOnly(record.before,current))return false;
    State expected;
    return exchange(current,record.transcript,record.localSide,expected)&&sameState(expected,record.after);
}
namespace {
// Validation itself needs temporary care states. Do not keep these two encoded
// snapshots on its caller stack as well (Xtensa otherwise reserves both frames).
__attribute__((noinline)) bool encodeValidatedRecord(const Record& record,std::uint8_t* bytes){
    // Validate first so callers can retain the prior buffer on every failure.
    Snapshot before,after;if(!encodeSnapshot(record.before,before)||!encodeSnapshot(record.after,after))return false;
    std::uint8_t transcript[kTranscriptBytes];if(!encodeTranscript(record.transcript,transcript,sizeof(transcript)))return false;
    std::memset(bytes,0,kRecordBytes);std::memcpy(bytes,"DGTR",4);put32(bytes+4,kVersion);put32(bytes+8,record.serial);
    bytes[12]=static_cast<std::uint8_t>(record.phase);bytes[13]=record.localSide;
    std::memcpy(bytes+16,transcript,sizeof(transcript));std::memcpy(bytes+16+kTranscriptBytes,before.bytes,kSnapshotSize);
    std::memcpy(bytes+16+kTranscriptBytes+kSnapshotSize,after.bytes,kSnapshotSize);put32(bytes+kRecordBytes-4,crc(bytes,kRecordBytes-4));return true;
}
}
bool encodeRecord(const Record& record,std::uint8_t* bytes,std::size_t capacity){
    return bytes&&capacity>=kRecordBytes&&valid(record)&&encodeValidatedRecord(record,bytes);
}
bool decodeRecord(const std::uint8_t* bytes,std::size_t length,Record& out){
    if(!bytes||(length!=kRecordBytes&&length!=kV19RecordBytes&&length!=kV20RecordBytes&&length!=kV21RecordBytes&&length!=kV22RecordBytes)||std::memcmp(bytes,"DGTR",4)||get32(bytes+4)!=kVersion||bytes[12]>static_cast<unsigned>(Phase::Applied)||bytes[13]>1||bytes[14]||bytes[15]||get32(bytes+length-4)!=crc(bytes,length-4))return false;
    const bool legacy=length!=kRecordBytes;
    const auto snapshotBytes=length==kV19RecordBytes?kV19SnapshotSize:length==kV20RecordBytes?kV20SnapshotSize:length==kV21RecordBytes?kV21SnapshotSize:length==kV22RecordBytes?kV22SnapshotSize:kSnapshotSize;
    const auto expectedStatus=legacy?SnapshotStatus::Migrated:SnapshotStatus::Ok;
    Record result;result.serial=get32(bytes+8);result.phase=static_cast<Phase>(bytes[12]);result.localSide=bytes[13];
    if(!decodeTranscript(bytes+16,kTranscriptBytes,result.transcript)||decodeSnapshot(bytes+16+kTranscriptBytes,snapshotBytes,result.before)!=expectedStatus||
       decodeSnapshot(bytes+16+kTranscriptBytes+snapshotBytes,snapshotBytes,result.after)!=expectedStatus||!valid(result))return false;
    out=result;return true;
}
} // namespace digivice::trade
