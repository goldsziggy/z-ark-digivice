#include "expedition_party.hpp"
#include "expeditions.hpp"
#include "forms.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

using namespace digivice;
using namespace digivice::nearby;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); } } while(false)
void invalid() {
    const Fighter good{1,11,1}; std::uint32_t form=123,level=45;
    CoopFoeDiagnostics diagnostics{1,2,3,4,5,6,7}; const auto before=diagnostics;
    for (const auto bad:{Fighter{},Fighter{UINT32_MAX,11,1},Fighter{1,999,1},Fighter{1,11,0},Fighter{1,11,100},Fighter{1,11,1,6,0}}) {
        CHECK(!previewCoopExpeditionFoe(good,bad,0,form,level,&diagnostics));
        CHECK(form==123&&level==45&&!std::memcmp(&before,&diagnostics,sizeof(before)));
    }
    CHECK(!previewCoopExpeditionFoe(good,good,6,form,level,&diagnostics));
    CHECK(form==123&&level==45&&!std::memcmp(&before,&diagnostics,sizeof(before)));
}
void cases() {
    // Installed starter families span types and evolution/stat profiles. These
    // are test inputs, never special cases in the production selector.
    const Fighter profiles[]{
        {1,11,1},{2,11,10},{3,12,20},{4,13,40},{5,14,70},{6,14,99},
        {7,18,10},{8,19,20},{9,20,40},{10,21,70},
        {11,25,10},{12,26,20},{13,27,40},{14,28,70},
        {15,32,10},{16,33,20},{17,34,40},{18,35,70}
    };
    unsigned selections=0,clearOrders=0,aliveOrders=0,nontrivial=0,changedWithPartner=0,maxCandidates=0,maxRounds=0;
    for (unsigned i=0;i<std::size(profiles);++i) for (unsigned pairing=0;pairing<3;++pairing) {
        auto first=profiles[i],second=pairing==0 ? profiles[i] : pairing==1 ? profiles[4] : profiles[(i+6)%std::size(profiles)];
        first.offenseBonus=first.protectionBonus=i%2 ? 5 : 0;
        second.offenseBonus=pairing==2 ? 5 : 0; second.protectionBonus=pairing==1 ? 5 : 0;
        const auto originalFirst=first,originalSecond=second;
        for (unsigned scenario=0;scenario<6;++scenario) {
            std::uint32_t form=0,level=0,swappedForm=0,swappedLevel=0;
            CoopFoeDiagnostics info,swapped;
            CHECK(previewCoopExpeditionFoe(first,second,scenario,form,level,&info));
            CHECK(previewCoopExpeditionFoe(second,first,scenario,swappedForm,swappedLevel,&swapped));
            CHECK(form==swappedForm&&level==swappedLevel);
            CHECK(!std::memcmp(&info,&swapped,sizeof(info)));
            CHECK(sameFighter(first,originalFirst)&&sameFighter(second,originalSecond));
            CHECK(forms::productionForm(form)&&forms::hasInstalledArt(form)&&combat::validFormProfile(form,level));
            CHECK(info.candidates>0&&info.candidates<=kCoopFoeCandidateLimit&&info.rounds<=kCoopFoeRoundLimit);
            maxCandidates=std::max(maxCandidates,info.candidates); maxRounds=std::max(maxRounds,info.rounds);
            auto renamedFirst=first,renamedSecond=second; renamedFirst.memberId=103; renamedSecond.memberId=104;
            CHECK(previewCoopExpeditionFoe(renamedFirst,renamedSecond,scenario,swappedForm,swappedLevel));
            CHECK(form==swappedForm&&level==swappedLevel);
            unsigned actualClears=0,plannedClears=0,survivors=2,leastHp=100,minTurns=72,maxTurns=0;
            for (unsigned trial=0;trial<3;++trial) for (unsigned order=0;order<2;++order) {
                Raid raid; CHECK(beginRaid(order?second:first,order?first:second,raidSeed(form,level,scenario),raid));
                CHECK(raid.energy[0]==100&&raid.energy[1]==100);
                CHECK(raid.hp[0]==combat::formProfile(raid.players[0].formId,raid.players[0].level).stats.maxHp);
                CHECK(raid.hp[1]==combat::formProfile(raid.players[1].formId,raid.players[1].level).stats.maxHp);
                if (trial<2) raid.rng=kCoopPlanningSeeds[trial];
                auto previous=raid;
                while (raid.status==RaidStatus::Active) {
                    CHECK(resolveAutoRaid(raid,raid.sequence));
                    CHECK(raid.sequence==previous.sequence+1&&raid.sequence<=3*kMaxRaidRounds);
                    CHECK(raid.hp[0]<=previous.hp[0]&&raid.hp[1]<=previous.hp[1]&&raid.energy[0]<=previous.energy[0]&&raid.energy[1]<=previous.energy[1]);
                    CHECK(raid.foeLevel==expeditions::tierLevel(level,raid.tier));
                    previous=raid;
                }
                CHECK(validRaid(raid));
                if (trial==2) { actualClears+=raid.status==RaidStatus::Cleared; continue; }
                plannedClears+=raid.status==RaidStatus::Cleared;
                survivors=std::min(survivors,static_cast<unsigned>(!!raid.hp[0])+static_cast<unsigned>(!!raid.hp[1]));
                for (unsigned p=0;p<2;++p) leastHp=std::min(leastHp,raid.hp[p]*100/combat::formProfile(raid.players[p].formId,raid.players[p].level).stats.maxHp);
                minTurns=std::min(minTurns,raid.sequence);maxTurns=std::max(maxTurns,raid.sequence);
            }
            CHECK(plannedClears==info.clearTrials&&survivors==info.minSurvivors&&leastHp==info.minHpPercent&&minTurns==info.minRounds&&maxTurns==info.maxRounds);
            clearOrders+=actualClears; aliveOrders+=info.minSurvivors==2; nontrivial+=info.maxRounds>=6;
            if (pairing==0) {
                CHECK(previewCoopExpeditionFoe(first,profiles[4],scenario,swappedForm,swappedLevel));
                changedWithPartner+=form!=swappedForm||level!=swappedLevel;
            }
            ++selections;
            std::printf("case,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",i,pairing,scenario,form,level,info.clearTrials,info.minSurvivors,info.minHpPercent,info.minRounds,info.maxRounds,info.rounds,actualClears);
        }
    }
    CHECK(changedWithPartner>0);CHECK(nontrivial>selections*3/4);CHECK(clearOrders>selections);
    std::printf("Selection corpus: %u selections; %u/%u clear orientations; %u selections keep both alive; %u nontrivial; %u partner-sensitive; max candidates %u / rounds %u\n",selections,clearOrders,selections*2,aliveOrders,nontrivial,changedWithPartner,maxCandidates,maxRounds);
}
}
int main() {
    CHECK(kCoopPlanningSeeds[0]>(7u<<24|99u<<16|465u)&&kCoopPlanningSeeds[1]>(7u<<24|99u<<16|465u));
    const auto start=std::chrono::steady_clock::now(); invalid();cases();
    std::printf("Co-op selector: %u checks, %u failures; elapsed %.3fs\n",checks,failures,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());
    return failures ? 1 : 0;
}
