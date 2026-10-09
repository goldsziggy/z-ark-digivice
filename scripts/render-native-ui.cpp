// Local-only visual audit of the real firmware renderer. Never reads saves,
// device ports or credentials. Private inputs/outputs are caller-selected.
#include "device_ui.hpp"
#include "setup_ui.hpp"
#include "sprite.hpp"
#include "sprite_bounds.hpp"
#include "local_form_facing.hpp"
#include "background_decode.hpp"
#include "forms.hpp"
#include "../firmware/tests/catalog_fixture.hpp"
#include <array>
#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <jpeglib.h>

using namespace digivice;
namespace ui=digivice::deviceui;
void require(bool ok,const char* reason){if(!ok){std::fprintf(stderr,"Visual audit failed: %s\n",reason);std::exit(1);}}
struct Harness {
 State state=newDevice(12345); ui::Model model{}; onboarding::StarterController starter; ui::Controller controller; std::uint64_t now=100;
 Harness(){model.writable=true;model.stepsAvailable=true;model.encounterReady=true;model.lifetimeSteps=12345;model.sessionSteps=426;model.stepStatus="PEDOMETER READY";sync();}
 void sync(){if(state.starterOfferSeed){model.starterCount=11;starter.configureChoices(11);model.starterFormId=starterForm(state,starter.selectedId());}model.starterStage=starter.stage();model.selectedId=starter.selectedId();controller.update(state,model);}
 ui::Intent tap(int x,int y){sync();now+=30;controller.touch(state,model,{ui::TouchKind::Down,static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),now});now+=60;auto result=controller.touch(state,model,{ui::TouchKind::Up,static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),now});dispatch(result);return result;}
 ui::Intent swipe(int x,int y,int endX,int endY){sync();now+=30;controller.touch(state,model,{ui::TouchKind::Down,static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),now});now+=100;auto result=controller.touch(state,model,{ui::TouchKind::Up,static_cast<std::int16_t>(endX),static_cast<std::int16_t>(endY),now});dispatch(result);return result;}
 void dispatch(ui::Intent intent){onboarding::Request request;switch(intent.kind){
 case ui::IntentKind::GameAction:require(apply(state,intent.action,intent.value)==Error::None,"synthetic action");break;
 case ui::IntentKind::StarterConfirm:request=starter.input(onboarding::Input::Confirm);break;
 case ui::IntentKind::StarterNext:starter.input(onboarding::Input::Next);break;
 case ui::IntentKind::StarterPrevious:starter.input(onboarding::Input::Previous);break;
 case ui::IntentKind::SleepTimeout:model.sleepTimeoutSeconds=static_cast<std::uint16_t>(intent.value);break;
 case ui::IntentKind::ToggleMute:model.muted=!model.muted;break;
 case ui::IntentKind::ToggleMusic:model.musicEnabled=!model.musicEnabled;break;
 case ui::IntentKind::Volume:model.volumePercent=static_cast<std::uint8_t>(intent.value);break;
 case ui::IntentKind::StarterBack:starter.input(onboarding::Input::HoldBack);break;
 default:break;}
 if(request.hatchId){require(apply(state,Action::Hatch,request.hatchId)==Error::None,"synthetic hatch");starter.resolve(true);}controller.resolve();sync();}
 void hatch(){tap(206,285);tap(206,312);tap(206,278);}
 void homePanel(ui::HomePanel panel){require(controller.screen()==ui::Screen::Home,"home panel navigation");for(unsigned i=0;i<4&&controller.homePanel()!=panel;++i)tap(355,190);require(controller.homePanel()==panel,"selected home panel");}
 void openHome(ui::HomePanel panel){homePanel(panel);tap(206,323);}
};
std::vector<std::uint8_t> bytes(const std::string& path){std::ifstream file(path,std::ios::binary);require(file.good(),"asset file");return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
struct JpegError {jpeg_error_mgr base;std::jmp_buf jump;};
void jpegFailure(j_common_ptr common){auto* error=reinterpret_cast<JpegError*>(common->err);std::longjmp(error->jump,1);}
void background(const std::string& path,std::array<std::uint16_t,ui::kPixels>& out){
 auto encoded=bytes(path); assets::JpegInfo dimensions;require(assets::inspectBackgroundJpeg(encoded.data(),encoded.size(),dimensions),"native JPEG preflight");
 jpeg_decompress_struct decoder{};JpegError error{};decoder.err=jpeg_std_error(&error.base);error.base.error_exit=jpegFailure;
 if(setjmp(error.jump)){jpeg_destroy_decompress(&decoder);require(false,"host JPEG decode");}
 jpeg_create_decompress(&decoder);jpeg_mem_src(&decoder,encoded.data(),encoded.size());jpeg_read_header(&decoder,TRUE);decoder.out_color_space=JCS_RGB;jpeg_start_decompress(&decoder);
 require(decoder.output_width==dimensions.width && decoder.output_height==dimensions.height && decoder.output_components==3 && dimensions.width<=480 && dimensions.height<=480,"JPEG dimensions");
 std::array<std::uint8_t,480*480*3> pixels{};
 while(decoder.output_scanline<decoder.output_height){auto* row=pixels.data()+decoder.output_scanline*decoder.output_width*3;require(jpeg_read_scanlines(&decoder,&row,1)==1,"JPEG row");}
 jpeg_finish_decompress(&decoder);jpeg_destroy_decompress(&decoder);
 std::size_t written=0;std::array<std::uint8_t,16*16*3> block{};
 for(unsigned y=0;y<dimensions.height;y+=16)for(unsigned x=0;x<dimensions.width;x+=16){const auto w=std::min(16u,dimensions.width-x),h=std::min(16u,dimensions.height-y);
  for(unsigned row=0;row<h;++row)std::copy_n(pixels.data()+((y+row)*dimensions.width+x)*3,w*3,block.data()+row*w*3);
  std::size_t count=0;require(assets::writeBackgroundBlock(dimensions,x,y,x+w-1,y+h-1,block.data(),w*h*3,out.data(),out.size(),count),"native background conversion");written+=count;}
 require(written==ui::kPixels,"complete background");
}
struct Frame {
 std::vector<std::uint8_t> encoded;std::array<std::uint16_t,sprite::kMaximumPixels> pixels{};std::array<std::uint8_t,sprite::kMaximumMaskBytes> mask{};
 static bool read(void* context,std::size_t offset,void* target,std::size_t length){const auto& b=static_cast<Frame*>(context)->encoded;if(offset>b.size()||length>b.size()-offset)return false;std::memcpy(target,b.data()+offset,length);return true;}
 bool load(const std::string& pack,std::uint32_t form,sprite::Animation animation,std::uint64_t elapsed,ui::SpriteFrame& result){result={};if(!form || !forms::productionForm(form))return false;char name[32];std::snprintf(name,sizeof(name),"/DSF%05u.DVA",form);std::ifstream file(pack+name,std::ios::binary);if(!file.good())return false;
  encoded={std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};sprite::Sprite decoder;require(decoder.open({this,read,encoded.size()})==sprite::Result::Ok,"native DVA open");const auto info=decoder.info();const auto& clip=info.clips[static_cast<unsigned>(animation)];require(clip.frameMs&&clip.frames,"animation clip");
  const auto stride=static_cast<std::size_t>(info.width)*info.height/8;
  std::array<std::uint8_t,sprite::kMaximumMaskBytes*8> clipMasks{};
  require(clip.frames<=8,"bounded clip");
  for(unsigned frame=0;frame<clip.frames;++frame)require(decoder.decode(animation,frame,pixels.data(),pixels.size(),clipMasks.data()+frame*stride,stride)==sprite::Result::Ok,"native union frame");
  sprite::OpaqueBounds bounds;require(sprite::opaqueClipBounds(clipMasks.data(),clip.frames*stride,info.width,info.height,clip.frames,{clip.frameMs,0,clip.frames},bounds),"stable opaque union");
  require(decoder.decode(animation,(elapsed/clip.frameMs)%clip.frames,pixels.data(),pixels.size(),mask.data(),mask.size())==sprite::Result::Ok,"native DVA frame");
  result={form,animation,pixels.data(),static_cast<std::size_t>(info.width)*info.height,mask.data(),stride,info.width,info.height,bounds.x,bounds.y,bounds.width,bounds.height};result.nativeFacing=sprite::localFormFacing(form);return true;}
};
struct Audit {
 std::string pack,output;std::array<std::uint16_t,ui::kPixels> scene{};std::array<std::uint16_t,ui::kPixels+2> pixels{};Frame main,partner;FILE* manifest=nullptr;
 Audit(const char* p,const char* o):pack(p),output(o){manifest=std::fopen((output+"/screens.tsv").c_str(),"w");require(manifest,"manifest output");std::fprintf(manifest,"file\ttitle\tscreen\tmain_form\tmain_decoded\tpartner_form\tpartner_decoded\tscene\n");}
 ~Audit(){if(manifest)std::fclose(manifest);}
 void write(const char* name){require(pixels.front()==0xbeef&&pixels.back()==0xbeef,"framebuffer canaries");for(int y=0;y<ui::kSize;++y)for(int x=0;x<ui::kSize;++x)if(!ui::Controller::inside(x,y))require(pixels[1+y*ui::kSize+x]==0,"circular clipping");
  auto* file=std::fopen((output+"/"+name+".ppm").c_str(),"wb");require(file,"screen output");std::fprintf(file,"P6\n412 412\n255\n");for(std::size_t i=1;i<=ui::kPixels;++i){auto p=pixels[i];const unsigned char rgb[]{static_cast<unsigned char>(((p>>11)&31)*255/31),static_cast<unsigned char>(((p>>5)&63)*255/63),static_cast<unsigned char>((p&31)*255/31)};require(std::fwrite(rgb,1,3,file)==3,"pixel write");}require(std::fclose(file)==0,"screen close");}
 void save(const char* name,const char* title,Harness& h,ui::Screen explicitScreen=ui::Screen::Egg,std::uint64_t at=1000,bool omitPartner=false,bool omitEnemy=false){h.sync();
  const ui::Screen expected[]{ui::Screen::Egg,ui::Screen::Starter,ui::Screen::StarterReview,ui::Screen::Home,ui::Screen::Care,ui::Screen::Home,ui::Screen::EncounterSettings,ui::Screen::Settings,ui::Screen::ModeReview,ui::Screen::Collection,ui::Screen::Stats,ui::Screen::Stats,ui::Screen::Stats,ui::Screen::Stats,ui::Screen::Evolution,ui::Screen::Evolution,ui::Screen::Evolution,ui::Screen::EvolutionReview,ui::Screen::EvolutionResult,ui::Screen::Encounter,ui::Screen::Battle,ui::Screen::Capture,ui::Screen::Battle,ui::Screen::Nearby,ui::Screen::NearbyReview,ui::Screen::Nearby,ui::Screen::Nearby,ui::Screen::Nearby,ui::Screen::Nearby,ui::Screen::Nearby,ui::Screen::Egg,ui::Screen::Egg,ui::Screen::Nearby,ui::Screen::Result};
  const auto index=std::strtoul(name,nullptr,10);require(index>=1&&index<=199&&h.controller.screen()==(index<=34 ? expected[index-1] : explicitScreen)&&isValid(h.state),"expected screen and valid synthetic state");
  const auto request=h.controller.artRequest(h.state,h.model,at);const std::string sceneId=request.sceneId?request.sceneId:"scene-digital-412-v1";auto key=sceneId.substr(6,sceneId.size()-13);for(auto& ch:key)ch=static_cast<char>(std::toupper(ch));background(pack+"/"+key+".JPG",scene);
  h.model.artwork={scene.data(),scene.size(),request.sceneId,{}};const auto loaded=!omitEnemy&&main.load(pack,request.formId,request.animation,request.elapsedMs,h.model.artwork.sprite);
  const auto partnerRequest=h.controller.partnerArtRequest(h.state,h.model,at);
  h.model.partnerArtwork={};const auto owned=!omitPartner&&partner.load(pack,partnerRequest.formId,partnerRequest.animation,partnerRequest.elapsedMs,h.model.partnerArtwork);pixels.fill(0xbeef);require(h.controller.render(h.state,h.model,pixels.data()+1,ui::kPixels,at),"native renderer");write(name);
  std::fprintf(manifest,"%s.png\t%s\t%s\t%u\t%d\t%u\t%d\t%s\n",name,title,ui::screenName(h.controller.screen()),request.formId,loaded,partnerRequest.formId,owned,request.sceneId?request.sceneId:"");}
 void setup(const char* name,const char* title,setupui::Controller& controller,const setupui::Model& model){pixels.fill(0xbeef);require(controller.render(model,pixels.data()+1,ui::kPixels),"setup renderer");write(name);std::fprintf(manifest,"%s.png\t%s\tsetup\t0\t0\t0\t0\tnative setup palette\n",name,title);}
};
void battleSequences(Audit& audit) {
 Harness h;h.hatch();require(apply(h.state,Action::Walk,100)==Error::None,"sequence encounter");
 h.state.wildFormId=18;h.state.wildSpecies=static_cast<Species>(forms::find(18)->lineage);h.state.wildLevel=1;h.state.wildMaxHp=h.state.wildHp=forms::stats(18,1).maxHp;h.sync();h.tap(206,274);
 require(h.controller.screen()==ui::Screen::Battle,"sequence battle picker");
 unsigned index=53;char name[96],title[128];
 auto save=[&](const char* label,std::uint64_t at,bool omitPartner=false,bool omitEnemy=false) {
  std::snprintf(name,sizeof(name),"%02u-battle-sequence",index++);
  audit.save(name,label,h,ui::Screen::Battle,at,omitPartner,omitEnemy);
  if(!omitPartner)require(h.model.partnerArtwork.pixels&&h.model.partnerArtwork.formId==11,"exact Impmon throughout playback");
  if(!omitEnemy)require(h.model.artwork.sprite.pixels&&h.model.artwork.sprite.formId==18,"exact Agumon throughout playback");
 };
 save("Impmon / Agumon - idle picker",h.now);
 auto* timeline=std::fopen((audit.output+"/battle-sequence.tsv").c_str(),"w");require(timeline,"timeline");
 std::fprintf(timeline,"file\tturn\telapsed_ms\tactor\tflash\tplayer_animation\tenemy_animation\tcommitted_sequence\n");
 battlepresentation::Sequencer sequencer;
 const std::uint32_t moments[]{0,160,350,520,700,1199,1200,1360,1550,1720,1900,2399,2400,3999,4000};
 for(unsigned turn=1;turn<=3;++turn) {
  const auto before=h.state;const auto intent=h.swipe(206,220,206,150);require(intent.action==Action::Attack,"real tactical intent");
  require(sequencer.startTactical(before,h.state,intent.action,intent.value,h.now),"committed turn playback");h.model.battle=&sequencer.view();const auto at=h.now;
  for(auto elapsed:moments) {
   const auto& view=sequencer.poll(at+elapsed);const char* actor=view.actor==battlepresentation::Actor::Player?"Impmon":view.actor==battlepresentation::Actor::Opponent?"Agumon":"return";
   std::snprintf(title,sizeof(title),"Turn %u / %ums / %s%s",turn,elapsed,actor,view.flash?" impact":"");save(title,at+elapsed);
   const auto p=h.controller.partnerArtRequest(h.state,h.model,at+elapsed),e=h.controller.artRequest(h.state,h.model,at+elapsed);
   std::fprintf(timeline,"%s.png\t%u\t%u\t%s\t%d\t%u\t%u\t%u\n",name,turn,elapsed,actor,view.flash,static_cast<unsigned>(p.animation),static_cast<unsigned>(e.animation),h.state.sequence);
  }
  h.now=at+4000;h.model.battle=nullptr;h.sync();require(h.controller.screen()==ui::Screen::Battle,"return picker");
 }
 std::fclose(timeline);
 save("Missing partner / exact opponent",h.now,true,false);save("Exact partner / missing opponent",h.now,false,true);
 // A real two-party resolver supplies both attacker roles. Local Agumon stays
 // left; remote Impmon stays right throughout the turn and its return.
 Harness n;n.hatch();n.openHome(ui::HomePanel::Nearby);nearby::View net;net.stage=nearby::Stage::Playing;net.host=true;net.session=88;
 require(nearby::begin({1,18,1},{19,11,1},nearby::Mode::Tactical,444,net.match),"nearby sequence");n.model.nearby=&net;
 const std::uint32_t nearbyMoments[]{0,600,850,1200,1379,1800,2399,2400};
 for(unsigned turn=0;turn<2;++turn) {
  const auto attacker=net.match.attacker;require(nearby::resolve(net.match,net.match.sequence,attacker==0?nearby::Choice::Physical:nearby::Choice::Brace,attacker==1?nearby::Choice::Physical:nearby::Choice::Brace),"nearby committed exchange");
  for(auto elapsed:nearbyMoments) {
   n.model.nearbyTurnElapsedMs=elapsed;std::snprintf(name,sizeof(name),"%02u-nearby-sequence",index++);
   std::snprintf(title,sizeof(title),"Nearby %s / %ums",attacker==0?"Agumon attacks":"Impmon attacks",elapsed);
   audit.save(name,title,n,ui::Screen::Nearby,elapsed+20000);
   require(n.model.partnerArtwork.formId==18&&n.model.partnerArtwork.pixels&&n.model.artwork.sprite.formId==11&&n.model.artwork.sprite.pixels,"exact Nearby actors");
  }
 }
 for(bool host:{true,false}) {
  require(nearby::begin({1,91,1},{19,11,1},nearby::Mode::Tactical,444,net.match),"right-facing Gaomon");net.host=host;n.model.nearbyTurnElapsedMs=ui::kNoNearbyTurn;
  std::snprintf(name,sizeof(name),"%02u-gaomon-facing",index++);audit.save(name,host?"Gaomon native-right / Impmon native-left":"Impmon mirrored / Gaomon mirrored",n,ui::Screen::Nearby);
 }
 // Reverse the actual wild roles too, so Impmon's Attack and Hurt requests
 // (not only Nearby Idle + VFX) are decoded and rendered on the right.
 Harness reverse;reverse.hatch();reverse.state=stableMemberFixture(18);
 require(apply(reverse.state,Action::Walk,100)==Error::None,"reverse encounter");
 reverse.state.wildFormId=11;reverse.state.wildSpecies=Species::Impmon;reverse.state.wildLevel=1;
 reverse.state.wildMaxHp=reverse.state.wildHp=forms::stats(11,1).maxHp;reverse.sync();reverse.tap(206,274);
 battlepresentation::Sequencer reverseSequence;
 const auto reverseSave=[&](const char* label,std::uint64_t at,bool missing=false) {
  std::snprintf(name,sizeof(name),"%02u-impmon-right-sequence",index++);audit.save(name,label,reverse,ui::Screen::Battle,at,false,missing);
  require(reverse.model.partnerArtwork.formId==18&&reverse.model.partnerArtwork.pixels,"exact reverse Agumon partner");
  if(!missing)require(reverse.model.artwork.sprite.formId==11&&reverse.model.artwork.sprite.pixels,"exact Impmon opponent");
 };
 reverseSave("Agumon / Impmon - idle picker",reverse.now);
 auto* reversed=std::fopen((audit.output+"/impmon-right-sequence.tsv").c_str(),"w");require(reversed,"reverse timeline");
 std::fprintf(reversed,"file\tturn\telapsed_ms\tactor\tflash\tplayer_animation\tenemy_animation\tcommitted_sequence\n");
 for(unsigned turn=1;turn<=2;++turn) {
  const auto before=reverse.state;const auto intent=reverse.swipe(206,220,206,150);require(intent.action==Action::Attack,"reverse tactical intent");
  require(reverseSequence.startTactical(before,reverse.state,intent.action,intent.value,reverse.now),"reverse committed playback");reverse.model.battle=&reverseSequence.view();const auto at=reverse.now;
  for(auto elapsed:moments) {
   const auto& view=reverseSequence.poll(at+elapsed);const char* actor=view.actor==battlepresentation::Actor::Player?"Agumon":view.actor==battlepresentation::Actor::Opponent?"Impmon":"return";
   std::snprintf(title,sizeof(title),"Impmon RIGHT / turn %u / %ums / %s%s",turn,elapsed,actor,view.flash?" hit":"");reverseSave(title,at+elapsed);
   const auto p=reverse.controller.partnerArtRequest(reverse.state,reverse.model,at+elapsed),e=reverse.controller.artRequest(reverse.state,reverse.model,at+elapsed);
   std::fprintf(reversed,"%s.png\t%u\t%u\t%s\t%d\t%u\t%u\t%u\n",name,turn,elapsed,actor,view.flash,static_cast<unsigned>(p.animation),static_cast<unsigned>(e.animation),reverse.state.sequence);
  }
  reverse.now=at+4000;reverse.model.battle=nullptr;reverse.sync();require(reverse.controller.screen()==ui::Screen::Battle,"reverse return picker");
 }
 std::fclose(reversed);reverseSave("Unavailable Impmon art / neutral status",reverse.now,true);
 std::printf("PASS native battle sequence: %u screens; 5 committed wild turns with Impmon on both sides; both Nearby actors; exact local art; no device or save access\n",index-53);
}
int main(int argc,char** argv){require(argc==3 || (argc==4&&std::strcmp(argv[3],"--battle-sequences")==0),"usage: render-native-ui PACK OUTPUT [--battle-sequences]");Audit audit(argv[1],argv[2]);if(argc==4){battleSequences(audit);return 0;}
 Harness h;audit.save("01-egg","Egg - no saved hatch",h);h.tap(206,285);audit.save("02-starter","Starter carousel",h);h.tap(206,312);audit.save("03-hatch-review","Separate hatch confirmation",h);h.tap(206,278);audit.save("04-home","Home - Care carousel panel",h);
 h.homePanel(ui::HomePanel::Partners);audit.save("06-home-partners","Home - Partners carousel panel",h);h.homePanel(ui::HomePanel::Settings);audit.save("51-home-settings","Home - Settings carousel panel",h,ui::Screen::Home);h.homePanel(ui::HomePanel::Nearby);audit.save("52-home-nearby","Home - Nearby carousel panel",h,ui::Screen::Home);
 h.openHome(ui::HomePanel::Care);audit.save("05-care","Care",h);h.tap(206,365);h.openHome(ui::HomePanel::Settings);h.tap(206,194);audit.save("07-encounter-settings","Encounter pacing",h);h.tap(206,365);audit.save("08-settings","Device settings",h);h.tap(120,312);audit.save("09-mode-review","Auto mode review",h);h.tap(206,365);h.tap(206,365);
 h.openHome(ui::HomePanel::Partners);audit.save("10-owned","Owned instance carousel",h);h.tap(120,312);audit.save("11-stats","Derived combat stats",h);h.swipe(250,180,160,180);audit.save("12-care-stats","Care bond and XP",h);h.swipe(250,180,160,180);audit.save("13-skills","Effective named skills",h);h.swipe(250,180,160,180);audit.save("14-types","Type advantages",h);h.tap(206,312);audit.save("15-evolution-locked","Evolution eligibility",h);h.tap(280,312);audit.save("35-evolution-unmet","Exact unmet requirements",h,ui::Screen::Evolution);h.tap(206,365);h.tap(120,312);audit.save("16-evolution-stats","Before and after stats",h);h.swipe(250,180,160,180);audit.save("17-evolution-skills","Evolution skill preview",h);
 const auto* edge=forms::outgoing(activeMember(h.state)->formId,0);require(edge,"evolution route");h.state.level=h.state.collection[0].level=edge->minLevel;h.state.collection[0].xp=xpForLevel(edge->minLevel);h.state.bond=h.state.collection[0].bond=edge->minBond;h.sync();h.tap(120,312);audit.save("36-evolution-ready","Eligible candidate carousel",h,ui::Screen::Evolution);h.tap(280,312);audit.save("18-evolution-review","Separate evolution confirmation",h);h.tap(206,302);audit.save("19-evolution-result","Committed evolution result",h);
 Harness fight;fight.hatch();require(apply(fight.state,Action::Walk,100)==Error::None,"test-only encounter input");fight.state.wildFormId=18;fight.state.wildSpecies=Species::Agumon;fight.state.wildLevel=1;fight.state.wildMaxHp=fight.state.wildHp=forms::stats(18,1).maxHp;require(isValid(fight.state),"wild fixture");fight.sync();audit.save("20-encounter","Wild encounter",fight);fight.tap(206,274);audit.save("21-tactical","Tactical select then swipe up",fight);
 fight.state.wildHp=fight.state.wildMaxHp/2;require(isValid(fight.state),"capture fixture");fight.sync();fight.tap(280,306);require(fight.controller.screen()==ui::Screen::Capture,"capture screen reached");audit.save("22-capture","Distinct flick capture",fight);
 fight.tap(206,365);battlepresentation::View playback;playback.locked=true;playback.phase=battlepresentation::Phase::Player;playback.actor=battlepresentation::Actor::Player;playback.playerFormId=11;playback.enemyFormId=18;playback.playerHp=80;playback.playerMaxHp=92;playback.enemyHp=40;playback.enemyMaxHp=104;playback.moveName=combat::formProfile(11,1).magicSkill;playback.actorName=forms::find(11)->name;playback.move=autobattle::Move::Magic;playback.turn=2;playback.turnCount=7;playback.damage=12;playback.progressPermille=500;Harness autoFight;autoFight.hatch();require(apply(autoFight.state,Action::Mode,1)==Error::None,"Auto mode fixture");require(apply(autoFight.state,Action::Walk,100)==Error::None,"Auto encounter fixture");autoFight.model.battle=&playback;autoFight.sync();audit.save("23-auto-playback","Auto frozen trace playback",autoFight);
 Harness n;n.hatch();n.openHome(ui::HomePanel::Nearby);nearby::View net;net.stage=nearby::Stage::Discovering;net.host=true;net.peerCount=2;net.peers[0].fighter={19,18,1};net.peers[0].mac.bytes[5]=0xAB;net.peers[0].available=true;net.peers[1]=net.peers[0];net.peers[1].fighter={20,25,1};net.peers[1].mac.bytes[5]=0xBA;n.model.nearby=&net;n.sync();audit.save("24-nearby-discovery","Real-view Nearby carousel",n);n.tap(206,312);audit.save("25-nearby-review","Challenge review",n);n.tap(206,302);net.stage=nearby::Stage::Incoming;net.host=false;net.offered[0]={19,18,1};net.offered[1]={1,11,1};n.sync();audit.save("26-nearby-accept","Mutual consent",n);
 require(nearby::begin(net.offered[0],net.offered[1],nearby::Mode::Tactical,123,net.match),"nearby fixture");net.stage=nearby::Stage::Playing;n.sync();audit.save("27-nearby-defense","Role-specific defense",n);require(nearby::resolve(net.match,0,nearby::Choice::Magic,nearby::Choice::Ward),"resolved exchange");n.model.nearbyTurnElapsedMs=850;n.sync();audit.save("28-nearby-strike","Named move and guard feedback",n);n.model.nearbyTurnElapsedMs=1200;audit.save("29-nearby-impact","Timed impact with both sprites",n);n.model.nearbyTurnElapsedMs=2400;audit.save("30-nearby-attack","Tactical attack selection",n);
 net.match.hp[0]=1;require(nearby::valid(net.match),"nearby final fixture");while(net.match.status==nearby::Status::Active){const auto attacker=net.match.attacker;require(nearby::resolve(net.match,net.match.sequence,attacker==0?nearby::Choice::Physical:nearby::Choice::Ward,attacker==1?nearby::Choice::Physical:nearby::Choice::Ward),"finish duel");}net.stage=nearby::Stage::Finished;n.model.nearbyTurnElapsedMs=2400;n.sync();require(n.controller.screen()==ui::Screen::Nearby,"nearby result screen");audit.save("33-nearby-result","Nearby friendly result",n);
 Harness result;result.hatch();require(apply(result.state,Action::Walk,100)==Error::None,"result encounter");result.sync();result.state.wildHp=1;require(apply(result.state,Action::Magic)==Error::None,"result win");result.sync();require(result.controller.screen()==ui::Screen::Result&&result.state.phase==Phase::Home,"result screen reached");audit.save("34-wild-result","Wild battle result",result);
 Harness offers;require(apply(offers.state,Action::StarterOfferSeed,0x12345678)==Error::None,"persisted offer fixture");offers.sync();offers.tap(206,285);for(unsigned i=0;i<8;++i)offers.swipe(250,180,160,180);
 audit.save("37-offer-one","Persisted extra Rookie 1",offers,ui::Screen::Starter);offers.swipe(250,180,160,180);audit.save("38-offer-two","Persisted extra Rookie 2",offers,ui::Screen::Starter);offers.swipe(250,180,160,180);audit.save("39-offer-three","Persisted extra Rookie 3",offers,ui::Screen::Starter);
 const auto capturedState=[](CaptureResult result){for(unsigned seed=1;seed<200;++seed){auto s=newGame(seed);require(apply(s,Action::Explore,100)==Error::None,"capture search encounter");s.wildFormId=18;s.wildSpecies=Species::Agumon;s.wildLevel=1;s.wildMaxHp=forms::stats(18,1).maxHp;s.wildHp=s.wildMaxHp/2;require(apply(s,Action::Flick,result==CaptureResult::Miss ? 0 : 160*256+180)==Error::None,"committed visual throw");if(s.lastCapture.result==result)return s;}require(false,"capture result fixture found");return newGame();};
 Harness containment;containment.state=capturedState(CaptureResult::Captured);battlepresentation::View ball;ball.locked=true;ball.capturePresentation=true;ball.captureCaught=true;ball.captureAttempt=containment.state.lastCapture.attempt;ball.captureRemaining=3-ball.captureAttempt;ball.playerFormId=activeMember(containment.state)->formId;ball.enemyFormId=containment.state.lastCapture.targetFormId;containment.model.battle=&ball;containment.sync();
 ball.captureElapsedMs=250;audit.save("40-capture-throw","Committed throw - no odds yet",containment,ui::Screen::Battle);
 ball.captureChance=containment.state.lastCapture.chance;ball.captureElapsedMs=650;audit.save("41-capture-wiggle-one","Containment wiggle 1",containment,ui::Screen::Battle);
 ball.captureElapsedMs=1250;audit.save("42-capture-wiggle-two","Containment wiggle 2",containment,ui::Screen::Battle);
 ball.captureElapsedMs=1850;audit.save("43-capture-wiggle-three","Containment wiggle 3",containment,ui::Screen::Battle);
 ball.captureElapsedMs=2600;audit.save("44-capture-caught","Saved capture reveal",containment,ui::Screen::Battle);
 containment.state=capturedState(CaptureResult::Escaped);ball.captureCaught=false;ball.captureChance=containment.state.lastCapture.chance;ball.enemyFormId=containment.state.lastCapture.targetFormId;containment.sync();audit.save("45-capture-escaped","Saved escape reveal",containment,ui::Screen::Battle);
 containment.state=capturedState(CaptureResult::Miss);ball.captureMiss=true;ball.captureChance=0;ball.captureElapsedMs=1000;ball.enemyFormId=containment.state.lastCapture.targetFormId;containment.sync();audit.save("46-capture-miss","Miss - no chance displayed",containment,ui::Screen::Battle);
 require(apply(containment.state,Action::Flick,0)==Error::None&&apply(containment.state,Action::Flick,0)==Error::None,"third throw fixture");require(containment.state.message==Message::CaptureEnded,"gentle third failure");ball.captureAttempt=3;ball.captureRemaining=0;containment.sync();audit.save("47-capture-third-failure","Third failure ends encounter",containment,ui::Screen::Battle);
 Harness idle;idle.hatch();idle.openHome(ui::HomePanel::Settings);idle.tap(206,137);idle.tap(206,137);idle.tap(206,137);require(idle.model.sleepTimeoutSeconds==0,"sleep cycle");audit.save("48-screen-timeout","Screen idle off - durable intent",idle,ui::Screen::Settings);
 Harness duplicate;duplicate.state=stableMemberFixture(18);duplicate.state.sequence=101;duplicate.state.captures=duplicate.state.encounters=19;duplicate.state.steps=1900;duplicate.state.nextMemberId=21;duplicate.state.collectionCount=3;duplicate.state.collection[2]=duplicate.state.collection[1];duplicate.state.collection[2].id=20;duplicate.state.collection[2].capturedAtSequence=100;require(isValid(duplicate.state),"duplicate fixture");duplicate.sync();duplicate.openHome(ui::HomePanel::Partners);duplicate.swipe(250,180,160,180);duplicate.swipe(250,180,160,180);audit.save("49-owned-duplicate","Owned duplicate - stable instance ID",duplicate,ui::Screen::Collection);
 Harness cared;cared.hatch();cared.state.bond=cared.state.collection[0].bond=200;cared.state.mood=cared.state.collection[0].mood=80;cared.state.fullness=cared.state.collection[0].fullness=80;cared.sync();cared.openHome(ui::HomePanel::Partners);cared.tap(120,312);audit.save("50-care-effective-stats","Base plus earned care bonus",cared,ui::Screen::Stats);
 Harness sound;sound.hatch();sound.openHome(ui::HomePanel::Settings);sound.tap(120,252);sound.model.audioAvailable=true;
 audit.save("53-sound-default","Sound - default15, optional music off",sound,ui::Screen::Sound);
 sound.tap(355,180);sound.tap(280,252);sound.tap(120,252);audit.save("54-sound-music-muted","Music preference on, global mute on",sound,ui::Screen::Sound);
 sound.model.audioPreferencesWritable=false;audit.save("55-sound-unsaved","Sound persistence warning, controls remain usable",sound,ui::Screen::Sound);
 Harness tradeUi;tradeUi.hatch();tradeUi.state=stableMemberFixture(18);tradeUi.state.starterId=1;
 tradeUi.state.collection[0].formId=11;tradeUi.state.collection[0].species=Species::Impmon;tradeUi.state.collection[0].hp=forms::stats(11,1).maxHp;tradeUi.state.journal[0]|=1u<<10;require(isValid(tradeUi.state),"trade partners fixture");tradeUi.sync();
 nearby::View peers;peers.stage=nearby::Stage::Discovering;peers.peerCount=1;peers.peers[0].mac.bytes[0]=2;peers.peers[0].mac.bytes[5]=2;peers.peers[0].fighter={19,18,1};peers.peers[0].available=true;
 tradewire::View tradeView;tradeView.stage=tradewire::Stage::Discovering;tradeView.peerCount=1;std::memcpy(tradeView.peers[0].identity.bytes,peers.peers[0].mac.bytes,6);tradeView.peers[0].compatible=true;tradeView.peers[0].advertisement.available=true;tradeView.peers[0].advertisement.nonce=23;
 tradeUi.model.nearby=&peers;tradeUi.model.trade=&tradeView;tradeUi.model.tradeWritable=true;tradeUi.sync();tradeUi.openHome(ui::HomePanel::Nearby);
 audit.save("56-nearby-trade-entry","Nearby - choose Battle or Trade",tradeUi,ui::Screen::Nearby);tradeUi.tap(280,310);audit.save("57-trade-choose","Choose a retained owned instance",tradeUi,ui::Screen::TradeChoose);
 tradeUi.tap(355,180);audit.save("58-trade-next-partner","Trade carousel - next owned partner",tradeUi,ui::Screen::TradeChoose);
 auto& transcript=tradeView.transcript;transcript.peers[0].bytes[0]=2;transcript.peers[0].bytes[5]=1;transcript.peers[1]=tradeView.peers[0].identity;transcript.session=42;transcript.nonces[0]=17;transcript.nonces[1]=23;transcript.revision=1;
 transcript.sourceSequences[0]=transcript.sourceSequences[1]=tradeUi.state.sequence;transcript.offers[0]=tradeUi.state.collection[0];transcript.offers[1]=tradeUi.state.collection[1];require(trade::valid(transcript),"review transcript");
 tradeView.stage=tradewire::Stage::Reviewing;tradeView.connected=true;tradeView.peerReviewed=true;tradeUi.sync();audit.save("59-trade-both-offers","Review both offers - explicit mutual confirmation",tradeUi,ui::Screen::TradeReview);
 const char* detailNames[]{"60-trade-give-stats","61-trade-give-progress","62-trade-get-stats","63-trade-get-progress"};
 const char* detailLabels[]{"You give - base stats","You give - XP and care","You get - base stats","You get - XP and care"};
 for(unsigned page=0;page<4;++page){tradeUi.tap(355,180);audit.save(detailNames[page],detailLabels[page],tradeUi,ui::Screen::TradeReview);}tradeUi.tap(355,180);
 tradeView.localConfirmed=true;tradeUi.sync();audit.save("64-trade-waiting","Confirmed exact offers - waiting for peer",tradeUi,ui::Screen::TradeReview);
 tradeView.localConfirmed=false;transcript.revision++;transcript.offers[1].mood--;tradeUi.sync();audit.save("65-trade-changed-offer","Changed offer requires fresh confirmation",tradeUi,ui::Screen::TradeReview);
 tradeView.stage=tradewire::Stage::Prepared;tradeView.durable=tradewire::Durable::Prepared;tradeView.connected=false;tradeUi.model.writable=false;tradeUi.sync();audit.save("66-trade-reconnect","Prepared - reconnect same peer; no local rollback",tradeUi,ui::Screen::TradeReview);
 tradeView.connected=true;tradeView.stage=tradewire::Stage::Applying;tradeView.durable=tradewire::Durable::Committed;tradeUi.sync();audit.save("67-trade-saving","Committed - finish saving before game actions",tradeUi,ui::Screen::TradeReview);
 tradeView.stage=tradewire::Stage::Applied;tradeView.durable=tradewire::Durable::Applied;tradeView.peerDurable=tradewire::Durable::Applied;tradeUi.model.writable=true;tradeUi.sync();audit.save("68-trade-applied","Trade receipt - both saved",tradeUi,ui::Screen::TradeReview);
 tradeView.stage=tradewire::Stage::Reviewing;tradeView.durable=tradewire::Durable::None;tradeView.peerDurable=tradewire::Durable::Prepared;tradeView.recoveryOffer=true;tradeUi.sync();audit.save("69-trade-recovery-review","Previous uncommitted offer - review before canceling",tradeUi,ui::Screen::TradeReview);
 Harness lone;lone.hatch();tradeView={};tradeView.stage=tradewire::Stage::Discovering;tradeView.peerCount=1;std::memcpy(tradeView.peers[0].identity.bytes,peers.peers[0].mac.bytes,6);tradeView.peers[0].compatible=true;tradeView.peers[0].advertisement.available=true;tradeView.peers[0].advertisement.nonce=1;lone.model.trade=&tradeView;lone.model.nearby=&peers;lone.model.tradeWritable=true;lone.sync();lone.openHome(ui::HomePanel::Nearby);lone.tap(280,310);audit.save("70-trade-keep-partner","Keep one other playable partner",lone,ui::Screen::TradeChoose);
 Harness pausedAuto;autobattle::Trace pausedTrace;bool pausedFound=false;
 for(unsigned seed=1;seed<200&&!pausedFound;++seed){auto candidate=newDevice(seed);require(apply(candidate,Action::Hatch,1)==Error::None&&apply(candidate,Action::Mode,1)==Error::None&&apply(candidate,Action::Walk,100)==Error::None,"auto pause fixture");candidate.wildFormId=18;candidate.wildSpecies=Species::Agumon;candidate.wildLevel=1;candidate.wildMaxHp=candidate.wildHp=forms::stats(18,1).maxHp;auto after=candidate;require(applyAutoFight(after,&pausedTrace)==Error::None,"auto attack chunk");if(after.autoCapture==AutoCapture::Awaiting){pausedAuto.state=after;pausedFound=true;}}
 require(pausedFound,"actual durable capture pause");battlepresentation::Sequencer autoMovie;require(autoMovie.startAuto(pausedTrace,pausedAuto.now),"pause chunk playback");pausedAuto.model.battle=&autoMovie.view();pausedAuto.sync();audit.save("71-auto-crossing-turn","Saved attack chunk still playing; capture input locked",pausedAuto,ui::Screen::Battle,pausedAuto.now);
 for(unsigned i=0;i<500&&autoMovie.locked();++i){pausedAuto.now+=100;autoMovie.poll(pausedAuto.now);pausedAuto.sync();}require(!autoMovie.locked(),"attack chunk finishes");audit.save("72-auto-capture-choice","Auto paused - flick yourself or explicitly resume",pausedAuto,ui::Screen::Capture,pausedAuto.now);
 auto miss=pausedAuto.swipe(206,300,310,270);require(miss.kind==ui::IntentKind::GameAction&&miss.action==Action::Flick&&pausedAuto.state.lastCapture.result==CaptureResult::Miss,"deliberate player flick miss");pausedAuto.now+=1000;pausedAuto.sync();audit.save("73-auto-missed-throw","One actual miss spent - two throws left",pausedAuto,ui::Screen::Capture,pausedAuto.now);
 auto resume=pausedAuto.tap(206,365);require(resume.kind==ui::IntentKind::GameAction&&resume.action==Action::AutoResume,"explicit resume only");audit.save("74-auto-resumed-result","Resume finished attacks with no automatic capture",pausedAuto,ui::Screen::Result,pausedAuto.now+1000);
 setupui::Controller setup;setupui::Model setupModel;setupModel.ready=true;setup.open();setup.update(setupModel);audit.setup("31-setup-status","Native setup status",setup,setupModel);setup.touch(setupModel,{ui::TouchKind::Down,280,197,10});setup.touch(setupModel,{ui::TouchKind::Up,280,197,80});audit.setup("32-setup-keyboard","Native setup keyboard",setup,setupModel);
 std::puts("PASS native visual audit: 74 screens; synthetic game states; exact private asset inputs; no device or save access");
}
