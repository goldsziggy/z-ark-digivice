// Host-only bounded playthrough of every graph root/branch using public actions.
#include "game.hpp"
#include "forms.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

namespace {
using namespace digivice;
constexpr unsigned kBattleCap = 1000, kActionCap = 30000, kCaseCap = 4096;
struct Counts { unsigned actions=0, care=0, battles=0, turns=0, wins=0, captures=0, retreats=0, releases=0; };
unsigned failures=0, cases=0, roots=0, leaves=0, careOnly=0;
bool first=true;
bool act(State& s, Action action, unsigned value, Counts& n) {
    if (++n.actions > kActionCap) return false;
    if (apply(s,action,value)!=Error::None) return false;
    if(action==Action::Feed || action==Action::Play || action==Action::Rest) ++n.care;
    return true;
}
State capturedFixture(unsigned id,unsigned seed) {
    const auto& f=*forms::find(id);
    auto s=newGame(seed); s.sequence=2; s.steps=100; s.encounters=s.captures=1;
    s.nextMemberId=3; s.collectionCount=2; s.activeCreatureId=2;
    s.collection[1]={2,static_cast<Species>(f.lineage),forms::stats(id,f.minLevel).maxHp,
        80,70,80,f.minBond,f.minLevel,1,xpForLevel(f.minLevel),id};
    const auto& m=s.collection[1]; s.hp=m.hp;s.energy=m.energy;s.fullness=m.fullness;s.mood=m.mood;s.bond=m.bond;s.level=m.level;
    s.journal[(id-1)/32] |= std::uint32_t(1)<<((id-1)%32);
    if(!isValid(s)) {std::fprintf(stderr,"Invalid captured fixture %u\n",id);std::exit(2);}
    return s;
}
bool fight(State& s, bool automatic, Counts& n) {
    if (++n.battles > kBattleCap || !act(s,Action::Walk,100,n) || s.phase!=Phase::Encounter) return false;
    const auto beforeXp=activeMember(s)->xp, beforeCaptures=s.captures;
    if(automatic) {
        autobattle::Trace trace;
        if(++n.actions>kActionCap || applyAuto(s,&trace)!=Error::None) return false;
        n.turns+=static_cast<unsigned>(trace.count);
    } else {
        if(!act(s,Action::Card,2,n)) return false; // Ordinary simulated Shelter card.
        for(unsigned turn=0;s.phase==Phase::Encounter && turn<256;++turn) {
            Action chosen=Action::Attack;
            if(s.wildHp<=s.wildMaxHp/2 && s.captureAttempts<3 && s.collectionCount<kCollectionCapacity) chosen=Action::Capture;
            else {
                unsigned best=0;
                constexpr Action actions[]{Action::Attack,Action::Magic,Action::Heavy};
                constexpr combat::Move moves[]{combat::Move::Physical,combat::Move::Magic,combat::Move::Heavy};
                for(unsigned i=0;i<(s.energy>=6?3u:2u);++i) {
                    const auto hit=combat::resolveForms(activeMember(s)->formId,s.level,s.wildFormId,s.wildLevel,moves[i],wildGuard(s));
                    if(!hit.reflected && hit.damage>best) {best=hit.damage;chosen=actions[i];}
                }
            }
            if(!act(s,chosen,0,n)) return false;
            ++n.turns;
        }
    }
    if(s.phase!=Phase::Home) return false;
    if(s.captures>beforeCaptures) ++n.captures;
    else if(activeMember(s)->xp>beforeXp) ++n.wins;
    else ++n.retreats;
    // Keep the selected member and founder; captured extras leave through the
    // real release action. This prevents a full roster hiding capture rewards.
    while(s.collectionCount>2) {
        const auto id=s.collection[s.collectionCount-1].id;
        if(!act(s,Action::Release,id,n)) return false;
        ++n.releases;
    }
    return true;
}
bool reach(State& s,const forms::EvolutionEdge& e,bool automatic,Counts& n) {
    while(s.level<e.minLevel || s.bond<e.minBond) {
        if(n.actions>=kActionCap) return false;
        if(s.level<e.minLevel) {
            while(s.hp<forms::stats(activeMember(s)->formId,s.level).maxHp || s.energy<100)
                if(!act(s,Action::Rest,0,n)) return false;
            if(!fight(s,automatic,n)) return false;
        } else {
            // Bond-only gates need no battle XP. Play still spends energy at
            // full mood; a subsequent useful Rest continues earning bond.
            const auto action=s.fullness<100?Action::Feed:s.mood<100 && s.energy>=5?Action::Play:
                s.energy<100?Action::Rest:Action::Play;
            if(!act(s,action,0,n)) return false;
        }
    }
    return true;
}
void visit(const State& state,unsigned root,bool automatic,unsigned seed,unsigned depth,std::vector<unsigned>& path) {
    if(depth>forms::kFormCount || cases>=kCaseCap) {++failures;return;}
    const auto source=activeMember(state)->formId;
    if(!forms::outgoing(source,0)) {++leaves;return;}
    for(unsigned branch=0;branch<2;++branch) {
        const auto* e=forms::outgoing(source,branch); if(!e) break;
        auto next=state; Counts n; const auto xp=activeMember(next)->xp, bond=next.bond, level=next.level;
        bool ok=reach(next,*e,automatic,n);
        const auto gateXp=activeMember(next)->xp, gateBond=next.bond, gateLevel=next.level;
        const auto before=next;
        if(ok) ok=act(next,Action::Evolve,e->to,n);
        if(ok) {
            const auto* member=activeMember(next); Snapshot snapshot; State restored;
            ok=member && member->id==before.activeCreatureId && member->formId==e->to &&
                member->xp==activeMember(before)->xp && next.bond==before.bond && next.energy==before.energy &&
                next.fullness==before.fullness && next.mood==before.mood && hasObtained(next,e->to) &&
                encodeSnapshot(next,snapshot) && decodeSnapshot(snapshot.bytes,sizeof snapshot.bytes,restored)==SnapshotStatus::Ok;
            Snapshot again; ok=ok && encodeSnapshot(restored,again);
            for(std::size_t i=0;ok && i<sizeof snapshot.bytes;++i) ok=snapshot.bytes[i]==again.bytes[i];
        }
        ++cases; if(!ok) ++failures;
        if(ok && !n.battles && xp==gateXp) ++careOnly;
        std::printf("%s{\"root\":%u,\"seed\":%u,\"mode\":\"%s\",\"from\":%u,\"to\":%u,\"path\":[",first?"":",\n",root,seed,automatic?"auto":"tactical",source,unsigned(e->to));first=false;
        for(std::size_t i=0;i<path.size();++i) std::printf("%s%u",i?",":"",path[i]);
        std::printf("],\"requiredLevel\":%u,\"requiredBond\":%u,\"startLevel\":%u,\"startXp\":%u,\"startBond\":%u,\"gateLevel\":%u,\"gateXp\":%u,\"gateBond\":%u,\"careActions\":%u,\"battles\":%u,\"combatTurns\":%u,\"wins\":%u,\"captures\":%u,\"retreats\":%u,\"releases\":%u,\"totalActions\":%u,\"result\":\"%s\"}",
            unsigned(e->minLevel),unsigned(e->minBond),level,xp,bond,gateLevel,gateXp,gateBond,n.care,n.battles,n.turns,n.wins,n.captures,n.retreats,n.releases,n.actions,ok?"PASS":"STALL_OR_FAILURE");
        if(ok) {path.push_back(e->to);visit(next,root,automatic,seed,depth+1,path);path.pop_back();}
    }
}
}
int main() {
    std::printf("{\"formatVersion\":1,\"rulesVersion\":%u,\"schemaVersion\":%u,\"edgeCount\":%zu,\"limits\":{\"battlesPerEdge\":%u,\"actionsPerEdge\":%u,\"totalCases\":%u},\"cases\":[\n",digivice::kRulesVersion,digivice::kSchemaVersion,digivice::forms::edgeCount(),kBattleCap,kActionCap,kCaseCap);
    for(unsigned id=1;id<=digivice::forms::kFormCount;++id) {
        bool parent=false;
        for(std::size_t i=0;i<digivice::forms::edgeCount();++i) if(digivice::forms::edgeAt(i)->to==id) parent=true;
        if(parent) continue;
        ++roots;
        for(const unsigned seed : {12345u,20261006u}) for(const bool automatic : {false,true}) {
            auto state=capturedFixture(id,seed); Counts ignored;
            if(automatic && !act(state,digivice::Action::Mode,1,ignored)) return 2;
            std::vector<unsigned> path{id}; visit(state,id,automatic,seed,0,path);
        }
    }
    std::printf("\n],\"summary\":{\"roots\":%u,\"edgePlaythroughs\":%u,\"terminalVisits\":%u,\"careOnlyTransitions\":%u,\"failures\":%u},\"result\":\"%s\"}\n",roots,cases,leaves,careOnly,failures,failures?"FAIL":"PASS");
    return failures?1:0;
}
