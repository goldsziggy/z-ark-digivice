#include "evolution_choice.hpp"
#include "forms.hpp"
#include "catalog_fixture.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace digivice;
unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %u: %s\n",__LINE__,#x); std::exit(1); } } while(false)
bool same(const State& a,const State& b) {
    Snapshot x,y;
    return encodeSnapshot(a,x) && encodeSnapshot(b,y) && !std::memcmp(x.bytes,y.bytes,kSnapshotSize);
}
State eligible(std::uint32_t starter,std::uint32_t& target) {
    auto s = newDevice(); CHECK(apply(s,Action::Hatch,starter)==Error::None);
    auto& member=s.collection[0]; target=forms::find(member.formId)->children[0];
    const forms::EvolutionEdge* edge=nullptr;
    for(unsigned i=0;i<2;++i){const auto* candidate=forms::outgoing(member.formId,i); if(candidate && candidate->to==target) edge=candidate;}
    CHECK(edge!=nullptr);
    const auto need=forms::evolutionNeed(*edge);
    member.level=s.level=need.level; member.xp=xpForLevel(member.level);
    member.bond=s.bond=need.bond; member.careState=need.care;
    member.hp=s.hp=combat::formProfile(member.formId,need.level).stats.maxHp;
    CHECK(isValid(s)); return s;
}
int main() {
    controls::EvolutionChoice choice; State candidate=newGame(); const auto unchanged=candidate;
    CHECK(!choice.confirm(newGame(),candidate) && same(candidate,unchanged));
    CHECK(!choice.propose(newDevice(),12) && choice.target()==0);
    for(std::uint32_t starter=1;starter<=8;starter++) {
        std::uint32_t target; auto state=eligible(starter,target); const auto before=state;
        CHECK(choice.propose(state,target) && choice.target()==target && same(state,before));
        CHECK(choice.confirm(state,candidate) && choice.target()==0);
        CHECK(candidate.collection[0].formId==target && candidate.sequence==state.sequence+1);
        CHECK(candidate.collection[0].xp==state.collection[0].xp && candidate.bond==state.bond && candidate.level==state.level);
        CHECK(same(state,before)); // Controller never replaces the caller's RAM or writes storage.
        const auto accepted=candidate;
        CHECK(!choice.confirm(state,candidate) && same(candidate,accepted));
        CHECK(choice.propose(state,target)); choice.cancel();
        CHECK(!choice.confirm(state,candidate) && same(candidate,accepted));
        CHECK(choice.propose(state,target)); CHECK(apply(state,Action::Feed)==Error::None);
        CHECK(!choice.confirm(state,candidate) && same(candidate,accepted));
        // Known target and eligibility must still be checked by the shared core.
        state=before; state.bond=state.collection[0].bond=0;
        CHECK(!choice.propose(state,target) && choice.target()==0);
        CHECK(!choice.propose(before,forms::kFormCount+1) && choice.target()==0);
        const auto* form=forms::find(target);
        CHECK(form->children[0] && !choice.propose(before,form->children[0])); // Cannot skip stages.
    }
    auto gaps = stableMemberFixture(18);
    const auto target = forms::find(18)->children[0];
    const forms::EvolutionEdge* gapEdge=nullptr;
    for(unsigned i=0;i<2;++i){const auto* candidate=forms::outgoing(18,i); if(candidate && candidate->to==target) gapEdge=candidate;}
    CHECK(gapEdge);
    const auto gapNeed=forms::evolutionNeed(*gapEdge);
    gaps.level = gaps.collection[1].level = gapNeed.level;
    gaps.collection[1].xp = xpForLevel(gapNeed.level); gaps.bond = gaps.collection[1].bond = gapNeed.bond;
    gaps.collection[1].careState = gapNeed.care;
    gaps.hp = gaps.collection[1].hp = combat::formProfile(18, gapNeed.level).stats.maxHp;
    CHECK(isValid(gaps) && activeMember(gaps)->id == 19);
    CHECK(choice.propose(gaps, target));
    CHECK(apply(gaps, Action::Release, 1) == Error::None && activeMember(gaps) == &gaps.collection[0]);
    CHECK(!choice.confirm(gaps, candidate)); // Release changed sequence after preview.
    CHECK(choice.propose(gaps, target) && choice.confirm(gaps, candidate));
    CHECK(activeMember(candidate)->id == 19 && activeMember(candidate)->formId == target && candidate.nextMemberId == 20);
    // A baby has no historical family child. Its real graph edge instead has
    // a bond gate above both profiles' minimum bond of zero.
    auto baby = stableMemberFixture(67);
    const auto* babyEdge = forms::outgoing(67, 0);
    CHECK(babyEdge && !forms::find(67)->children[0] && babyEdge->minLevel == 1 && babyEdge->minBond > 0);
    CHECK(!choice.propose(baby, babyEdge->to));
    const auto babyXp = activeMember(baby)->xp;
    // Rules12 awards bond only for useful care; a full-care loop must not be
    // used as an unbounded progression fixture. Exercise that bound first.
    for (unsigned cycle=0; cycle<12; ++cycle) {
        CHECK(apply(baby, Action::Feed) == Error::None);
        CHECK(apply(baby, Action::Play) == Error::None);
        CHECK(apply(baby, Action::Rest) == Error::None);
    }
    const auto usefulCareBond=baby.bond;
    CHECK(baby.fullness==100 && baby.mood==100 && baby.energy==100);
    CHECK(apply(baby, Action::Feed) == Error::None);
    CHECK(apply(baby, Action::Play) == Error::None);
    CHECK(apply(baby, Action::Rest) == Error::None);
    CHECK(baby.bond==usefulCareBond && activeMember(baby)->xp==babyXp+6);
    CHECK(!choice.propose(baby,babyEdge->to));
    // Explicit synthetic eligibility fixture: this suite checks confirmation,
    // not acquisition of bond through encounters (covered by native tests).
    const auto need=forms::evolutionNeed(*babyEdge);
    baby.level=baby.collection[1].level=need.level;
    baby.collection[1].xp=xpForLevel(need.level);
    baby.bond=baby.collection[1].bond=need.bond;
    baby.collection[1].careState=need.care;
    baby.hp=baby.collection[1].hp=combat::formProfile(67,need.level).stats.maxHp;
    CHECK(isValid(baby));
    const auto babyBefore = baby;
    const auto preparedXp = activeMember(baby)->xp;
    CHECK(choice.propose(baby, babyEdge->to) && same(baby, babyBefore));
    CHECK(choice.confirm(baby, candidate) && activeMember(candidate)->id == 19);
    CHECK(activeMember(candidate)->xp == preparedXp && activeMember(candidate)->formId == babyEdge->to);
    CHECK(activeMember(candidate)->species == static_cast<Species>(forms::find(babyEdge->to)->lineage));
    CHECK(hasObtained(candidate, 67) && hasObtained(candidate, babyEdge->to));
    CHECK(same(baby, babyBefore)); // Caller still owns the durability boundary.
    std::printf("PASS evolution choice: %u checks; controller=%zuB; native eligibility, stale/cancel/repeat confirmation\n",checks,sizeof(choice));
}
