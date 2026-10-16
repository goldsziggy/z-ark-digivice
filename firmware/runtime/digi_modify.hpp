#pragma once
#include "game.hpp"
#include "digi_modify_cards.hpp"
#include <cstdint>

// Main-owner, host-testable adapter contract. No driver, UID trust, heap or bus IO.
namespace digivice::digimodify {
enum class Presence : std::uint8_t { Unavailable, Absent, Present };
struct Sample { Presence presence=Presence::Unavailable; std::uint32_t cardId=0; };
struct Event { bool present=false; std::uint32_t cardId=0; };
class ReaderLatch {
public:
    static constexpr std::uint64_t kPresentMs=80, kRemovalMs=300, kMaxGapMs=250;
    Event observe(Sample sample,std::uint64_t now,std::uint64_t maximumGapMs=kMaxGapMs) {
        // I/O failure, clock reversal or a sampling gap is NOT card removal.
        if(seen_ && (now<last_ || now-last_>maximumGapMs)) reset();
        last_=now; seen_=true;
        if(sample.presence==Presence::Unavailable) { reset(); return {}; }
        if(sample.presence==Presence::Absent) {
            presenting_=false;
            if(!removing_) {removing_=true;since_=now;}
            if(now-since_>=kRemovalMs) armed_=true;
            return {};
        }
        removing_=false;
        if(!armed_) return {};
        if(!presenting_ || candidate_!=sample.cardId) {
            presenting_=true;candidate_=sample.cardId;since_=now;return {};
        }
        if(now-since_<kPresentMs) return {};
        armed_=false;presenting_=false;
        return {true,sample.cardId}; // Unknown IDs still latch until removal.
    }
    void reset() { armed_=presenting_=removing_=seen_=false; candidate_=0; }
private:
    std::uint64_t since_=0,last_=0;
    std::uint32_t candidate_=0;
    bool armed_=false,presenting_=false,removing_=false,seen_=false;
};
enum class Authority : std::uint8_t { LocalWild, NearbyHost, NearbyGuest, Practice, Inactive };
enum class Result : std::uint8_t { None, Ready, UnknownCard, Unavailable, NotBattle, AlreadyUsed, Rejected, SaveFailed };
inline Result prepare(const State& state,Authority authority,bool enabled,std::uint32_t id,State& candidate) {
    if(!find(id)) return Result::UnknownCard;
    if(!enabled || authority!=Authority::LocalWild) return Result::Unavailable;
    if(state.phase!=Phase::Encounter) return Result::NotBattle;
    candidate=state;
    const auto error=apply(candidate,Action::DigiModify,id);
    return error==Error::None?Result::Ready:error==Error::CardAlreadyUsed?Result::AlreadyUsed:Result::Rejected;
}
struct OverlayView { bool active=false; std::uint16_t progress=0; Effect effect=Effect::CriticalBoost; std::uint8_t element=0; };
class Overlay {
public:
    static constexpr std::uint64_t kDurationMs=900;
    void start(std::uint64_t now,std::uint32_t encounter,Effect effect=Effect::CriticalBoost,std::uint8_t pending=0) {effect_=effect;element_=pending>=5?pending-4:0;start_=now;encounter_=encounter;active_=true;}
    void cancel() {active_=false;}
    OverlayView view(std::uint64_t now,const State& state,bool showingBattle=false) const {
        if(!active_ || (state.phase!=Phase::Encounter && !showingBattle) || state.encounters!=encounter_ || now<start_ || now-start_>=kDurationMs) return {};
        return {true,static_cast<std::uint16_t>((now-start_)*1000/kDurationMs),effect_,element_};
    }
private:
    Effect effect_=Effect::CriticalBoost;
    std::uint8_t element_=0;
    std::uint64_t start_=0;
    std::uint32_t encounter_=0;
    bool active_=false;
};
static_assert(sizeof(ReaderLatch)+sizeof(Overlay)<=80,"Digi-Modify timing state must remain bounded");
} // namespace digivice::digimodify
