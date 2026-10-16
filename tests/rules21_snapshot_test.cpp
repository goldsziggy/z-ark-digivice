#include "game.hpp"
#include "legacy_v21.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace old = digivice::legacy_v21;
static unsigned checks=0, failures=0, checkpoints=0;
static void require(bool good, const char* what) { ++checks; if(!good){++failures;std::cerr<<what<<"\n";} }
static unsigned nibble(char c) { return c>='0'&&c<='9' ? c-'0' : c>='a'&&c<='f' ? c-'a'+10 : 255; }
static bool fixture(const char* path, old::State& out) {
    std::ifstream input(path);std::string text((std::istreambuf_iterator<char>(input)),{});
    auto at=text.find("\"snapshot\"");if(at==std::string::npos)return false;
    at=text.find('"',text.find(':',at))+1;const auto end=text.find('"',at);
    if(end-at!=old::kSnapshotSize*2)return false;
    old::Snapshot bytes;
    for(std::size_t i=0;i<old::kSnapshotSize;++i){const auto a=nibble(text[at+i*2]),b=nibble(text[at+i*2+1]);if(a>15||b>15)return false;bytes.bytes[i]=static_cast<std::uint8_t>(a*16+b);}
    return old::decodeSnapshot(bytes.bytes,sizeof(bytes.bytes),out)==old::SnapshotStatus::Ok;
}
static void checkpoint(const old::State& before) {
    ++checkpoints;
    old::Snapshot historical;require(old::encodeSnapshot(before,historical),"encode genuine rules21 snapshot");
    digivice::State after;
    require(digivice::decodeSnapshot(historical.bytes,sizeof(historical.bytes),after)==digivice::SnapshotStatus::Migrated,"decode rules21 as migrated");
    require(digivice::isValid(after),"migrated save valid");
    digivice::Snapshot current;require(digivice::encodeSnapshot(after,current),"encode rules22 snapshot");
    static_assert(old::kSnapshotSize==digivice::kSnapshotSize);
    require(std::memcmp(historical.bytes+12,current.bytes+12,old::kSnapshotSize-16)==0,"all saved payload bytes preserved");
    digivice::State restored;require(digivice::decodeSnapshot(current.bytes,sizeof(current.bytes),restored)==digivice::SnapshotStatus::Ok,"current save roundtrip");
    digivice::Snapshot again;require(digivice::encodeSnapshot(restored,again),"re-encode current save");
    require(std::memcmp(current.bytes,again.bytes,sizeof(current.bytes))==0,"canonical current save unchanged");
    char oldJson[old::kJsonCapacity],newJson[digivice::kJsonCapacity];
    require(old::writeJson(before,oldJson,sizeof(oldJson))>0,"old JSON");
    require(digivice::writeJson(after,newJson,sizeof(newJson))>0,"new JSON");
    std::string actual(newJson);const std::string from="\"schemaVersion\":30,\"rulesVersion\":22",to="\"schemaVersion\":29,\"rulesVersion\":21";
    auto at=actual.find(from);require(at!=std::string::npos,"new epoch JSON");if(at!=std::string::npos)actual.replace(at,from.size(),to);
    require(actual==oldJson,"all state projection fields preserved except epoch headers");
    if(after.phase==digivice::Phase::Expedition){
        const auto keys=after.dungeonKeys,tier=after.expeditionTier,oldHp=after.hp,oldEnergy=after.energy;
        require(digivice::apply(after,digivice::Action::NextExpedition,0)==digivice::Error::None,"resume old intermission");
        require(after.dungeonKeys==keys && after.expeditionTier==tier+1,"resume no double fee or skipped tier");
        require(after.hp==oldHp && after.energy==oldEnergy,"resume retains HP and energy");
        require(after.wildRules==22,"new tier uses current epoch");
    }
}
int main(int argc,char**argv){
    if(argc!=2){std::cerr<<"expected synthetic rules21 fixture path\n";return 2;}
    old::State base;require(fixture(argv[1],base),"load genuine synthetic rules21 fixture");if(failures)return 1;
    checkpoint(base);
    unsigned intermissions=0,focus=0,capture=0,terminal=0;
    for(unsigned scenario=0;scenario<6;++scenario){
        auto state=base;
        require(old::apply(state,old::Action::StartExpedition,scenario)==old::Error::None,"old expedition start");
        for(unsigned n=0;n<100;++n){
            checkpoint(state);
            old::Error result=old::Error::None;
            if(state.phase==old::Phase::Expedition){++intermissions;result=old::apply(state,old::Action::NextExpedition,0);}
            else if(state.phase!=old::Phase::Encounter){++terminal;break;}
            else if(state.autoCapture==old::AutoCapture::FocusStrike||state.autoCapture==old::AutoCapture::FocusBlock){++focus;result=old::applyFocus(state,2400,nullptr);}
            else if(state.autoCapture==old::AutoCapture::Awaiting){++capture;result=old::applyAutoResume(state,nullptr);}
            else result=old::applyAutoFight(state,nullptr);
            require(result==old::Error::None,"old transition valid");if(result!=old::Error::None)break;
        }
    }
    require(intermissions>=2,"covered old intermissions");require(focus>0,"covered old Focus pauses");require(capture>0,"covered old capture pauses");require(terminal==6,"all six old scenarios terminated");
    std::cout<<checks<<" checks, "<<failures<<" failures, "<<checkpoints<<" genuine schema29 checkpoints, "<<intermissions<<" intermissions, "<<focus<<" Focus, "<<capture<<" capture pauses\n";
    return failures?1:0;
}
