#include "expedition_party.hpp"
#include "expeditions.hpp"
#include "forms.hpp"
#include <algorithm>
#include <cstring>

namespace digivice::nearby {
namespace {
struct Candidate { std::uint32_t form = 0, level = 0, key = 0; int rough = 0, danger = 0; };
struct Pool { Candidate target[6]{}, gentle[2]{}; };
std::uint32_t mix(std::uint32_t value) {
    value ^= value >> 16; value *= 0x7feb352du; value ^= value >> 15;
    value *= 0x846ca68bu; return value ^ (value >> 16);
}
bool before(const Fighter& a, const Fighter& b) {
    const std::uint32_t x[]{a.formId,a.level,a.offenseBonus,a.protectionBonus};
    const std::uint32_t y[]{b.formId,b.level,b.offenseBonus,b.protectionBonus};
    for (unsigned i=0;i<4;++i) if (x[i]!=y[i]) return x[i]<y[i];
    return false;
}
int distance(int a, int b) { return a>b ? a-b : b-a; }
std::uint32_t floorDamage(std::uint32_t hp) { return std::clamp<std::uint32_t>((hp+23u)/24u,4u,32u); }
std::uint32_t power(std::uint32_t level) { return std::min<std::uint32_t>(16u,level/3u); }
unsigned group(const forms::Form& form, expeditions::Theme theme) {
    return !std::strcmp(form.type,expeditions::themeName(theme)) ? 0u : !std::strcmp(form.type,"neutral") ? 1u : 2u;
}
bool same(const Candidate& a,const Candidate& b) { return a.form==b.form && a.level==b.level; }
bool betterRough(const Candidate& a,const Candidate& b,bool gentle) {
    if (!a.form) return false;
    if (!b.form) return true;
    const int x=gentle ? a.danger : a.rough, y=gentle ? b.danger : b.rough;
    return x!=y ? x<y : a.key<b.key;
}
template <std::size_t N> void retain(Candidate (&list)[N],const Candidate& candidate,bool gentle) {
    // Keep several forms in each shortlist; a single species' many levels
    // must not crowd out other type/stat combinations.
    unsigned count=0,last=0;
    for (unsigned i=0;i<N;++i) {
        if (same(list[i],candidate)) return;
        if (list[i].form==candidate.form) { ++count; last=i; }
    }
    if (count>=2) {
        if (!betterRough(candidate,list[last],gentle)) return;
        list[last]=candidate;
    } else {
        if (!betterRough(candidate,list[N-1],gentle)) return;
        list[N-1]=candidate;
    }
    for (unsigned i=N-1;i>0;--i)
        if (betterRough(list[i],list[i-1],gentle)) std::swap(list[i],list[i-1]);
}
Candidate estimate(const Fighter (&players)[2],std::uint32_t scenario,
                   std::uint32_t form,std::uint32_t level,std::uint32_t key) {
    const auto tiers=expeditions::tierCount(scenario);
    const auto finalLevel=expeditions::tierLevel(level,tiers);
    const auto foeHp=combat::formProfile(form,finalLevel).stats.maxHp;
    unsigned totalHp=0;
    for (unsigned tier=1;tier<=tiers;++tier) totalHp+=combat::formProfile(form,expeditions::tierLevel(level,tier)).stats.maxHp;
    unsigned outgoing=0,incoming[2]{};
    const combat::Move moves[]{combat::Move::Physical,combat::Move::Magic};
    const combat::Defense guards[]{combat::Defense::Brace,combat::Defense::Ward,combat::Defense::Counter};
    for (unsigned i=0;i<2;++i) {
        const auto& p=players[i]; const combat::CareBonus care{p.offenseBonus,p.protectionBonus};
        for (const auto move:moves) {
            for (const auto guard:guards)
                outgoing+=combat::resolveCareForms(p.formId,p.level,form,finalLevel,move,guard,care,{},floorDamage(foeHp),power(p.level)).damage;
            // Screening only: expected random matching guard reduces a basic
            // reply by 1/6. Exact selection below plays every actual guard/RNG.
            incoming[i]+=combat::resolveCareForms(form,finalLevel,p.formId,p.level,move,combat::Defense::None,{},care,
                floorDamage(combat::formProfile(p.formId,p.level).stats.maxHp),power(finalLevel)).damage;
        }
    }
    const auto turns=std::min<std::uint32_t>(288u,(totalHp*12u+outgoing-1u)/outgoing);
    unsigned worstDamagePercent=0;
    for (unsigned i=0;i<2;++i) {
        const auto hp=combat::formProfile(players[i].formId,players[i].level).stats.maxHp;
        const auto bonus=100u+std::min<std::uint32_t>(40u,4u*finalLevel);
        // Each partner defends half the turns, mean of two enemy moves.
        const auto damagePercent=static_cast<unsigned>((static_cast<std::uint64_t>(incoming[i])*bonus*5u*turns)/(24u*hp));
        worstDamagePercent=std::max<std::uint32_t>(worstDamagePercent,damagePercent);
    }
    const int targetTurns=tiers==3 ? 27 : 12;
    const int danger=static_cast<int>(worstDamagePercent);
    return {form,level,key,distance(danger,65)*4+distance(static_cast<int>(turns),targetTurns)*8,danger};
}
bool evaluate(const Fighter (&players)[2],std::uint32_t scenario,const Candidate& candidate,
              CoopFoeDiagnostics& result,int& score) {
    result={}; result.minSurvivors=2; result.minHpPercent=100; result.minRounds=3*kMaxRaidRounds;
    unsigned minProgress=300;
    for (const auto planningSeed:kCoopPlanningSeeds) for (unsigned order=0;order<2;++order) {
        Raid raid;
        if (!beginRaid(players[order],players[1-order],raidSeed(candidate.form,candidate.level,scenario),raid)) return false;
        // Use common planning randomness for every candidate, independent of
        // the packed seed that will drive the real battle.
        raid.rng=planningSeed;
        while (raid.status==RaidStatus::Active && raid.sequence<3*kMaxRaidRounds) {
            if (!resolveAutoRaid(raid,raid.sequence)) return false;
        }
        if (raid.status==RaidStatus::Active) return false;
        result.rounds+=raid.sequence;
        result.clearTrials+=raid.status==RaidStatus::Cleared;
        result.minRounds=std::min<std::uint32_t>(result.minRounds,raid.sequence);
        result.maxRounds=std::max<std::uint32_t>(result.maxRounds,raid.sequence);
        result.minSurvivors=std::min<std::uint32_t>(result.minSurvivors,static_cast<unsigned>(!!raid.hp[0])+static_cast<unsigned>(!!raid.hp[1]));
        for (unsigned i=0;i<2;++i)
            result.minHpPercent=std::min<std::uint32_t>(result.minHpPercent,raid.hp[i]*100u/combat::formProfile(raid.players[i].formId,raid.players[i].level).stats.maxHp);
        minProgress=std::min<std::uint32_t>(minProgress,(raid.tier-1u)*100u+(raid.foeMax-raid.foeHp)*100u/raid.foeMax);
    }
    // More complete planning trajectories beat fewer, across both orders;
    // keeping both companions alive then beats a KO carry. Within that class,
    // prefer sustained pressure with roughly one-third HP left, not a trivial
    // win or the hardest possible loss. No species-specific exceptions.
    score=static_cast<int>(result.clearTrials)*100000 + static_cast<int>(result.minSurvivors)*10000;
    if (result.clearTrials==4)
        score+=2000-distance(static_cast<int>(result.minHpPercent),35)*20-
            distance(static_cast<int>(result.maxRounds),expeditions::tierCount(scenario)==3 ? 27 : 12)*8;
    else score+=static_cast<int>(minProgress)*10;
    score+=group(*forms::find(candidate.form),expeditions::kScenarios[scenario].theme)==0 ? 60 : 0;
    return true;
}
}
bool previewCoopExpeditionFoe(const Fighter& first,const Fighter& second,std::uint32_t scenario,
    std::uint32_t& formId,std::uint32_t& level,CoopFoeDiagnostics* diagnostics) {
    if (!validFighter(first)||!validFighter(second)||!expeditions::tierCount(scenario)) return false;
    Fighter players[]{first,second};
    if (before(players[1],players[0])) std::swap(players[0],players[1]);
    players[0].memberId=1; players[1].memberId=2;
    std::uint32_t key=mix(scenario+1u);
    for (const auto& p:players) for (const auto value:{p.formId,p.level,p.offenseBonus,p.protectionBonus}) key=mix(key^value);
    const auto low=std::min<std::uint32_t>(first.level,second.level), high=std::max<std::uint32_t>(first.level,second.level), mean=(low+high)/2;
    const std::uint32_t anchors[]{1u,std::max<std::uint32_t>(1u,low/2u),low,(low+mean)/2u,mean,(mean+high)/2u,high,std::min<std::uint32_t>(99u,high+3u)};
    Pool pools[3]{};
    for (unsigned id=1;id<=forms::kFormCount;++id) {
        if (!forms::productionForm(id)||!forms::hasInstalledArt(id)) continue;
        const auto* form=forms::find(id);
        auto& pool=pools[group(*form,expeditions::kScenarios[scenario].theme)];
        std::uint32_t previous=0;
        for (const auto anchor:anchors) {
            const auto candidateLevel=std::max<std::uint32_t>(form->minLevel,anchor);
            if (candidateLevel==previous) continue;
            previous=candidateLevel;
            const auto candidate=estimate(players,scenario,id,candidateLevel,mix(key^raidSeed(id,candidateLevel,scenario)));
            retain(pool.target,candidate,false); retain(pool.gentle,candidate,true);
        }
    }
    Candidate best; CoopFoeDiagnostics chosen;
    int bestScore=-1000000; unsigned candidates=0,rounds=0;
    for (const auto& pool:pools) {
        for (unsigned i=0;i<8;++i) {
            const auto& candidate=i<6 ? pool.target[i] : pool.gentle[i-6];
            if (!candidate.form) continue;
            bool duplicate=false;
            if (i>=6) for (const auto& target:pool.target) duplicate=duplicate||same(candidate,target);
            if (duplicate) continue;
            CoopFoeDiagnostics outcome; int score=0;
            ++candidates;
            if (!evaluate(players,scenario,candidate,outcome,score)) return false;
            rounds+=outcome.rounds;
            if (score>bestScore||(score==bestScore&&candidate.key<best.key)) {
                best=candidate; bestScore=score; chosen=outcome;
            }
        }
    }
    if (!best.form) return false;
    chosen.candidates=candidates; chosen.rounds=rounds;
    formId=best.form; level=best.level;
    if (diagnostics) *diagnostics=chosen;
    return true;
}
} // namespace digivice::nearby
