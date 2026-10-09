#include "trade_session.hpp"

namespace digivice::devicetrade {
namespace {
bool terminal(trade::Phase p){return p==trade::Phase::Applied || p==trade::Phase::Aborted;}
State beforeCommit(const trade::Record& r){
    auto s=r.before;s.sequence=r.after.sequence-1;s.explorationSteps=r.after.explorationSteps;
    s.encounterRng=r.after.encounterRng;s.encounterTarget=r.after.encounterTarget;s.encounterProgress=r.after.encounterProgress;
    s.pendingEncounter=r.after.pendingEncounter;return s;
}
}
bool Session::fault(const char* reason){healthy_=false;diagnostic_=reason;return false;}
bool Session::blocksForeground()const{
    const auto* r=record();return !healthy_ || (r&&!terminal(r->phase));
}
bool Session::freezesGameWrites()const{
    const auto* r=record();return !healthy_ || (r&&r->phase==trade::Phase::Committed);
}
bool Session::permitsBackground()const{
    return !freezesGameWrites() && (!blocksForeground() || state_.sequence<UINT32_MAX-1);
}
bool Session::careMatches(const trade::Record& r)const{
    if(!isValid(state_))return false;
    if(r.phase==trade::Phase::Prepared)return trade::backgroundOnly(r.before,state_);
    if(r.phase==trade::Phase::Committed){
        if(trade::sameState(state_,r.after))return true;
        const auto before=beforeCommit(r);
        return trade::backgroundOnly(r.before,state_) && trade::backgroundOnly(state_,before);
    }
    if(r.phase==trade::Phase::Applied)return state_.receivedTrades==r.after.receivedTrades &&
        state_.sequence>=r.after.sequence && state_.nextMemberId>=r.after.nextMemberId &&
        !findMember(state_,r.transcript.offers[r.localSide].id);
    return state_.receivedTrades==r.before.receivedTrades && state_.sequence>=r.before.sequence;
}
bool Session::restore(){
    healthy_=false;const auto boot=journal_.restore();
    if(boot==Boot::RecoveryRequired)return fault(journal_.diagnostic());
    if(!saves_.writable())return fault("care storage requires recovery; trade records preserved");
    const auto* r=record();
    if(!r){
        if(state_.receivedTrades)return fault("trade receipt missing for existing acquisitions; ownership recovery required");
        healthy_=true;diagnostic_="no pending trade";return true;
    }
    if(!careMatches(*r))return fault("care state conflicts with durable trade receipt; no rollback or new game");
    // Before any Prepared packet, both care slots use the trade-aware schema.
    // This also prevents a normal firmware downgrade loading a pre-escrow save.
    if(r->phase!=trade::Phase::Committed && !saves_.checkpointMirrored(state_))return fault(saves_.diagnostic());
    if(boot==Boot::NeedsMirror && !journal_.repairMirror())return fault(journal_.diagnostic());
    healthy_=true;
    if(r->phase==trade::Phase::Committed)return applyCommitted();
    diagnostic_=r->phase==trade::Phase::Prepared?"trade locked; reconnect the same peer to finish":"terminal trade receipt retained";
    return true;
}
bool Session::mayReplace(bool priorPeerTerminal)const{
    const auto* r=record();
    return healthy_ && journal_.writable() && journal_.mirrored() && (!r || (terminal(r->phase)&&priorPeerTerminal));
}
bool Session::prepare(const trade::Transcript& t,unsigned side,bool priorPeerTerminal){
    const auto* r=record();
    if(r&&trade::sameTranscript(r->transcript,t)&&r->localSide==side)return healthy_&&r->phase==trade::Phase::Prepared;
    // Empty Store has no mirrors yet. A first transaction is allowed only after
    // a successful restore and an unchanged, writable care baseline.
    if(!healthy_ || (r&&!mayReplace(priorPeerTerminal)) || !saves_.writable() || (r&&r->serial==UINT32_MAX))return false;
    if(!trade::prepare(state_,t,side,r?r->serial+1:1,candidate_))return false;
    if(!saves_.checkpointMirrored(state_))return fault(saves_.diagnostic());
    if(!journal_.checkpoint(candidate_))return fault(journal_.diagnostic());
    diagnostic_="trade prepared; reconnect the same peer to finish before playing";return true;
}
bool Session::commit(){
    const auto* r=record();if(!healthy_||!r)return false;
    if(r->phase==trade::Phase::Committed||r->phase==trade::Phase::Applied)return true;
    if(!trade::commit(*r,state_,candidate_))return false;
    if(!journal_.checkpoint(candidate_))return fault(journal_.diagnostic());
    diagnostic_="trade decision durable; applying ownership";return true;
}
bool Session::applyCommitted(){
    const auto* r=record();if(!healthy_||!r)return false;
    if(r->phase==trade::Phase::Applied)return true;
    if(r->phase!=trade::Phase::Committed||!careMatches(*r))return fault("care state cannot apply the durable trade decision");
    if(!saves_.checkpointMirrored(r->after))return fault(saves_.diagnostic());
    // Publish only after both care slots contain the same ownership result.
    state_=r->after;
    if(!trade::applied(*r,state_,candidate_))return fault("trade application receipt is invalid");
    if(!journal_.checkpoint(candidate_))return fault(journal_.diagnostic());
    diagnostic_="trade applied; completion receipt retained";return true;
}
bool Session::abort(const trade::Transcript& t,unsigned side,bool priorPeerTerminal){
    const auto* r=record();if(!healthy_||!saves_.writable())return false;
    if(r&&trade::sameTranscript(r->transcript,t)&&r->localSide==side){
        if(r->phase==trade::Phase::Aborted)return true;
        if(!trade::abort(*r,candidate_))return false;
    }else{
        if(side || (r&&!mayReplace(priorPeerTerminal)) || (r&&r->serial==UINT32_MAX) ||
           !trade::abortUnprepared(state_,t,side,r?r->serial+1:1,candidate_))return false;
        if(!saves_.checkpointMirrored(state_))return fault(saves_.diagnostic());
    }
    if(!journal_.checkpoint(candidate_))return fault(journal_.diagnostic());
    // Abort never restores the old before snapshot over newer walking state.
    diagnostic_="trade cancelled; original partners retained";return true;
}
} // namespace digivice::devicetrade
