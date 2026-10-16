// Host-only execution of the shipped core, presentation and framebuffer renderer.
// No device access. Scripted fixtures are synthetic and never load the user's save.
#include "digi_modify.hpp"
#include "digi_audio.hpp"
#include "music_synth.hpp"
#include "live_battle.hpp"
#include "device_ui.hpp"
#include "battle_presentation.hpp"
#include "local_form_art.hpp"
#include "local_form_facing.hpp"
#include "forms.hpp"
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
using namespace digivice;
#define REQUIRE(x) do{if(!(x)){std::fprintf(stderr,"SIM FAIL %d: %s\n",__LINE__,#x);std::exit(2);}}while(0)
const char* actionName(Action a){return a==Action::Magic?"magic":a==Action::Attack?"attack":a==Action::Heavy?"heavy":a==Action::AutoFight?"auto-fight":a==Action::AutoResume?"auto-resume":a==Action::Focus?"focus":a==Action::NextExpedition?"next-expedition":"action";}
struct Art {
 sprite::LocalFormArt file;
 std::uint16_t pixels[1024]{};std::uint8_t mask[128]{};
 explicit Art(const char* dir):file(dir){}
 deviceui::SpriteFrame get(deviceui::ArtRequest r){
  if(!r.formId)return {};
  if(file.formId()!=r.formId)REQUIRE(file.open(r.formId)==sprite::Result::Ok);
  const auto& info=file.info();const auto& clip=info.clips[static_cast<unsigned>(r.animation)];
  unsigned left=info.width,top=info.height,right=0,bottom=0;
  for(unsigned frame=0;frame<clip.frames;++frame){
   REQUIRE(file.decode(r.animation,frame,pixels,1024,mask,128)==sprite::Result::Ok);
   for(unsigned y=0;y<info.height;++y)for(unsigned x=0;x<info.width;++x){unsigned i=y*info.width+x;if(mask[i/8]&(1u<<(i%8))){left=std::min(left,x);top=std::min(top,y);right=std::max(right,x);bottom=std::max(bottom,y);}}
  }
  REQUIRE(left<=right&&top<=bottom);
  REQUIRE(file.decode(r.animation,(r.elapsedMs/clip.frameMs)%clip.frames,pixels,1024,mask,128)==sprite::Result::Ok);
  deviceui::SpriteFrame f;f.formId=r.formId;f.animation=r.animation;f.pixels=pixels;f.pixelCount=1024;f.mask=mask;f.maskBytes=128;f.width=info.width;f.height=info.height;f.nativeFacing=sprite::localFormFacing(r.formId);f.contentX=left;f.contentY=top;f.contentWidth=right-left+1;f.contentHeight=bottom-top+1;
  return f;
 }
};
struct Sim {
 State state;deviceui::Controller ui;deviceui::Model model; battlepresentation::Sequencer battle;
 digimodify::ReaderLatch latch;digimodify::Overlay overlay;Art own,enemy;
 std::vector<std::uint16_t> pixels=std::vector<std::uint16_t>(deviceui::kPixels);
 std::vector<unsigned char> rgb=std::vector<unsigned char>(deviceui::kPixels*3);std::uint64_t now=0;FILE* frames;FILE* log;FILE* pcm;device::AudioMixer mixer;
 Sim(const char* assets,const char* output):own(assets),enemy(assets){
  frames=std::fopen(output,"wb");REQUIRE(frames);log=std::fopen((std::string(output)+".events.jsonl").c_str(),"w");REQUIRE(log);
  pcm=std::fopen((std::string(output)+".s16le").c_str(),"wb");REQUIRE(pcm);
  model.writable=true;model.starterStage=onboarding::Stage::Complete;
 }
 ~Sim(){std::fclose(frames);std::fclose(log);std::fclose(pcm);}
 void sound(device::AudioCue cue){if(cue==device::AudioCue::Count)return;mixer.start(cue);std::fprintf(log,"{\"ms\":%llu,\"event\":\"%s\",\"audio\":true,\"tier\":%u}\n",now,device::cueName(cue),state.expeditionTier);}
 void commit(State candidate,const char* event){
  Snapshot bytes;REQUIRE(encodeSnapshot(candidate,bytes));State readback;
  REQUIRE(decodeSnapshot(bytes.bytes,sizeof bytes.bytes,readback)==SnapshotStatus::Ok);state=readback;
  std::fprintf(log,"{\"ms\":%llu,\"event\":\"%s\",\"seq\":%u,\"hp\":%u,\"enemyHp\":%u,\"effect\":%u,\"used\":%s,\"tier\":%u,\"phase\":%u,\"critical\":%u}\n",now,event,state.sequence,state.hp,state.wildHp,state.digiModifyReady,state.cardUsed?"true":"false",state.expeditionTier,unsigned(state.phase),state.lastCritical);
 }
 void sample(digimodify::Sample value){auto e=latch.observe(value,now);if(!e.present)return;State next;
  const auto result=digimodify::prepare(state,digimodify::Authority::LocalWild,true,e.cardId,next);
  REQUIRE(result==digimodify::Result::Ready);commit(next,digimodify::find(e.cardId)->name);overlay.start(now,state.encounters,digimodify::find(e.cardId)->effect,state.digiModifyReady);sound(device::cardCue(digimodify::find(e.cardId)->effect));
 }
 void frame(){
  battle.poll(now);sound(device::battleCue(battle.consumeCue(),battle.view()));model.battle=battle.locked()?&battle.view():nullptr;model.digiModify=overlay.view(now,state,battle.locked());
  ui.update(state,model);model.artwork.sprite=enemy.get(ui.artRequest(state,model,now));model.partnerArtwork=own.get(ui.partnerArtRequest(state,model,now));
  REQUIRE(ui.render(state,model,pixels.data(),pixels.size(),now));
  for(std::size_t i=0;i<pixels.size();++i){auto p=pixels[i];rgb[i*3]=((p>>11)&31)*255/31;rgb[i*3+1]=((p>>5)&63)*255/63;rgb[i*3+2]=(p&31)*255/31;}
  REQUIRE(std::fwrite(rgb.data(),1,rgb.size(),frames)==rgb.size());
  std::int16_t audio[device::kAudioSampleRate/20];
  for(auto& sample:audio)sample=mixer.sample(50,false,device::MusicScene::Quiet);
  REQUIRE(std::fwrite(audio,sizeof(audio[0]),device::kAudioSampleRate/20,pcm)==device::kAudioSampleRate/20);now+=50;
 }
 void wait(unsigned ms){for(unsigned i=0;i<ms;i+=50)frame();}
 void card(unsigned id){for(unsigned i=0;i<8;++i){sample({digimodify::Presence::Absent,0});frame();}for(unsigned i=0;i<24;++i){sample({digimodify::Presence::Present,id});frame();}}
 void action(Action action,unsigned value=0){State before=state,next=state;autobattle::Trace trace;
  auto err=livebattle::applyAction(next,action,value,&trace);if(err!=Error::None)std::fprintf(stderr,"action %s: %s\n",actionName(action),errorText(err));REQUIRE(err==Error::None);commit(next,actionName(action));
  const bool automatic=livebattle::automatic(action);
  if(automatic&&trace.count)REQUIRE(battle.startAuto(trace,now,&before));
  else if(!automatic && (action==Action::Attack||action==Action::Magic||action==Action::Heavy||action==Action::Flick||action==Action::RingCapture)) REQUIRE(battle.startTactical(before,state,action,value,now));
 }
 void play(){while(battle.locked())frame();}
 void autoTurn(){if(state.autoCapture==AutoCapture::Awaiting)action(Action::AutoResume);else if(state.autoCapture==AutoCapture::FocusStrike||state.autoCapture==AutoCapture::FocusBlock)action(Action::Focus,kFocusNoTap);else action(Action::AutoFight);play();}
 void setup(bool automatic,bool dungeon=false){
  auto s=newDevice(381);unsigned starter=1;while(starter<=8 && !forms::canReach(starterForm(s,starter),61))++starter;REQUIRE(starter<=8);REQUIRE(apply(s,Action::Hatch,starter)==Error::None);REQUIRE(apply(s,Action::WorldSeed,171)==Error::None);
  auto& m=s.collection[0];m.formId=61;m.species=static_cast<Species>(forms::find(61)->lineage);m.level=s.level=24;m.xp=xpForLevel(24);m.hp=s.hp=combat::formProfile(61,24).stats.maxHp;m.energy=s.energy=100;m.bond=s.bond=80;s.journal[60/32]|=1u<<(60%32);
  REQUIRE(isValid(s));REQUIRE(apply(s,Action::Mode,automatic?1:0)==Error::None);
  if(dungeon){s.dungeonKeys=3;REQUIRE(apply(s,Action::StartExpedition,0)==Error::None);}else{
   REQUIRE(apply(s,Action::Walk,100)==Error::None);s.wildFormId=139;s.wildSpecies=static_cast<Species>(forms::find(139)->lineage);s.wildLevel=24;s.wildMaxHp=s.wildHp=combat::formProfile(139,24).stats.maxHp;
  }REQUIRE(isValid(s));ui=deviceui::Controller{};battle.cancel();overlay.cancel();commit(s,"synthetic fixture");
 }
};
int main(int argc,char** argv){REQUIRE(argc==4);Sim s(argv[1],argv[3]);
 if(!std::strcmp(argv[2],"manual")){
  s.setup(false);s.wait(1000);s.card(0xD101);s.wait(400);s.action(Action::Magic);s.play();s.wait(800);
 } else if(!std::strcmp(argv[2],"fx")){
  for(unsigned id:{0xD101u,0xD102u,0xD103u,0xD104u,0xD106u}){
   s.setup(id!=0xD101);s.wait(650);
   if(id==0xD104){s.autoTurn();s.wait(250);}
   s.card(id);s.wait(150);
   if(id==0xD101){s.action(Action::Magic);s.play();}else s.autoTurn();
   s.wait(650);
  }
 } else if(!std::strcmp(argv[2],"element")){
  s.setup(false);s.wait(650);s.action(Action::Magic);s.play();s.wait(700);
  s.setup(false);s.wait(650);s.card(0xD106);s.wait(200);s.action(Action::Magic);s.play();s.wait(700);
  s.setup(true);s.wait(650);s.card(0xD106);s.wait(200);s.autoTurn();s.wait(700);
 } else if(!std::strcmp(argv[2],"auto")){
  for(unsigned id:{0xD102u,0xD103u,0xD104u}){
   s.setup(true);s.wait(700);if(id==0xD104){s.autoTurn();s.wait(300);}s.card(id);s.wait(300);
   for(unsigned turn=0;turn<2 && s.state.phase==Phase::Encounter;++turn)s.autoTurn();s.wait(700);
  }
 } else if(!std::strcmp(argv[2],"dungeon")){
  s.setup(true,true);s.wait(900);
  for(unsigned tier=1;tier<=3;++tier){REQUIRE(s.state.expeditionTier==tier);s.card(0xD100+tier);s.wait(250);
   for(unsigned turn=0;turn<60 && s.state.phase==Phase::Encounter;++turn)s.autoTurn();
   REQUIRE(s.state.phase==(tier<3?Phase::Expedition:Phase::Home));s.wait(900);
   if(tier<3){s.action(Action::NextExpedition);s.wait(500);}
  }REQUIRE(s.state.dungeonClears==1);s.wait(900);
 } else return 3;
 std::fprintf(stderr,"Recorded %llu ms of actual core/UI execution\n",s.now);
}
