#pragma once
#include "trade.hpp"
#include <cstddef>
#include <cstdint>

// DGT1 is separate from the unchanged DGN1 friendly-battle protocol. The radio
// supplies the actual source MAC. MAC/nonce/CRC binding is NOT authentication;
// this offline protocol assumes honest devices without restored save backups.
namespace digivice::tradewire {
constexpr std::size_t kMaxPacket=240,kMaxPeers=4,kTxCapacity=8;
constexpr std::uint64_t kRetryMs=500,kHelloMs=2000,kPeerLifetimeMs=10000,kReconnectMs=5000;
constexpr std::uint16_t kWireVersion=1,kTradeCapability=1,kCatalogVersion=6;
using Identity=trade::Identity;
enum class Kind : std::uint8_t { Hello=1,Invite,Review,Offer,ReviewAck,Prepared,Commit,Applied,Abort,Status,Cancel };
enum class Durable : std::uint8_t { None,Prepared,Committed,Applied,Aborted };
enum class Request : std::uint8_t { None,Prepare,Commit,Apply,Abort };
enum class Stage : std::uint8_t { Closed,Discovering,Inviting,Reviewing,Preparing,Prepared,Committing,Applying,Applied,Aborted,Cancelled,Recovering };
struct Advertisement {
    std::uint32_t nonce=0,sourceSequence=0;
    CreatureMember member{};
    bool available=false;
    std::uint16_t rules=kRulesVersion,catalog=kCatalogVersion,capabilities=kTradeCapability;
    std::uint32_t receivedTrades=0;
};
struct Message {
    Kind kind=Kind::Hello;
    Identity sender{};
    Durable durable=Durable::None;
    Advertisement advertisement{};
    trade::Transcript transcript{};
};
struct Datagram { Identity destination{};std::uint16_t length=0;std::uint8_t bytes[kMaxPacket]{}; };
// Strict canonical little-endian codec. Decode failure leaves output untouched.
// Every transactional message contains the complete152B canonical transcript.
bool encode(const Message&,std::uint8_t*,std::size_t capacity,std::size_t& length);
bool decode(const Identity& radioSource,const std::uint8_t*,std::size_t length,Message&);
struct Peer { Identity identity{};Advertisement advertisement{};std::uint64_t seenMs=0;bool compatible=false; };
struct View {
    Stage stage=Stage::Closed;
    Peer peers[kMaxPeers]{};std::size_t peerCount=0;
    trade::Transcript transcript{};
    unsigned localSide=0;
    Durable durable=Durable::None,peerDurable=Durable::None;
    Request requested=Request::None;
    bool localConfirmed=false,peerConfirmed=false,peerReviewed=false,offerPending=false,connected=false;
    bool recoveryOffer=false; // Prior peer journal surfaced for explicit review/Abort.
};

// Single owner, fixed memory. No game/save writes, heap, radio calls or implicit
// timeout rollback. Runtime MUST checkpoint/verify its journal before setDurable,
// and mirror the care swap before setDurable(Applied). The MAC send callback is
// never a durable application acknowledgment. Closed radio does not clear a lock.
class Protocol {
public:
    bool open(const Identity& local,const CreatureMember&,std::uint32_t sourceSequence,
              std::uint32_t nonzeroNonce,std::uint64_t nowMs,bool available=true,
              std::uint32_t receivedTrades=0);
    bool invite(const Identity& peer,std::uint64_t nonzeroSession,std::uint64_t nowMs);
    bool offer(const CreatureMember&,std::uint32_t sourceSequence,std::uint64_t nowMs,
               std::uint32_t receivedTrades=UINT32_MAX);
    bool confirm(const trade::Transcript& reviewed,std::uint64_t nowMs);
    bool cancel(const trade::Transcript& reviewed,std::uint64_t nowMs);
    bool setDurable(Durable,std::uint64_t nowMs);
    bool restore(const Identity& local,const trade::Transcript&,Durable,std::uint64_t nowMs,
                 std::uint32_t receivedTrades=UINT32_MAX);
    void close();
    void tick(std::uint64_t nowMs);
    bool receive(const Identity& radioSource,const std::uint8_t*,std::size_t,std::uint64_t nowMs);
    bool pop(Datagram&);
    const View& view() const {return view_;}
    // Local Applied releases game use independently of peer packets. Retaining
    // or replacing terminal receipts is the durable runtime owner's job.
    bool peerApplied() const {return view_.peerDurable==Durable::Applied;}
private:
    void send(Kind,std::uint64_t nowMs);
    void sendTranscript(Kind,const trade::Transcript&,std::uint64_t nowMs);
    void announce(std::uint64_t nowMs);
    void resend(std::uint64_t nowMs);
    void advance();
    void adopt(const trade::Transcript&,std::uint64_t nowMs);
    void changedReview(const trade::Transcript&,std::uint64_t nowMs);
    bool knownPeer(const Message&,std::uint64_t nowMs) const;
    bool replyCompleted(const Message&,std::uint64_t nowMs);
    bool enqueue(const Message&,const Identity&,std::uint64_t nowMs);
    View view_{};Identity local_{};Advertisement localAdvertisement_{};
    trade::Transcript previous_{},proposed_{};
    bool hasPrevious_=false,hasProposal_=false,cancelRequested_=false;
    std::uint64_t lastSend_=0,lastHello_=0,lastReceive_=0,clock_=0;
    std::uint64_t lastRecoveryReply_=0;
    bool hasRecoveryReply_=false;
    Datagram queue_[kTxCapacity]{};std::size_t queueRead_=0,queueCount_=0;
};
} // namespace digivice::tradewire
