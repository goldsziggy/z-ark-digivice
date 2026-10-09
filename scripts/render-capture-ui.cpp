// Actual native controller, procedural CC0 original silhouettes and fallback.
// No external artwork, saves, hardware or network input.
#include "device_ui.hpp"
#include "capture_ring.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
using namespace digivice;
namespace ui=digivice::deviceui;
void require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"%s\n",why);std::exit(1);}}
struct Preview {
    State state=newGame(12345);ui::Model model{};ui::Controller controller;
    std::uint64_t now=100,epoch=0;
    std::array<std::uint16_t,32*32> pixels{};std::array<std::uint8_t,32*32/8> mask{};
    Preview() {
        model.writable=true;sync();require(apply(state,Action::Explore,100)==Error::None,"Encounter fixture");sync();
        tap(206,274);state.wildHp=state.wildMaxHp/2;sync();tap(280,306);epoch=now;
        require(controller.screen()==ui::Screen::Capture,"Capture entry");
    }
    void sync(){controller.update(state,model);}
    void tap(int x,int y){
        require(!controller.touch(state,model,{ui::TouchKind::Down,static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),now+=30}),"Fresh Down");
        const auto intent=controller.touch(state,model,{ui::TouchKind::Up,static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),now+=60});
        require(intent.kind==ui::IntentKind::Navigation,"Navigation only");controller.resolve();sync();
    }
    void original(unsigned shape){
        // Three original geometric critters, broad/square/tall, with simple eyes.
        pixels.fill(0);mask.fill(0);const int w=shape==0?28:shape==1?22:12,h=shape==0?12:shape==1?22:28;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){
            const bool cut=(x<2||x>=w-2)&&(y<3||y>=h-3);if(cut)continue;
            const auto i=y*32+x;mask[i/8]|=1u<<(i%8);pixels[i]=shape==0?0x7ded:shape==1?0xb5ff:0xfcce;
            if(y>=h/3&&y<h/3+2&&(x==w/3||x==w-w/3-1))pixels[i]=0x1125;
        }
        auto& art=model.artwork.sprite;art={};art.formId=state.wildFormId;art.width=art.height=32;
        art.pixels=pixels.data();art.pixelCount=pixels.size();art.mask=mask.data();art.maskBytes=mask.size();
        art.contentWidth=w;art.contentHeight=h;
    }
    void phase(bool hit){const auto target=capturering::sample(0,state.wildFormId).targetRadius;now=epoch+(hit?(100-target)*capturering::kCycleMs/80:0);}
};
int main(int argc,char** argv){
    require(argc==2,"Usage: render-capture-ui OUTPUT_DIRECTORY");const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
    std::ofstream index(out/"index.html");index<<"<!doctype html><meta charset=utf-8><title>Native capture timing preview</title><style>body{background:#09141d;color:#ecf6d9;font:16px system-ui;margin:24px}main{display:flex;flex-wrap:wrap;gap:20px}figure{margin:0}img{width:412px;height:412px;image-rendering:pixelated}figcaption{max-width:412px}</style><h1>Native capture timing</h1><p>Actual controller; original CC0 synthetic creatures and neutral fallback. Host preview, no physical acceptance claim.</p><main>";
    unsigned screens=0;
    auto save=[&](const char* name,const char* caption,Preview& p){
        p.sync();std::array<std::uint16_t,ui::kPixels+2> frame;frame.fill(0xbeef);
        require(p.controller.render(p.state,p.model,frame.data()+1,ui::kPixels,p.now),"Render");
        require(frame.front()==0xbeef&&frame.back()==0xbeef,"Framebuffer bounds");
        std::ofstream file(out/(std::string(name)+".ppm"),std::ios::binary);file<<"P6\n412 412\n255\n";
        for(int y=0;y<ui::kSize;++y)for(int x=0;x<ui::kSize;++x){const auto value=frame[1+y*ui::kSize+x];require(ui::Controller::inside(x,y)||value==0,"Circular clipping");
            const char rgb[]{static_cast<char>(((value>>11)&31)*255/31),static_cast<char>(((value>>5)&63)*255/63),static_cast<char>((value&31)*255/31)};file.write(rgb,3);}
        require(file.good(),"PPM write");index<<"<figure><img src='"<<name<<".png' alt='"<<caption<<"'><figcaption>"<<caption<<"</figcaption></figure>";++screens;
    };
    Preview start;start.original(1);save("capture-start","Fresh entry starts large; amber boundaries show the accepted band",start);
    start.phase(true);save("capture-on-target","Moving ring inside the band: unchanged capture odds on a landed throw",start);
    start.now=start.epoch+2200;save("capture-late","Small late ring; direct touch commits at once",start);
    Preview miss;miss.original(0);save("capture-wide","Wide original sprite stays within the arena; play area responds on Down",miss);
    Preview tall;tall.original(2);tall.phase(true);save("capture-tall","Tall original sprite, large play area responds on Down",tall);
    Preview fallback;save("capture-fallback","Missing art fallback retains identical timing and touch controls",fallback);
    Preview automatic;automatic.state.battleMode=BattleMode::Auto;automatic.state.autoCapture=AutoCapture::Awaiting;automatic.state.wildTurn=1;require(isValid(automatic.state)&&captureChance(automatic.state)>0,"Valid manual Auto pause");automatic.sync();automatic.original(1);save("capture-auto","Auto pauses for a manual throw or explicit Skip / Resume Fight",automatic);
    index<<"</main>";require(index.good(),"Index write");std::printf("PASS %u original/fallback native capture screens\n",screens);
}
