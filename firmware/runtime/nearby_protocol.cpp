#include "nearby_protocol.hpp"
#include <cstring>

namespace digivice::nearby {
namespace {
enum Kind : std::uint8_t { Hello=1, Challenge, Accept, State, ChoicePacket, Ack, Cancel, Sync };
constexpr std::size_t header=34,footer=4;
constexpr std::size_t fighterBytes=20,helloBytes=fighterBytes+1,offerBytes=2*fighterBytes+9;
static_assert(header+kMatchBytes+footer<=kMaxPacket&&header+offerBytes+footer<=kMaxPacket);
constexpr Mac broadcast{{255,255,255,255,255,255}};
bool validMac(const Mac& m){unsigned any=0;for(auto b:m.bytes)any|=b;return any&&!(m.bytes[0]&1u);}
int compare(const Mac& a,const Mac& b){return std::memcmp(a.bytes,b.bytes,6);}
void put(std::uint8_t* p,std::uint64_t n,unsigned size){for(unsigned i=0;i<size;++i)p[i]=static_cast<std::uint8_t>(n>>(i*8));}
std::uint64_t get(const std::uint8_t* p,unsigned size){std::uint64_t n=0;for(unsigned i=0;i<size;++i)n|=static_cast<std::uint64_t>(p[i])<<(i*8);return n;}
std::uint32_t crc(const std::uint8_t* p,std::size_t n){std::uint32_t c=~0u;for(std::size_t i=0;i<n;++i){c^=p[i];for(unsigned b=0;b<8;++b)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
void fighter(std::uint8_t* p,const Fighter& f){put(p,f.memberId,4);put(p+4,f.formId,4);put(p+8,f.level,4);put(p+12,f.offenseBonus,4);put(p+16,f.protectionBonus,4);}
Fighter fighter(const std::uint8_t* p){return {static_cast<std::uint32_t>(get(p,4)),static_cast<std::uint32_t>(get(p+4,4)),static_cast<std::uint32_t>(get(p+8,4)),static_cast<std::uint32_t>(get(p+12,4)),static_cast<std::uint32_t>(get(p+16,4))};}
bool sameMatch(const Match& a,const Match& b){std::uint8_t x[kMatchBytes],y[kMatchBytes];return encode(a,x,sizeof(x))&&encode(b,y,sizeof(y))&&!std::memcmp(x,y,sizeof(x));}
bool sessionStage(Stage s){return s==Stage::Outgoing||s==Stage::Incoming||s==Stage::Accepting||s==Stage::Playing||s==Stage::Reconnecting||s==Stage::Finished;}
}
bool sameMac(const Mac& a,const Mac& b){return !compare(a,b);}
bool Protocol::open(const Mac& local,const Fighter& selected,std::uint64_t now,std::uint32_t nonce){
    if(!nonce||!validMac(local)||!validFighter(selected))return false;
    close();local_=local;localFighter_=selected;localNonce_=nonce;view_.stage=Stage::Discovering;opened_=lastReceive_=lastSend_=lastAdvance_=now;announce(now);return true;
}
void Protocol::close(){view_={};local_={};localFighter_={};seed_=localNonce_=peerNonce_=0;chosen_[0]=chosen_[1]=Choice::None;choiceAcknowledged_=false;queueRead_=queueCount_=0;lastReceive_=lastSend_=lastHello_=lastAdvance_=opened_=0;}
void Protocol::clearSession(Stage stage){view_.stage=stage;view_.localChoicePending=false;view_.peerAcknowledged=false;chosen_[0]=chosen_[1]=Choice::None;choiceAcknowledged_=false;}
void Protocol::announce(std::uint64_t now){send(Hello,0,now);lastHello_=now;}
void Protocol::send(std::uint8_t kind,std::uint32_t sequence,std::uint64_t now){
    if(queueCount_>=kTxCapacity)return; // tick/repeated peer traffic retries the same state.
    Datagram packet;packet.destination=kind==Hello?broadcast:view_.opponent;
    auto* p=packet.bytes;std::memcpy(p,"DGN1",4);p[4]=1;p[5]=kind;
    std::memcpy(p+8,local_.bytes,6);put(p+14,kRules,2);put(p+16,kCatalog,2);
    put(p+18,kind==Hello?0:view_.session,8);put(p+26,sequence,4);put(p+30,localNonce_,4);
    auto* payload=p+header;std::size_t size=0;
    if(kind==Hello){fighter(payload,localFighter_);payload[fighterBytes]=view_.stage==Stage::Discovering?1:0;size=helloBytes;}
    else if(kind==Challenge||kind==Accept){fighter(payload,view_.offered[0]);fighter(payload+fighterBytes,view_.offered[1]);payload[2*fighterBytes]=static_cast<std::uint8_t>(view_.offeredMode);put(payload+2*fighterBytes+1,seed_,4);put(payload+2*fighterBytes+5,view_.host?peerNonce_:localNonce_,4);size=offerBytes;}
    else if(kind==State){if(!encode(view_.match,payload,kMaxPacket-header-footer))return;size=kMatchBytes;}
    else if(kind==ChoicePacket){payload[0]=static_cast<std::uint8_t>(chosen_[1]);size=1;}
    else if(kind==Ack){payload[0]=ackKind_;size=1;}
    packet.length=static_cast<std::uint16_t>(header+size+footer);put(p+6,packet.length,2);put(p+packet.length-footer,crc(p,packet.length-footer),4);
    queue_[(queueRead_+queueCount_)%kTxCapacity]=packet;++queueCount_;if(kind!=Hello)lastSend_=now;
}
void Protocol::acknowledge(std::uint8_t kind,std::uint32_t sequence,std::uint64_t now){ackKind_=kind;ackSequence_=sequence;send(Ack,ackSequence_,now);}
bool Protocol::pop(Datagram& packet){if(!queueCount_)return false;packet=queue_[queueRead_];queueRead_=(queueRead_+1)%kTxCapacity;--queueCount_;return true;}
bool Protocol::challenge(std::size_t index,Mode mode,std::uint64_t session,std::uint32_t seed,std::uint64_t now){
    if(view_.stage!=Stage::Discovering||index>=view_.peerCount||!session||!seed||static_cast<unsigned>(mode)>1)return false;
    const auto& peer=view_.peers[index];if(!peer.available||!peer.compatible||now<peer.seenMs||now-peer.seenMs>kPeerLifetimeMs)return false;
    view_.opponent=peer.mac;peerNonce_=peer.openNonce;view_.offered[0]=localFighter_;view_.offered[1]=peer.fighter;view_.offeredMode=mode;
    view_.session=session;view_.host=true;view_.stage=Stage::Outgoing;seed_=seed;lastReceive_=lastAdvance_=now;
    chosen_[0]=chosen_[1]=Choice::None;view_.peerAcknowledged=false;send(Challenge,0,now);return true;
}
bool Protocol::accept(std::uint64_t now){
    if(view_.stage!=Stage::Incoming||view_.host)return false;
    view_.stage=Stage::Accepting;lastReceive_=lastAdvance_=now;send(Accept,0,now);return true;
}
void Protocol::beginHost(std::uint64_t now){
    Match match;if(!begin(view_.offered[0],view_.offered[1],view_.offeredMode,seed_,match))return;
    view_.match=match;view_.stage=Stage::Playing;view_.localChoicePending=false;view_.peerAcknowledged=false;
    chosen_[0]=chosen_[1]=Choice::None;lastAdvance_=lastReceive_=now;send(State,0,now);
}
bool Protocol::choose(Choice choice,std::uint64_t now){
    if(view_.stage!=Stage::Playing||view_.match.mode!=Mode::Tactical||view_.localChoicePending)return false;
    const auto actor=view_.host?0u:1u;if(!legalChoice(view_.match,actor,choice))return false;
    chosen_[actor]=choice;view_.localChoicePending=true;choiceAcknowledged_=false;
    if(view_.host)advance(now);else send(ChoicePacket,view_.match.sequence,now);return true;
}
void Protocol::advance(std::uint64_t now){
    if(!view_.host||view_.stage!=Stage::Playing||!view_.peerAcknowledged||view_.match.status!=Status::Active)return;
    Match next=view_.match;
    if(next.sequence&&now>=lastAdvance_&&now-lastAdvance_<kAutoPaceMs)return;
    if(next.mode==Mode::Auto){if(now<lastAdvance_||now-lastAdvance_<kAutoPaceMs||!resolveAuto(next,next.sequence))return;}
    else if(chosen_[0]==Choice::None||chosen_[1]==Choice::None||!resolve(next,next.sequence,chosen_[0],chosen_[1]))return;
    view_.match=next;chosen_[0]=chosen_[1]=Choice::None;view_.localChoicePending=false;view_.peerAcknowledged=false;lastAdvance_=now;
    if(next.status!=Status::Active)view_.stage=Stage::Finished;
    send(State,next.sequence,now);
}
void Protocol::cancel(std::uint64_t now){
    if(view_.stage==Stage::Closed)return;
    queueRead_=queueCount_=0;if(view_.session)send(Cancel,view_.match.sequence,now);
    clearSession(Stage::Cancelled);lastAdvance_=now;
}
void Protocol::tick(std::uint64_t now){
    if(view_.stage==Stage::Closed)return;
    if(now<opened_||now<lastReceive_){cancel(now);return;} // Monotonic clock reset ends this ephemeral session.
    for(std::size_t i=0;i<view_.peerCount;++i)if(now-view_.peers[i].seenMs>kPeerLifetimeMs)view_.peers[i].available=false;
    if((view_.stage==Stage::Outgoing||view_.stage==Stage::Incoming||view_.stage==Stage::Accepting)&&now-lastHello_>=2000)announce(now);
    if(view_.stage==Stage::Discovering){if(now-lastHello_>=2000)announce(now);return;}
    if(view_.stage==Stage::Cancelled){if(view_.session&&now-lastAdvance_<2000&&now-lastSend_>=kRetryMs)send(Cancel,view_.match.sequence,now);return;}
    if(!sessionStage(view_.stage))return;
    if(view_.stage==Stage::Finished&&now-lastReceive_>=kSessionTimeoutMs)return;
    if(now-lastReceive_>=kSessionTimeoutMs){clearSession(Stage::TimedOut);queueRead_=queueCount_=0;return;}
    if(view_.stage==Stage::Playing&&now-lastReceive_>=kReconnectMs)view_.stage=Stage::Reconnecting;
    if((view_.stage==Stage::Playing||view_.stage==Stage::Reconnecting)&&view_.match.status==Status::Active&&now-lastAdvance_>=kTurnTimeoutMs){clearSession(Stage::TimedOut);queueRead_=queueCount_=0;return;}
    if(view_.stage==Stage::Playing)advance(now);
    if(now-lastSend_<kRetryMs)return;
    switch(view_.stage){
    case Stage::Outgoing:send(Challenge,0,now);break;
    case Stage::Accepting:send(Accept,0,now);break;
    case Stage::Playing:case Stage::Finished:case Stage::Reconnecting:
        // A finished result both sides have already accepted must go quiet.
        // Further State/Sync retries keep the radio and the panel busy after
        // the duel, which is when a late display stripe can lock that device.
        if(view_.stage==Stage::Finished && view_.peerAcknowledged) break;
        if(view_.host)send(State,view_.match.sequence,now);
        else if(view_.localChoicePending&&!choiceAcknowledged_)send(ChoicePacket,view_.match.sequence,now);
        else send(Sync,view_.match.sequence,now);
        break;
    default:break;
    }
}
bool Protocol::receive(const Mac& source,const std::uint8_t* p,std::size_t length,std::uint64_t now){
    if(view_.stage==Stage::Closed||!validMac(source)||sameMac(source,local_)||!p||length<header+footer||length>kMaxPacket||
       std::memcmp(p,"DGN1",4)||p[4]!=1||p[5]<Hello||p[5]>Sync||get(p+6,2)!=length||
       std::memcmp(p+8,source.bytes,6)||get(p+length-footer,4)!=crc(p,length-footer)||now<opened_)return false;
    const auto nonce=static_cast<std::uint32_t>(get(p+30,4));if(!nonce)return false;
    const auto kind=p[5];const auto session=get(p+18,8);const auto sequence=static_cast<std::uint32_t>(get(p+26,4));
    const auto* payload=p+header;const auto size=length-header-footer;
    const bool compatible=get(p+14,2)==kRules&&get(p+16,2)==kCatalog;
    if(kind==Hello){
        if(size!=helloBytes||session||sequence||payload[fighterBytes]>1)return false;
        const auto f=fighter(payload);if(!validFighter(f))return false;
        std::size_t index=0;for(;index<view_.peerCount;++index)if(sameMac(view_.peers[index].mac,source))break;
        if(index==view_.peerCount){if(index<kMaxPeers)++view_.peerCount;else {index=kMaxPeers;for(std::size_t i=0;i<kMaxPeers;++i)if(now-view_.peers[i].seenMs>kPeerLifetimeMs){index=i;break;}if(index==kMaxPeers)return false;}}
        view_.peers[index]={source,f,now,nonce,payload[fighterBytes]!=0,compatible};return true;
    }
    if(!compatible){if(sameMac(source,view_.opponent)&&nonce==peerNonce_&&session==view_.session&&sessionStage(view_.stage))clearSession(Stage::Incompatible);return false;}
    if(kind==Challenge){
        if(size!=offerBytes||!session||sequence||payload[2*fighterBytes]>1||!get(payload+2*fighterBytes+1,4)||get(payload+2*fighterBytes+5,4)!=localNonce_)return false;
        const auto host=fighter(payload),guest=fighter(payload+fighterBytes);if(!validFighter(host)||!sameFighter(guest,localFighter_))return false;
        // Bind both fighters, including care bonuses, to the observed open
        // session. A valid-range replacement is not the agreed frozen offer.
        bool discovered=false;for(std::size_t i=0;i<view_.peerCount;++i)if(sameMac(view_.peers[i].mac,source)&&view_.peers[i].openNonce==nonce&&view_.peers[i].compatible&&sameFighter(view_.peers[i].fighter,host)&&now>=view_.peers[i].seenMs&&now-view_.peers[i].seenMs<=kPeerLifetimeMs)discovered=true;
        if(!discovered)return false;
        const auto mode=static_cast<Mode>(payload[2*fighterBytes]);const auto seed=static_cast<std::uint32_t>(get(payload+2*fighterBytes+1,4));
        if(sameMac(source,view_.opponent)&&view_.session==session&&view_.stage!=Stage::Discovering){
            if(view_.host||nonce!=peerNonce_||!sameFighter(host,view_.offered[0])||!sameFighter(guest,view_.offered[1])||mode!=view_.offeredMode||seed!=seed_)return false;
            if(view_.stage==Stage::Accepting)send(Accept,0,now);
            return true;
        }
        if(view_.stage==Stage::Outgoing){if(!sameMac(source,view_.opponent))return false;if(compare(local_,source)<0){send(Challenge,0,now);return true;}}
        else if(view_.stage!=Stage::Discovering)return false;
        queueRead_=queueCount_=0;view_.opponent=source;peerNonce_=nonce;view_.offered[0]=host;view_.offered[1]=guest;view_.offeredMode=mode;
        view_.session=session;view_.host=false;seed_=seed;view_.stage=Stage::Incoming;lastReceive_=lastAdvance_=now;return true;
    }
    if(!session||session!=view_.session||nonce!=peerNonce_||!sameMac(source,view_.opponent)||!sessionStage(view_.stage))return false;
    if(kind==Cancel){if(size)return false;queueRead_=queueCount_=0;clearSession(Stage::Cancelled);lastAdvance_=now;return true;}
    if(kind==Accept){
        if(!view_.host||size!=offerBytes||sequence||get(payload+2*fighterBytes+5,4)!=peerNonce_||!sameFighter(fighter(payload),view_.offered[0])||!sameFighter(fighter(payload+fighterBytes),view_.offered[1])||payload[2*fighterBytes]!=static_cast<unsigned>(view_.offeredMode)||get(payload+2*fighterBytes+1,4)!=seed_)return false;
        if(view_.stage==Stage::Outgoing){lastReceive_=now;beginHost(now);return true;}
        if(view_.stage==Stage::Playing||view_.stage==Stage::Finished||view_.stage==Stage::Reconnecting){lastReceive_=now;send(State,view_.match.sequence,now);return true;}return false;
    }
    if(kind==State){
        if(view_.host||!(view_.stage==Stage::Accepting||view_.stage==Stage::Playing||view_.stage==Stage::Finished||view_.stage==Stage::Reconnecting))return false;
        Match next;if(!decode(payload,size,next)||next.sequence!=sequence||next.seed!=seed_||next.mode!=view_.offeredMode||!sameFighter(next.fighters[0],view_.offered[0])||!sameFighter(next.fighters[1],view_.offered[1]))return false;
        if(view_.stage==Stage::Accepting){Match initial;if(sequence||!begin(view_.offered[0],view_.offered[1],view_.offeredMode,seed_,initial)||!sameMatch(next,initial))return false;}
        else if(sequence<view_.match.sequence){acknowledge(State,view_.match.sequence,now);return true;}
        else if(sequence==view_.match.sequence&&!sameMatch(next,view_.match))return false;
        else if(sequence>view_.match.sequence){
            // Stop-and-wait means the authority can be at most one exchange ahead.
            // Verify our own committed choice and the entire deterministic result.
            if(view_.match.status!=Status::Active||sequence!=view_.match.sequence+1)return false;
            Match expected=view_.match;
            if(expected.mode==Mode::Auto){if(!resolveAuto(expected,expected.sequence))return false;}
            else {
                const auto hostChoice=expected.attacker?next.lastDefense:next.lastAttack;
                const auto guestChoice=expected.attacker?next.lastAttack:next.lastDefense;
                if(!view_.localChoicePending||chosen_[1]!=guestChoice||!resolve(expected,expected.sequence,hostChoice,guestChoice))return false;
            }
            if(!sameMatch(expected,next))return false;
        }
        const bool changed=view_.stage==Stage::Accepting||sequence>view_.match.sequence;
        view_.match=next;view_.stage=next.status==Status::Active?Stage::Playing:Stage::Finished;view_.peerAcknowledged=true;lastReceive_=now;
        if(changed){chosen_[0]=chosen_[1]=Choice::None;view_.localChoicePending=false;choiceAcknowledged_=false;lastAdvance_=now;}
        acknowledge(State,sequence,now);return true;
    }
    if(kind==ChoicePacket){
        if(!view_.host||size!=1||payload[0]>6||view_.match.mode!=Mode::Tactical||!(view_.stage==Stage::Playing||view_.stage==Stage::Reconnecting))return false;
        if(sequence!=view_.match.sequence){send(State,view_.match.sequence,now);return false;}
        const auto choice=static_cast<Choice>(payload[0]);if(!legalChoice(view_.match,1,choice)||(chosen_[1]!=Choice::None&&chosen_[1]!=choice))return false;
        chosen_[1]=choice;if(!view_.peerAcknowledged)lastAdvance_=now;view_.peerAcknowledged=true;view_.stage=Stage::Playing;lastReceive_=now;acknowledge(ChoicePacket,sequence,now);advance(now);return true;
    }
    if(kind==Ack){
        if(size!=1||sequence!=view_.match.sequence)return false;
        if(view_.host&&payload[0]==State&&(view_.stage==Stage::Playing||view_.stage==Stage::Finished||view_.stage==Stage::Reconnecting)){
            if(!view_.peerAcknowledged)lastAdvance_=now;
            view_.peerAcknowledged=true;view_.stage=view_.match.status==Status::Active?Stage::Playing:Stage::Finished;lastReceive_=now;advance(now);return true;
        }
        if(!view_.host&&payload[0]==ChoicePacket&&view_.localChoicePending){choiceAcknowledged_=true;lastReceive_=now;return true;}return false;
    }
    if(kind==Sync){
        if(size||!view_.host||!(view_.stage==Stage::Playing||view_.stage==Stage::Finished||view_.stage==Stage::Reconnecting))return false;
        lastReceive_=now;
        if(!(view_.stage==Stage::Finished && view_.peerAcknowledged)) send(State,view_.match.sequence,now);
        return true;
    }
    return false;
}
} // namespace digivice::nearby
