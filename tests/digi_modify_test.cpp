#include "digi_modify.hpp"
#include "live_battle.hpp"
#include "device_ui.hpp"
#include "battle_presentation.hpp"
#include "forms.hpp"
#include "trade.hpp"
#include "../firmware/main/display_orientation.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace digivice;
namespace dm=digivice::digimodify;
namespace bp=digivice::battlepresentation;
namespace {
unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x);std::exit(1);}}while(false)
constexpr auto card=dm::kCards[0].id;
constexpr dm::Sample absent{dm::Presence::Absent,0},present{dm::Presence::Present,card};
State battle(BattleMode mode=BattleMode::Tactical) {
    auto s=newDevice(381);CHECK(apply(s,Action::Hatch,1)==Error::None);
    CHECK(apply(s,Action::WorldSeed,171)==Error::None);
    CHECK(apply(s,Action::Mode,static_cast<unsigned>(mode))==Error::None);
    CHECK(apply(s,Action::Walk,100)==Error::None);
    auto& m=s.collection[0];m.level=s.level=8;m.xp=xpForLevel(8);
    s.hp=m.hp=combat::formProfile(m.formId,m.level).stats.maxHp;
    s.wildFormId=forms::initialForm(6);s.wildSpecies=Species::Agumon;s.wildLevel=8;
    s.wildMaxHp=s.wildHp=combat::formProfile(s.wildFormId,s.wildLevel).stats.maxHp;
    CHECK(isValid(s));return s;
}
void rearm(dm::ReaderLatch& r,std::uint64_t start) {
    CHECK(!r.observe(absent,start).present);CHECK(!r.observe(absent,start+150).present);
    CHECK(!r.observe(absent,start+300).present);
}
void latch() {
    dm::ReaderLatch r;
    for(unsigned t=0;t<1000;t+=20) CHECK(!r.observe(present,t).present); // boot held
    rearm(r,1000);
    CHECK(!r.observe(present,1320).present);CHECK(!r.observe(present,1399).present);
    const auto e=r.observe(present,1400);CHECK(e.present&&e.cardId==card);
    for(unsigned t=1420;t<2000;t+=20) CHECK(!r.observe(present,t).present);
    CHECK(!r.observe(absent,2000).present);CHECK(!r.observe(present,2250).present); // removal glitch
    CHECK(!r.observe({dm::Presence::Present,0},2270).present); // ID swap not removal
    rearm(r,2300);CHECK(!r.observe(present,2620).present);CHECK(r.observe(present,2700).present);
    rearm(r,2720);CHECK(!r.observe({dm::Presence::Present,999},3040).present);
    CHECK(r.observe({dm::Presence::Present,999},3120).cardId==999);
    CHECK(!r.observe(present,3140).present);CHECK(!r.observe(present,3240).present); // unknown held
    r.observe({},3260);CHECK(!r.observe(present,3280).present);
    rearm(r,3300);r.observe(present,3620);CHECK(!r.observe(present,4000).present); // stale gap
    rearm(r,4020);r.observe(present,4340);CHECK(!r.observe(present,4300).present); // clock reverse
    rearm(r,4500);r.observe({dm::Presence::Present,999},4820);r.observe(present,4860);
    CHECK(!r.observe(present,4900).present);CHECK(r.observe(present,4940).present); // stable identity
    CHECK(!dm::find(0)&&!dm::find(1)&&!dm::find(2)&&!dm::find(0xD105));
}
void authority() {
    auto s=battle();State out=newDevice(99);const auto original=out;
    for(const auto a:{dm::Authority::NearbyHost,dm::Authority::NearbyGuest,dm::Authority::Practice,dm::Authority::Inactive}) {
        CHECK(dm::prepare(s,a,true,card,out)==dm::Result::Unavailable);CHECK(trade::sameState(out,original));
    }
    CHECK(dm::prepare(s,dm::Authority::LocalWild,false,card,out)==dm::Result::Unavailable);
    CHECK(dm::prepare(s,dm::Authority::LocalWild,true,1,out)==dm::Result::UnknownCard);
    auto home=s;CHECK(apply(home,Action::Retreat)==Error::None);
    CHECK(dm::prepare(home,dm::Authority::LocalWild,true,card,out)==dm::Result::NotBattle);
    CHECK(dm::prepare(s,dm::Authority::LocalWild,true,card,out)==dm::Result::Ready);
    CHECK(out.cardUsed&&out.digiModifyReady&&!s.cardUsed&&!s.digiModifyReady);
    s=out;CHECK(dm::prepare(s,dm::Authority::LocalWild,true,card,out)==dm::Result::AlreadyUsed);
    CHECK(trade::sameState(s,out));
}
void charge() {
    auto s=battle();const auto before=s;
    CHECK(apply(s,Action::DigiModify,999)==Error::InvalidValue);CHECK(trade::sameState(s,before));
    CHECK(apply(s,Action::DigiModify,card)==Error::None);CHECK(s.digiModifyReady&&s.cardUsed);
    CHECK(apply(s,Action::Card,1)==Error::CardAlreadyUsed);
    CHECK(apply(s,Action::DigiModify,card)==Error::CardAlreadyUsed);
    s.energy=s.collection[0].energy=0;const auto noEnergy=s;
    CHECK(apply(s,Action::Heavy)==Error::LowEnergy);CHECK(trade::sameState(s,noEnergy));
    s.energy=s.collection[0].energy=80;
    for(unsigned turn=0;turn<6;++turn) {s.wildTurn=turn;if(wildGuard(s)==combat::Defense::Counter)break;}
    CHECK(wildGuard(s)==combat::Defense::Counter);
    const auto hp=s.wildHp;CHECK(apply(s,Action::Heavy)==Error::None);
    CHECK(s.phase==Phase::Encounter&&s.wildHp==hp&&s.digiModifyReady&&!s.lastCritical);
    const auto hit=previewWildAttack(s,combat::Move::Physical);const auto rng=s.rngState;
    CHECK(hit.critical&&hit.damage>0&&s.rngState==rng);
    CHECK(apply(s,Action::Attack)==Error::None);CHECK(!s.digiModifyReady&&s.lastCritical);
    CHECK(s.wildHp==hp-hit.damage&&s.cardUsed);
    CHECK(apply(s,Action::DigiModify,card)==Error::CardAlreadyUsed);
    // Natural critical and forced critical use one x1.5 multiplier and same RNG draw.
    auto natural=battle();while(!previewWildAttack(natural,combat::Move::Magic).critical) ++natural.rngState;
    auto forced=natural;CHECK(apply(forced,Action::DigiModify,card)==Error::None);
    CHECK(previewWildAttack(natural,combat::Move::Magic).damage==previewWildAttack(forced,combat::Move::Magic).damage);
    CHECK(apply(natural,Action::Magic)==Error::None&&apply(forced,Action::Magic)==Error::None);
    CHECK(natural.rngState==forced.rngState&&natural.wildHp==forced.wildHp);
    // Capture aim miss has no successful outgoing hit. Charge stays ready.
    auto miss=battle();miss.wildHp=miss.wildMaxHp/2;CHECK(apply(miss,Action::DigiModify,card)==Error::None);
    CHECK(apply(miss,Action::Flick,0)==Error::None);CHECK(miss.lastCapture.result==CaptureResult::Miss&&miss.digiModifyReady);
    CHECK(apply(miss,Action::Retreat)==Error::None);CHECK(!miss.digiModifyReady&&!miss.cardUsed);
    CHECK(apply(miss,Action::Walk,100)==Error::None);CHECK(!miss.digiModifyReady&&!miss.cardUsed);
    auto win=battle();win.wildHp=1;CHECK(apply(win,Action::DigiModify,card)==Error::None);
    CHECK(apply(win,Action::Magic)==Error::None);CHECK(win.phase==Phase::Home&&win.lastCritical&&!win.digiModifyReady&&!win.cardUsed);
    auto lost=battle();lost.hp=lost.collection[0].hp=1;CHECK(apply(lost,Action::DigiModify,card)==Error::None);
    for(unsigned t=0;t<6;++t){lost.wildTurn=t;if(wildGuard(lost)==combat::Defense::Counter)break;}
    CHECK(apply(lost,Action::Heavy)==Error::None);CHECK(lost.phase==Phase::Home&&!lost.digiModifyReady&&!lost.cardUsed);
    auto legacy=battle();legacy.wildRules=19;CHECK(apply(legacy,Action::DigiModify,card)==Error::InvalidAction);
    // Capture success cleans up an unused charge.
    bool caught=false;
    for(unsigned rngSeed=1;rngSeed<100&&!caught;++rngSeed){auto c=battle();c.wildHp=1;c.rngState=rngSeed;CHECK(apply(c,Action::DigiModify,card)==Error::None);CHECK(apply(c,Action::Capture)==Error::None);if(c.phase==Phase::Home){caught=true;CHECK(!c.digiModifyReady&&!c.cardUsed);}}
    CHECK(caught);
}
std::uint32_t crc(const std::uint8_t* p,std::size_t n){std::uint32_t c=~0u;while(n--){c^=*p++;for(unsigned b=0;b<8;++b)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
void seal(Snapshot& b){auto c=crc(b.bytes,kSnapshotSize-4);for(unsigned i=0;i<4;++i)b.bytes[kSnapshotSize-4+i]=c>>(i*8);}
void snapshots() {
    auto s=battle();Snapshot bytes;CHECK(encodeSnapshot(s,bytes)&&bytes.bytes[4]==30);
    CHECK(apply(s,Action::DigiModify,card)==Error::None);CHECK(encodeSnapshot(s,bytes)&&bytes.bytes[4]==31);
    State restored;CHECK(decodeSnapshot(bytes.bytes,sizeof bytes.bytes,restored)==SnapshotStatus::Ok);
    CHECK(restored.digiModifyReady&&restored.cardUsed&&trade::sameState(restored,s));
    const auto original=restored;bytes.bytes[4]=30;seal(bytes);
    CHECK(decodeSnapshot(bytes.bytes,sizeof bytes.bytes,restored)==SnapshotStatus::InvalidState&&trade::sameState(restored,original));
    CHECK(apply(s,Action::Attack)==Error::None);CHECK(encodeSnapshot(s,bytes)&&bytes.bytes[4]==30);
    CHECK(decodeSnapshot(bytes.bytes,sizeof bytes.bytes,restored)==SnapshotStatus::Ok&&!restored.digiModifyReady&&restored.cardUsed);
    s.digiModifyReady=true;s.cardUsed=false;CHECK(!isValid(s));
    CHECK(sizeof(State)==12348&&sizeof(dm::ReaderLatch)+sizeof(dm::Overlay)<=80);
}
void autoAndPlayback() {
    auto s=battle(BattleMode::Auto);autobattle::Trace trace;
    CHECK(livebattle::applyAction(s,Action::AutoFight,0,&trace)==Error::None&&trace.count==1&&trace.partial);
    CHECK(s.phase==Phase::Encounter&&s.wildTurn==1);
    bp::Sequencer play;CHECK(play.startAuto(trace,100));const auto hp=play.view().enemyHp;
    const auto committed=trace;
    CHECK(apply(s,Action::DigiModify,card)==Error::None);
    CHECK(play.view().enemyHp==hp&&trace.steps[0].enemyHpAfter==committed.steps[0].enemyHpAfter);
    play.cancel();CHECK(s.digiModifyReady); // Visual cancellation cannot spend it.
    unsigned exchanges=0;
    for(unsigned n=0;n<60&&s.phase==Phase::Encounter;++n) {
        const auto input=s.autoCapture==AutoCapture::Awaiting?1u:
            s.autoCapture==AutoCapture::FocusStrike||s.autoCapture==AutoCapture::FocusBlock?kFocusNoTap+2:0u;
        const bool ready=s.digiModifyReady;const auto turn=s.wildTurn;
        CHECK(livebattle::applyAction(s,input>=2?Action::Focus:input==1?Action::AutoResume:Action::AutoFight,input>=2?input-2:0,&trace)==Error::None);
        CHECK(trace.count<=1);if(trace.count){++exchanges;CHECK(play.startAuto(trace,1000+n*10000));play.cancel();
            if(ready&&!trace.steps[0].reflected){CHECK(trace.steps[0].critical&&!s.digiModifyReady);}}
        if(s.phase==Phase::Encounter)CHECK(s.wildTurn==turn||s.wildTurn==turn+1);
    }
    CHECK(s.phase==Phase::Home&&exchanges>0&&!s.digiModifyReady&&!s.cardUsed);
    auto limit=battle(BattleMode::Auto);limit.wildTurn=48;
    CHECK(applyAutoStep(limit,0,&trace)==Error::None&&limit.phase==Phase::Home&&trace.count==0);
    auto tactical=battle();CHECK(applyAutoStep(tactical,0,&trace)==Error::WrongMode);
    CHECK(livebattle::applyAction(tactical,Action::Focus,UINT32_MAX,&trace)==Error::InvalidValue);
    CHECK(livebattle::applyAction(tactical,Action::DigiModify,card,&trace)==Error::None);
    CHECK(livebattle::applyAction(tactical,Action::Magic,0,&trace)==Error::None&&!tactical.digiModifyReady);
}
void overlayAndPixels(const char* output) {
    auto s=battle();CHECK(apply(s,Action::DigiModify,card)==Error::None);
    dm::Overlay overlay;overlay.start(100,s.encounters);
    CHECK(overlay.view(100,s).active&&overlay.view(999,s).active&&!overlay.view(1000,s).active);
    CHECK(!overlay.view(99,s).active);auto ended=s;CHECK(apply(ended,Action::Retreat)==Error::None);CHECK(!overlay.view(110,ended).active);CHECK(overlay.view(110,ended,true).active);CHECK(!overlay.view(1000,ended,true).active);
    auto other=s;++other.encounters;CHECK(!overlay.view(110,other).active);
    deviceui::Controller ui;deviceui::Model model;model.writable=true;model.starterStage=onboarding::Stage::Complete;
    bp::View view;view.locked=true;view.playerFormId=activeMember(s)->formId;view.enemyFormId=s.wildFormId;
    view.playerHp=view.playerMaxHp=s.hp;view.enemyHp=view.enemyMaxHp=s.wildHp;model.battle=&view;
    ui.update(s,model);CHECK(ui.screen()==deviceui::Screen::Battle);
    std::vector<std::uint16_t> base(deviceui::kPixels),shown(deviceui::kPixels);
    CHECK(ui.render(s,model,base.data(),base.size(),500));
    model.digiModify=overlay.view(500,s);CHECK(ui.render(s,model,shown.data(),shown.size(),500));
    unsigned changed=0;
    for(int y=0;y<412;++y)for(int x=0;x<412;++x){const auto i=y*412+x;
        if(base[i]!=shown[i]) {++changed;CHECK((x>=94&&x<318&&y>=24&&y<76)||(x>=70&&x<190&&y>=143&&y<245));}
        if(y>=85&&y<=130)CHECK(base[i]==shown[i]); // exact names, HP text and bars
    }
    CHECK(changed>1000&&changed<224*84+2000);
    for(int side:{412,480})for(auto rotation:{display::Orientation::Native,display::Orientation::Cw90,display::Orientation::HalfTurn,display::Orientation::Ccw90}) {
        std::vector<std::uint16_t> mapped(side*side);
        for(int row=0;row<side;row+=16) CHECK(display::blitPhysicalRows(rotation,{0,0,412,412},shown.data(),412,side,row,std::min(16,side-row),mapped.data()));
        for(int y=0;y<side;++y)for(int x=0;x<side;++x){auto p=display::physicalToLogical(rotation,{x,y},side);CHECK(mapped[y*side+x]==shown[p.y*412+p.x]);}
    }
    if(output){auto* f=std::fopen(output,"wb");CHECK(f);std::fprintf(f,"P6\n412 412\n255\n");for(auto p:shown){unsigned char rgb[]{static_cast<unsigned char>(((p>>11)&31)*255/31),static_cast<unsigned char>(((p>>5)&63)*255/63),static_cast<unsigned char>((p&31)*255/31)};std::fwrite(rgb,1,3,f);}std::fclose(f);}
    deviceui::Controller encounterUi;model.battle=nullptr;model.digiModify={};
    encounterUi.update(s,model);CHECK(encounterUi.screen()==deviceui::Screen::Encounter);
    CHECK(encounterUi.render(s,model,base.data(),base.size(),500));
    model.digiModify=overlay.view(500,s);CHECK(encounterUi.render(s,model,shown.data(),shown.size(),500));
    for(int y=85;y<=130;++y)for(int x=0;x<412;++x)CHECK(base[y*412+x]==shown[y*412+x]);
    // All effect shapes and labels preserve exact opponent/partner HUD pixels
    // and the actual encounter action button, including the last visible frame.
    for(unsigned effect=1;effect<=5;++effect) for(unsigned progress:{0u,500u,999u}) {
        model.digiModify={true,static_cast<std::uint16_t>(progress),static_cast<dm::Effect>(effect),2};
        CHECK(encounterUi.render(s,model,shown.data(),shown.size(),500));
        for(int y=85;y<=130;++y)for(int x=0;x<412;++x)CHECK(base[y*412+x]==shown[y*412+x]);
        for(int y=248;y<=360;++y)for(int x=0;x<412;++x)CHECK(base[y*412+x]==shown[y*412+x]);
    }
    overlay.cancel();CHECK(!overlay.view(600,s).active);
}
}
int main(int argc,char** argv){latch();authority();charge();snapshots();autoAndPlayback();overlayAndPixels(argc>1?argv[1]:nullptr);std::printf("Memory: State=%zu, latch=%zu, overlay=%zu, sequencer=%zu, trace=%zu bytes\n",sizeof(State),sizeof(dm::ReaderLatch),sizeof(dm::Overlay),sizeof(bp::Sequencer),sizeof(autobattle::Trace));std::printf("Digi-Modify: %u checks passed; logical input, core, playback and 412/480-pixel mapping only.\n",checks);}
