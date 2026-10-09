// Focused host preview of the actual native controller. Synthetic original
// creatures and neutral missing-art fallback only; no asset/save/device input.
#include "device_ui.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace digivice;
namespace ui=digivice::deviceui;
void require(bool ok,const char* message) {if(!ok){std::fprintf(stderr,"%s\n",message);std::exit(1);}}
struct Preview {
    State state=newGame(12345);
    ui::Model model{};
    ui::Controller controller;
    nearby::View view{};
    std::uint64_t now=100;
    Preview() {
        model.writable=true;model.stepsAvailable=true;model.lifetimeSteps=12345;
        model.nearby=&view;model.nearbyLocalFighter={1,1,1};
        view.stage=nearby::Stage::Discovering;view.peerCount=1;
        view.peers[0].mac={{2,1,2,3,4,5}};view.peers[0].fighter={2,4,1};
        view.peers[0].available=true;view.peers[0].openNonce=77;sync();
    }
    void sync(){controller.update(state,model);}
    ui::Intent tap(int x,int y) {
        sync();controller.touch(state,model,{ui::TouchKind::Down,static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),now+=30});
        auto result=controller.touch(state,model,{ui::TouchKind::Up,static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),now+=60});
        require(static_cast<bool>(result),"Expected native touch action");controller.resolve();sync();return result;
    }
    void open(ui::HomePanel panel) {
        for(unsigned i=0;i<4&&controller.homePanel()!=panel;++i)tap(355,190);
        tap(206,323);
    }
};
int main(int argc,char** argv) {
    require(argc==2,"Usage: render-nearby-ui OUTPUT_DIRECTORY");
    const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
    std::ofstream index(out/"index.html");
    index<<"<!doctype html><meta charset=utf-8><title>Nearby and settings preview</title>"
        "<style>body{background:#09141d;color:#ecf6d9;font:16px system-ui;margin:24px}main{display:flex;flex-wrap:wrap;gap:20px}figure{margin:0}img{width:412px;height:412px;image-rendering:pixelated}figcaption{max-width:412px}</style>"
        "<h1>Native UI: synthetic original/fallback states</h1><p>No physical touch, sound, radio or device claim.</p><main>";
    unsigned screens=0;
    auto save=[&](const char* name,const char* caption,Preview& p) {
        p.sync();std::array<std::uint16_t,ui::kPixels+2> pixels;pixels.fill(0xbeef);
        require(p.controller.render(p.state,p.model,pixels.data()+1,ui::kPixels,p.now+10000),"Native render failed");
        require(pixels.front()==0xbeef&&pixels.back()==0xbeef,"Framebuffer bounds");
        std::ofstream file(out/(std::string(name)+".ppm"),std::ios::binary);file<<"P6\n412 412\n255\n";
        for(int y=0;y<ui::kSize;++y)for(int x=0;x<ui::kSize;++x) {
            const auto value=pixels[1+y*ui::kSize+x];require(ui::Controller::inside(x,y)||value==0,"Round clipping");
            const char rgb[]{static_cast<char>(((value>>11)&31)*255/31),static_cast<char>(((value>>5)&63)*255/63),static_cast<char>((value&31)*255/31)};file.write(rgb,3);
        }
        require(file.good(),"Preview output failed");
        index<<"<figure><img src='"<<name<<".png' alt='"<<caption<<"'><figcaption>"<<caption<<"</figcaption></figure>";++screens;
    };
    Preview p;p.open(ui::HomePanel::Nearby);p.tap(120,312);
    save("nearby-tactical","Tactical review: local choice, separate challenge",p);
    p.tap(280,240);save("nearby-auto","Auto review: explicit selection, no care write",p);
    const auto invite=p.tap(206,302);require(invite.nearbyMode==nearby::Mode::Auto,"Reviewed Auto intent");
    p.view.opponent=p.view.peers[0].mac;p.view.offered[0]=p.model.nearbyLocalFighter;p.view.offered[1]=p.view.peers[0].fighter;
    p.view.session=123;p.view.offeredMode=nearby::Mode::Auto;p.view.stage=nearby::Stage::Outgoing;p.view.host=true;
    save("nearby-outgoing-auto","Outgoing Auto: frozen mode and both fighters",p);
    p.view.stage=nearby::Stage::Incoming;p.view.host=false;save("nearby-incoming-auto","Incoming Auto: explicit accept or decline",p);
    require(nearby::begin(p.view.offered[0],p.view.offered[1],nearby::Mode::Auto,42,p.view.match),"Original fixture match");
    p.view.stage=nearby::Stage::Playing;p.view.host=true;save("nearby-auto-host","Auto host: no attack or capture controls",p);
    p.view.host=false;save("nearby-auto-guest","Auto guest: same frozen mode",p);
    require(nearby::resolveAuto(p.view.match,0),"Original fixture exchange");p.model.nearbyTurnElapsedMs=1200;
    save("nearby-auto-guest-impact","Auto guest exchange: native feedback, neutral art fallback",p);
    p.view.host=true;save("nearby-auto-host-impact","Auto host exchange: native feedback, neutral art fallback",p);
    Preview home;home.model.stepsAvailable=false;home.model.stepsRecovering=true;
    save("steps-recovering","Recovering sensor: lifetime total remains visible",home);
    home.model.stepsRecovering=false;save("steps-unavailable","Unavailable sensor: lifetime total remains visible",home);
    home.model.stepsAvailable=true;home.state.encounterRate=EncounterRate::Off;
    save("steps-paused","Encounters Off: lifetime total remains visible",home);
    Preview sound;sound.open(ui::HomePanel::Settings);sound.tap(120,252);sound.model.audioAvailable=true;sound.model.volumePercent=100;
    save("sound-100","Volume 100 percent: separate mute and music controls",sound);
    sound.model.muted=true;sound.model.musicEnabled=true;save("sound-muted-music","Mute on and music on remain distinct",sound);
    index<<"</main>";require(index.good(),"Preview index failed");
    std::printf("PASS %u synthetic native screens; original identities/neutral fallback only\n",screens);
}
