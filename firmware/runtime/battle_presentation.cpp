#include "battle_presentation.hpp"
#include "combat.hpp"
#include "forms.hpp"
#include "legacy_combat_v7.hpp"
#include "legacy_combat_v8.hpp"
#include "legacy_combat_v9.hpp"
#include <algorithm>

namespace digivice::battlepresentation {
namespace {
using Move = autobattle::Move;
using Outcome = autobattle::Outcome;
combat::Profile profile(std::uint32_t form, std::uint32_t level, std::uint32_t rules) {
    if(rules==7) { const auto p=legacy_v7::combat::formProfile(form,level); return {p.name,p.type,p.physicalSkill,p.heavySkill,p.magicSkill,{p.stats.maxHp,p.stats.attack,p.stats.defense,p.stats.magic,p.stats.resistance}}; }
    if(rules==8) { const auto p=legacy_v8::combat::formProfile(form,level); return {p.name,p.type,p.physicalSkill,p.heavySkill,p.magicSkill,{p.stats.maxHp,p.stats.attack,p.stats.defense,p.stats.magic,p.stats.resistance}}; }
    if(rules==9) { const auto p=legacy_v9::combat::formProfile(form,level); return {p.name,p.type,p.physicalSkill,p.heavySkill,p.magicSkill,{p.stats.maxHp,p.stats.attack,p.stats.defense,p.stats.magic,p.stats.resistance}}; }
    return combat::formProfile(form,level);
}
bool attack(Move move) { return move==Move::Physical || move==Move::Magic || move==Move::Heavy; }
const char* skill(const combat::Profile& p, Move move) {
    switch(move) {
    case Move::Physical:return p.physicalSkill;
    case Move::Heavy:return p.heavySkill;
    case Move::Magic:return p.magicSkill;
    case Move::Counter:return "Counter";
    case Move::Capture:return "Capture throw";
    default:return "";
    }
}
std::uint32_t loss(std::uint32_t before,std::uint32_t after) { return before>after ? before-after : 0; }
bool valid(const autobattle::Trace& t,bool tactical) {
    if(t.kind!=autobattle::Kind::Wild || !t.count || t.count>autobattle::kMaxTraceSteps ||
       t.startSequence==UINT32_MAX || t.endSequence!=t.startSequence+1 ||
       (t.combatRulesVersion!=4 && t.combatRulesVersion!=7 && t.combatRulesVersion!=8 && t.combatRulesVersion!=9 && t.combatRulesVersion!=12) ||
       (t.outcome!=Outcome::None && t.outcome!=Outcome::Won && t.outcome!=Outcome::Captured && t.outcome!=Outcome::Retreated)) return false;
    if(!combat::validCareBonus({t.playerOffenseBonus,t.playerProtectionBonus}) ||
       !combat::validCareBonus({t.enemyOffenseBonus,t.enemyProtectionBonus}) ||
       (t.combatRulesVersion!=12 && (t.playerOffenseBonus || t.playerProtectionBonus || t.enemyOffenseBonus || t.enemyProtectionBonus))) return false;
    const auto p=profile(t.playerFormId,t.playerLevel,t.combatRulesVersion);
    const auto e=profile(t.enemyFormId,t.enemyLevel,t.combatRulesVersion);
    if(!p.stats.maxHp || !e.stats.maxHp || p.stats.maxHp>2048 || e.stats.maxHp>2048) return false;
    for(std::size_t i=0;i<t.count;++i) {
        const auto& s=t.steps[i];
        if(!tactical && t.outcome==Outcome::None && !attack(s.action)) return false;
        if(s.action==Move::Capture && (t.combatRulesVersion==12 || s.captureAttempt)) {
            const auto result=static_cast<CaptureResult>(s.captureResult);
            if(s.captureAttempt<1 || s.captureAttempt>3 || result==CaptureResult::None ||
               s.captureResult>static_cast<std::uint8_t>(CaptureResult::Captured) ||
               (result==CaptureResult::Miss ? s.captureChance!=0 : s.captureChance<(tactical?1u:10u) || s.captureChance>90) ||
               s.captured!=(result==CaptureResult::Captured) || s.reflected || s.opponentAction!=Move::None ||
               s.playerHpAfter!=s.playerHpBefore || s.enemyHpAfter!=s.enemyHpBefore) return false;
            std::uint8_t previous=0;
            for(std::size_t j=0;j<i;++j) if(t.steps[j].action==Move::Capture) previous=t.steps[j].captureAttempt;
            if((previous && s.captureAttempt!=previous+1) || (!tactical && !previous && s.captureAttempt!=1)) return false;
        } else if(s.captureChance || s.captureAttempt || s.captureResult) return false;
        if(s.defending || (!attack(s.action) && s.action!=Move::Capture) ||
           (s.opponentAction!=Move::None && !attack(s.opponentAction) && s.opponentAction!=Move::Counter) ||
           (s.guard!=Move::None && s.guard!=Move::Brace && s.guard!=Move::Ward && s.guard!=Move::Counter) ||
           !s.playerHpBefore || !s.enemyHpBefore || s.playerHpBefore>p.stats.maxHp || s.enemyHpBefore>e.stats.maxHp ||
           s.playerHpAfter>s.playerHpBefore || s.enemyHpAfter>s.enemyHpBefore ||
           (s.opponentAction==Move::None && s.playerHpAfter!=s.playerHpBefore) ||
           ((s.captured || !s.enemyHpAfter) && s.opponentAction!=Move::None) ||
           (s.captured && (s.action!=Move::Capture || !s.enemyHpAfter)) ||
           (s.reflected && (s.action!=Move::Heavy || s.guard!=Move::Counter || s.opponentAction!=Move::Counter || s.enemyHpAfter!=s.enemyHpBefore)) ||
           (!s.reflected && s.opponentAction==Move::Counter) ||
           (i && (t.steps[i-1].captured || s.playerHpBefore!=t.steps[i-1].playerHpAfter || s.enemyHpBefore!=t.steps[i-1].enemyHpAfter))) return false;
    }
    const auto& last=t.steps[t.count-1];
    if(!tactical && t.outcome==Outcome::None && last.enemyHpAfter>e.stats.maxHp/2) return false;
    // A calm third miss ends the fight only when that throw is the trace's outcome.
    // Rules 16 keeps the same battle going, so later attacks may follow the throw.
    const bool captureEnded=last.action==Move::Capture && last.captureAttempt==3 && !last.captured &&
        t.outcome==Outcome::Retreated && last.playerHpAfter==last.playerHpBefore;
    return (last.captured==(t.outcome==Outcome::Captured)) && (t.outcome!=Outcome::Won || !last.enemyHpAfter) &&
           (!captureEnded || t.outcome==Outcome::Retreated) &&
           (t.outcome!=Outcome::Retreated || (!last.playerHpAfter || captureEnded || (!tactical && t.count==autobattle::kMaxTraceSteps))) &&
           (t.outcome!=Outcome::None || (last.playerHpAfter && last.enemyHpAfter && !last.captured));
}
}
bool Sequencer::startAuto(const autobattle::Trace& t,std::uint64_t now) { return begin(t,now,false,false); }
bool Sequencer::begin(const autobattle::Trace& t,std::uint64_t now,bool tactical,bool aimMiss) {
    if(locked() || !valid(t,tactical)) return false;
    trace_=t; view_={}; cue_=Cue::None; index_=0; aimMiss_=aimMiss;
    view_.playerFormId=t.playerFormId; view_.enemyFormId=t.enemyFormId;
    view_.playerMaxHp=profile(t.playerFormId,t.playerLevel,t.combatRulesVersion).stats.maxHp;
    view_.enemyMaxHp=profile(t.enemyFormId,t.enemyLevel,t.combatRulesVersion).stats.maxHp;
    view_.playerHp=t.steps[0].playerHpBefore; view_.enemyHp=t.steps[0].enemyHpBefore;
    view_.outcome=t.outcome; view_.turnCount=static_cast<std::uint32_t>(t.count); view_.locked=true;
    lastAt_=now; enter(Phase::Player,now); return true;
}
bool Sequencer::startTactical(const State& before,const State& after,Action action,std::uint32_t value,std::uint64_t now) {
    if(locked() || !isValid(before) || !isValid(after) || before.phase!=digivice::Phase::Encounter ||
       (before.battleMode!=BattleMode::Tactical && !(before.battleMode==BattleMode::Auto &&
        before.autoCapture==AutoCapture::Awaiting && (action==Action::Flick || action==Action::RingCapture))) || before.sequence==UINT32_MAX || after.sequence!=before.sequence+1 ||
       before.activeCreatureId!=after.activeCreatureId ||
       (action!=Action::Attack && action!=Action::Heavy && action!=Action::Magic && action!=Action::Capture && action!=Action::Flick && action!=Action::RingCapture)) return false;
    const auto* member=activeMember(before);
    if(!member) return false;
    autobattle::Trace t{};
    t.startSequence=before.sequence; t.endSequence=after.sequence;
    t.combatRulesVersion=before.wildRules>=12?12:before.wildRules<8?7:before.wildRules==8?8:before.wildRules==9?9:4;
    if(t.combatRulesVersion==12) { const auto care=memberCare(*member); t.playerOffenseBonus=care.offense; t.playerProtectionBonus=care.protection; }
    t.playerFormId=member->formId; t.playerLevel=member->level; t.playerSpecies=static_cast<std::uint32_t>(member->species);
    t.enemyFormId=before.wildFormId; t.enemyLevel=before.wildLevel; t.enemySpecies=static_cast<std::uint32_t>(before.wildSpecies);
    t.includeFormIds=true; t.count=1;
    auto& s=t.steps[0];
    s.action=action==Action::Heavy?Move::Heavy:action==Action::Magic?Move::Magic:action==Action::Attack?Move::Physical:Move::Capture;
    const auto guard=wildGuard(before);
    s.guard=guard==combat::Defense::Brace?Move::Brace:guard==combat::Defense::Ward?Move::Ward:guard==combat::Defense::Counter?Move::Counter:Move::None;
    s.playerHpBefore=before.hp; s.enemyHpBefore=before.wildHp;
    s.captured=after.collectionCount>before.collectionCount;
    s.reflected=action==Action::Heavy && guard==combat::Defense::Counter;
    const bool terminal=after.phase==digivice::Phase::Home;
    if(!terminal && (after.phase!=digivice::Phase::Encounter || after.wildFormId!=before.wildFormId || after.encounters!=before.encounters)) return false;
    const bool calmCapture=action==Action::RingCapture || t.combatRulesVersion==12 || before.autoCapture==AutoCapture::Awaiting;
    const bool captureEnded=calmCapture && s.action==Move::Capture && after.message==Message::CaptureEnded;
    if(terminal && !s.captured && after.message!=Message::Won && after.message!=Message::Trained && after.message!=Message::Retreated && !captureEnded) return false;
    t.outcome=!terminal?Outcome::None:s.captured?Outcome::Captured:after.message==Message::Retreated || captureEnded?Outcome::Retreated:Outcome::Won;
    s.playerHpAfter=!terminal?after.hp:t.outcome==Outcome::Retreated && !captureEnded?0:before.hp;
    s.enemyHpAfter=!terminal?after.wildHp:t.outcome==Outcome::Won?0:before.wildHp;
    // The terminal save clears the foe and applies gentle retreat healing. Only
    // here reconstruct outgoing HP with the existing immutable combat resolver.
    if(t.outcome==Outcome::Retreated && attack(s.action) && !s.reflected) {
        const auto move=action==Action::Magic?combat::Move::Magic:action==Action::Heavy?combat::Move::Heavy:combat::Move::Physical;
        std::uint32_t damage=0;
        if(before.wildRules<8) damage=legacy_v7::combat::resolveForms(member->formId,member->level,before.wildFormId,before.wildLevel,static_cast<legacy_v7::combat::Move>(move),static_cast<legacy_v7::combat::Defense>(guard)).damage;
        else if(before.wildRules==8) damage=legacy_v8::combat::resolveForms(member->formId,member->level,before.wildFormId,before.wildLevel,static_cast<legacy_v8::combat::Move>(move),static_cast<legacy_v8::combat::Defense>(guard)).damage;
        else if(before.wildRules>=12) damage=combat::resolveCareForms(member->formId,member->level,before.wildFormId,before.wildLevel,move,guard,memberCare(*member),{}).damage;
        else damage=combat::resolveForms(member->formId,member->level,before.wildFormId,before.wildLevel,move,guard).damage;
        damage+=before.attackBoost;
        s.enemyHpAfter=damage>=before.wildHp?0:before.wildHp-damage;
    }
    s.opponentAction=s.captured || !s.enemyHpAfter?Move::None:s.reflected?Move::Counter:before.wildTurn%2?Move::Magic:Move::Physical;
    bool aimMiss=false;
    if(action==Action::Flick) { FlickTrajectory trajectory; if(!decodeFlick(value,trajectory)) return false; aimMiss=!trajectory.hit; }
    if(calmCapture && s.action==Move::Capture) {
        const auto& record=after.lastCapture;
        const auto chance=action==Action::RingCapture ? ringCaptureChance(before,value) : aimMiss?0:captureChance(before);
        if(record.sequence!=after.sequence || record.targetFormId!=before.wildFormId || record.targetLevel!=before.wildLevel ||
           record.attempt!=before.captureAttempts+1 || (record.result==CaptureResult::Miss)!=aimMiss ||
           record.chance!=chance) return false;
        s.captureChance=record.chance; s.captureAttempt=record.attempt; s.captureResult=static_cast<std::uint8_t>(record.result);
        s.opponentAction=Move::None;
    }
    return begin(t,now,true,aimMiss);
}
bool Sequencer::startSavedCapture(const State& state,std::uint64_t now) {
    if(locked() || !isValid(state)) return false;
    const auto& record=state.lastCapture;
    const auto* member=activeMember(state);
    if(!member || !forms::productionForm(record.targetFormId) || !record.sequence ||
       record.sequence!=state.foregroundSequence || record.result==CaptureResult::None) return false;
    const bool caught=record.result==CaptureResult::Captured;
    const bool ended=!caught && record.attempt==3 && state.phase==digivice::Phase::Home &&
        state.message==Message::CaptureEnded;
    if(caught) {
        if(state.phase!=digivice::Phase::Home || (state.message!=Message::Captured && state.message!=Message::Trained)) return false;
        bool present=false;
        for(std::size_t i=0;i<state.collectionCount;++i) {
            const auto& captured=state.collection[i];
            present=present || (captured.capturedAtSequence==record.sequence && captured.formId==record.targetFormId && captured.level==record.targetLevel);
        }
        if(!present) return false;
    } else if(ended) {
        if(state.phase!=digivice::Phase::Home || state.message!=Message::CaptureEnded) return false;
    } else if(state.phase!=digivice::Phase::Encounter || state.message!=Message::CaptureMissed ||
              state.captureAttempts!=record.attempt || state.wildFormId!=record.targetFormId || state.wildLevel!=record.targetLevel) return false;
    autobattle::Trace t{};
    t.startSequence=record.sequence-1; t.endSequence=record.sequence;
    t.combatRulesVersion=state.phase==digivice::Phase::Encounter && state.wildRules<12 ?
        state.wildRules<8?7:state.wildRules==8?8:state.wildRules==9?9:4 : 12;
    t.playerFormId=member->formId; t.playerLevel=member->level;
    t.enemyFormId=record.targetFormId; t.enemyLevel=record.targetLevel;
    t.outcome=caught?Outcome::Captured:ended?Outcome::Retreated:Outcome::None; t.count=1;
    auto& s=t.steps[0]; s.action=Move::Capture; s.captured=caught;
    s.playerHpBefore=s.playerHpAfter=state.hp;
    // Terminal records intentionally do not contain old combat HP. The trace
    // uses the profile only for validation; the visible unknown target HP is0.
    s.enemyHpBefore=s.enemyHpAfter=state.phase==digivice::Phase::Encounter?state.wildHp:combat::formProfile(record.targetFormId,record.targetLevel).stats.maxHp;
    s.captureChance=record.chance; s.captureAttempt=record.attempt; s.captureResult=static_cast<std::uint8_t>(record.result);
    if(!begin(t,now,true,record.result==CaptureResult::Miss)) return false;
    if(state.phase!=digivice::Phase::Encounter) view_.enemyHp=0;
    return true;
}
void Sequencer::emit(Cue cue) { if(cue_==Cue::None) cue_=cue; }
Cue Sequencer::consumeCue() { const auto result=cue_; cue_=Cue::None; return result; }
void Sequencer::enter(Phase phase,std::uint64_t now) {
    enteredAt_=now; impacted_=false;
    view_.phase=phase; view_.flash=false; view_.progressPermille=0;
    view_.turn=index_+1; view_.damage=0; view_.captured=false; view_.aimMiss=false;
    view_.capturePresentation=false; view_.captureMiss=false; view_.captureCaught=false;
    view_.captureAttempt=0; view_.captureRemaining=0; view_.captureChance=0; view_.captureElapsedMs=0;
    const auto& s=trace_.steps[index_]; view_.guard=s.guard; view_.reflected=s.reflected;
    if(phase==Phase::Summary) {
        view_.captured=view_.outcome==Outcome::Captured; view_.reflected=false;
        view_.actor=Actor::None; view_.actorName=""; view_.move=Move::None;
        view_.moveName=view_.outcome==Outcome::Won?"Victory":view_.outcome==Outcome::Captured?"Captured":view_.outcome==Outcome::Retreated?"Retreated":"Your turn";
        if(view_.outcome==Outcome::Won) emit(Cue::Win);
        else if(view_.outcome==Outcome::Retreated) emit(Cue::Retreat);
        return;
    }
    const bool player=phase==Phase::Player;
    view_.actor=player?Actor::Player:Actor::Opponent;
    const auto p=profile(player?trace_.playerFormId:trace_.enemyFormId,player?trace_.playerLevel:trace_.enemyLevel,trace_.combatRulesVersion);
    view_.actorName=p.name; view_.move=player?s.action:s.opponentAction; view_.moveName=skill(p,view_.move);
    view_.aimMiss=player && aimMiss_;
    view_.damage=player?loss(s.enemyHpBefore,s.enemyHpAfter):loss(s.playerHpBefore,s.playerHpAfter);
    if(player && s.captureAttempt && s.action==Move::Capture) {
        view_.capturePresentation=true; view_.captureAttempt=s.captureAttempt;
        view_.captureRemaining=static_cast<std::uint8_t>(3-s.captureAttempt);
        captureStage_=0; impactAt_=now;
    }
    emit(view_.move==Move::Capture?Cue::CaptureThrow:view_.move==Move::Magic?Cue::Magic:Cue::Attack);
}
void Sequencer::pollCapture(std::uint64_t now) {
    const auto& s=trace_.steps[index_];
    const bool miss=s.captureResult==static_cast<std::uint8_t>(CaptureResult::Miss);
    auto elapsed=now-impactAt_;
    if(captureStage_==0 && elapsed>=kCaptureThrowMs) {
        captureStage_=miss?2:1; impactAt_=now; elapsed=0;
        view_.captureChance=static_cast<std::uint8_t>(s.captureChance);
        if(miss) { view_.captureMiss=true; view_.moveName="Missed"; emit(Cue::CaptureFail); }
    } else if(captureStage_==1 && elapsed>=kCaptureWiggleMs) {
        captureStage_=2; impactAt_=now; elapsed=0;
        view_.captureCaught=s.captured; view_.captured=s.captured;
        view_.moveName=s.captured?"Captured":s.captureAttempt==3?"Wild moved on":"Broke free";
        emit(s.captured?Cue::CaptureSuccess:Cue::CaptureFail);
    } else if(captureStage_==2 && elapsed>=kCaptureResultMs) {
        if(static_cast<std::size_t>(index_)+1<trace_.count) { ++index_; enter(Phase::Player,now); }
        else {
            view_.captureElapsedMs=static_cast<std::uint32_t>(kCaptureThrowMs+(miss?0:kCaptureWiggleMs)+kCaptureResultMs);
            view_.progressPermille=1000; view_.phase=Phase::Summary; view_.actor=Actor::None;
            view_.locked=false; view_.flash=false; cue_=Cue::None;
        }
        return;
    }
    const auto offset=captureStage_==0?0:kCaptureThrowMs+(captureStage_==2 && !miss?kCaptureWiggleMs:0);
    const auto duration=captureStage_==0?kCaptureThrowMs:captureStage_==1?kCaptureWiggleMs:kCaptureResultMs;
    view_.captureElapsedMs=static_cast<std::uint32_t>(offset+std::min(elapsed,duration));
    const auto total=kCaptureThrowMs+(miss?0:kCaptureWiggleMs)+kCaptureResultMs;
    view_.progressPermille=static_cast<std::uint32_t>(view_.captureElapsedMs*1000/total);
    view_.flash=captureStage_==2 && elapsed<180;
}
void Sequencer::impact(std::uint64_t now) {
    impacted_=true; impactAt_=now;
    const auto& s=trace_.steps[index_];
    if(view_.phase==Phase::Player) {
        view_.enemyHp=s.enemyHpAfter; view_.captured=s.captured;
        if(s.action==Move::Capture) emit(s.captured?Cue::CaptureSuccess:Cue::CaptureFail);
        else if(view_.damage) emit(Cue::Hit);
    } else { view_.playerHp=s.playerHpAfter; if(view_.damage) emit(Cue::Hit); }
}
const View& Sequencer::poll(std::uint64_t now) {
    if(!locked() || now<lastAt_) return view_;
    lastAt_=now;
    if(view_.paused) return view_;
    if(view_.capturePresentation) { pollCapture(now); return view_; }
    const auto elapsed=now-enteredAt_;
    if(view_.phase==Phase::Summary) {
        view_.progressPermille=static_cast<std::uint32_t>(std::min(elapsed,kSummaryMs)*1000/kSummaryMs);
        if(elapsed>=kSummaryMs) { view_.locked=false; cue_=Cue::None; }
        return view_;
    }
    if(!impacted_ && elapsed>=kImpactMs) impact(now);
    else if(impacted_ && now-impactAt_>=kActorMs-kImpactMs) {
        const auto& s=trace_.steps[index_];
        if(view_.phase==Phase::Player && s.opponentAction!=Move::None) enter(Phase::Opponent,now);
        else if(static_cast<std::size_t>(index_)+1<trace_.count) { ++index_; enter(Phase::Player,now); }
        else enter(Phase::Summary,now);
        return view_;
    }
    view_.progressPermille=static_cast<std::uint32_t>(std::min(elapsed,kActorMs)*1000/kActorMs);
    view_.flash=impacted_ && now-impactAt_<180 && (view_.damage || view_.move==Move::Capture);
    return view_;
}
void Sequencer::pause(bool paused,std::uint64_t now) {
    if(!locked() || now<lastAt_ || paused==view_.paused) return;
    lastAt_=now;
    if(paused) { pausedAt_=now; view_.paused=true; view_.flash=false; cue_=Cue::None; }
    else {
        const auto duration=now-pausedAt_; enteredAt_+=duration;
        if(impacted_ || view_.capturePresentation) impactAt_+=duration;
        view_.paused=false;
    }
}
void Sequencer::cancel() { view_={}; cue_=Cue::None; }
} // namespace digivice::battlepresentation
