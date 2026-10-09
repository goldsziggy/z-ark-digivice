#include "trade_protocol.hpp"
#include <cstring>
#include <limits>

namespace digivice::tradewire {
namespace {
constexpr std::size_t header=16,footer=4,helloBytes=64;
constexpr Identity broadcast{{255,255,255,255,255,255}};
constexpr auto max32=std::numeric_limits<std::uint32_t>::max();
static_assert(header+trade::kTranscriptBytes+footer<=kMaxPacket);
bool same(const Identity& a,const Identity& b){return trade::sameIdentity(a,b);}
int order(const Identity& a,const Identity& b){return std::memcmp(a.bytes,b.bytes,6);}
bool identity(const Identity& id){unsigned bits=0;for(auto b:id.bytes)bits|=b;return bits&&!(id.bytes[0]&1u);}
void put(std::uint8_t* p,std::uint32_t n,unsigned size=4){for(unsigned i=0;i<size;++i)p[i]=static_cast<std::uint8_t>(n>>(8*i));}
std::uint32_t get(const std::uint8_t* p,unsigned size=4){std::uint32_t n=0;for(unsigned i=0;i<size;++i)n|=std::uint32_t(p[i])<<(8*i);return n;}
std::uint32_t crc(const std::uint8_t* p,std::size_t n){std::uint32_t c=~0u;for(std::size_t i=0;i<n;++i){c^=p[i];for(unsigned b=0;b<8;++b)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
void memberBytes(const CreatureMember& m,std::uint8_t* p){const std::uint32_t fields[]{m.id,static_cast<std::uint32_t>(m.species),m.hp,m.energy,m.fullness,m.mood,m.bond,m.level,m.capturedAtSequence,m.xp,m.formId};for(unsigned i=0;i<11;++i)put(p+4*i,fields[i]);}
bool member(const std::uint8_t* p,CreatureMember& m){if(get(p+4)>65535)return false;m={get(p),static_cast<Species>(get(p+4)),get(p+8),get(p+12),get(p+16),get(p+20),get(p+24),get(p+28),get(p+32),get(p+36),get(p+40)};return true;}
bool advertisement(const Advertisement& a){return a.nonce&&a.sourceSequence!=max32&&a.rules&&a.catalog&&a.capabilities&&a.receivedTrades<=a.sourceSequence&&trade::validMember(a.member,a.sourceSequence);}
bool compatible(const Advertisement& a){return a.rules==kRulesVersion&&a.catalog==kCatalogVersion&&(a.capabilities&kTradeCapability);}
unsigned side(const trade::Transcript& t,const Identity& id){return same(t.peers[0],id)?0u:same(t.peers[1],id)?1u:2u;}
bool transaction(const trade::Transcript& a,const trade::Transcript& b){return a.rules==b.rules&&a.session==b.session&&same(a.peers[0],b.peers[0])&&same(a.peers[1],b.peers[1])&&a.nonces[0]==b.nonces[0]&&a.nonces[1]==b.nonces[1]&&a.receivedTrades[0]==b.receivedTrades[0]&&a.receivedTrades[1]==b.receivedTrades[1];}
bool exactOffer(const trade::Transcript& a,const trade::Transcript& b,unsigned actor){return a.sourceSequences[actor]==b.sourceSequences[actor]&&trade::sameMember(a.offers[actor],b.offers[actor]);}
bool prepared(Durable d){return d==Durable::Prepared||d==Durable::Committed||d==Durable::Applied;}
bool message(const Message& m){
    if(!identity(m.sender)||static_cast<unsigned>(m.kind)<1||static_cast<unsigned>(m.kind)>static_cast<unsigned>(Kind::Cancel)||static_cast<unsigned>(m.durable)>static_cast<unsigned>(Durable::Aborted))return false;
    if(m.kind==Kind::Hello)return m.durable==Durable::None&&advertisement(m.advertisement);
    if(!trade::valid(m.transcript))return false;
    const auto actor=side(m.transcript,m.sender);if(actor>1)return false;
    switch(m.kind){
    case Kind::Review:return actor==0&&m.durable==Durable::None;
    case Kind::Offer:return actor==1&&m.durable==Durable::None;
    case Kind::Prepared:return m.durable==Durable::Prepared;
    case Kind::Commit:return actor==0&&m.durable==Durable::Committed;
    case Kind::Applied:return m.durable==Durable::Applied;
    case Kind::Abort:return actor==0&&m.durable==Durable::Aborted;
    case Kind::Status:return true;
    case Kind::Cancel:return actor==1&&(m.durable==Durable::None||m.durable==Durable::Prepared);
    default:return m.durable==Durable::None;
    }
}
}
bool encode(const Message& m,std::uint8_t* output,std::size_t capacity,std::size_t& length){
    const auto size=header+(m.kind==Kind::Hello?helloBytes:trade::kTranscriptBytes)+footer;
    if(!output||capacity<size||!message(m))return false;
    std::uint8_t bytes[kMaxPacket]{};std::memcpy(bytes,"DGT1",4);bytes[4]=kWireVersion;bytes[5]=static_cast<std::uint8_t>(m.kind);put(bytes+6,static_cast<std::uint32_t>(size),2);std::memcpy(bytes+8,m.sender.bytes,6);
    bytes[14]=m.kind==Kind::Hello?static_cast<std::uint8_t>(m.advertisement.available):static_cast<std::uint8_t>(m.durable);
    auto* p=bytes+header;
    if(m.kind==Kind::Hello){const auto& a=m.advertisement;put(p,a.rules,2);put(p+2,a.catalog,2);put(p+4,a.capabilities,2);put(p+8,a.nonce);put(p+12,a.sourceSequence);memberBytes(a.member,p+16);put(p+60,a.receivedTrades);}
    else if(!trade::encodeTranscript(m.transcript,p,trade::kTranscriptBytes))return false;
    put(bytes+size-footer,crc(bytes,size-footer));std::memcpy(output,bytes,size);length=size;return true;
}
bool decode(const Identity& source,const std::uint8_t* p,std::size_t length,Message& output){
    if(!identity(source)||!p||length<header+footer||length>kMaxPacket||std::memcmp(p,"DGT1",4)||p[4]!=kWireVersion||p[5]<1||p[5]>static_cast<unsigned>(Kind::Cancel)||get(p+6,2)!=length||std::memcmp(p+8,source.bytes,6)||p[15]||get(p+length-footer)!=crc(p,length-footer))return false;
    Message m;m.sender=source;m.kind=static_cast<Kind>(p[5]);const auto* payload=p+header;
    if(m.kind==Kind::Hello){
        if(length!=header+helloBytes+footer||p[14]>1||payload[6]||payload[7])return false;
        auto& a=m.advertisement;a.available=p[14]!=0;a.rules=static_cast<std::uint16_t>(get(payload,2));a.catalog=static_cast<std::uint16_t>(get(payload+2,2));a.capabilities=static_cast<std::uint16_t>(get(payload+4,2));a.nonce=get(payload+8);a.sourceSequence=get(payload+12);
        if(!member(payload+16,a.member))return false;
        a.receivedTrades=get(payload+60);
    }else{
        if(length!=header+trade::kTranscriptBytes+footer||p[14]>static_cast<unsigned>(Durable::Aborted)||!trade::decodeTranscript(payload,trade::kTranscriptBytes,m.transcript))return false;
        m.durable=static_cast<Durable>(p[14]);
    }
    if(!message(m))return false;
    output=m;
    return true;
}
void Protocol::close(){
    view_=View{};
    local_=Identity{};
    localAdvertisement_=Advertisement{};
    previous_=trade::Transcript{};
    proposed_=previous_; // One canonical empty value; avoids Xtensa GCC aggregate ICE.
    hasPrevious_=hasProposal_=cancelRequested_=false;
    lastSend_=lastHello_=lastReceive_=clock_=0;
    lastRecoveryReply_=0;
    hasRecoveryReply_=false;
    queueRead_=queueCount_=0;
}
bool Protocol::open(const Identity& local,const CreatureMember& member,std::uint32_t sequence,std::uint32_t nonce,std::uint64_t now,bool available,std::uint32_t receivedTrades){
    Advertisement a;a.nonce=nonce;a.sourceSequence=sequence;a.member=member;a.available=available;a.receivedTrades=receivedTrades;
    if(!identity(local)||!advertisement(a)||view_.durable==Durable::Prepared||view_.durable==Durable::Committed||view_.requested!=Request::None)return false;
    close();local_=local;localAdvertisement_=a;view_.stage=Stage::Discovering;clock_=lastReceive_=now;announce(now);return true;
}
bool Protocol::enqueue(const Message& m,const Identity& destination,std::uint64_t now){
    if(queueCount_==kTxCapacity)return false;
    Datagram packet;packet.destination=destination;std::size_t length=0;
    if(!encode(m,packet.bytes,sizeof(packet.bytes),length))return false;
    packet.length=static_cast<std::uint16_t>(length);queue_[(queueRead_+queueCount_)%kTxCapacity]=packet;++queueCount_;
    if(m.kind!=Kind::Hello)lastSend_=now;
    return true;
}
bool Protocol::pop(Datagram& out){if(!queueCount_)return false;out=queue_[queueRead_];queueRead_=(queueRead_+1)%kTxCapacity;--queueCount_;return true;}
void Protocol::announce(std::uint64_t now){Message m;m.sender=local_;m.advertisement=localAdvertisement_;m.advertisement.available&=view_.stage==Stage::Discovering;enqueue(m,broadcast,now);lastHello_=now;}
void Protocol::sendTranscript(Kind kind,const trade::Transcript& t,std::uint64_t now){
    Message m;m.sender=local_;m.kind=kind;m.transcript=t;
    m.durable=kind==Kind::Prepared?Durable::Prepared:kind==Kind::Commit?Durable::Committed:kind==Kind::Applied?Durable::Applied:kind==Kind::Abort?Durable::Aborted:kind==Kind::Status||kind==Kind::Cancel?view_.durable:Durable::None;
    const auto actor=side(t,local_);if(actor<2)enqueue(m,t.peers[1-actor],now);
}
void Protocol::send(Kind kind,std::uint64_t now){sendTranscript(kind,view_.transcript,now);}
void Protocol::adopt(const trade::Transcript& t,std::uint64_t now){
    view_.transcript=t;view_.localSide=side(t,local_);view_.stage=Stage::Reviewing;
    view_.durable=view_.peerDurable=Durable::None;view_.requested=Request::None;
    view_.localConfirmed=view_.peerConfirmed=view_.peerReviewed=view_.offerPending=view_.recoveryOffer=false;
    view_.connected=true;hasProposal_=cancelRequested_=false;queueRead_=queueCount_=0;lastReceive_=now;
    localAdvertisement_.nonce=t.nonces[view_.localSide];localAdvertisement_.member=t.offers[view_.localSide];localAdvertisement_.sourceSequence=t.sourceSequences[view_.localSide];
    if(localAdvertisement_.receivedTrades<t.receivedTrades[view_.localSide])localAdvertisement_.receivedTrades=t.receivedTrades[view_.localSide];
}
void Protocol::changedReview(const trade::Transcript& t,std::uint64_t now){previous_=view_.transcript;hasPrevious_=true;adopt(t,now);send(Kind::Review,now);}
bool Protocol::knownPeer(const Message& m,std::uint64_t now)const{
    const auto actor=side(m.transcript,m.sender);if(actor>1)return false;
    for(std::size_t i=0;i<view_.peerCount;++i){const auto& peer=view_.peers[i];if(same(peer.identity,m.sender)&&peer.compatible&&peer.advertisement.nonce==m.transcript.nonces[actor]&&peer.advertisement.sourceSequence==m.transcript.sourceSequences[actor]&&peer.advertisement.receivedTrades==m.transcript.receivedTrades[actor]&&trade::sameMember(peer.advertisement.member,m.transcript.offers[actor])&&now>=peer.seenMs&&now-peer.seenMs<=kPeerLifetimeMs)return true;}
    return false;
}
bool Protocol::replyCompleted(const Message& m,std::uint64_t now){
    const auto actor=side(m.transcript,local_);if(actor>1)return false;
    const bool aborted=m.kind==Kind::Abort;
    // A progressed counter alone cannot prove that an old proposal committed.
    // Only the peer's terminal Applied or coordinator's durable Commit proves
    // a decision under the honest-device/no-rollback scope. Abort is likewise
    // authoritative only from the coordinator. These replies never adopt a
    // transcript, request storage, or change a live negotiation.
    if(!aborted&&((m.kind!=Kind::Applied&&m.kind!=Kind::Commit)||localAdvertisement_.receivedTrades<=m.transcript.receivedTrades[actor]))return false;
    if(hasRecoveryReply_&&now>=lastRecoveryReply_&&now-lastRecoveryReply_<kRetryMs)return true;
    Message reply;reply.kind=aborted?Kind::Status:Kind::Applied;reply.sender=local_;reply.transcript=m.transcript;reply.durable=aborted?Durable::Aborted:Durable::Applied;
    const auto last=lastSend_;
    const bool sent=enqueue(reply,m.sender,now);lastSend_=last;
    if(sent){lastRecoveryReply_=now;hasRecoveryReply_=true;}
    return true;
}
bool Protocol::invite(const Identity& identity,std::uint64_t session,std::uint64_t now){
    if(view_.stage!=Stage::Discovering||!session||!localAdvertisement_.available||now<clock_)return false;
    const Peer* peer=nullptr;for(std::size_t i=0;i<view_.peerCount;++i)if(same(view_.peers[i].identity,identity))peer=&view_.peers[i];
    if(!peer||!peer->compatible||!peer->advertisement.available||now<peer->seenMs||now-peer->seenMs>kPeerLifetimeMs)return false;
    trade::Transcript t;const auto actor=order(local_,identity)<0?0u:1u;t.peers[actor]=local_;t.peers[1-actor]=identity;t.session=session;t.revision=1;
    t.nonces[actor]=localAdvertisement_.nonce;t.nonces[1-actor]=peer->advertisement.nonce;t.sourceSequences[actor]=localAdvertisement_.sourceSequence;t.sourceSequences[1-actor]=peer->advertisement.sourceSequence;t.offers[actor]=localAdvertisement_.member;t.offers[1-actor]=peer->advertisement.member;t.receivedTrades[actor]=localAdvertisement_.receivedTrades;t.receivedTrades[1-actor]=peer->advertisement.receivedTrades;
    if(!trade::valid(t))return false;
    adopt(t,now);
    hasPrevious_=false;
    view_.stage=Stage::Inviting;
    announce(now);
    send(Kind::Invite,now);
    return true;
}
bool Protocol::offer(const CreatureMember& member,std::uint32_t sequence,std::uint64_t now,std::uint32_t receivedTrades){
    if(receivedTrades==max32)receivedTrades=localAdvertisement_.receivedTrades;
    if(now<clock_||sequence==max32||receivedTrades==max32||receivedTrades<localAdvertisement_.receivedTrades||!trade::validMember(member,sequence))return false;
    if(view_.stage==Stage::Discovering){localAdvertisement_.member=member;localAdvertisement_.sourceSequence=sequence;localAdvertisement_.receivedTrades=receivedTrades;localAdvertisement_.available=true;announce(now);return true;}
    if(receivedTrades!=view_.transcript.receivedTrades[view_.localSide]||view_.stage!=Stage::Reviewing||view_.durable!=Durable::None||view_.requested!=Request::None||view_.peerConfirmed||!view_.peerReviewed||hasProposal_||cancelRequested_||view_.transcript.revision==max32||sequence<view_.transcript.sourceSequences[view_.localSide])return false;
    auto t=view_.transcript;t.offers[view_.localSide]=member;t.sourceSequences[view_.localSide]=sequence;++t.revision;
    if(!trade::valid(t))return false;
    if(view_.localSide==0)changedReview(t,now);else{proposed_=t;hasProposal_=true;view_.offerPending=true;sendTranscript(Kind::Offer,proposed_,now);}return true;
}
bool Protocol::confirm(const trade::Transcript& reviewed,std::uint64_t now){
    if(now<clock_||(view_.stage!=Stage::Reviewing&&view_.stage!=Stage::Recovering)||view_.durable!=Durable::None||view_.requested!=Request::None||hasProposal_||cancelRequested_||!view_.peerReviewed||!trade::sameTranscript(reviewed,view_.transcript))return false;
    view_.requested=Request::Prepare;view_.stage=Stage::Preparing;queueRead_=queueCount_=0;return true;
}
bool Protocol::cancel(const trade::Transcript& reviewed,std::uint64_t now){
    if(now<clock_||!trade::sameTranscript(reviewed,view_.transcript)||view_.durable==Durable::Committed||view_.durable==Durable::Applied||view_.durable==Durable::Aborted||view_.requested==Request::Commit||view_.requested==Request::Apply)return false;
    // If Prepare may already be writing, finish that durable boundary first.
    cancelRequested_=true;hasProposal_=false;view_.offerPending=false;
    if(view_.requested!=Request::Prepare){if(view_.localSide==0)view_.requested=Request::Abort;else send(Kind::Cancel,now);}
    return true;
}
void Protocol::advance(){
    if(view_.requested!=Request::None)return;
    if(view_.durable==Durable::Committed){view_.requested=Request::Apply;view_.stage=Stage::Applying;return;}
    if(view_.localSide==1&&view_.durable==Durable::Prepared&&view_.peerDurable==Durable::Aborted){view_.requested=Request::Abort;return;}
    if(cancelRequested_&&view_.localSide==0&&view_.durable!=Durable::Applied&&view_.durable!=Durable::Aborted){view_.requested=Request::Abort;return;}
    if(view_.localSide==0&&view_.durable==Durable::Prepared&&prepared(view_.peerDurable)){view_.requested=Request::Commit;view_.stage=Stage::Committing;}
}
bool Protocol::setDurable(Durable durable,std::uint64_t now){
    if(now<clock_)return false;
    const auto required=durable==Durable::Prepared?Request::Prepare:durable==Durable::Committed?Request::Commit:durable==Durable::Applied?Request::Apply:durable==Durable::Aborted?Request::Abort:Request::None;
    if(required==Request::None||view_.requested!=required)return false;
    view_.durable=durable;view_.requested=Request::None;queueRead_=queueCount_=0;hasProposal_=hasPrevious_=false;view_.offerPending=false;
    if(durable==Durable::Prepared){view_.localConfirmed=true;view_.stage=Stage::Prepared;send(Kind::Prepared,now);if(cancelRequested_&&view_.localSide==1)send(Kind::Cancel,now);}
    else if(durable==Durable::Committed){view_.stage=Stage::Committing;send(view_.localSide==0?Kind::Commit:Kind::Status,now);}
    else if(durable==Durable::Applied){localAdvertisement_.receivedTrades=view_.transcript.receivedTrades[view_.localSide]+1;view_.stage=Stage::Applied;send(Kind::Applied,now);}
    else {view_.stage=Stage::Aborted;send(view_.localSide==0?Kind::Abort:Kind::Status,now);}
    advance();return true;
}
bool Protocol::restore(const Identity& local,const trade::Transcript& t,Durable durable,std::uint64_t now,std::uint32_t receivedTrades){
    if(!identity(local)||!trade::valid(t)||side(t,local)>1||durable==Durable::None||static_cast<unsigned>(durable)>static_cast<unsigned>(Durable::Aborted))return false;
    const auto expected=t.receivedTrades[side(t,local)]+(durable==Durable::Applied?1u:0u);
    if(receivedTrades==max32)receivedTrades=expected;
    if(receivedTrades<expected||receivedTrades==max32)return false;
    close();local_=local;clock_=now;adopt(t,now);localAdvertisement_.receivedTrades=receivedTrades;view_.durable=durable;view_.localConfirmed=prepared(durable);view_.connected=false;
    view_.stage=durable==Durable::Prepared?Stage::Prepared:durable==Durable::Committed?Stage::Committing:durable==Durable::Applied?Stage::Applied:Stage::Aborted;
    announce(now);resend(now);advance();return true;
}
void Protocol::resend(std::uint64_t now){
    if(view_.durable==Durable::Applied)send(Kind::Applied,now);
    else if(view_.durable==Durable::Committed)send(view_.localSide==0?Kind::Commit:Kind::Status,now);
    else if(view_.durable==Durable::Aborted)send(view_.localSide==0?Kind::Abort:Kind::Status,now);
    else if(cancelRequested_&&view_.localSide==1)send(Kind::Cancel,now);
    else if(view_.durable==Durable::Prepared)send(Kind::Prepared,now);
    else if(view_.stage==Stage::Inviting)send(Kind::Invite,now);
    else if(hasProposal_)sendTranscript(Kind::Offer,proposed_,now);
    else if(view_.localSide==0&&!view_.peerReviewed&&view_.transcript.session)send(Kind::Review,now);
    else if(view_.transcript.session)send(Kind::Status,now);
}
void Protocol::tick(std::uint64_t now){
    if(view_.stage==Stage::Closed)return;
    if(now<clock_){view_.connected=false;clock_=lastReceive_=lastSend_=lastHello_=now;return;} // Clock reset never aborts escrow.
    clock_=now;if(now-lastReceive_>=kReconnectMs)view_.connected=false;
    for(std::size_t i=0;i<view_.peerCount;++i)if(now<view_.peers[i].seenMs||now-view_.peers[i].seenMs>kPeerLifetimeMs)view_.peers[i].advertisement.available=false;
    if(now-lastHello_>=kHelloMs)announce(now);
    if(view_.stage!=Stage::Discovering&&now-lastSend_>=kRetryMs)resend(now);
}
bool Protocol::receive(const Identity& source,const std::uint8_t* bytes,std::size_t size,std::uint64_t now){
    Message m;if(view_.stage==Stage::Closed||same(source,local_)||now<clock_||!decode(source,bytes,size,m))return false;
    if(m.kind==Kind::Hello){
        std::size_t i=0;for(;i<view_.peerCount;++i)if(same(view_.peers[i].identity,source))break;
        if(i==view_.peerCount){if(i<kMaxPeers)++view_.peerCount;else{for(i=0;i<kMaxPeers;++i)if(now>=view_.peers[i].seenMs&&now-view_.peers[i].seenMs>kPeerLifetimeMs)break;if(i==kMaxPeers)return false;}}
        view_.peers[i]={source,m.advertisement,now,compatible(m.advertisement)};
        // Resume the persisted transaction, independent of the peer's new open nonce.
        if(view_.transcript.session&&same(source,view_.transcript.peers[1-view_.localSide])&&view_.durable!=Durable::None)resend(now);
        return true;
    }
    const auto actor=side(m.transcript,local_);if(actor>1)return false;
    const bool exact=trade::sameTranscript(m.transcript,view_.transcript);
    if(!exact&&replyCompleted(m,now))return true;
    if(m.kind==Kind::Invite){
        if(m.transcript.rules!=kRulesVersion||!knownPeer(m,now)||m.transcript.revision!=1||m.transcript.nonces[actor]!=localAdvertisement_.nonce||!trade::sameMember(m.transcript.offers[actor],localAdvertisement_.member)||m.transcript.sourceSequences[actor]!=localAdvertisement_.sourceSequence||m.transcript.receivedTrades[actor]!=localAdvertisement_.receivedTrades)return false;
        if(exact){if(view_.durable==Durable::None){view_.peerReviewed=true;view_.stage=Stage::Reviewing;send(Kind::ReviewAck,now);}else resend(now);return true;}
        if(view_.stage==Stage::Inviting){if(!same(source,view_.transcript.peers[1-view_.localSide]))return false;if(view_.localSide==0){send(Kind::Invite,now);return true;}}
        else if(view_.stage!=Stage::Discovering||!localAdvertisement_.available)return false;
        adopt(m.transcript,now);hasPrevious_=false;view_.peerReviewed=true;send(Kind::ReviewAck,now);return true;
    }
    // A guest may cancel the last acknowledged review while the coordinator's
    // next edit is in flight. Retire that old pair durably; do not replace the
    // user's cancellation with an unacknowledged review.
    if(!exact&&m.kind==Kind::Cancel&&view_.localSide==0&&view_.durable==Durable::None&&view_.requested==Request::None&&hasPrevious_&&!view_.peerReviewed&&trade::sameTranscript(previous_,m.transcript)){
        adopt(m.transcript,now);hasPrevious_=false;cancelRequested_=true;view_.requested=Request::Abort;return true;
    }
    // A peer that journaled a prior review can recover even when this device
    // rebooted before consent. This is an explicit recovery review, never prepare.
    if(!exact&&m.durable==Durable::Prepared&&(m.kind==Kind::Prepared||m.kind==Kind::Status)&&view_.durable==Durable::None&&view_.requested==Request::None){
        const bool prior=hasPrevious_&&!view_.peerReviewed&&trade::sameTranscript(previous_,m.transcript);
        if(prior||(view_.stage==Stage::Discovering&&knownPeer(m,now))){
            adopt(m.transcript,now);hasPrevious_=false;view_.recoveryOffer=true;view_.stage=Stage::Recovering;view_.peerReviewed=view_.peerConfirmed=true;view_.peerDurable=Durable::Prepared;send(Kind::Status,now);return true;
        }
    }
    if(!transaction(m.transcript,view_.transcript)||!same(source,view_.transcript.peers[1-view_.localSide]))return false;
    if(m.kind==Kind::Offer){
        if(view_.localSide!=0||view_.durable!=Durable::None||view_.requested!=Request::None||view_.peerConfirmed||cancelRequested_||view_.stage==Stage::Aborted){resend(now);return false;}
        if(m.transcript.revision==view_.transcript.revision&&exact){send(Kind::Review,now);return true;}
        if(view_.transcript.revision==max32||m.transcript.revision!=view_.transcript.revision+1||!view_.peerReviewed||!exactOffer(m.transcript,view_.transcript,0)||m.transcript.sourceSequences[1]<view_.transcript.sourceSequences[1]){resend(now);return false;}
        changedReview(m.transcript,now);return true;
    }
    if(m.kind==Kind::Review){
        if(view_.localSide!=1||view_.durable!=Durable::None||view_.requested!=Request::None||view_.peerConfirmed||cancelRequested_||view_.stage==Stage::Aborted){resend(now);return false;}
        if(exact){view_.peerReviewed=true;send(Kind::ReviewAck,now);return true;}
        if(view_.transcript.revision==max32||m.transcript.revision!=view_.transcript.revision+1||m.transcript.sourceSequences[0]<view_.transcript.sourceSequences[0])return false;
        const bool ownOffer=exactOffer(m.transcript,view_.transcript,1)||(hasProposal_&&exactOffer(m.transcript,proposed_,1));if(!ownOffer)return false;
        const auto pending=proposed_;const bool retryProposal=hasProposal_&&!exactOffer(m.transcript,pending,1);
        previous_=view_.transcript;hasPrevious_=true;adopt(m.transcript,now);view_.peerReviewed=true;send(Kind::ReviewAck,now);
        if(retryProposal&&view_.transcript.revision!=max32){proposed_=view_.transcript;proposed_.offers[1]=pending.offers[1];proposed_.sourceSequences[1]=pending.sourceSequences[1];++proposed_.revision;hasProposal_=true;view_.offerPending=true;sendTranscript(Kind::Offer,proposed_,now);}
        return true;
    }
    if(!exact)return false;
    lastReceive_=now;view_.connected=true;
    if(m.kind==Kind::ReviewAck){if(view_.durable!=Durable::None){resend(now);return true;}view_.peerReviewed=true;hasPrevious_=false;if(view_.stage==Stage::Inviting)view_.stage=Stage::Reviewing;return true;}
    if(m.kind==Kind::Cancel){
        if(view_.localSide!=0)return false;
        if(view_.durable==Durable::Committed||view_.durable==Durable::Applied||view_.durable==Durable::Aborted||view_.requested==Request::Commit||view_.requested==Request::Apply){resend(now);return true;}
        cancelRequested_=true;if(view_.requested!=Request::Prepare)view_.requested=Request::Abort;return true;
    }
    if(m.kind==Kind::Abort||(m.kind==Kind::Status&&m.durable==Durable::Aborted&&view_.localSide==1)){
        if(view_.durable==Durable::Committed||view_.durable==Durable::Applied)return false;
        view_.peerDurable=Durable::Aborted;
        if(view_.durable==Durable::Prepared){view_.requested=Request::Abort;return true;}
        if(view_.requested==Request::Prepare){cancelRequested_=true;return true;}
        if(view_.durable==Durable::Aborted&&view_.localSide==1)send(Kind::Status,now);
        view_.requested=Request::None;view_.stage=Stage::Aborted;return true;
    }
    if(m.kind==Kind::Commit||(m.kind==Kind::Status&&m.durable==Durable::Committed&&view_.localSide==1)||m.kind==Kind::Applied){
        if(view_.durable==Durable::None||view_.durable==Durable::Aborted)return false;
        if(m.kind==Kind::Applied){view_.peerDurable=Durable::Applied;view_.peerConfirmed=true;}
        else {if(view_.peerDurable!=Durable::Applied)view_.peerDurable=Durable::Committed;view_.peerConfirmed=true;}
        if(view_.localSide==1&&view_.durable==Durable::Prepared){view_.requested=Request::Commit;view_.stage=Stage::Committing;return true;}
        if(view_.durable==Durable::Applied){if(m.kind!=Kind::Applied)send(Kind::Applied,now);return true;}
        advance();return view_.durable!=Durable::None;
    }
    if(m.kind==Kind::Prepared||(m.kind==Kind::Status&&m.durable==Durable::Prepared)){
        if(view_.durable==Durable::Aborted){resend(now);return true;}
        // Observing peer readiness freezes the reviewed pair, including an
        // in-flight local offer proposal which has not been acknowledged.
        hasProposal_=false;view_.offerPending=false;if(!prepared(view_.peerDurable))view_.peerDurable=Durable::Prepared;view_.peerConfirmed=view_.peerReviewed=true;
        if(view_.durable==Durable::Committed||view_.durable==Durable::Applied)resend(now);else advance();
        return true;
    }
    if(m.kind==Kind::Status){
        if(m.durable==Durable::None){view_.peerReviewed=true;if(view_.durable!=Durable::None)resend(now);return true;}
        if(m.durable==Durable::Aborted&&view_.durable==Durable::Aborted){view_.peerDurable=m.durable;return true;}
    }
    return false;
}
} // namespace digivice::tradewire
