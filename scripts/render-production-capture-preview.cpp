// Local/private preview only. Loads existing exact-form assets supplied by the
// caller; contains no artwork, credentials, physical saves, or network code.
// The native renderer, DVA decoder, clip bounds and RGB565 background conversion
// are production code. Host libjpeg replaces only the ESP32-S3 ROM JPEG decoder.
#include "device_ui.hpp"
#include "capture_ring.hpp"
#include "local_form_art.hpp"
#include "local_form_facing.hpp"
#include "sprite_bounds.hpp"
#include "background_decode.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <jpeglib.h>

using namespace digivice;
namespace ui=digivice::deviceui;
namespace {
void require(bool ok,const char* why) { if(!ok) { std::fprintf(stderr,"%s\n",why); std::exit(1); } }
struct DecodedForm {
    std::uint32_t id=0; sprite::Info info{};
    std::vector<std::uint16_t> pixels;
    std::vector<std::uint8_t> masks;
    std::array<sprite::OpaqueBounds,sprite::kAnimationCount> bounds{};
    void load(const char* directory,std::uint32_t form) {
        sprite::LocalFormArt source(directory);
        require(source.open(form)==sprite::Result::Ok,"Exact production DVA unavailable/invalid");
        id=form; info=source.info(); const auto count=std::size_t(info.width)*info.height;
        pixels.resize(count*info.frameCount); masks.resize(count/8*info.frameCount);
        for(unsigned animation=0;animation<sprite::kAnimationCount;++animation) {
            const auto clip=info.clips[animation];
            for(unsigned frame=0;frame<clip.frames;++frame) {
                const auto index=clip.firstFrame+frame;
                require(source.decode(static_cast<sprite::Animation>(animation),frame,pixels.data()+index*count,count,
                    masks.data()+index*count/8,count/8)==sprite::Result::Ok,"Production DVA frame decode failed");
            }
            require(sprite::opaqueClipBounds(masks.data(),masks.size(),info.width,info.height,info.frameCount,clip,bounds[animation]),"Production clip bounds invalid");
        }
    }
    ui::SpriteFrame frame(const ui::ArtRequest& request) const {
        require(request.formId==id,"Renderer requested a different form; no substitute is permitted");
        const auto animation=static_cast<unsigned>(request.animation); require(animation<sprite::kAnimationCount,"Invalid animation");
        const auto clip=info.clips[animation]; const auto index=clip.firstFrame+(request.elapsedMs/clip.frameMs)%clip.frames;
        const auto count=std::size_t(info.width)*info.height; const auto content=bounds[animation];
        ui::SpriteFrame result;
        result.formId=id; result.animation=request.animation; result.nativeFacing=sprite::localFormFacing(id);
        result.width=info.width; result.height=info.height; result.pixels=pixels.data()+index*count; result.pixelCount=count;
        result.mask=masks.data()+index*count/8; result.maskBytes=count/8;
        result.contentX=content.x; result.contentY=content.y; result.contentWidth=content.width; result.contentHeight=content.height;
        return result;
    }
};
std::vector<std::uint16_t> background(const char* path) {
    std::ifstream file(path,std::ios::binary); require(file.good(),"Background missing");
    const std::vector<std::uint8_t> encoded{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    assets::JpegInfo info;
    require(assets::inspectBackgroundJpeg(encoded.data(),encoded.size(),info),"Background outside production JPEG contract");
    jpeg_decompress_struct decoder{}; jpeg_error_mgr error{};
    decoder.err=jpeg_std_error(&error); jpeg_create_decompress(&decoder);
    jpeg_mem_src(&decoder,encoded.data(),encoded.size()); require(jpeg_read_header(&decoder,TRUE)==JPEG_HEADER_OK,"JPEG header");
    decoder.out_color_space=JCS_RGB; jpeg_start_decompress(&decoder);
    require(decoder.output_width==info.width&&decoder.output_height==info.height&&decoder.output_components==3,"JPEG dimensions/components");
    std::vector<std::uint8_t> rgb(std::size_t(info.width)*info.height*3);
    while(decoder.output_scanline<info.height) {
        auto* row=rgb.data()+std::size_t(decoder.output_scanline)*info.width*3;
        require(jpeg_read_scanlines(&decoder,&row,1)==1,"JPEG row decode");
    }
    jpeg_finish_decompress(&decoder); jpeg_destroy_decompress(&decoder);
    std::vector<std::uint16_t> result(assets::kBackgroundPixels);
    std::array<std::uint8_t,16*16*3> block{}; std::size_t written=0;
    for(unsigned top=0;top<info.height;top+=16)for(unsigned left=0;left<info.width;left+=16) {
        const auto width=std::min(16u,info.width-left),height=std::min(16u,info.height-top);
        for(unsigned y=0;y<height;++y)std::memcpy(block.data()+y*width*3,rgb.data()+((top+y)*info.width+left)*3,width*3);
        std::size_t count=0;
        require(assets::writeBackgroundBlock(info,left,top,left+width-1,top+height-1,block.data(),width*height*3,
            result.data(),result.size(),count),"Production background block conversion failed");
        written+=count;
    }
    require(written==assets::kBackgroundPixels,"Background coverage mismatch"); return result;
}
void ppm(const std::filesystem::path& path,const std::uint16_t* pixels) {
    std::ofstream file(path,std::ios::binary); file<<"P6\n412 412\n255\n";
    for(std::size_t i=0;i<ui::kPixels;++i) {
        const auto value=pixels[i]; const char rgb[]{static_cast<char>(((value>>11)&31)*255/31),
            static_cast<char>(((value>>5)&63)*255/63),static_cast<char>((value&31)*255/31)};
        file.write(rgb,3);
    }
    require(file.good(),"Preview frame write failed");
}
State encounter(std::uint32_t& world,std::uint32_t& profile) {
    constexpr std::uint32_t form=18;
    for(world=1;world<100000;++world)if(selectWildForm(1,world,11,1)==form)break;
    require(world<100000,"No deterministic Agumon seed found");
    for(profile=1;profile<=128;++profile) {
        auto state=newDevice(profile);
        require(apply(state,Action::Hatch,1)==Error::None&&apply(state,Action::WorldSeed,world)==Error::None&&
            apply(state,Action::Mode,1)==Error::None&&apply(state,Action::Explore,1000)==Error::None,"Synthetic fixture actions failed");
        require(state.wildFormId==form,"World seed did not select exact Agumon");
        require(applyAutoFight(state)==Error::None,"Attack-only Auto failed");
        if(state.autoCapture!=AutoCapture::Awaiting)continue;
        auto preview=state;
        require(apply(preview,Action::RingCapture,(capturering::kMaxRadius-capturering::sample(0,form).targetRadius)*capturering::kCycleMs/(capturering::kMaxRadius-capturering::kMinRadius))==Error::None,"Capture probe failed");
        // Deterministic fixture selection only; no live save or attempt reroll.
        if(preview.lastCapture.result==CaptureResult::Captured)return state;
    }
    require(false,"No deterministic successful capture fixture"); return {};
}
}
int main(int argc,char** argv) {
    require(argc==4,"Usage: render-production-capture-preview OUTPUT_DIR PRIVATE_SD_DIRECTORY MEADOW_JPEG");
    const std::filesystem::path out=argv[1]; std::filesystem::create_directories(out/"frames");
    auto scene=background(argv[3]); DecodedForm wild,partner; wild.load(argv[2],18); partner.load(argv[2],11);
    require(sprite::localFormFacing(18)==ui::SpriteFacing::Left,"Agumon orientation is not the audited native facing");
    std::uint32_t world=0,profile=0; State state=encounter(world,profile);
    std::printf("Deterministic fixture: profile=%u world=%u form=%u HP=%u/%u chance=%u\n",profile,world,state.wildFormId,state.wildHp,state.wildMaxHp,captureChance(state));
    const auto fixture=state; ui::Model model; model.writable=true; ui::Controller controller;
    battlepresentation::Sequencer playback;
    constexpr std::uint64_t epoch=100;
    const auto target=capturering::sample(0,state.wildFormId).targetRadius;
    const std::uint64_t pressAt=capturering::kCycleMs+(capturering::kMaxRadius-target)*capturering::kCycleMs/(capturering::kMaxRadius-capturering::kMinRadius);
    const auto releaseAt=pressAt+100;
    bool pressed=false,released=false,afterSaved=false;unsigned frames=0;std::uint32_t thrownPhase=0,thrownChance=0;unsigned gradesSeen=0;
    std::array<std::uint16_t,ui::kPixels+2> frame{};
    for(std::uint64_t elapsed=0;elapsed<=8500;elapsed+=33) {
        const auto now=epoch+elapsed;
        if(playback.locked())playback.poll(now);
        model.battle=playback.locked()?&playback.view():nullptr;
        controller.update(state,model);
        if(!pressed&&elapsed>=pressAt) {
            require(controller.screen()==ui::Screen::Capture,"Ring screen unavailable");
            const auto intent=controller.touch(state,model,{ui::TouchKind::Down,300,180,now});
            require(intent.kind==ui::IntentKind::GameAction&&intent.action==Action::RingCapture&&capturering::sample(intent.value,state.wildFormId).grade==capturering::Grade::Green,"Arena tap did not produce the sampled green ring phase");
            thrownPhase=intent.value;thrownChance=ringCaptureChance(state,thrownPhase);
            const auto before=state; require(apply(state,intent.action,intent.value)==Error::None,"Authoritative capture rejected");
            require(state.lastCapture.chance==thrownChance,"Displayed and actual roll odds differ");
            require(playback.startTactical(before,state,intent.action,intent.value,now),"Committed capture presentation rejected");
            controller.resolve(); pressed=true; model.battle=&playback.view(); controller.update(state,model);
        }
        if(pressed&&!released&&elapsed>=releaseAt) {
            require(!controller.touch(state,model,{ui::TouchKind::Up,300,180,now}),"Release must not repeat the direct tap");
            released=true;
        }
        const auto request=controller.artRequest(state,model,now);
        require(request.formId==18&&request.sceneId&&!std::strcmp(request.sceneId,"scene-meadow-412-v1"),"Renderer requested wrong creature or capture scene");
        model.artwork={};model.artwork.background=scene.data();model.artwork.backgroundPixels=scene.size();model.artwork.backgroundId=request.sceneId;
        model.artwork.sprite=wild.frame(request);
        const auto partnerRequest=controller.partnerArtRequest(state,model,now);
        model.partnerArtwork=partnerRequest.formId?partner.frame(partnerRequest):ui::SpriteFrame{};
        frame.fill(0xbeef); require(controller.render(state,model,frame.data()+1,ui::kPixels,now),"Actual native renderer failed");
        require(frame.front()==0xbeef&&frame.back()==0xbeef,"Framebuffer guard changed");
        for(int y=0;y<ui::kSize;++y)for(int x=0;x<ui::kSize;++x)require(ui::Controller::inside(x,y)||frame[1+y*ui::kSize+x]==0,"Circular clip failed");
        char name[40]; std::snprintf(name,sizeof(name),"frame-%04u.ppm",frames++); ppm(out/"frames"/name,frame.data()+1);
        if(!elapsed)ppm(out/"capture-entry.ppm",frame.data()+1);
        if(!pressed) {
            const auto grade=capturering::sample(elapsed,fixture.wildFormId).grade;
            const auto bit=1u<<static_cast<unsigned>(grade);
            if(!(gradesSeen&bit)) {
                ppm(out/(grade==capturering::Grade::Green ? "capture-green.ppm" : grade==capturering::Grade::Orange ? "capture-orange.ppm" : "capture-red.ppm"),frame.data()+1);
                gradesSeen|=bit;
            }
        }
        if(!pressed&&elapsed+33>=pressAt)ppm(out/"capture-on-target.ppm",frame.data()+1);
        if(released&&playback.view().captureElapsedMs>=700&&!afterSaved) { ppm(out/"capture-wiggle.ppm",frame.data()+1);afterSaved=true; }
        if(elapsed>=8400)ppm(out/"capture-result.ppm",frame.data()+1);
    }
    require(gradesSeen==7,"Preview must show red, orange and green");
    require(released&&state.lastCapture.result==CaptureResult::Captured&&state.captures==fixture.captures+1,"One exact captured result required");
    std::ofstream metadata(out/"render.json");
    metadata<<"{\n  \"renderer\":\"native deviceui::Controller\",\n  \"privateProductionArt\":true,\n  \"formId\":18,\n  \"name\":\"Agumon\",\n  \"nativeFacing\":\"left\",\n  \"captureMirrored\":false,\n  \"sceneId\":\"scene-meadow-412-v1\",\n  \"hostJpegDecoder\":\"libjpeg; shared production validation, block conversion and 60% dimming\",\n  \"injectedProfileSeed\":"<<profile<<",\n  \"injectedWorldSeed\":"<<world<<",\n  \"fixture\":\"newDevice, hatch1, world-seed, modeAuto, explore1000, auto-fight; no direct state overrides\",\n  \"tap\":{\"x\":300,\"y\":180,\"action\":\"ring-capture\",\"phaseMs\":"<<thrownPhase<<",\"chance\":"<<thrownChance<<"},\n  \"captureResult\":\"captured\",\n  \"captureAttempts\":1,\n  \"targetOpacityPercent\":75,\n  \"gradesShown\":[\"red\",\"orange\",\"green\"],\n  \"ringTargetRadius\":"<<target<<",\n  \"frames\":"<<frames<<",\n  \"frameStepMs\":33,\n  \"width\":412,\n  \"height\":412,\n  \"framebufferAndCircularClipChecks\":\"PASS\",\n  \"devicesAccessed\":false,\n  \"networkUsed\":false\n}\n";
    require(metadata.good(),"Preview metadata write failed");
    std::printf("%u production capture frames; exactAgumon18, left-facing, meadow; profile=%u world=%u; one actual arena tap/capture\n",frames,profile,world);
}
