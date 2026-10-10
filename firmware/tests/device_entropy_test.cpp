#include "device_entropy.hpp"
#include "esp_mac.h"
#include "game.hpp"
#include "save_store.hpp"
#include "park_save_backend.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <set>
#include <stdexcept>
#include <vector>
namespace {
unsigned checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
#define CHECK(x) check((x),#x)
digivice::entropy::Material injected{{2,0x34,0x56,0x78,0x9a,0xbc},{1,2,3,4}};
bool identityFailure=false,enabled=false;
std::vector<unsigned> calls;
}
esp_err_t esp_efuse_mac_get_default(std::uint8_t* out){calls.push_back(1);std::memcpy(out,injected.identity,6);return identityFailure?ESP_FAIL:ESP_OK;}
void bootloader_random_enable(){CHECK(!enabled);enabled=true;calls.push_back(2);}
void esp_fill_random(void* out,std::size_t size){CHECK(enabled&&size==sizeof(injected.words));calls.push_back(3);std::memcpy(out,injected.words,size);}
void bootloader_random_disable(){CHECK(enabled);enabled=false;calls.push_back(4);}
namespace {
using namespace digivice;
std::vector<std::uint32_t> formsFor(const State& state,unsigned start,unsigned count){
    std::vector<std::uint32_t> result;
    for(unsigned i=start;i<start+count;++i)result.push_back(selectWildForm(i,worldSelectionSeed(state),activeMember(state)->formId,state.level));
    return result;
}
void sdkContract(){
    const auto first=device::collectStartupEntropy();CHECK(first.ready());CHECK(!enabled);CHECK(calls==std::vector<unsigned>({1,2,3,4}));
    CHECK(first.profile!=first.world&&first.world!=first.pacing&&first.profile!=first.pacing&&first.offers!=first.profile&&first.offers!=first.world&&first.offers!=first.pacing);
    calls.clear();const auto again=device::collectStartupEntropy();CHECK(first.profile==again.profile&&first.world==again.world&&first.pacing==again.pacing&&first.offers==again.offers);
    calls.clear();identityFailure=true;CHECK(!device::collectStartupEntropy().ready());CHECK(calls==std::vector<unsigned>({1}));identityFailure=false;
    const auto original=injected;
    for(unsigned invalid=0;invalid<3;++invalid){calls.clear();std::memset(injected.identity,invalid==1?255:0,6);if(invalid==2)injected.identity[0]=1;CHECK(!device::collectStartupEntropy().ready());CHECK(calls==std::vector<unsigned>({1}));}
    injected=original;
}
void independentIdentities(){
    std::set<std::uint32_t> profiles,worlds;std::set<std::vector<std::uint32_t>> sequences;
    const auto original=injected;
    for(unsigned device=0;device<256;++device){
        injected.identity[5]=static_cast<std::uint8_t>(device);
        const auto entropy=device::collectStartupEntropy();CHECK(entropy.ready());
        CHECK(profiles.insert(entropy.profile).second);CHECK(worlds.insert(entropy.world).second);
        auto s=newDevice(entropy.profile);CHECK(apply(s,Action::Hatch,1)==Error::None);CHECK(apply(s,Action::WorldSeed,entropy.world)==Error::None);
        CHECK(sequences.insert(formsFor(s,1,32)).second);
    }
    injected=original;
    const auto first=entropy::seeds(injected);injected.words[0]^=0x55aa55aau;const auto next=entropy::seeds(injected);
    CHECK(first.profile!=next.profile&&first.world!=next.world&&first.pacing!=next.pacing&&first.offers!=next.offers);injected=original;
}
void savedContinuation(){
    const auto seed=entropy::seeds(injected);
    // Existing profile retains original capture stream and both frozen foes.
    auto s=newDevice();CHECK(apply(s,Action::Hatch,1)==Error::None);
    CHECK(apply(s,Action::EncounterSeed,101)==Error::None);
    CHECK(apply(s,Action::AccrueSteps,1000)==Error::None);CHECK(apply(s,Action::PresentEncounter)==Error::None);
    CHECK(apply(s,Action::AccrueSteps,1000)==Error::None);const auto before=s;
    ParkSaveBackend backend;storage::SaveStore saves(backend);CHECK(saves.restore(s)==storage::BootStatus::Empty);CHECK(saves.checkpoint(s));
    auto candidate=s;CHECK(apply(candidate,Action::WorldSeed,seed.world)==Error::None);CHECK(saves.checkpoint(candidate));s=candidate;
    CHECK(s.seed==before.seed&&s.rngState==before.rngState&&s.encounterRng==before.encounterRng&&s.encounterTarget==before.encounterTarget&&s.encounterProgress==before.encounterProgress);
    CHECK(!std::memcmp(s.collection,before.collection,sizeof(s.collection)));
    CHECK(!std::memcmp(&s.pendingEncounter,&before.pendingEncounter,sizeof(s.pendingEncounter))&&s.wildFormId==before.wildFormId);
    const auto expected=formsFor(s,s.encounters+1,64);const auto writes=backend.writes;
    for(unsigned reboot=0;reboot<16;++reboot){
        State restored;storage::SaveStore reader(backend);CHECK(reader.restore(restored)==storage::BootStatus::Loaded);
        CHECK(restored.worldSeed==seed.world&&restored.encounters==s.encounters&&formsFor(restored,restored.encounters+1,64)==expected);
        CHECK(apply(restored,Action::WorldSeed,seed.world^reboot^1u)==Error::InvalidAction);
        Snapshot a,b;CHECK(encodeSnapshot(s,a)&&encodeSnapshot(restored,b));CHECK(!std::memcmp(a.bytes,b.bytes,sizeof(a.bytes)));
    }
    CHECK(backend.writes==writes);
    auto future=s;CHECK(apply(future,Action::Mode,0)==Error::WrongPhase); // No hidden reset on an active save.
    CHECK(formsFor(s,s.encounters+2,32)!=formsFor(s,1,32));
}
std::uint32_t playEncounter(State& state) {
    CHECK(state.phase==Phase::Home);
    CHECK(apply(state,Action::AccrueSteps,1000)==Error::None);
    CHECK(apply(state,Action::PresentEncounter)==Error::None);
    const auto form=state.wildFormId;
    CHECK(applyAutoFight(state)==Error::None);
    // Rules 18: an Auto fight may pause for a focus tap (answered untapped here)
    // or a capture choice (skipped) before it finishes.
    for(unsigned guard=0;guard<16&&state.phase==Phase::Encounter;++guard){
        if(state.autoCapture==AutoCapture::FocusStrike||state.autoCapture==AutoCapture::FocusBlock)CHECK(applyFocus(state,kFocusNoTap)==Error::None);
        else if(state.autoCapture==AutoCapture::Awaiting)CHECK(applyAutoResume(state)==Error::None);
        else CHECK(applyAutoFight(state)==Error::None);
    }
    CHECK(state.phase==Phase::Home);
    for(unsigned rest=0,count=recoveryRestCount(state);rest<count;++rest)CHECK(apply(state,Action::Rest)==Error::None);
    return form;
}
void actualEncounterRestart() {
    const auto original=injected;std::set<std::vector<std::uint32_t>> actualSequences;
    for(unsigned identity=0;identity<16;++identity) {
        injected.identity[5]=static_cast<std::uint8_t>(identity);const auto seed=entropy::seeds(injected);
        auto continuous=newDevice(seed.profile);CHECK(apply(continuous,Action::Hatch,1)==Error::None);
        CHECK(apply(continuous,Action::WorldSeed,seed.world)==Error::None);CHECK(apply(continuous,Action::EncounterSeed,seed.pacing)==Error::None);
        CHECK(apply(continuous,Action::Mode,1)==Error::None);auto rebooting=continuous;
        ParkSaveBackend backend;storage::SaveStore first(backend);CHECK(first.restore(rebooting)==storage::BootStatus::Empty&&first.checkpoint(rebooting));
        std::vector<std::uint32_t> sequence;
        for(unsigned event=0;event<24;++event) {
            storage::SaveStore restarted(backend);State restored;CHECK(restarted.restore(restored)==storage::BootStatus::Loaded);
            const auto expected=playEncounter(continuous),actual=playEncounter(restored);CHECK(actual==expected);sequence.push_back(actual);
            Snapshot a,b;CHECK(encodeSnapshot(continuous,a)&&encodeSnapshot(restored,b));CHECK(!std::memcmp(a.bytes,b.bytes,sizeof(a.bytes)));
            CHECK(restarted.checkpoint(restored));CHECK(restored.encounters==event+1);
        }
        CHECK(actualSequences.insert(sequence).second);
    }
    injected=original;
}
void writeFailure(){
    for(unsigned failure=0;failure<3;++failure){
        auto s=newDevice();CHECK(apply(s,Action::Hatch,1)==Error::None);ParkSaveBackend backend;storage::SaveStore save(backend);
        CHECK(save.restore(s)==storage::BootStatus::Empty&&save.checkpoint(s));const auto before=s;
        backend.failBefore=failure==0;backend.failAfter=failure==1;backend.failReadback=failure==2;
        auto candidate=s;CHECK(apply(candidate,Action::WorldSeed,123456)==Error::None);CHECK(!save.checkpoint(candidate));
        CHECK(!save.writable()&&s.worldSeed==0&&s.sequence==before.sequence);
        backend.failBefore=backend.failAfter=backend.failReadback=false;storage::SaveStore restart(backend);State restored;
        CHECK(restart.restore(restored)==storage::BootStatus::Loaded);CHECK(restored.worldSeed==(failure?123456u:0u));
    }
}
}
int main(){try{sdkContract();independentIdentities();savedContinuation();actualEncounterRestart();writeFailure();std::printf("PASS %u entropy/identity/save-continuation checks; actual SDK adapter with injected SDK calls, no board claim.\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL entropy: %s\n",e.what());return 1;}}
