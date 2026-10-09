#pragma once
#include "nearby_match.hpp"
#include <cstddef>
#include <cstdint>

namespace digivice::nearby {
constexpr std::size_t kMaxPacket=240,kMaxPeers=4,kTxCapacity=8;
constexpr std::uint64_t kPeerLifetimeMs=10000,kRetryMs=500,kReconnectMs=5000,kSessionTimeoutMs=30000,kAutoPaceMs=2400,kTurnTimeoutMs=120000;
struct Mac { std::uint8_t bytes[6]{}; };
bool sameMac(const Mac&,const Mac&);
struct Datagram { Mac destination{};std::uint16_t length=0;std::uint8_t bytes[kMaxPacket]{}; };
struct Peer { Mac mac{}; Fighter fighter{};std::uint64_t seenMs=0;std::uint32_t openNonce=0;bool available=false,compatible=true; };
enum class Stage : std::uint8_t { Closed, Discovering, Outgoing, Incoming, Accepting, Playing, Reconnecting, Finished, Cancelled, TimedOut, Incompatible };
struct View {
    Stage stage=Stage::Closed;
    Peer peers[kMaxPeers]{};std::size_t peerCount=0;
    Mac opponent{};Fighter offered[2]{};Mode offeredMode=Mode::Tactical;
    bool host=false,localChoicePending=false,peerAcknowledged=false;
    std::uint64_t session=0;
    Match match{};
};
// Single main-task owner. Hardware only drains pop() to ESP-NOW and supplies
// real radio source MAC to receive(). No heap, blocking, game save, or credentials.
// Open only after native UI enters Nearby with an actual selected owned member.
// The selected fighter and its bounded care bonuses are copied at open. They
// remain fixed through discovery, consent, retransmission and match resolution.
class Protocol {
public:
    bool open(const Mac& local,const Fighter&,std::uint64_t nowMs,std::uint32_t nonzeroOpenNonce);
    bool challenge(std::size_t peerIndex,Mode,std::uint64_t session,std::uint32_t seed,std::uint64_t nowMs);
    bool accept(std::uint64_t nowMs); // Explicit recipient consent to frozen offer.
    bool choose(Choice,std::uint64_t nowMs);
    void cancel(std::uint64_t nowMs);
    void close(); // Local reboot/close never resumes or rewards an old duel.
    void tick(std::uint64_t nowMs);
    bool receive(const Mac& source,const std::uint8_t*,std::size_t,std::uint64_t nowMs);
    bool pop(Datagram&);
    const View& view() const {return view_;}
private:
    void send(std::uint8_t kind,std::uint32_t sequence,std::uint64_t nowMs);
    void announce(std::uint64_t nowMs);
    void beginHost(std::uint64_t nowMs);
    void acknowledge(std::uint8_t kind,std::uint32_t sequence,std::uint64_t nowMs);
    void advance(std::uint64_t nowMs);
    void clearSession(Stage);
    View view_{};Mac local_{};Fighter localFighter_{};
    std::uint32_t seed_=0,localNonce_=0,peerNonce_=0;Choice chosen_[2]{Choice::None,Choice::None};
    bool choiceAcknowledged_=false;
    std::uint64_t lastReceive_=0,lastSend_=0,lastHello_=0,lastAdvance_=0,opened_=0;
    std::uint8_t ackKind_=0;std::uint32_t ackSequence_=0;
    Datagram queue_[kTxCapacity]{};std::size_t queueRead_=0,queueCount_=0;
};
} // namespace digivice::nearby
