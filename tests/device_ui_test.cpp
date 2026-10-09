#include "device_ui.hpp"
#include "forms.hpp"
#include "local_form_facing.hpp"
#include "../firmware/main/display_orientation.hpp"
#include "../firmware/tests/catalog_fixture.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace digivice;
using namespace digivice::deviceui;
unsigned checks=0;
#define CHECK(value) do { ++checks; if(!(value)) { std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#value); std::exit(1); } } while(false)

struct Harness {
    State state=newDevice(12345);
    Model model{};
    onboarding::StarterController starter;
    Controller ui;
    std::uint64_t now=100;
    unsigned gameWrites=0;
    Harness() { model.writable=true; sync(); }
    void sync() { if(state.starterOfferSeed) { model.starterCount=11; starter.configureChoices(11); model.starterFormId=starterForm(state,starter.selectedId()); } model.starterStage=starter.stage(); model.selectedId=starter.selectedId(); ui.update(state,model); }
    Intent event(TouchKind kind,int x,int y,std::uint64_t delta=30) {
        now+=delta; return ui.touch(state,model,{kind,static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),now});
    }
    Intent tap(int x,int y) {
        CHECK(!event(TouchKind::Down,x,y));
        return event(TouchKind::Up,x,y,60);
    }
    Intent swipe(int x,int y,int endX,int endY) {
        CHECK(!event(TouchKind::Down,x,y));
        CHECK(!event(TouchKind::Move,(x+endX)/2,(y+endY)/2,50));
        return event(TouchKind::Up,endX,endY,50);
    }
    void strike() {
        dispatch(swipe(206,220,206,150));
    }
    void dispatch(Intent intent,bool commit=true) {
        CHECK(intent);
        onboarding::Request request;
        switch(intent.kind) {
        case IntentKind::GameAction: {
            State next=state; CHECK(apply(next,intent.action,intent.value)==Error::None);
            if(commit) {state=next;++gameWrites;} break;
        }
        case IntentKind::StarterConfirm: request=starter.input(onboarding::Input::Confirm); break;
        case IntentKind::StarterNext: starter.input(onboarding::Input::Next); break;
        case IntentKind::StarterPrevious:
            starter.input(onboarding::Input::Previous); break;
        case IntentKind::StarterBack: starter.input(onboarding::Input::HoldBack); break;
        case IntentKind::SleepTimeout: if(commit) model.sleepTimeoutSeconds=static_cast<std::uint16_t>(intent.value); break;
        case IntentKind::ToggleMute: model.muted=!model.muted; break;
        case IntentKind::ToggleGyro: model.gyroEnabled=!model.gyroEnabled; break;
        default: break;
        }
        if(request.hatchId) {
            State next=state; CHECK(apply(next,Action::Hatch,request.hatchId)==Error::None);
            if(commit) {state=next;++gameWrites;}
            starter.resolve(commit);
        }
        ui.resolve(commit ? nullptr : "SAVE FAILED"); sync();
    }
    void choose() {
        dispatch(tap(206,285)); CHECK(ui.screen()==Screen::Starter);
        dispatch(tap(206,312)); CHECK(ui.screen()==Screen::StarterReview);
        dispatch(tap(206,278)); CHECK(ui.screen()==Screen::Home);
    }
    void selectHome(HomePanel panel) {
        CHECK(ui.screen()==Screen::Home);
        for(unsigned i=0;i<4 && ui.homePanel()!=panel;++i) dispatch(tap(355,190));
        CHECK(ui.homePanel()==panel);
    }
    void openHome(HomePanel panel) {
        selectHome(panel); dispatch(tap(206,323));
    }
    void encounter() {
        CHECK(ui.screen()==Screen::Home && ui.walkingEligible() && ui.interactionIdle());
        // Test-only injection is deliberately outside the release UI.
        CHECK(apply(state,Action::Walk,100)==Error::None); sync(); CHECK(ui.screen()==Screen::Encounter);
        dispatch(tap(206,274)); CHECK(ui.screen()==Screen::Battle);
    }
};

void orientationGestures() {
    using display::Orientation;
    using display::Point;
    // Independent native-coordinate goldens: no forward transform generates
    // the input to the inverse under test. The shared Controller stays logical.
    struct Positions {
        Orientation orientation;
        Point egg, review, hatch, back;
        Point browseStart, browseMiddle, browseEnd;
        Point selectStart, selectMiddle, selectEnd;
        Point strikeStart, strikeMiddle, strikeEnd;
        Point orb, flickMiddle, flickEnd, downward;
    };
    const Positions cases[]{
        {Orientation::Cw90,{126,206},{99,206},{133,206},{46,206},
         {231,250},{231,205},{231,160}, {231,170},{231,210},{231,250},
         {191,206},{226,206},{261,206}, {111,206},{161,206},{211,206},{81,206}},
        {Orientation::Ccw90,{285,205},{312,205},{278,205},{365,205},
         {180,161},{180,206},{180,251}, {180,241},{180,201},{180,161},
         {220,205},{185,205},{150,205}, {300,205},{250,205},{200,205},{330,205}}
    };
    for (const auto& c:cases) {
        auto event=[&](Harness& h,TouchKind kind,Point native,std::uint64_t dt=30) {
            const auto logical=display::panelToLogical(c.orientation,native);
            return h.event(kind,logical.x,logical.y,dt);
        };
        auto tap=[&](Harness& h,Point native) {
            CHECK(!event(h,TouchKind::Down,native));
            return event(h,TouchKind::Up,native,60);
        };
        auto swipe=[&](Harness& h,Point start,Point middle,Point end) {
            CHECK(!event(h,TouchKind::Down,start));
            CHECK(!event(h,TouchKind::Move,middle,50));
            return event(h,TouchKind::Up,end,50);
        };
        Harness starter; const auto egg=starter.state;
        CHECK(!event(starter,TouchKind::Up,c.egg));
        starter.dispatch(tap(starter,c.egg));
        starter.dispatch(swipe(starter,c.browseStart,c.browseMiddle,c.browseEnd));
        CHECK(starter.model.selectedId==2 && starter.gameWrites==0);
        CHECK(!event(starter,TouchKind::Up,c.review));
        starter.dispatch(tap(starter,c.review));
        CHECK(starter.ui.screen()==Screen::StarterReview);
        starter.dispatch(tap(starter,c.back));
        CHECK(starter.ui.screen()==Screen::Starter && std::memcmp(&egg,&starter.state,sizeof(State))==0);
        starter.dispatch(tap(starter,c.review));
        const auto hatch=tap(starter,c.hatch);
        CHECK(hatch.kind==IntentKind::StarterConfirm && starter.ui.pending());
        CHECK(!event(starter,TouchKind::Up,c.hatch));
        CHECK(!tap(starter,c.hatch));
        starter.dispatch(hatch,false);
        CHECK(starter.gameWrites==0 && std::memcmp(&egg,&starter.state,sizeof(State))==0);
        starter.dispatch(tap(starter,c.hatch));
        CHECK(starter.gameWrites==1 && starter.state.starterId==2 && starter.ui.screen()==Screen::Home);

        Harness battle; battle.choose(); battle.encounter();
        const auto beforeBattle=battle.state; const auto writes=battle.gameWrites;
        const auto select=swipe(battle,c.selectStart,c.selectMiddle,c.selectEnd);
        CHECK(select.kind==IntentKind::Navigation); battle.dispatch(select);
        CHECK(battle.gameWrites==writes && std::memcmp(&beforeBattle,&battle.state,sizeof(State))==0);
        CHECK(!event(battle,TouchKind::Up,c.strikeEnd));
        const auto attack=swipe(battle,c.strikeStart,c.strikeMiddle,c.strikeEnd);
        CHECK(attack.kind==IntentKind::GameAction && attack.action==Action::Magic);
        CHECK(!event(battle,TouchKind::Up,c.strikeEnd));
        CHECK(!swipe(battle,c.strikeStart,c.strikeMiddle,c.strikeEnd));
        battle.dispatch(attack);
        CHECK(battle.gameWrites==writes+1 && battle.state.sequence==beforeBattle.sequence+1);

        Harness capture; capture.choose(); capture.encounter();
        while(capture.state.wildHp>capture.state.wildMaxHp/2) capture.strike();
        capture.dispatch(capture.tap(280,306)); CHECK(capture.ui.screen()==Screen::Capture);
        const auto beforeFlick=capture.state;
        CHECK(!tap(capture,c.orb));
        CHECK(!event(capture,TouchKind::Down,c.orb));
        CHECK(!event(capture,TouchKind::Up,c.downward,100));
        CHECK(!event(capture,TouchKind::Down,c.orb));
        CHECK(!event(capture,TouchKind::Cancel,c.orb));
        CHECK(!event(capture,TouchKind::Up,c.flickEnd,100));
        const auto flick=swipe(capture,c.orb,c.flickMiddle,c.flickEnd);
        CHECK(flick.kind==IntentKind::GameAction && flick.action==Action::Flick && flick.value==160*256+180);
        CHECK(!event(capture,TouchKind::Up,c.flickEnd));
        CHECK(std::memcmp(&beforeFlick,&capture.state,sizeof(State))==0);
        capture.dispatch(flick);
        CHECK(capture.state.sequence==beforeFlick.sequence+1);
    }
}

void homeCarousel() {
    Harness h; h.choose();
    const auto saved=h.state; const auto writes=h.gameWrites;
    CHECK(h.ui.homePanel()==HomePanel::Care && h.ui.walkingEligible() && h.ui.interactionIdle());
    // Held Home input still counts walking, but cannot start an encounter.
    CHECK(!h.event(TouchKind::Down,355,190));
    CHECK(h.ui.walkingEligible() && !h.ui.interactionIdle());
    h.dispatch(h.event(TouchKind::Up,355,190,60));
    CHECK(h.ui.homePanel()==HomePanel::Partners && h.ui.interactionIdle());
    h.dispatch(h.tap(55,190)); CHECK(h.ui.homePanel()==HomePanel::Care);
    h.dispatch(h.tap(55,190)); CHECK(h.ui.homePanel()==HomePanel::Nearby);
    h.dispatch(h.swipe(260,190,160,190)); CHECK(h.ui.homePanel()==HomePanel::Care);
    h.dispatch(h.swipe(160,190,260,190)); CHECK(h.ui.homePanel()==HomePanel::Nearby);
    // Swipes may start on a chevron; release changes one panel, never opens it.
    h.dispatch(h.swipe(355,190,220,190)); CHECK(h.ui.homePanel()==HomePanel::Care);
    CHECK(!h.swipe(200,180,210,250)); CHECK(h.ui.homePanel()==HomePanel::Care);
    CHECK(!h.swipe(206,323,140,323)); CHECK(h.ui.screen()==Screen::Home);
    CHECK(!h.event(TouchKind::Down,355,190)); h.ui.cancelTouch();
    CHECK(!h.event(TouchKind::Up,355,190)); CHECK(h.ui.homePanel()==HomePanel::Care);
    CHECK(!h.event(TouchKind::Down,355,190)); h.model.inputEnabled=false; h.sync();
    CHECK(!h.event(TouchKind::Up,355,190)); h.model.inputEnabled=true; h.sync();
    CHECK(h.ui.homePanel()==HomePanel::Care && h.ui.interactionIdle());
    constexpr HomePanel panels[]{HomePanel::Care,HomePanel::Partners,HomePanel::Settings,HomePanel::Nearby};
    constexpr Screen destinations[]{Screen::Care,Screen::Collection,Screen::Settings,Screen::Nearby};
    for(unsigned i=0;i<4;++i) {
        h.selectHome(panels[i]); CHECK(h.ui.walkingEligible() && h.ui.interactionIdle());
        const auto request=h.ui.artRequest(h.state,h.model,1000);
        CHECK(request.formId==activeMember(h.state)->formId);
        h.dispatch(h.tap(206,323)); CHECK(h.ui.screen()==destinations[i] && !h.ui.walkingEligible());
        h.dispatch(h.tap(206,365)); CHECK(h.ui.screen()==Screen::Home && h.ui.homePanel()==panels[i]);
    }
    CHECK(h.gameWrites==writes && std::memcmp(&saved,&h.state,sizeof(State))==0);
    // The hero is bounded to a176px square, even for16px frames and max tilt.
    // Unique synthetic pixels give a direct raster bound, not an implementation mirror.
    std::array<std::uint16_t,32*32> pixels; pixels.fill(0xF81F);
    std::array<std::uint8_t,128> mask; mask.fill(0xFF);
    std::array<std::uint16_t,kPixels+2> output;
    auto& art=h.model.artwork.sprite;
    art.formId=activeMember(h.state)->formId; art.pixels=pixels.data(); art.mask=mask.data();
    for(unsigned side:{16u,32u}) for(int tilt:{-8,0,8}) {
        art.width=art.height=static_cast<std::uint16_t>(side); art.pixelCount=side*side; art.maskBytes=side*side/8;
        h.model.gyroEnabled=true; h.model.tiltX=h.model.tiltY=static_cast<std::int16_t>(tilt);
        output.fill(0xBEEF); CHECK(h.ui.render(h.state,h.model,output.data()+1,kPixels,1000));
        CHECK(output.front()==0xBEEF && output.back()==0xBEEF);
        const int size=side==16 ? 176 : 160;
        const int x0=206+tilt/2-size/2, y0=196+tilt/2-size/2;
        unsigned colored=0;
        for(int y=0;y<kSize;++y) for(int x=0;x<kSize;++x) {
            if(output[1+y*kSize+x]!=0xF81F) continue;
            ++colored; CHECK(x>=x0 && x<x0+size && y>=y0 && y<y0+size);
            CHECK(Controller::inside(x,y) && y<300 && y>102);
        }
        CHECK(colored==static_cast<unsigned>(size*size));
    }
    // Independent native-screen goldens exercise Home after both quarter turns.
    using display::Orientation; using display::Point;
    struct Rotated { Orientation orientation; Point left,right,primary,back,start,middle,end; };
    const Rotated rotations[]{
        {Orientation::Cw90,{221,55},{221,355},{88,206},{46,206},{221,260},{221,210},{221,160}},
        {Orientation::Ccw90,{190,356},{190,56},{323,205},{365,205},{190,151},{190,201},{190,251}}
    };
    for(const auto& r:rotations) {
        Harness n; n.choose(); const auto before=n.state;
        auto event=[&](TouchKind kind,Point native,std::uint64_t dt=30) {
            const auto p=display::panelToLogical(r.orientation,native); return n.event(kind,p.x,p.y,dt);
        };
        auto tap=[&](Point native) { CHECK(!event(TouchKind::Down,native)); return event(TouchKind::Up,native,60); };
        n.dispatch(tap(r.right)); CHECK(n.ui.homePanel()==HomePanel::Partners);
        n.dispatch(tap(r.left)); CHECK(n.ui.homePanel()==HomePanel::Care);
        CHECK(!event(TouchKind::Down,r.start)); CHECK(!n.ui.interactionIdle());
        CHECK(!event(TouchKind::Move,r.middle,50)); n.dispatch(event(TouchKind::Up,r.end,50));
        CHECK(n.ui.homePanel()==HomePanel::Partners);
        n.dispatch(tap(r.primary)); CHECK(n.ui.screen()==Screen::Collection);
        CHECK(!event(TouchKind::Up,r.primary));
        n.dispatch(tap(r.back)); CHECK(n.ui.screen()==Screen::Home && n.ui.homePanel()==HomePanel::Partners);
        CHECK(std::memcmp(&before,&n.state,sizeof(State))==0 && n.gameWrites==1);
    }
}

void walkingCheckpoints() {
    auto checkpoint=[](Harness& h,Action action,std::uint32_t value) {
        const State before=h.state; State after=before;
        CHECK(apply(after,action,value)==Error::None);
        CHECK(h.ui.acknowledgeWalking(before,after,action,value));
        h.state=after; h.sync();
    };
    Harness h; h.choose();
    // A hardware seed and accrual during a held CTA preserve that exact contact.
    CHECK(!h.event(TouchKind::Down,206,323));
    checkpoint(h,Action::EncounterSeed,6789);
    CHECK(!h.ui.interactionIdle() && h.ui.screen()==Screen::Home);
    checkpoint(h,Action::AccrueSteps,1000);
    CHECK(!h.ui.interactionIdle() && h.state.pendingEncounter.formId);
    h.dispatch(h.event(TouchKind::Up,206,323,60)); CHECK(h.ui.screen()==Screen::Care);
    // A pending care proposal uses the newest state, with one eventual write.
    const auto feed=h.tap(120,252); CHECK(feed.kind==IntentKind::GameAction && h.ui.pending());
    checkpoint(h,Action::AccrueSteps,1); CHECK(h.ui.pending() && !h.ui.interactionIdle());
    h.dispatch(feed); const auto careAt=h.now;
    CHECK(h.ui.artRequest(h.state,h.model,careAt+100).animation==sprite::Animation::Care);
    checkpoint(h,Action::AccrueSteps,1);
    CHECK(h.ui.artRequest(h.state,h.model,careAt+100).animation==sprite::Animation::Care);
    CHECK(h.ui.artRequest(h.state,h.model,careAt+800).animation==sprite::Animation::Idle);
    h.dispatch(h.tap(206,365)); h.openHome(HomePanel::Settings);
    h.dispatch(h.tap(120,312)); CHECK(h.ui.screen()==Screen::ModeReview);
    CHECK(!h.event(TouchKind::Down,206,282)); checkpoint(h,Action::AccrueSteps,1);
    CHECK(h.ui.screen()==Screen::ModeReview && !h.ui.interactionIdle());
    auto mode=h.event(TouchKind::Up,206,282,60); CHECK(mode.kind==IntentKind::GameAction && mode.action==Action::Mode);
    h.dispatch(mode); CHECK(h.state.battleMode==BattleMode::Auto);

    // The native sequence-bound evolution review is rebound only after a
    // proven background-only checkpoint, preserving all original gate checks.
    Harness evolution; evolution.state=stableMemberFixture(67); evolution.sync();
    const auto* edge=forms::outgoing(67,0); CHECK(edge);
    evolution.state.bond=evolution.state.collection[1].bond=edge->minBond; evolution.sync();
    evolution.openHome(HomePanel::Partners); evolution.dispatch(evolution.swipe(250,180,160,180));
    evolution.dispatch(evolution.tap(120,312)); evolution.dispatch(evolution.tap(206,312));
    evolution.dispatch(evolution.tap(280,312)); CHECK(evolution.ui.screen()==Screen::EvolutionReview);
    CHECK(!evolution.event(TouchKind::Down,206,302));
    checkpoint(evolution,Action::EncounterSeed,2468); checkpoint(evolution,Action::AccrueSteps,1);
    CHECK(evolution.ui.screen()==Screen::EvolutionReview);
    const auto evolve=evolution.event(TouchKind::Up,206,302,60);
    CHECK(evolve.kind==IntentKind::GameAction && evolve.action==Action::Evolve && evolve.value==edge->to);
    checkpoint(evolution,Action::AccrueSteps,1); CHECK(evolution.ui.pending());
    evolution.dispatch(evolve); CHECK(evolution.ui.screen()==Screen::EvolutionResult);

    Harness release; release.state=stableMemberFixture(67); release.sync();
    release.openHome(HomePanel::Partners); release.dispatch(release.tap(120,312));
    release.dispatch(release.tap(206,312)); CHECK(release.ui.screen()==Screen::ReleaseReview);
    const auto released=release.state.collection[0].id;
    CHECK(!release.event(TouchKind::Down,206,302)); checkpoint(release,Action::AccrueSteps,1);
    CHECK(release.ui.screen()==Screen::ReleaseReview);
    const auto remove=release.event(TouchKind::Up,206,302,60);
    CHECK(remove.kind==IntentKind::GameAction && remove.action==Action::Release && remove.value==released);
    release.dispatch(remove); CHECK(!findMember(release.state,released));

    Harness fight; fight.choose(); fight.encounter();
    fight.dispatch(fight.swipe(250,180,160,180)); // Heavy, not the reset default Physical.
    checkpoint(fight,Action::EncounterSeed,3456); checkpoint(fight,Action::AccrueSteps,1000);
    CHECK(fight.ui.screen()==Screen::Battle && fight.state.pendingEncounter.formId);
    const auto attack=fight.swipe(206,220,206,150); CHECK(attack.action==Action::Heavy);
    fight.dispatch(attack);
    battlepresentation::View playback{}; playback.locked=true;
    fight.model.battle=&playback; fight.sync();
    checkpoint(fight,Action::AccrueSteps,1);
    CHECK(fight.ui.screen()==Screen::Battle && !fight.ui.encounterPresentationEligible());
    CHECK(!fight.swipe(206,220,206,150));
    fight.model.battle=nullptr; fight.sync();
    while(fight.state.wildHp>fight.state.wildMaxHp/2) fight.strike();
    fight.dispatch(fight.tap(280,306)); CHECK(fight.ui.screen()==Screen::Capture);
    CHECK(!fight.event(TouchKind::Down,206,300)); checkpoint(fight,Action::AccrueSteps,1);
    CHECK(fight.ui.screen()==Screen::Capture && !fight.ui.interactionIdle());
    CHECK(!fight.event(TouchKind::Move,206,250,50));
    const auto flick=fight.event(TouchKind::Up,206,200,50);
    CHECK(flick.kind==IntentKind::GameAction && flick.action==Action::Flick);
    fight.dispatch(flick);

    // No broad sequence bypass: foreground commands and altered gameplay,
    // even paired with a legitimate accrual, retain ordinary invalidation.
    Harness bad; bad.choose(); bad.openHome(HomePanel::Settings);
    bad.dispatch(bad.tap(120,312)); CHECK(bad.ui.screen()==Screen::ModeReview);
    const auto before=bad.state; State next=before;
    CHECK(apply(next,Action::AccrueSteps,1)==Error::None);
    CHECK(!bad.ui.acknowledgeWalking(before,next,Action::AccrueSteps,2));
    CHECK(!bad.ui.acknowledgeWalking(before,next,Action::Rest,1));
    auto wrong=next; ++wrong.sequence;
    CHECK(!bad.ui.acknowledgeWalking(before,wrong,Action::AccrueSteps,1));
    wrong=next; wrong.battleMode=BattleMode::Auto;
    CHECK(isValid(wrong) && !bad.ui.acknowledgeWalking(before,wrong,Action::AccrueSteps,1));
    wrong=next; wrong.hp=--wrong.collection[0].hp;
    CHECK(isValid(wrong) && !bad.ui.acknowledgeWalking(before,wrong,Action::AccrueSteps,1));
    wrong=next; wrong.rngState^=123;
    CHECK(isValid(wrong) && !bad.ui.acknowledgeWalking(before,wrong,Action::AccrueSteps,1));
    wrong=before; CHECK(apply(wrong,Action::Feed)==Error::None);
    CHECK(!bad.ui.acknowledgeWalking(before,wrong,Action::AccrueSteps,1));
    bad.state=wrong; bad.sync(); CHECK(bad.ui.screen()==Screen::Settings);
    CHECK(!bad.event(TouchKind::Up,206,282));
}

// Every horizontal screen has tap parity with its original swipe action.
// Fixed CCW90 raw points are independent goldens, not generated by the mapping.
void horizontalTaps() {
    const auto equivalent=[](Harness base,unsigned cycle,bool picker=false) {
        std::array<std::uint16_t,kPixels> tapPixels,swipePixels,rawPixels;
        const auto saved=base.state;const auto writes=base.gameWrites;
        for(bool next:{false,true}) for(unsigned i=0;i<cycle+1;++i) {
            Harness tapped=base,swiped=base,raw=base;
            const auto tap=tapped.tap(next?355:55,180);
            const auto swipe=swiped.swipe(next?250:160,180,next?160:250,180);
            const auto logical=display::panelToLogical(display::Orientation::Ccw90,{180,next?56:356});
            const auto rotated=raw.tap(logical.x,logical.y);
            CHECK(tap.kind==swipe.kind && tap.kind==rotated.kind && tap.action==swipe.action && tap.value==swipe.value);
            CHECK(!tapped.event(TouchKind::Up,next?355:55,180));
            if(tap) {tapped.dispatch(tap);swiped.dispatch(swipe);raw.dispatch(rotated);}
            CHECK(tapped.ui.screen()==swiped.ui.screen() && tapped.ui.screen()==raw.ui.screen());
            CHECK(tapped.ui.render(tapped.state,tapped.model,tapPixels.data(),kPixels,90000));
            CHECK(swiped.ui.render(swiped.state,swiped.model,swipePixels.data(),kPixels,90000));
            CHECK(raw.ui.render(raw.state,raw.model,rawPixels.data(),kPixels,90000));
            CHECK(tapPixels==swipePixels && tapPixels==rawPixels);
            CHECK(tapped.gameWrites==writes && std::memcmp(&saved,&tapped.state,sizeof(State))==0);
            if(picker) {
                Harness icons=base;
                const auto point=display::panelToLogical(display::Orientation::Ccw90,{278,next?152:264});
                const auto icon=icons.tap(point.x,point.y);
                CHECK(icon.kind==IntentKind::Navigation);icons.dispatch(icon);
                CHECK(icons.ui.render(icons.state,icons.model,rawPixels.data(),kPixels,90000));
                CHECK(tapPixels==rawPixels);
            }
            base=tapped;
        }
    };
    Harness egg;CHECK(!egg.tap(55,180) && !egg.tap(355,180)); // No horizontal egg action.
    Harness starter;starter.dispatch(starter.tap(206,285));equivalent(starter,8);
    starter.dispatch(starter.tap(55,180));CHECK(starter.model.selectedId==8);
    starter.dispatch(starter.tap(355,180));CHECK(starter.model.selectedId==1);
    starter.dispatch(starter.tap(206,312));CHECK(starter.ui.screen()==Screen::StarterReview);
    CHECK(!starter.tap(55,180) && !starter.tap(355,180));
    Harness home;home.choose();equivalent(home,4);
    Harness one;one.choose();one.openHome(HomePanel::Partners);equivalent(one,1);
    CHECK(!one.tap(55,180) && !one.tap(355,180));
    Harness partners;partners.choose();partners.state=stableMemberFixture(18);partners.sync();partners.openHome(HomePanel::Partners);equivalent(partners,2);
    partners.dispatch(partners.tap(355,180));CHECK(partners.ui.artRequest(partners.state,partners.model,0).formId==18);
    partners.dispatch(partners.tap(120,312));CHECK(partners.ui.screen()==Screen::Stats);equivalent(partners,4);
    partners.dispatch(partners.tap(206,312));CHECK(partners.ui.screen()==Screen::Evolution);equivalent(partners,2);
    partners.dispatch(partners.tap(120,312));equivalent(partners,2); // Stats/skills info pages.
    partners.dispatch(partners.tap(355,180));equivalent(partners,2);
    partners.dispatch(partners.tap(206,365));partners.dispatch(partners.tap(280,312));
    CHECK(partners.ui.screen()==Screen::Evolution && !partners.tap(55,180) && !partners.tap(355,180)); // Unmet requirements.
    Harness battle;battle.choose();battle.encounter();equivalent(battle,3,true);
    battle.dispatch(battle.tap(355,180));
    auto heavy=battle.swipe(206,220,206,150);CHECK(heavy.kind==IntentKind::GameAction && heavy.action==Action::Heavy);
    CHECK(!battle.tap(55,180) && !battle.tap(355,180));battle.dispatch(heavy,false);
    battle.dispatch(battle.tap(55,180));
    auto physical=battle.swipe(206,220,206,150);CHECK(physical.action==Action::Attack);battle.dispatch(physical,false);
    // Existing center controls and vertical capture remain separate actions.
    battle.state.wildHp=battle.state.wildMaxHp/2;battle.sync();battle.dispatch(battle.tap(280,306));
    CHECK(battle.ui.screen()==Screen::Capture && !battle.tap(55,180) && !battle.tap(355,180));
    CHECK(!battle.tap(206,300));const auto flick=battle.swipe(206,300,206,200);CHECK(flick.action==Action::Flick);
    Harness nearbyUi;nearbyUi.choose();nearbyUi.openHome(HomePanel::Nearby);
    nearby::View network;network.stage=nearby::Stage::Discovering;network.host=true;network.peerCount=2;
    network.peers[0].fighter={1,18,1};network.peers[0].available=true;network.peers[0].mac.bytes[5]=1;
    network.peers[1].fighter={2,25,1};network.peers[1].available=true;network.peers[1].mac.bytes[5]=2;
    nearbyUi.model.nearby=&network;nearbyUi.sync();equivalent(nearbyUi,2);
    network.peerCount=1;nearbyUi.sync();CHECK(!nearbyUi.tap(55,180) && !nearbyUi.tap(355,180));
    network.peerCount=0;nearbyUi.sync();CHECK(!nearbyUi.tap(55,180) && !nearbyUi.tap(355,180));
    CHECK(nearby::begin({1,11,1},{19,18,1},nearby::Mode::Tactical,444,network.match));
    network.stage=nearby::Stage::Playing;network.session=88;nearbyUi.sync();equivalent(nearbyUi,3,true);
    network.host=false;nearbyUi.sync();equivalent(nearbyUi,3,true);
    nearbyUi.dispatch(nearbyUi.tap(355,180));auto counter=nearbyUi.swipe(206,300,206,190);
    CHECK(counter.kind==IntentKind::NearbyChoose && counter.value==static_cast<unsigned>(nearby::Choice::Counter));nearbyUi.dispatch(counter);
    for(auto stage:{nearby::Stage::Reconnecting,nearby::Stage::Finished}) {
        network.stage=stage;nearbyUi.sync();CHECK(!nearbyUi.tap(55,180) && !nearbyUi.tap(355,180));
    }
    network.stage=nearby::Stage::Playing;network.localChoicePending=true;nearbyUi.sync();CHECK(!nearbyUi.tap(355,180));
    // Exact edge boundaries, repeated Up, cancellation, hold timeout, jitter,
    // and an excursion returning to origin cannot select or advance twice.
    Harness edges;edges.choose();
    for(auto p:{display::Point{29,180},{84,180},{327,180},{382,180},{55,133},{55,246},{206,180}})
        CHECK(!edges.tap(p.x,p.y));
    edges.dispatch(edges.tap(30,134));CHECK(edges.ui.homePanel()==HomePanel::Nearby);
    edges.dispatch(edges.tap(381,245));CHECK(edges.ui.homePanel()==HomePanel::Care);
    CHECK(!edges.event(TouchKind::Down,355,180));CHECK(!edges.event(TouchKind::Move,250,180,60));CHECK(!edges.event(TouchKind::Up,355,180,60));
    CHECK(edges.ui.homePanel()==HomePanel::Care);
    CHECK(!edges.event(TouchKind::Down,355,180));CHECK(!edges.event(TouchKind::Up,355,180,1501));
    CHECK(!edges.event(TouchKind::Down,355,180));CHECK(!edges.event(TouchKind::Cancel,355,180));CHECK(!edges.event(TouchKind::Up,355,180));
    edges.dispatch(edges.swipe(55,180,355,180));CHECK(edges.ui.homePanel()==HomePanel::Nearby);
    CHECK(!edges.event(TouchKind::Up,355,180));
    CHECK(!edges.event(TouchKind::Down,355,180));CHECK(!edges.event(TouchKind::Move,351,184,40));edges.dispatch(edges.event(TouchKind::Up,355,180,40));
    CHECK(edges.ui.homePanel()==HomePanel::Care);
}

// Replay committed turns and inspect asymmetric source pixels across all
// animation transitions. Expected left/right landmarks are independent goldens.
void battleArtworkSequence() {
    CHECK(sprite::localFormFacing(11)==SpriteFacing::Left);
    CHECK(sprite::localFormFacing(73)==SpriteFacing::Right && sprite::localFormFacing(91)==SpriteFacing::Right);
    CHECK(sprite::localFormFacing(87)==SpriteFacing::Front);
    CHECK(sprite::localFormFacing(13)==SpriteFacing::Unknown && sprite::localFormFacing(1)==SpriteFacing::Unknown);
    Harness h; h.choose(); h.encounter();
    h.state.wildFormId=18; h.state.wildSpecies=static_cast<Species>(forms::find(18)->lineage);
    h.state.wildLevel=1; h.state.wildMaxHp=h.state.wildHp=forms::stats(18,1).maxHp;
    CHECK(isValid(h.state)); h.sync();
    std::array<std::uint16_t,32*32> source;
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)
        source[y*32+x]=x<8 ? 0xF81F : x>=24 ? 0x07FF : 0x1357;
    std::array<std::uint8_t,128> mask; mask.fill(0xff);
    std::array<std::uint16_t,kPixels+2> pixels; pixels.fill(0xbeef);
    const auto load=[&](const ArtRequest& request,SpriteFrame& frame,SpriteFacing facing) {
        frame={request.formId,request.animation,source.data(),source.size(),mask.data(),mask.size(),32,32};
        frame.nativeFacing=facing;
    };
    const auto render=[&](std::uint64_t at,SpriteFacing left=SpriteFacing::Left,SpriteFacing right=SpriteFacing::Left) {
        h.sync();
        const auto partner=h.ui.partnerArtRequest(h.state,h.model,at);
        const auto enemy=h.ui.artRequest(h.state,h.model,at);
        CHECK(partner.formId==11 && enemy.formId==18);
        load(partner,h.model.partnerArtwork,left); load(enemy,h.model.artwork.sprite,right);
        CHECK(h.ui.render(h.state,h.model,pixels.data()+1,kPixels,at));
        CHECK(pixels.front()==0xbeef && pixels.back()==0xbeef);
        CHECK(pixels[1+157*kSize+76]==(left==SpriteFacing::Left ? 0x07FF : 0xF81F));
        CHECK(pixels[1+157*kSize+160]==(left==SpriteFacing::Left ? 0xF81F : 0x07FF));
        CHECK(pixels[1+157*kSize+252]==(right==SpriteFacing::Right ? 0x07FF : 0xF81F));
        CHECK(pixels[1+157*kSize+336]==(right==SpriteFacing::Right ? 0xF81F : 0x07FF));
    };
    CHECK(h.ui.partnerArtRequest(h.state,h.model,h.now).animation==sprite::Animation::Idle);
    render(h.now);
    render(h.now,SpriteFacing::Right,SpriteFacing::Right);
    render(h.now,SpriteFacing::Front,SpriteFacing::Front);
    render(h.now,SpriteFacing::Unknown,SpriteFacing::Unknown);
    battlepresentation::Sequencer sequencer;
    const std::uint32_t moments[]{0,160,350,520,700,1199,1200,1360,1550,1720,1900,2399,2400,3999,4000};
    for(unsigned turn=0;turn<3;++turn) {
        const auto before=h.state;
        auto intent=h.swipe(206,220,206,150); CHECK(intent.action==Action::Attack); h.dispatch(intent);
        CHECK(sequencer.startTactical(before,h.state,intent.action,intent.value,h.now));
        h.model.battle=&sequencer.view();
        const auto at=h.now;
        for(auto elapsed:moments) {
            const auto& view=sequencer.poll(at+elapsed);
            render(at+elapsed);
            const auto partner=h.ui.partnerArtRequest(h.state,h.model,at+elapsed);
            const auto enemy=h.ui.artRequest(h.state,h.model,at+elapsed);
            if(view.locked && view.actor==battlepresentation::Actor::Player) {
                CHECK(partner.animation==sprite::Animation::Attack);
                CHECK(enemy.animation==(view.flash ? sprite::Animation::Hurt : sprite::Animation::Idle));
            } else if(view.locked && view.actor==battlepresentation::Actor::Opponent) {
                CHECK(partner.animation==(view.flash ? sprite::Animation::Hurt : sprite::Animation::Idle));
                CHECK(enemy.animation==sprite::Animation::Attack);
            } else CHECK(partner.animation==sprite::Animation::Idle && enemy.animation==sprite::Animation::Idle);
        }
        h.now=at+4000; h.model.battle=nullptr; h.sync();
        CHECK(h.ui.screen()==Screen::Battle && !sequencer.locked()); render(h.now);
    }
    // An off-center cropped frame must retain its occupied bounds after mirror.
    mask.fill(0); source.fill(0xF81F);
    for(unsigned y=5;y<25;++y)for(unsigned x=3;x<15;++x) {
        const auto index=y*32+x; mask[index/8]|=1u<<(index%8);
        if(x>=9)source[index]=0x07FF;
    }
    for(auto facing:{SpriteFacing::Left,SpriteFacing::Right}) {
        load(h.ui.partnerArtRequest(h.state,h.model,h.now),h.model.partnerArtwork,facing);
        h.model.partnerArtwork.contentX=3;h.model.partnerArtwork.contentY=5;
        h.model.partnerArtwork.contentWidth=12;h.model.partnerArtwork.contentHeight=20;
        CHECK(h.ui.render(h.state,h.model,pixels.data()+1,kPixels,h.now));
        const auto low=facing==SpriteFacing::Left ? 0x07FF : 0xF81F;
        CHECK(pixels[1+150*kSize+88]==low && pixels[1+150*kSize+117]==low);
        CHECK(pixels[1+150*kSize+118]!=low && pixels[1+150*kSize+147]!=low);
        CHECK(pixels[1+150*kSize+87]!=0xF81F && pixels[1+150*kSize+87]!=0x07FF);
        CHECK(pixels[1+150*kSize+148]!=0xF81F && pixels[1+150*kSize+148]!=0x07FF);
    }
    // Wrong-form/animation buffers never display; absent art is a neutral tile.
    h.model.partnerArtwork={}; h.model.artwork.sprite={};
    CHECK(h.ui.render(h.state,h.model,pixels.data()+1,kPixels,h.now));
    std::array<std::uint16_t,kPixels> missing;std::copy_n(pixels.data()+1,kPixels,missing.data());
    load(h.ui.partnerArtRequest(h.state,h.model,h.now),h.model.partnerArtwork,SpriteFacing::Left);
    h.model.partnerArtwork.formId=18;
    CHECK(h.ui.render(h.state,h.model,pixels.data()+1,kPixels,h.now));
    CHECK(std::memcmp(missing.data(),pixels.data()+1,sizeof(missing))==0);
    h.model.partnerArtwork.formId=11;h.model.partnerArtwork.animation=sprite::Animation::Attack;
    CHECK(h.ui.render(h.state,h.model,pixels.data()+1,kPixels,h.now));
    CHECK(std::memcmp(missing.data(),pixels.data()+1,sizeof(missing))==0);
    // A failed durable repair must not display the quarantined enemy or accept
    // an interaction even if a caller accidentally leaves inputEnabled true.
    const auto saved=h.state;
    h.model.encounterRecoveryRequired=true;h.model.inputEnabled=true;h.sync();
    CHECK(!h.ui.artRequest(h.state,h.model,h.now).formId && !h.ui.partnerArtRequest(h.state,h.model,h.now).formId);
    CHECK(!h.swipe(206,220,206,150) && !h.tap(206,365));
    CHECK(h.ui.render(h.state,h.model,pixels.data()+1,kPixels,h.now));
    CHECK(std::memcmp(missing.data(),pixels.data()+1,sizeof(missing))!=0);
    CHECK(std::memcmp(&saved,&h.state,sizeof(saved))==0);
}

int main(int argc,char** argv) {
    orientationGestures();
    horizontalTaps();
    battleArtworkSequence();
    homeCarousel();
    walkingCheckpoints();
    Harness h; const auto egg=h.state;
    CHECK(h.ui.screen()==Screen::Egg && sizeof(Controller)<512);
    CHECK(!h.event(TouchKind::Up,206,285)); // Opening with a held finger cannot hatch.
    CHECK(!h.event(TouchKind::Down,0,0)); CHECK(!h.event(TouchKind::Up,206,285));
    CHECK(!h.event(TouchKind::Down,206,285)); CHECK(!h.event(TouchKind::Move,250,240));
    CHECK(!h.event(TouchKind::Up,206,285));
    CHECK(std::memcmp(&egg,&h.state,sizeof(State))==0 && h.gameWrites==0);
    h.dispatch(h.tap(206,285));
    CHECK(h.ui.screen()==Screen::Starter && h.gameWrites==0);
    CHECK(!h.event(TouchKind::Up,206,312)); // Up cannot be reused on the next screen.
    h.dispatch(h.swipe(160,180,250,180)); CHECK(h.model.selectedId==8);
    h.dispatch(h.swipe(250,180,160,180)); CHECK(h.model.selectedId==1);
    h.dispatch(h.tap(206,312)); CHECK(h.ui.screen()==Screen::StarterReview);
    h.dispatch(h.tap(206,365)); CHECK(h.ui.screen()==Screen::Starter);
    CHECK(h.state.phase==Phase::Egg && h.gameWrites==0);
    h.dispatch(h.tap(206,312));
    auto hatch=h.tap(206,278); CHECK(hatch.kind==IntentKind::StarterConfirm && h.ui.pending());
    CHECK(!h.tap(206,278)); // No second mutation while storage is handling the proposal.
    h.dispatch(hatch,false); CHECK(h.ui.screen()==Screen::StarterReview);
    CHECK(h.state.phase==Phase::Egg && h.gameWrites==0);
    h.dispatch(h.tap(206,278)); CHECK(h.state.phase==Phase::Home && h.gameWrites==1);
    CHECK(h.state.starterId==1 && h.ui.screen()==Screen::Home);

    // Care emits proposals only. An unsuccessful save cannot alter this state.
    h.openHome(HomePanel::Care); CHECK(h.ui.screen()==Screen::Care);
    const auto beforeCare=h.state;
    auto feed=h.tap(120,252); CHECK(feed.kind==IntentKind::GameAction && feed.action==Action::Feed);
    CHECK(std::memcmp(&beforeCare,&h.state,sizeof(State))==0);
    h.dispatch(feed,false); CHECK(std::memcmp(&beforeCare,&h.state,sizeof(State))==0);
    h.model.writable=false; h.sync(); CHECK(!h.tap(120,252));
    h.model.writable=true; h.sync(); h.dispatch(h.tap(120,252)); CHECK(h.gameWrites==2);
    h.dispatch(h.tap(206,365)); CHECK(h.ui.screen()==Screen::Home);

    const auto beforeWalk=h.state.steps;
    h.encounter(); CHECK(h.state.steps==beforeWalk+100 && h.state.phase==Phase::Encounter);
    CHECK(!h.tap(280,306)); // No capture above half HP.
    while(h.state.wildHp>h.state.wildMaxHp/2) {
        h.strike(); CHECK(h.state.phase==Phase::Encounter);
    }
    h.dispatch(h.tap(280,306)); CHECK(h.ui.screen()==Screen::Capture);
    const auto beforeFlick=h.state;
    CHECK(!h.tap(206,300)); // A tap on the orb never spends an attempt.
    CHECK(!h.event(TouchKind::Down,206,250)); CHECK(!h.event(TouchKind::Up,206,180,60));
    CHECK(!h.event(TouchKind::Down,206,300)); CHECK(!h.event(TouchKind::Move,206,310));
    CHECK(!h.event(TouchKind::Up,206,330,60)); // Downward motion never throws.
    CHECK(!h.event(TouchKind::Down,206,300)); CHECK(!h.event(TouchKind::Move,411,411));
    CHECK(!h.event(TouchKind::Up,206,200,60)); // Out-of-circle path stays cancelled.
    CHECK(!h.event(TouchKind::Down,206,300));
    CHECK(!h.event(TouchKind::Cancel,206,300)); CHECK(!h.event(TouchKind::Up,206,200,60));
    CHECK(!h.event(TouchKind::Down,206,300)); CHECK(!h.event(TouchKind::Down,207,300));
    CHECK(!h.event(TouchKind::Up,206,200,60)); // Second contact cancels.
    CHECK(std::memcmp(&beforeFlick,&h.state,sizeof(State))==0);

    // Exact reference fling: 100 px /100 ms ->reach180, center impact.
    CHECK(!h.event(TouchKind::Down,206,300));
    CHECK(!h.event(TouchKind::Move,206,250,50));
    const auto flick=h.event(TouchKind::Up,206,200,50);
    CHECK(flick.kind==IntentKind::GameAction && flick.action==Action::Flick && flick.value==160*256+180);
    FlickTrajectory trajectory; CHECK(decodeFlick(flick.value,trajectory) && trajectory.hit);
    CHECK(!h.event(TouchKind::Up,206,200,50));
    CHECK(std::memcmp(&beforeFlick,&h.state,sizeof(State))==0);
    h.dispatch(flick); CHECK(h.state.sequence==beforeFlick.sequence+1);
    CHECK(!h.tap(120,252)); // A committed throw animation cannot double-trigger.
    h.now+=600;
    while(h.state.phase==Phase::Encounter) h.strike();
    CHECK(h.ui.screen()==Screen::Result);
    h.dispatch(h.tap(206,299)); CHECK(h.ui.screen()==Screen::Home);

    // The same release/confirmation path covers every starter, without direct
    // mutation by UI code or any write before the final explicit hatch.
    for(unsigned id=1;id<=8;++id) {
        Harness choice; choice.dispatch(choice.tap(206,285));
        for(unsigned i=1;i<id;++i) choice.dispatch(choice.swipe(250,180,160,180));
        CHECK(choice.model.selectedId==id && choice.gameWrites==0);
        choice.dispatch(choice.tap(206,312)); CHECK(choice.gameWrites==0);
        choice.dispatch(choice.tap(206,278));
        CHECK(choice.state.starterId==id && choice.gameWrites==1);
    }

    // A battle-mode setting requires its own review and a distinct release.
    Harness mode; mode.choose();
    mode.openHome(HomePanel::Settings); CHECK(mode.ui.screen()==Screen::Settings);
    const auto beforeMode=mode.state;
    auto reviewMode=mode.tap(120,312);
    CHECK(reviewMode.kind==IntentKind::Navigation);
    mode.dispatch(reviewMode); CHECK(mode.ui.screen()==Screen::ModeReview);
    CHECK(std::memcmp(&beforeMode,&mode.state,sizeof(State))==0);
    CHECK(!mode.event(TouchKind::Up,206,282));
    mode.dispatch(mode.tap(206,365)); CHECK(mode.ui.screen()==Screen::Settings);
    CHECK(mode.state.battleMode==BattleMode::Tactical);
    mode.dispatch(mode.tap(120,312));
    auto confirmMode=mode.tap(206,282);
    CHECK(confirmMode.kind==IntentKind::GameAction && confirmMode.action==Action::Mode && confirmMode.value==1);
    mode.dispatch(confirmMode,false);
    CHECK(mode.state.battleMode==BattleMode::Tactical && mode.ui.screen()==Screen::ModeReview);
    // Permission changes revoke the outstanding review, including its touch.
    CHECK(!mode.event(TouchKind::Down,206,282)); mode.model.writable=false; mode.sync();
    CHECK(mode.ui.screen()==Screen::Settings && !mode.event(TouchKind::Up,206,282));
    mode.model.writable=true; mode.sync(); mode.dispatch(mode.tap(120,312));
    CHECK(!mode.event(TouchKind::Down,206,282)); mode.model.inputEnabled=false; mode.sync();
    CHECK(mode.ui.screen()==Screen::Settings && !mode.event(TouchKind::Up,206,282));
    mode.model.inputEnabled=true; mode.sync(); mode.dispatch(mode.tap(120,312));
    // An independent console/care action makes the reviewed revision stale.
    CHECK(apply(mode.state,Action::Feed)==Error::None); mode.sync();
    CHECK(mode.ui.screen()==Screen::Settings && mode.state.battleMode==BattleMode::Tactical);
    mode.dispatch(mode.tap(120,312)); mode.model.selectedId=2; mode.ui.update(mode.state,mode.model);
    CHECK(mode.ui.screen()==Screen::Settings); mode.sync();
    mode.dispatch(mode.tap(120,312));
    confirmMode=mode.tap(206,282);
    CHECK(confirmMode.kind==IntentKind::GameAction && confirmMode.action==Action::Mode && confirmMode.value==1);
    mode.dispatch(confirmMode); CHECK(mode.state.battleMode==BattleMode::Auto && mode.ui.screen()==Screen::Settings);
    mode.dispatch(mode.tap(120,312));
    confirmMode=mode.tap(206,282);
    CHECK(confirmMode.kind==IntentKind::GameAction && confirmMode.action==Action::Mode && confirmMode.value==0);
    mode.dispatch(confirmMode); CHECK(mode.state.battleMode==BattleMode::Tactical);

    // Retain a legal encounter to test stale gesture cancellation separately.
    Harness stale; stale.choose(); stale.encounter();
    while(stale.state.wildHp>stale.state.wildMaxHp/2) stale.strike();
    stale.dispatch(stale.tap(280,306));
    CHECK(!stale.event(TouchKind::Down,206,300));
    CHECK(apply(stale.state,Action::Walk,1)==Error::None); stale.sync();
    CHECK(!stale.event(TouchKind::Up,206,200,100)); CHECK(stale.ui.screen()==Screen::Battle);
    stale.dispatch(stale.tap(280,306));
    CHECK(!stale.event(TouchKind::Down,206,300)); stale.model.inputEnabled=false; stale.sync();
    CHECK(!stale.event(TouchKind::Up,206,200,100)); stale.model.inputEnabled=true; stale.sync();
    CHECK(!stale.event(TouchKind::Up,206,200));
    CHECK(!stale.event(TouchKind::Down,206,300));
    CHECK(!stale.ui.touch(stale.state,stale.model,{TouchKind::Up,206,200,stale.now-1}));

    // Setup is a navigation request even before hatch and never proposes a save.
    Harness setup;
    auto openSetup=setup.tap(206,350);
    CHECK(openSetup.kind==IntentKind::OpenSetup && setup.gameWrites==0 && setup.state.phase==Phase::Egg);
    setup.dispatch(openSetup); CHECK(setup.ui.screen()==Screen::Egg);
    setup.model.writable=false; setup.sync(); CHECK(setup.tap(206,350).kind==IntentKind::OpenSetup);
    const auto beforeSetup=mode.state;
    CHECK(mode.tap(280,312).kind==IntentKind::OpenSetup);
    CHECK(std::memcmp(&beforeSetup,&mode.state,sizeof(State))==0);

    // Artwork requests identify the visible form, including an unhatched choice
    // and a browsed collection member; they never substitute an active ID.
    Harness art; CHECK(art.ui.artRequest(art.state,art.model,0).formId==0);
    art.dispatch(art.tap(206,285));
    for(unsigned id=1;id<=8;++id) {
        const auto request=art.ui.artRequest(art.state,art.model,800);
        CHECK(request.formId==forms::initialForm(combat::starterSpecies(id)));
        CHECK(request.animation==sprite::Animation::Idle && request.elapsedMs==800);
        CHECK(std::strcmp(request.sceneId,"scene-meadow-412-v1")==0);
        art.dispatch(art.swipe(250,180,160,180));
    }
    art.dispatch(art.tap(206,312)); art.dispatch(art.tap(206,278));
    const auto homeArt=art.ui.artRequest(art.state,art.model,800);
    CHECK(homeArt.formId==activeMember(art.state)->formId);
    CHECK(h.state.collectionCount>=2);
    h.openHome(HomePanel::Partners); CHECK(h.ui.screen()==Screen::Collection);
    h.dispatch(h.swipe(250,180,160,180));
    CHECK(h.ui.artRequest(h.state,h.model,h.now).formId==h.state.collection[1].formId);
    h.dispatch(h.tap(206,365));
    const auto wildArt=stale.ui.artRequest(stale.state,stale.model,stale.now);
    CHECK(wildArt.formId==stale.state.wildFormId);
    CHECK(std::strcmp(wildArt.sceneId,"scene-meadow-412-v1")==0); // First encounter, same cosmetic cycle as browser.

    // Render has explicit capacity checks, bounded writes and black corners.
    std::array<std::uint16_t,kPixels+2> framebuffer;
    framebuffer.fill(0xBEEF);
    CHECK(!h.ui.render(h.state,h.model,nullptr,kPixels,1000));
    CHECK(!h.ui.render(h.state,h.model,framebuffer.data()+1,kPixels-1,1000));
    CHECK(framebuffer[1]==0xBEEF);
    CHECK(h.ui.render(h.state,h.model,framebuffer.data()+1,kPixels,1000));
    CHECK(framebuffer.front()==0xBEEF && framebuffer.back()==0xBEEF);
    CHECK(framebuffer[1]==0 && framebuffer[kPixels]==0);
    // Raw gyro capability must not turn Home into a physical-step claim.
    Harness steps; steps.choose();
    CHECK(steps.ui.screen()==Screen::Home && !steps.model.stepsAvailable);
    std::array<std::uint16_t,kPixels> stepScreen;
    CHECK(steps.ui.render(steps.state,steps.model,stepScreen.data(),stepScreen.size(),1000));
    steps.model.motionAvailable=true;
    CHECK(steps.ui.render(steps.state,steps.model,framebuffer.data()+1,kPixels,1000));
    CHECK(std::memcmp(stepScreen.data(),framebuffer.data()+1,kPixels*sizeof(std::uint16_t))==0);
    steps.model.stepsAvailable=true;
    CHECK(steps.ui.render(steps.state,steps.model,framebuffer.data()+1,kPixels,1000));
    CHECK(std::memcmp(stepScreen.data(),framebuffer.data()+1,kPixels*sizeof(std::uint16_t))!=0);
    const auto beforeSteps=steps.state;
    steps.openHome(HomePanel::Settings); steps.dispatch(steps.tap(206,194)); CHECK(steps.ui.screen()==Screen::EncounterSettings);
    CHECK(std::memcmp(&beforeSteps,&steps.state,sizeof(State))==0);
    auto rate=steps.tap(120,252); CHECK(rate.kind==IntentKind::GameAction && rate.action==Action::EncounterRate && rate.value==0);
    steps.dispatch(rate); CHECK(steps.state.encounterRate==EncounterRate::Off);
    CHECK(steps.ui.render(steps.state,steps.model,stepScreen.data(),stepScreen.size(),1000));
    steps.dispatch(steps.tap(280,312)); CHECK(steps.state.encounterRate==EncounterRate::Frequent);
    CHECK(!steps.ui.walkingEligible());

    // Decoded artwork is caller-owned and optional. Tests use only synthetic
    // original pixels; neither a filesystem nor private asset is needed here.
    static std::array<std::uint16_t,kPixels> background;
    background.fill(0x1234);
    std::array<std::uint16_t,32*32> spritePixels;
    spritePixels.fill(0xF800);
    std::array<std::uint8_t,128> spriteMask{};
    constexpr auto opaque=16*32+16;
    spritePixels[opaque]=0; // Opaque black must draw; color is not an alpha key.
    spriteMask[opaque/8]=static_cast<std::uint8_t>(1u<<(opaque%8));
    CHECK(art.ui.render(art.state,art.model,stepScreen.data(),kPixels,800));
    const auto buttonBefore=stepScreen[310*kSize+120];
    art.model.artwork.background=background.data(); art.model.artwork.backgroundPixels=kPixels;
    art.model.artwork.backgroundId=homeArt.sceneId;
    CHECK(art.ui.render(art.state,art.model,stepScreen.data(),kPixels,800));
    CHECK(stepScreen[205*kSize+95]==0x1234 && stepScreen[0]==0);
    CHECK(stepScreen[310*kSize+120]==buttonBefore); // Controls stay opaque.
    auto& frame=art.model.artwork.sprite;
    frame.formId=homeArt.formId; frame.animation=homeArt.animation;
    frame.pixels=spritePixels.data(); frame.pixelCount=spritePixels.size();
    frame.mask=spriteMask.data(); frame.maskBytes=spriteMask.size(); frame.width=frame.height=32;
    CHECK(art.ui.render(art.state,art.model,framebuffer.data()+1,kPixels,800));
    CHECK(framebuffer[1+196*kSize+206]==0 && framebuffer[1+196*kSize+204]==0x1234);
    CHECK(framebuffer.front()==0xBEEF && framebuffer.back()==0xBEEF);
    const auto goodFrame=frame;
    std::array<std::uint8_t,32> smallMask{};
    constexpr auto smallOpaque=8*16+8;
    smallMask[smallOpaque/8]=static_cast<std::uint8_t>(1u<<(smallOpaque%8));
    frame.width=frame.height=16; frame.pixelCount=256;
    frame.mask=smallMask.data(); frame.maskBytes=smallMask.size();
    CHECK(art.ui.render(art.state,art.model,framebuffer.data()+1,kPixels,800));
    CHECK(framebuffer[1+196*kSize+206]==0xF800); //16px resident frames scale11x on Home.
    frame=goodFrame;
    // Invalid or stale frame metadata must reproduce the existing fallback.
    frame.formId=homeArt.formId+1;
    CHECK(art.ui.render(art.state,art.model,framebuffer.data()+1,kPixels,800));
    CHECK(std::memcmp(stepScreen.data(),framebuffer.data()+1,kPixels*sizeof(std::uint16_t))==0);
    for(unsigned invalid=0;invalid<9;++invalid) {
        frame=goodFrame;
        switch(invalid) {
        case 0: frame.animation=sprite::Animation::Attack; break;
        case 1: frame.pixelCount=1023; break;
        case 2: frame.maskBytes=127; break;
        case 3: frame.mask=nullptr; break;
        case 4: frame.width=31; break;
        case 5: frame.height=16; break;
        case 6: frame.pixels=nullptr; break;
        case 7: frame.contentX=31; frame.contentWidth=2; frame.contentHeight=8; break;
        case 8: frame.contentWidth=8; frame.contentHeight=0; break;
        }
        CHECK(art.ui.render(art.state,art.model,framebuffer.data()+1,kPixels,800));
        CHECK(std::memcmp(stepScreen.data(),framebuffer.data()+1,kPixels*sizeof(std::uint16_t))==0);
    }
    frame={}; art.model.artwork={};
    CHECK(art.ui.render(art.state,art.model,stepScreen.data(),kPixels,800));
    art.model.artwork.background=background.data(); art.model.artwork.backgroundPixels=kPixels-1;
    art.model.artwork.backgroundId=homeArt.sceneId;
    CHECK(art.ui.render(art.state,art.model,framebuffer.data()+1,kPixels,800));
    CHECK(std::memcmp(stepScreen.data(),framebuffer.data()+1,kPixels*sizeof(std::uint16_t))==0);
    art.model.artwork.backgroundPixels=kPixels; art.model.artwork.backgroundId="scene-digital-412-v1";
    CHECK(art.ui.render(art.state,art.model,framebuffer.data()+1,kPixels,800));
    CHECK(std::memcmp(stepScreen.data(),framebuffer.data()+1,kPixels*sizeof(std::uint16_t))==0);

    art.model.artwork={};
    art.openHome(HomePanel::Care); art.dispatch(art.tap(120,252));
    auto animated=art.ui.artRequest(art.state,art.model,art.now);
    CHECK(animated.animation==sprite::Animation::Care && animated.elapsedMs==0);
    CHECK(art.ui.artRequest(art.state,art.model,art.now+600).animation==sprite::Animation::Idle);
    art.dispatch(art.tap(206,312));
    CHECK(art.ui.artRequest(art.state,art.model,art.now).animation==sprite::Animation::Sleep);
    art.dispatch(art.tap(206,365)); art.encounter(); art.strike();
    CHECK(art.ui.artRequest(art.state,art.model,art.now).animation==sprite::Animation::Hurt);

    // Tactical carousel selection never mutates the core. Only a separate
    // clear upward swipe can propose the selected named move.
    Harness tactical; tactical.choose(); tactical.encounter();
    const auto beforeSelection=tactical.state;
    auto selected=tactical.swipe(170,180,250,180);
    CHECK(selected.kind==IntentKind::Navigation); tactical.dispatch(selected);
    CHECK(std::memcmp(&beforeSelection,&tactical.state,sizeof(State))==0);
    CHECK(!tactical.swipe(206,180,240,140)); // Diagonal.
    CHECK(!tactical.swipe(206,150,206,210)); // Downward.
    CHECK(!tactical.swipe(206,180,216,170)); // Jitter.
    CHECK(!tactical.event(TouchKind::Down,206,210));
    CHECK(!tactical.event(TouchKind::Cancel,206,210)); CHECK(!tactical.event(TouchKind::Up,206,130));
    CHECK(!tactical.event(TouchKind::Down,206,210)); CHECK(!tactical.event(TouchKind::Down,207,210));
    CHECK(!tactical.event(TouchKind::Up,206,130));
    CHECK(std::memcmp(&beforeSelection,&tactical.state,sizeof(State))==0);
    auto magic=tactical.swipe(206,220,206,150);
    CHECK(magic.kind==IntentKind::GameAction && magic.action==Action::Magic);
    CHECK(!tactical.event(TouchKind::Up,206,150));
    CHECK(!tactical.swipe(206,220,206,150)); // Pending durability locks repeated input.
    tactical.dispatch(magic); CHECK(tactical.state.sequence==beforeSelection.sequence+1);
    tactical.dispatch(tactical.swipe(250,180,170,180)); // Physical -> Heavy, then a fresh upward commit.
    auto heavy=tactical.swipe(206,220,206,150); CHECK(heavy.action==Action::Heavy);
    tactical.dispatch(heavy,false);
    tactical.dispatch(tactical.swipe(170,180,250,180));
    auto physical=tactical.swipe(206,220,206,150); CHECK(physical.action==Action::Attack);
    tactical.dispatch(physical,false);

    // Playback keeps both frozen combatants visible after durable Auto changes
    // core phase to Home. It never authorizes actions or steps while resolving.
    battlepresentation::View playback;
    playback.locked=true; playback.phase=battlepresentation::Phase::Player;
    playback.actor=battlepresentation::Actor::Player; playback.playerFormId=18; playback.enemyFormId=11;
    playback.playerHp=50; playback.enemyHp=40; playback.playerMaxHp=100; playback.enemyMaxHp=100;
    playback.actorName="Agumon"; playback.moveName="Pepper Breath"; playback.move=autobattle::Move::Magic;
    playback.progressPermille=400; playback.flash=true; playback.damage=12; playback.turn=1; playback.turnCount=3;
    tactical.state=newGame(); tactical.model.battle=&playback; tactical.sync();
    CHECK(tactical.ui.screen()==Screen::Battle && !tactical.ui.walkingEligible());
    CHECK(tactical.ui.artRequest(tactical.state,tactical.model,1000).formId==11);
    CHECK(!tactical.swipe(206,220,206,150)); CHECK(!tactical.tap(206,365));
    CHECK(tactical.ui.render(tactical.state,tactical.model,framebuffer.data()+1,kPixels,1000));
    CHECK(framebuffer.front()==0xBEEF && framebuffer.back()==0xBEEF);
    playback.phase=battlepresentation::Phase::Summary; playback.outcome=autobattle::Outcome::Won;
    CHECK(tactical.ui.render(tactical.state,tactical.model,framebuffer.data()+1,kPixels,1100));
    playback.locked=false; tactical.sync(); CHECK(tactical.ui.screen()==Screen::Result);

    // Digivolution uses graph edges, preserves stable owned identity, and only
    // commits after a separate reviewed touch. Test fixtures never touch a board.
    Harness evolution; evolution.state=stableMemberFixture(18); evolution.sync();
    const auto* route=forms::outgoing(activeMember(evolution.state)->formId,0); CHECK(route);
    const auto target=route->to;
    auto openEvolution=[&]() {
        evolution.openHome(HomePanel::Partners); CHECK(evolution.ui.screen()==Screen::Collection);
        evolution.dispatch(evolution.swipe(250,180,160,180)); // Active member19, not collection slot0.
        evolution.dispatch(evolution.tap(120,312)); CHECK(evolution.ui.screen()==Screen::Stats);
        CHECK(evolution.ui.artRequest(evolution.state,evolution.model,0).formId==18);
        CHECK(!evolution.ui.walkingEligible());
        for (unsigned page=0;page<4;++page) {
            CHECK(evolution.ui.render(evolution.state,evolution.model,framebuffer.data()+1,kPixels,1000));
            CHECK(framebuffer.front()==0xBEEF && framebuffer.back()==0xBEEF);
            evolution.dispatch(evolution.swipe(250,180,160,180));
        }
        evolution.dispatch(evolution.tap(206,312)); CHECK(evolution.ui.screen()==Screen::Evolution);
    };
    openEvolution();
    CHECK(evolution.ui.artRequest(evolution.state,evolution.model,0).formId==target);
    const auto tooYoung=evolution.state;
    evolution.dispatch(evolution.tap(280,312)); // Locked selection opens unmet requirements, never a mutation.
    CHECK(evolution.ui.screen()==Screen::Evolution);
    evolution.dispatch(evolution.tap(206,365)); // Close explanation, preserve candidate.
    CHECK(std::memcmp(&tooYoung,&evolution.state,sizeof(State))==0);
    evolution.state.level=evolution.state.collection[1].level=route->minLevel;
    evolution.state.collection[1].xp=xpForLevel(route->minLevel);
    evolution.state.bond=evolution.state.collection[1].bond=route->minBond;
    CHECK(isValid(evolution.state)); evolution.sync();
    for (unsigned page=0;page<3;++page) {
        CHECK(evolution.ui.render(evolution.state,evolution.model,framebuffer.data()+1,kPixels,1000));
        CHECK(framebuffer.front()==0xBEEF && framebuffer.back()==0xBEEF);
        evolution.dispatch(evolution.tap(120,312));
    }
    const auto beforeEvolution=evolution.state;
    evolution.dispatch(evolution.tap(280,312)); CHECK(evolution.ui.screen()==Screen::EvolutionReview);
    CHECK(std::memcmp(&beforeEvolution,&evolution.state,sizeof(State))==0);
    CHECK(!evolution.event(TouchKind::Up,206,302)); // Opening release cannot confirm.
    evolution.dispatch(evolution.tap(206,365)); CHECK(evolution.ui.screen()==Screen::Evolution);
    CHECK(std::memcmp(&beforeEvolution,&evolution.state,sizeof(State))==0);
    evolution.dispatch(evolution.tap(280,312));
    CHECK(!evolution.event(TouchKind::Down,206,302));
    CHECK(!evolution.event(TouchKind::Cancel,206,302));
    CHECK(evolution.ui.screen()==Screen::Evolution && !evolution.event(TouchKind::Up,206,302));
    evolution.dispatch(evolution.tap(280,312));
    CHECK(!evolution.event(TouchKind::Down,206,302)); evolution.model.inputEnabled=false; evolution.sync();
    CHECK(evolution.ui.screen()==Screen::Evolution && !evolution.event(TouchKind::Up,206,302));
    evolution.model.inputEnabled=true; evolution.sync();
    evolution.dispatch(evolution.tap(280,312));
    CHECK(apply(evolution.state,Action::Feed)==Error::None); evolution.sync();
    CHECK(evolution.ui.screen()==Screen::Evolution); // State revision revokes review.
    evolution.dispatch(evolution.tap(280,312));
    auto evolve=evolution.tap(206,302); CHECK(evolve.kind==IntentKind::GameAction && evolve.action==Action::Evolve && evolve.value==target);
    CHECK(!evolution.tap(206,302));
    evolution.dispatch(evolve,false); CHECK(evolution.ui.screen()==Screen::Evolution);
    CHECK(activeMember(evolution.state)->formId==18); // No display of success after failed save.
    evolution.dispatch(evolution.tap(280,312));
    const auto beforeCommit=evolution.state;
    evolution.dispatch(evolution.tap(206,302)); CHECK(evolution.ui.screen()==Screen::EvolutionResult);
    CHECK(activeMember(evolution.state)->id==19 && activeMember(evolution.state)->formId==target);
    CHECK(activeMember(evolution.state)->xp==activeMember(beforeCommit)->xp && evolution.state.bond==beforeCommit.bond);
    CHECK(evolution.state.nextMemberId==beforeCommit.nextMemberId && evolution.state.sequence==beforeCommit.sequence+1);
    CHECK(evolution.ui.artRequest(evolution.state,evolution.model,0).formId==target);
    CHECK(evolution.ui.artRequest(evolution.state,evolution.model,0).animation==sprite::Animation::Celebrate);
    CHECK(evolution.ui.render(evolution.state,evolution.model,framebuffer.data()+1,kPixels,1000));
    evolution.dispatch(evolution.tap(206,306)); CHECK(evolution.ui.screen()==Screen::Stats);
    CHECK(evolution.ui.artRequest(evolution.state,evolution.model,0).formId==target);

    // Full collection and duplicate species keep instance identity distinct.
    // Release is offered only for an inactive instance and needs its own review.
    Harness roster; roster.choose(); roster.state.sequence=100;
    roster.state.captures=roster.state.encounters=7; roster.state.steps=700;
    roster.state.collectionCount=8; roster.state.nextMemberId=9;
    for (unsigned i=1;i<8;++i) {
        roster.state.collection[i]=roster.state.collection[0];
        roster.state.collection[i].id=i+1; roster.state.collection[i].capturedAtSequence=i;
    }
    CHECK(isValid(roster.state)); roster.sync(); roster.openHome(HomePanel::Partners);
    CHECK(roster.ui.screen()==Screen::Collection);
    CHECK(!roster.tap(280,312)); // Already equipped instance cannot be selected again.
    roster.dispatch(roster.tap(120,312)); roster.dispatch(roster.tap(206,312));
    CHECK(roster.ui.screen()==Screen::Evolution); // Active partner never shows Release.
    roster.dispatch(roster.tap(206,365)); roster.dispatch(roster.tap(206,365));
    roster.dispatch(roster.swipe(250,180,160,180)); roster.dispatch(roster.tap(120,312));
    CHECK(roster.ui.screen()==Screen::Stats);
    CHECK(roster.ui.artRequest(roster.state,roster.model,0).formId==roster.state.collection[1].formId);
    const auto beforeRelease=roster.state;
    roster.dispatch(roster.tap(206,312)); CHECK(roster.ui.screen()==Screen::ReleaseReview);
    CHECK(!roster.event(TouchKind::Up,206,302));
    CHECK(std::memcmp(&beforeRelease,&roster.state,sizeof(State))==0);
    roster.dispatch(roster.tap(206,365)); CHECK(roster.ui.screen()==Screen::Stats);
    CHECK(std::memcmp(&beforeRelease,&roster.state,sizeof(State))==0);
    roster.dispatch(roster.tap(206,312)); CHECK(!roster.event(TouchKind::Down,206,302));
    CHECK(apply(roster.state,Action::Feed)==Error::None); roster.sync();
    CHECK(roster.ui.screen()==Screen::Stats && !roster.event(TouchKind::Up,206,302));
    roster.dispatch(roster.tap(206,312));
    auto release=roster.tap(206,302); CHECK(release.action==Action::Release && release.value==2);
    roster.dispatch(release,false); CHECK(roster.ui.screen()==Screen::Stats && roster.state.collectionCount==8);
    roster.dispatch(roster.tap(206,312)); roster.dispatch(roster.tap(206,302));
    CHECK(roster.ui.screen()==Screen::Collection && roster.state.collectionCount==7);
    CHECK(!findMember(roster.state,2) && roster.state.activeCreatureId==1 && roster.state.nextMemberId==9);
    CHECK(roster.state.collection[1].id==3); // Slot moved; stable ID did not.
    auto equip=roster.tap(280,312); CHECK(equip.action==Action::Select && equip.value==3);
    roster.dispatch(equip,false); CHECK(roster.state.activeCreatureId==1);
    roster.dispatch(roster.tap(280,312)); CHECK(roster.state.activeCreatureId==3);
    CHECK(!roster.tap(280,312));
    Controller restoredRoster; restoredRoster.update(roster.state,roster.model);
    CHECK(restoredRoster.screen()==Screen::Home && roster.state.activeCreatureId==3);
    // Read-only saves still allow browsing; mutation buttons remain disabled.
    roster.model.writable=false; roster.sync(); roster.dispatch(roster.swipe(250,180,160,180));
    CHECK(!roster.tap(280,312)); roster.dispatch(roster.tap(120,312));
    CHECK(!roster.tap(206,312));

    // Actual graph-only baby route works even though historical children[] is empty.
    Harness baby; baby.state=stableMemberFixture(67); baby.sync();
    const auto* babyRoute=forms::outgoing(67,0); CHECK(babyRoute && !forms::find(67)->children[0]);
    baby.state.bond=baby.state.collection[1].bond=babyRoute->minBond; baby.sync();
    baby.openHome(HomePanel::Partners); baby.dispatch(baby.swipe(250,180,160,180)); baby.dispatch(baby.tap(120,312));
    baby.dispatch(baby.tap(206,312)); CHECK(baby.ui.artRequest(baby.state,baby.model,0).formId==babyRoute->to);
    baby.dispatch(baby.tap(280,312)); baby.dispatch(baby.tap(206,302));
    CHECK(activeMember(baby.state)->id==19 && activeMember(baby.state)->formId==babyRoute->to);
    // A restarted controller has no outstanding confirmation and returns Home.
    Controller restarted; restarted.update(beforeCommit,evolution.model); CHECK(restarted.screen()==Screen::Home);
    CHECK(!restarted.touch(beforeCommit,evolution.model,{TouchKind::Up,206,302,1000}));

    // Nearby uses only the real borrowed protocol view. Peer changes revoke a
    // challenge review; mutual consent and role-specific choices stay explicit.
    Harness nearbyUi; nearbyUi.choose();
    const auto companionBeforeDuel=nearbyUi.state;
    nearbyUi.selectHome(HomePanel::Nearby);
    auto opened=nearbyUi.tap(206,323); CHECK(opened.kind==IntentKind::OpenNearby);
    nearbyUi.dispatch(opened); CHECK(nearbyUi.ui.screen()==Screen::Nearby && !nearbyUi.ui.walkingEligible());
    CHECK(nearbyUi.ui.render(nearbyUi.state,nearbyUi.model,framebuffer.data()+1,kPixels,1000));
    CHECK(!nearbyUi.tap(206,312)); // No invented peer or network success.
    nearby::View network; network.stage=nearby::Stage::Discovering; network.host=true;
    network.peerCount=4;
    for (unsigned i=0;i<4;++i) {
        network.peers[i].mac.bytes[5]=static_cast<std::uint8_t>(i+1);
        network.peers[i].fighter={i+11,18,1}; network.peers[i].available=true;
    }
    nearbyUi.model.nearby=&network; nearbyUi.sync();
    CHECK(nearbyUi.ui.artRequest(nearbyUi.state,nearbyUi.model,0).formId==18);
    nearbyUi.dispatch(nearbyUi.swipe(250,180,160,180)); nearbyUi.dispatch(nearbyUi.tap(206,312));
    CHECK(nearbyUi.ui.screen()==Screen::NearbyReview);
    CHECK(!nearbyUi.event(TouchKind::Up,206,302));
    CHECK(!nearbyUi.event(TouchKind::Down,206,302));
    network.peers[1].fighter.level=2; nearbyUi.sync(); // Same MAC, different frozen profile.
    CHECK(nearbyUi.ui.screen()==Screen::Nearby && !nearbyUi.event(TouchKind::Up,206,302));
    nearbyUi.dispatch(nearbyUi.tap(206,312)); nearbyUi.dispatch(nearbyUi.tap(206,365));
    CHECK(nearbyUi.ui.screen()==Screen::Nearby);
    nearbyUi.dispatch(nearbyUi.tap(206,312));
    auto challenge=nearbyUi.tap(206,302); CHECK(challenge.kind==IntentKind::NearbyChallenge && challenge.value==1);
    nearbyUi.dispatch(challenge); CHECK(nearbyUi.ui.screen()==Screen::Nearby);
    network.stage=nearby::Stage::Incoming; network.host=false; network.session=42;
    network.offered[0]={17,18,1}; network.offered[1]={1,11,1}; network.offeredMode=nearby::Mode::Tactical;
    nearbyUi.sync(); CHECK(nearbyUi.ui.render(nearbyUi.state,nearbyUi.model,framebuffer.data()+1,kPixels,1000));
    auto accept=nearbyUi.tap(120,252); CHECK(accept.kind==IntentKind::NearbyAccept); nearbyUi.dispatch(accept);
    CHECK(nearby::begin(network.offered[0],network.offered[1],nearby::Mode::Tactical,123,network.match));
    network.stage=nearby::Stage::Playing; nearbyUi.sync();
    CHECK(network.match.attacker==0); // Guest defends first.
    nearbyUi.dispatch(nearbyUi.tap(120,252)); nearbyUi.dispatch(nearbyUi.tap(280,252)); // Icons browse; only a fresh upward swipe commits.
    auto defense=nearbyUi.swipe(206,300,206,180); CHECK(defense.kind==IntentKind::NearbyChoose && defense.value==static_cast<unsigned>(nearby::Choice::Brace));
    network.localChoicePending=true; nearbyUi.dispatch(defense); CHECK(!nearbyUi.swipe(250,180,160,180));
    CHECK(nearby::resolve(network.match,0,nearby::Choice::Physical,nearby::Choice::Brace));
    network.localChoicePending=false; nearbyUi.sync(); CHECK(network.match.attacker==1);
    nearbyUi.dispatch(nearbyUi.swipe(170,180,250,180));
    auto duelMove=nearbyUi.swipe(206,220,206,150);
    CHECK(duelMove.kind==IntentKind::NearbyChoose && duelMove.value==static_cast<unsigned>(nearby::Choice::Magic));
    CHECK(!nearbyUi.swipe(206,220,206,150));
    network.localChoicePending=true; nearbyUi.dispatch(duelMove);
    CHECK(nearbyUi.ui.render(nearbyUi.state,nearbyUi.model,framebuffer.data()+1,kPixels,1000));
    network.stage=nearby::Stage::Reconnecting; nearbyUi.sync(); CHECK(!nearbyUi.tap(120,252));
    CHECK(nearbyUi.ui.render(nearbyUi.state,nearbyUi.model,framebuffer.data()+1,kPixels,1000));
    network.stage=nearby::Stage::Finished; network.match.status=nearby::Status::HostWon; nearbyUi.sync();
    CHECK(nearbyUi.ui.render(nearbyUi.state,nearbyUi.model,framebuffer.data()+1,kPixels,1000));
    auto close=nearbyUi.tap(206,365); CHECK(close.kind==IntentKind::CloseNearby); nearbyUi.dispatch(close);
    CHECK(nearbyUi.ui.screen()==Screen::Home && nearbyUi.ui.walkingEligible());
    CHECK(std::memcmp(&companionBeforeDuel,&nearbyUi.state,sizeof(State))==0);
    CHECK(framebuffer.front()==0xBEEF && framebuffer.back()==0xBEEF);

    // Nearby feedback is a pure timed view of the already-resolved exchange.
    // Real decoded SD frames enter through these same two caller-owned slots.
    Harness timedDuel; timedDuel.choose(); timedDuel.openHome(HomePanel::Nearby);
    nearby::View timedNetwork; timedNetwork.stage=nearby::Stage::Playing; timedNetwork.host=false; timedNetwork.session=88;
    CHECK(nearby::begin({17,18,1},{1,11,1},nearby::Mode::Tactical,444,timedNetwork.match));
    CHECK(nearby::resolve(timedNetwork.match,0,nearby::Choice::Physical,nearby::Choice::Brace));
    timedDuel.model.nearby=&timedNetwork; timedDuel.model.nearbyTurnElapsedMs=0; timedDuel.sync();
    std::array<std::uint16_t,32*32> localPixels,remotePixels; localPixels.fill(0xF800); remotePixels.fill(0x001F);
    std::array<std::uint8_t,128> opaqueMask; opaqueMask.fill(0xff);
    auto& localFrame=timedDuel.model.partnerArtwork; auto& remoteFrame=timedDuel.model.artwork.sprite;
    localFrame={11,sprite::Animation::Idle,localPixels.data(),localPixels.size(),opaqueMask.data(),opaqueMask.size(),32,32};
    remoteFrame={18,sprite::Animation::Idle,remotePixels.data(),remotePixels.size(),opaqueMask.data(),opaqueMask.size(),32,32};
    CHECK(timedDuel.ui.artRequest(timedDuel.state,timedDuel.model,5000).formId==18);
    CHECK(timedDuel.ui.artRequest(timedDuel.state,timedDuel.model,5000).animation==sprite::Animation::Idle);
    CHECK(timedDuel.ui.render(timedDuel.state,timedDuel.model,framebuffer.data()+1,kPixels,5000));
    CHECK(framebuffer[1+176*kSize+118]==0xF800 && framebuffer[1+176*kSize+294]==0x001F);
    const auto fullBarPixel=framebuffer[1+124*kSize+171]; // Local defender HP before impact.
    localFrame.formId=18; CHECK(timedDuel.ui.render(timedDuel.state,timedDuel.model,framebuffer.data()+1,kPixels,5000));
    CHECK(framebuffer[1+176*kSize+118]!=0xF800); localFrame.formId=11;
    remoteFrame.formId=11; CHECK(timedDuel.ui.render(timedDuel.state,timedDuel.model,framebuffer.data()+1,kPixels,5000));
    CHECK(framebuffer[1+176*kSize+294]!=0x001F); remoteFrame.formId=18;
    const auto timedSave=timedDuel.state; const auto timedMatch=timedNetwork.match;
    const std::uint32_t moments[]{0,599,600,1100,1199,1200,1379,1380,1799,1800,2399,2400,UINT32_MAX-1,kNoNearbyTurn};
    for (const auto elapsed:moments) {
        timedDuel.model.nearbyTurnElapsedMs=elapsed; timedDuel.sync();
        CHECK(timedDuel.ui.render(timedDuel.state,timedDuel.model,framebuffer.data()+1,kPixels,5000));
        CHECK(framebuffer.front()==0xBEEF && framebuffer.back()==0xBEEF && framebuffer[1]==0 && framebuffer[kPixels]==0);
        CHECK(timedDuel.ui.render(timedDuel.state,timedDuel.model,stepScreen.data(),stepScreen.size(),5000));
        CHECK(std::memcmp(stepScreen.data(),framebuffer.data()+1,kPixels*sizeof(std::uint16_t))==0);
        CHECK(std::memcmp(&timedSave,&timedDuel.state,sizeof(State))==0);
        CHECK(std::memcmp(&timedMatch,&timedNetwork.match,sizeof(nearby::Match))==0);
        if (elapsed<1200) CHECK(framebuffer[1+124*kSize+171]==fullBarPixel);
        else CHECK(framebuffer[1+124*kSize+171]!=fullBarPixel);
        if (elapsed<kNearbyFeedbackMs) {
            CHECK(!timedDuel.tap(120,252)); CHECK(!timedDuel.swipe(206,220,206,150));
            CHECK(!timedDuel.swipe(206,300,206,200)); // Never arms capture in Nearby.
        }
    }
    // A contact held across feedback expiry cannot become the next attack.
    timedDuel.model.nearbyTurnElapsedMs=2399; timedDuel.sync();
    CHECK(!timedDuel.event(TouchKind::Down,206,220));
    timedDuel.model.nearbyTurnElapsedMs=2400; timedDuel.sync();
    CHECK(!timedDuel.event(TouchKind::Up,206,150,100));
    CHECK(timedDuel.swipe(170,280,250,280).kind==IntentKind::Navigation);
    timedDuel.ui.resolve(); timedDuel.sync();
    // Reflected Heavy returns to the actual attacker, irrespective of local role.
    CHECK(nearby::begin({17,18,1},{1,11,1},nearby::Mode::Tactical,445,timedNetwork.match));
    CHECK(nearby::resolve(timedNetwork.match,0,nearby::Choice::Heavy,nearby::Choice::Counter));
    CHECK(timedNetwork.match.lastReflected && timedNetwork.match.lastDamage[0]>0);
    timedDuel.model.nearbyTurnElapsedMs=1200; timedDuel.sync();
    CHECK(timedDuel.ui.render(timedDuel.state,timedDuel.model,framebuffer.data()+1,kPixels,5000));
    CHECK(framebuffer[1+176*kSize+118]==0xF800 && framebuffer[1+176*kSize+294]!=0x001F);
    // Auto uses the same bounded presentation and never resolves another turn.
    CHECK(nearby::begin({17,18,1},{1,11,1},nearby::Mode::Auto,446,timedNetwork.match));
    CHECK(nearby::resolveAuto(timedNetwork.match,0)); const auto autoResolved=timedNetwork.match;
    timedDuel.model.nearbyTurnElapsedMs=600; timedDuel.sync();
    CHECK(timedDuel.ui.render(timedDuel.state,timedDuel.model,framebuffer.data()+1,kPixels,5000));
    CHECK(std::memcmp(&autoResolved,&timedNetwork.match,sizeof(nearby::Match))==0);
    // Final-hit feedback is still shown before the outcome card; disconnection
    // takes priority immediately and cannot continue any attack gesture.
    timedNetwork.match.hp[1]=1; CHECK(nearby::valid(timedNetwork.match));
    while(timedNetwork.match.status==nearby::Status::Active) CHECK(nearby::resolveAuto(timedNetwork.match,timedNetwork.match.sequence));
    timedNetwork.stage=nearby::Stage::Finished; timedDuel.model.nearbyTurnElapsedMs=0; timedDuel.sync();
    CHECK(timedDuel.ui.render(timedDuel.state,timedDuel.model,stepScreen.data(),stepScreen.size(),5000));
    timedDuel.model.nearbyTurnElapsedMs=2400; timedDuel.sync();
    CHECK(timedDuel.ui.render(timedDuel.state,timedDuel.model,framebuffer.data()+1,kPixels,5000));
    CHECK(std::memcmp(stepScreen.data(),framebuffer.data()+1,kPixels*sizeof(std::uint16_t))!=0);
    timedNetwork.stage=nearby::Stage::Reconnecting; timedDuel.model.nearbyTurnElapsedMs=0; timedDuel.sync();
    CHECK(!timedDuel.swipe(206,220,206,150));
    CHECK(timedDuel.ui.render(timedDuel.state,timedDuel.model,framebuffer.data()+1,kPixels,5000));
    CHECK(std::memcmp(&timedSave,&timedDuel.state,sizeof(State))==0);
    auto timedLeave=timedDuel.tap(206,365); CHECK(timedLeave.kind==IntentKind::CloseNearby);

    // New carousel gestures are relative and never reuse the selection touch
    // as a commit. Disabled Catch absorbs drags; no invisible Prev/Next taps.
    Harness carousel; carousel.choose(); carousel.encounter();
    const auto carouselBefore=carousel.state;
    carousel.dispatch(carousel.tap(120,252)); carousel.dispatch(carousel.tap(280,252));
    CHECK(!carousel.swipe(300,310,300,210)); // Disabled CATCH origin cannot attack.
    const Action cycle[]{Action::Heavy,Action::Magic,Action::Attack};
    for(const auto action:cycle) {
        carousel.dispatch(carousel.swipe(250,278,160,278));
        CHECK(!carousel.event(TouchKind::Up,160,278));
        auto commit=carousel.swipe(206,300,206,190); CHECK(commit.action==action);
        carousel.dispatch(commit,false);
    }
    CHECK(std::memcmp(&carouselBefore,&carousel.state,sizeof(State))==0);
    carousel.state.energy=carousel.state.collection[0].energy=0; carousel.sync();
    carousel.dispatch(carousel.swipe(250,278,160,278));
    const auto exhausted=carousel.state;
    auto noEnergy=carousel.swipe(206,300,206,190); CHECK(noEnergy.kind==IntentKind::Navigation);
    CHECK(!carousel.ui.pending() && std::memcmp(&exhausted,&carousel.state,sizeof(State))==0);

    Harness browsing; browsing.dispatch(browsing.tap(206,285));
    const auto starterState=browsing.state;
    CHECK(!browsing.tap(120,252) && !browsing.tap(280,252));
    CHECK(!browsing.swipe(206,190,245,150)); // Diagonal cannot choose a starter.
    CHECK(!browsing.swipe(206,312,290,220)); // Button-origin drag never browses.
    browsing.dispatch(browsing.swipe(160,180,250,180)); CHECK(browsing.model.selectedId==8);
    browsing.dispatch(browsing.swipe(250,180,160,180)); CHECK(browsing.model.selectedId==1);
    CHECK(std::memcmp(&starterState,&browsing.state,sizeof(State))==0);
    CHECK(browsing.ui.render(browsing.state,browsing.model,stepScreen.data(),stepScreen.size(),1000));
    CHECK(browsing.ui.render(browsing.state,browsing.model,framebuffer.data()+1,kPixels,7000));
    CHECK(std::memcmp(stepScreen.data()+334*kSize,framebuffer.data()+1+334*kSize,13*kSize*sizeof(std::uint16_t))!=0); // Hint fades.
    CHECK(browsing.ui.render(browsing.state,browsing.model,stepScreen.data(),stepScreen.size(),8000));
    CHECK(browsing.ui.render(browsing.state,browsing.model,framebuffer.data()+1,kPixels,9000));
    CHECK(std::memcmp(stepScreen.data()+334*kSize,framebuffer.data()+1+334*kSize,13*kSize*sizeof(std::uint16_t))==0); // Expired, not perpetual.
    browsing.dispatch(browsing.tap(206,365));
    CHECK(browsing.ui.render(browsing.state,browsing.model,framebuffer.data()+1,kPixels,9100));
    browsing.dispatch(browsing.tap(206,285));
    CHECK(browsing.ui.render(browsing.state,browsing.model,framebuffer.data()+1,kPixels,9200));
    CHECK(std::memcmp(stepScreen.data()+334*kSize,framebuffer.data()+1+334*kSize,13*kSize*sizeof(std::uint16_t))==0); // Re-entry cannot restart first-use hint.

    // A changed Nearby role/mode at the same sequence revokes held commits.
    Harness roles; roles.choose(); roles.openHome(HomePanel::Nearby); nearby::View roleView;
    roleView.stage=nearby::Stage::Playing; roleView.host=false;
    CHECK(nearby::begin({17,18,1},{1,11,1},nearby::Mode::Tactical,449,roleView.match));
    roles.model.nearby=&roleView; roles.sync(); const auto roleBefore=roles.state;
    roles.dispatch(roles.swipe(250,278,160,278)); // Brace -> Counter.
    auto counter=roles.swipe(206,300,206,190); CHECK(counter.kind==IntentKind::NearbyChoose && counter.value==static_cast<unsigned>(nearby::Choice::Counter)); roles.dispatch(counter);
    roles.dispatch(roles.swipe(250,278,160,278));
    auto ward=roles.swipe(206,300,206,190); CHECK(ward.value==static_cast<unsigned>(nearby::Choice::Ward)); roles.dispatch(ward);
    CHECK(!roles.event(TouchKind::Down,206,300)); roleView.host=true; roles.sync();
    CHECK(!roles.event(TouchKind::Up,206,190,100));
    CHECK(!roles.event(TouchKind::Down,206,300)); roleView.match.mode=nearby::Mode::Auto; roles.sync();
    CHECK(!roles.event(TouchKind::Up,206,190,100));
    CHECK(std::memcmp(&roleBefore,&roles.state,sizeof(State))==0);

    // Screen idle emits only a bounded setting proposal; failed persistence
    // keeps the old model value and repeated releases cannot cycle it twice.
    Harness sleep; sleep.choose(); sleep.openHome(HomePanel::Settings);
    const auto sleepState=sleep.state;
    auto idle=sleep.tap(206,138); CHECK(idle.kind==IntentKind::SleepTimeout && idle.value==120);
    CHECK(sleep.model.sleepTimeoutSeconds==60 && sleep.ui.pending());
    CHECK(!sleep.tap(206,138)); sleep.dispatch(idle,false); CHECK(sleep.model.sleepTimeoutSeconds==60);
    for(const auto seconds:{120u,300u,0u,30u,60u}) {
        idle=sleep.tap(206,138); CHECK(idle.kind==IntentKind::SleepTimeout && idle.value==seconds);
        sleep.dispatch(idle); CHECK(sleep.model.sleepTimeoutSeconds==seconds);
    }
    CHECK(std::memcmp(&sleepState,&sleep.state,sizeof(State))==0 && sleep.gameWrites==1);

    // Appending three saved offers preserves a live fixed choice; swipes and
    // Back retain the actual form identity for slots9..11 without hatch writes.
    Harness offers; offers.dispatch(offers.tap(206,285)); offers.dispatch(offers.swipe(160,180,250,180));
    CHECK(offers.model.selectedId==8);
    CHECK(apply(offers.state,Action::StarterOfferSeed,0x12345678)==Error::None); offers.sync();
    CHECK(offers.model.selectedId==8 && offers.model.starterCount==11);
    for(unsigned slot=9;slot<=11;++slot) {
        offers.dispatch(offers.swipe(250,180,160,180)); CHECK(offers.model.selectedId==slot);
        CHECK(offers.ui.artRequest(offers.state,offers.model,1000).formId==starterForm(offers.state,slot));
    }
    offers.dispatch(offers.tap(206,312)); offers.dispatch(offers.tap(206,365));
    CHECK(offers.model.selectedId==11 && offers.state.phase==Phase::Egg && offers.gameWrites==0);
    CHECK(!offers.event(TouchKind::Down,206,312)); offers.model.starterFormId=starterForm(offers.state,10); offers.ui.update(offers.state,offers.model);
    CHECK(!offers.event(TouchKind::Up,206,312)); // Changed viewed art invalidates held confirm.

    // Capture presentation only reads the committed record. It has no chance
    // billboard before the throw; no outcome leaks through the wiggles.
    Harness cinema; cinema.choose(); CHECK(apply(cinema.state,Action::Explore,100)==Error::None);
    cinema.state.wildHp=cinema.state.wildMaxHp/2; cinema.sync();
    CHECK(apply(cinema.state,Action::Flick,0)==Error::None); cinema.sync();
    CHECK(cinema.state.lastCapture.result==CaptureResult::Miss && cinema.state.lastCapture.chance==0);
    const auto committedCapture=cinema.state; battlepresentation::View capture;
    capture.locked=true;capture.capturePresentation=true;capture.captureAttempt=1;capture.captureRemaining=2;
    capture.enemyFormId=cinema.state.lastCapture.targetFormId;capture.playerFormId=activeMember(cinema.state)->formId;
    cinema.model.battle=&capture;cinema.sync();
    for(const auto elapsed:{0u,499u,500u,650u,1250u,1850u,2299u,2300u,3899u}) {
        capture.captureElapsedMs=elapsed;capture.captureMiss=false;capture.captureCaught=false;capture.captureChance=elapsed<500 ? 0 : 50;
        CHECK(cinema.ui.render(cinema.state,cinema.model,stepScreen.data(),kPixels,1000));
        CHECK(!cinema.swipe(206,300,206,180)); CHECK(!cinema.tap(206,365));
        capture.captureCaught=true;CHECK(cinema.ui.render(cinema.state,cinema.model,framebuffer.data()+1,kPixels,1000));
        if(elapsed<2300)CHECK(std::memcmp(stepScreen.data(),framebuffer.data()+1,kPixels*sizeof(std::uint16_t))==0);
        else CHECK(std::memcmp(stepScreen.data(),framebuffer.data()+1,kPixels*sizeof(std::uint16_t))!=0);
        CHECK(framebuffer.front()==0xBEEF && framebuffer.back()==0xBEEF);
    }
    capture.captureElapsedMs=1000;capture.captureMiss=true;capture.captureCaught=false;capture.captureChance=0;
    CHECK(cinema.ui.render(cinema.state,cinema.model,stepScreen.data(),kPixels,1000));capture.captureChance=90;
    CHECK(cinema.ui.render(cinema.state,cinema.model,framebuffer.data()+1,kPixels,1000));
    CHECK(std::memcmp(stepScreen.data(),framebuffer.data()+1,kPixels*sizeof(std::uint16_t))==0); // Miss can never show invented odds.
    CHECK(std::memcmp(&committedCapture,&cinema.state,sizeof(State))==0);

    if(argc==2) {
        // Optional local headless artifact: RGB PPM, original art only.
        Harness screenshot; screenshot.choose();
        CHECK(screenshot.ui.render(screenshot.state,screenshot.model,framebuffer.data()+1,kPixels,800));
        auto* file=std::fopen(argv[1],"wb"); CHECK(file);
        std::fprintf(file,"P6\n412 412\n255\n");
        for(std::size_t i=1;i<=kPixels;++i) {
            const auto color=framebuffer[i]; const unsigned char rgb[]{
                static_cast<unsigned char>(((color>>11)&31)*255/31),
                static_cast<unsigned char>(((color>>5)&63)*255/63),
                static_cast<unsigned char>((color&31)*255/31)};
            if(std::fwrite(rgb,1,3,file)!=3) { std::fclose(file); std::fprintf(stderr,"PPM write failed\n"); return 1; }
        }
        CHECK(std::fclose(file)==0);
    }
    std::printf("PASS physical UI: %u checks, controller=%zu bytes, model=%zu bytes, framebuffer=%zu bytes\n",checks,sizeof(Controller),sizeof(Model),kPixels*sizeof(std::uint16_t));
    return 0;
}
