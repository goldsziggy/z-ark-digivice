#include "prefetch.hpp"
#include "catalog_fixture.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace digivice;
unsigned checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %u: %s\n",__LINE__,#x); std::exit(1); } } while(false)
constexpr const char* publicIds[]{"sprite-mote-v1","sprite-glint-v1","sprite-lumen-v1","sprite-flicker-v1",
    "sprite-rill-v1","sprite-brine-v1","sprite-pelagia-v1","sprite-cinder-v1","sprite-scoria-v1","sprite-pyrel-v1","scene-forest-412-v1"};
bool contains(const assets::Plan& plan,const char* id) {
    for(std::size_t i=0;i<plan.count;++i) if(!std::strcmp(plan.ids[i],id)) return true;
    return false;
}
assets::Plan inspect(const State& state,bool development=false) {
    const auto before=state; const auto result=assets::plan(state,development);
    CHECK(!std::memcmp(&state,&before,sizeof(state)) && result.count<=4);
    for(std::size_t i=0;i<result.count;++i) {
        bool listed=false; for(const auto* id:publicIds) if(!std::strcmp(result.ids[i],id)) listed=true;
        CHECK(listed && std::strlen(result.ids[i])<48);
        if(!development) CHECK(std::strncmp(result.ids[i],"sprite-",7)!=0);
        for(std::size_t j=0;j<i;++j) CHECK(std::strcmp(result.ids[i],result.ids[j]));
    }
    return result;
}
int main() {
    auto state=newGame(); auto initial=inspect(state);
    CHECK(initial.count==1 && contains(initial,"scene-forest-412-v1"));
    CHECK(contains(inspect(state,true),"sprite-mote-v1")); // Explicit development opt-in only.
    CHECK(apply(state,Action::Walk,100)==Error::None);
    CHECK(inspect(state).count==1);
    // The saved pending foe may differ from a prediction using today's partner.
    // Include its art alongside the active match without growing the four slots.
    CHECK(apply(state,Action::AccrueSteps,1000)==Error::None);
    state.pendingEncounter={5,1,12}; // Preserved synthetic pending Rill, never newly selected.
    CHECK(isValid(state));
    const auto waiting=inspect(state);
    CHECK(waiting.count==1);
    CHECK(contains(inspect(state,true),"sprite-mote-v1") && contains(inspect(state,true),"sprite-rill-v1"));
    state=newGame();
    CHECK(apply(state,Action::AccrueSteps,1000)==Error::None);
    state.pendingEncounter={5,1,12};
    CHECK(isValid(state));
    const auto homeWaiting=inspect(state);
    CHECK(homeWaiting.count==1 && !contains(homeWaiting,"sprite-rill-v1"));
    CHECK(contains(inspect(state,true),"sprite-rill-v1"));
    state.pendingEncounter.formId=11; // Exact-form private art is independent of this cache.
    CHECK(isValid(state));
    const auto withoutPublicArt=inspect(state);
    CHECK(withoutPublicArt.count==1 && !contains(withoutPublicArt,"sprite-flicker-v1"));
    CHECK(inspect(newDevice()).count==0);
    state.activeCreatureId=19;
    CHECK(inspect(state).count==0); // Unknown identity cannot index outside collection.
    // Fixed working set throughout the catalog; no generated request for a name
    // without approved public artwork. Private SD lookup is separate and opt-in.
    for(std::uint32_t form=1;form<=forms::kFormCount;++form) {
        auto current=stableMemberFixture(form); CHECK(isValid(current));
        CHECK(activeMember(current)->id==19 && current.collectionCount==2);
        const auto plan=inspect(current); CHECK(plan.count>=1 && contains(plan,"scene-forest-412-v1"));
        const auto dev=inspect(current,true);
        if(form<=10) CHECK(dev.count>plan.count); // Test art retained only for explicit fixtures.
        if(form>10) {
            for(std::size_t i=0;i<plan.count;++i) CHECK(!std::strncmp(plan.ids[i],"sprite-",7) || !std::strncmp(plan.ids[i],"scene-",6));
        }
        CHECK(apply(current,Action::Release,1)==Error::None && current.collection[0].id==19);
        const auto compacted=inspect(current);
        CHECK(compacted.count==plan.count && !std::memcmp(compacted.ids,plan.ids,sizeof(plan.ids)));
        CHECK(apply(current,Action::Walk,100)==Error::None);
        const auto during=inspect(current); CHECK(during.count>=1);
    }
    state=stableMemberFixture(2);
    CHECK(!std::strcmp(inspect(state,true).ids[0],"sprite-glint-v1") && inspect(state).count==1);
    state=stableMemberFixture(10);
    CHECK(!std::strcmp(inspect(state,true).ids[0],"sprite-pyrel-v1") && inspect(state).count==1);
    std::printf("PASS prefetch: %u checks; Plan=%zuB; production excludes named test packs; explicit development fixtures remain bounded\n",checks,sizeof(assets::Plan));
}
