#pragma once
#include "game.hpp"
#include <cstddef>
#include <cstdint>

// Pure bounded ownership exchange. Transport consent and durable write ordering
// belong to the caller; no clock, network, storage, allocation or timeout here.
namespace digivice::trade {
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kMemberBytes = 44, kTranscriptBytes = 152;
struct Identity { std::uint8_t bytes[6]{}; };
struct Transcript {
    Identity peers[2]{}; // Lexicographic order; peer0 is the decision coordinator.
    std::uint64_t session = 0;
    std::uint32_t nonces[2]{}, revision = 0, sourceSequences[2]{};
    CreatureMember offers[2]{};
    std::uint32_t receivedTrades[2]{}; // Pre-trade local acquisition ordinals.
    // Preserve the consent epoch when recovering installed rules13 journals.
    // This occupies the existing rules word in the 152-byte wire transcript.
    std::uint32_t rules = kRulesVersion;
};
enum class Phase : std::uint8_t { Prepared, Committed, Aborted, Applied };
struct Record {
    std::uint32_t serial = 0;
    Phase phase = Phase::Prepared;
    std::uint8_t localSide = 0;
    Transcript transcript{};
    State before{}, after{};
};
constexpr std::size_t kRecordBytes = 16 + kTranscriptBytes + 2 * kSnapshotSize + 4;
// Installed schema19/20 journals embed 660/664-byte snapshots. Their envelope
// and transcript stay v1; decoding migrates snapshots, never consent/decision.
constexpr std::size_t kV19RecordBytes = 16 + kTranscriptBytes + 2 * kV19SnapshotSize + 4;
constexpr std::size_t kV21RecordBytes = 16 + kTranscriptBytes + 2 * kV21SnapshotSize + 4;
constexpr std::size_t kV20RecordBytes = 16 + kTranscriptBytes + 2 * kV20SnapshotSize + 4;

bool sameIdentity(const Identity&, const Identity&);
bool sameMember(const CreatureMember&, const CreatureMember&);
bool validMember(const CreatureMember&, std::uint32_t sourceSequence);
// A partner other than the offered member must remain playable in the collection.
// Caller additionally excludes practice/Nearby battles and any other trade lock.
bool canOffer(const State&, std::uint32_t memberId);
bool valid(const Transcript&);
bool sameTranscript(const Transcript&, const Transcript&);
bool encodeTranscript(const Transcript&, std::uint8_t*, std::size_t capacity);
bool decodeTranscript(const std::uint8_t*, std::size_t length, Transcript&);
// CRC transcript identity detects corruption/stale consent; it is NOT authentication.
std::uint32_t fingerprint(const Transcript&);

// Successful functions assign out transactionally. Prepared must be persisted and
// verified before acknowledging readiness. Only the coordinator may decide Abort;
// a prepared participant applies an exact coordinator decision bound to its durable
// transcript, never a timeout. MAC/CRC binding does not authenticate the peer.
bool prepare(const State&, const Transcript&, unsigned localSide, std::uint32_t serial, Record& out);
// Only AccrueSteps/EncounterSeed background fields may have changed since Prepared.
// Freeze all game writes after constructing Committed until after is mirrored to
// both care-save slots and Applied is durably recorded. No stale snapshot overwrite.
bool commit(const Record& prepared, const State& current, Record& out);
bool abort(const Record& prepared, Record& out);
// A coordinator with no durable decision may reject a recovered peer's Prepared
// transcript even if its old offered member has since changed. The caller must
// first prove the durable journal has no conflicting transaction/commit receipt.
bool abortUnprepared(const State& current, const Transcript&, unsigned localSide,
                     std::uint32_t serial, Record& out);
bool applied(const Record& committed, const State& current, Record& out);
bool valid(const Record&);
bool encodeRecord(const Record&, std::uint8_t*, std::size_t capacity);
bool decodeRecord(const std::uint8_t*, std::size_t length, Record&);
bool sameState(const State&, const State&);
bool backgroundOnly(const State& before, const State& current);
} // namespace digivice::trade
