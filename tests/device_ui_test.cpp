#include "device_ui.hpp"
#include "capture_ring.hpp"
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
    std::uint64_t captureEpoch=0;
    std::uint32_t captureSequence=UINT32_MAX;
    bool captureVisible=false;
    Harness() { model.writable=true; sync(); }
    void sync() { if(state.starterOfferSeed) { model.starterCount=11; starter.configureChoices(11); model.starterFormId=starterForm(state,starter.selectedId()); } model.starterStage=starter.stage(); model.selectedId=starter.selectedId(); ui.update(state,model);
        if(ui.screen()==Screen::Capture) {
            if(!captureVisible || captureSequence!=state.sequence) captureEpoch=now;
            std::array<std::uint16_t,kPixels> frame;
            CHECK(ui.render(state,model,frame.data(),kPixels,now)); // First visible frame starts Auto entry/retry.
        }
        captureVisible=ui.screen()==Screen::Capture;captureSequence=state.sequence;
    }
    Intent event(TouchKind kind,int x,int y,std::uint64_t delta=30) {
        now+=delta; return ui.touch(state,model,{kind,static_cast<std::int16_t>(x),static_cast<std::int16_t>(y),now});
    }
    Intent tap(int x,int y) {
        const auto down=event(TouchKind::Down,x,y);
        const auto up=event(TouchKind::Up,x,y,60);
        CHECK(!down || (down.kind==IntentKind::GameAction && down.action==Action::RingCapture && !up));
        return down ? down : up;
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
        case IntentKind::ToggleMusic: model.musicEnabled=!model.musicEnabled; break;
        case IntentKind::Volume: model.volumePercent=static_cast<std::uint8_t>(intent.value); break;
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
    void browseMember(std::uint32_t id) {
        CHECK(ui.screen()==Screen::Collection && findMember(state,id));
        for(unsigned i=0;i<state.collectionCount && ui.selectedMemberId()!=id;++i) dispatch(tap(355,180));
        CHECK(ui.selectedMemberId()==id);
    }
    void encounter() {
        CHECK(ui.screen()==Screen::Home && ui.walkingEligible() && ui.interactionIdle());
        // Test-only injection is deliberately outside the release UI.
        CHECK(apply(state,Action::Walk,100)==Error::None); sync(); CHECK(ui.screen()==Screen::Encounter);
        dispatch(tap(206,274)); CHECK(ui.screen()==Screen::Battle);
    }
};

void nextCapturePhase(Harness& h,bool hit) {
    const auto target=capturering::sample(0,h.state.wildFormId).targetRadius;
    const std::uint64_t phase=hit ? (100-target)*capturering::kCycleMs/80 : 0;
    h.now=h.captureEpoch+((h.now-h.captureEpoch)/capturering::kCycleMs+1)*capturering::kCycleMs+phase-30;
}
Intent timedThrow(Harness& h,bool hit,int x=206,int y=306) {
    nextCapturePhase(h,hit);return h.tap(x,y);
}
void meetRoute(State& state, std::uint32_t memberId, const forms::EvolutionEdge* edge) {
    CHECK(edge);
    const auto need = forms::evolutionNeed(*edge);
    auto* member = const_cast<CreatureMember*>(findMember(state, memberId));
    CHECK(member);
    member->level = need.level;
    member->xp = xpForLevel(need.level);
    member->bond = need.bond;
    member->careState = (member->careState & ~0x7fu) | need.care;
    member->hp = combat::formProfile(member->formId, need.level).stats.maxHp;
    if (memberId == state.activeCreatureId) {
        state.level = member->level;
        state.bond = member->bond;
        state.hp = member->hp;
    }
    CHECK(isValid(state));
}

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
            const auto down=event(h,TouchKind::Down,native);
            const auto up=event(h,TouchKind::Up,native,60);
            CHECK(!down || (down.action==Action::RingCapture && !up));return down ? down : up;
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
        CHECK(!event(capture,TouchKind::Up,c.orb));
        nextCapturePhase(capture,true);const auto flick=tap(capture,c.orb);
        CHECK(flick.kind==IntentKind::GameAction && flick.action==Action::RingCapture && capturering::sample(flick.value,capture.state.wildFormId).grade==capturering::Grade::Green);
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
    Harness evolution; evolution.state=stableMemberFixture(67);
    const auto* edge=forms::outgoing(67,0); CHECK(edge);
    meetRoute(evolution.state, 19, edge); evolution.sync();
    evolution.openHome(HomePanel::Partners); evolution.browseMember(evolution.state.activeCreatureId);
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
    release.openHome(HomePanel::Partners); release.browseMember(1); release.dispatch(release.tap(120,312));
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
    const auto flick=fight.event(TouchKind::Down,206,300);CHECK(flick.kind==IntentKind::GameAction && flick.action==Action::RingCapture);
    checkpoint(fight,Action::AccrueSteps,1);
    CHECK(fight.ui.screen()==Screen::Capture && !fight.ui.interactionIdle());
    CHECK(!fight.event(TouchKind::Move,208,302,50));
    CHECK(!fight.event(TouchKind::Up,206,300,50));
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
    partners.browseMember(partners.state.activeCreatureId);CHECK(partners.ui.artRequest(partners.state,partners.model,0).formId==18);
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
    // Capture uses its own direct Down; side targets no longer browse skills.
    battle.state.wildHp=battle.state.wildMaxHp/2;battle.sync();battle.dispatch(battle.tap(280,306));
    CHECK(battle.ui.screen()==Screen::Capture && !battle.tap(206,60));
    CHECK(!battle.event(TouchKind::Up,206,300));const auto flick=timedThrow(battle,true);CHECK(flick.action==Action::RingCapture);
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

void nearbyModeConsent() {
    Harness h;h.choose();CHECK(apply(h.state,Action::Mode,1)==Error::None);h.sync();
    const auto saved=h.state;const auto writes=h.gameWrites;
    nearby::View view;view.stage=nearby::Stage::Discovering;view.peerCount=1;
    view.peers[0].mac={{2,1,2,3,4,5}};view.peers[0].fighter={21,18,2};view.peers[0].openNonce=77;view.peers[0].available=true;
    const auto* member=activeMember(h.state);const auto care=memberCare(*member);
    h.model.nearbyLocalFighter={member->id,member->formId,member->level,care.offense,care.protection};
    h.model.nearby=&view;h.sync();h.openHome(HomePanel::Nearby);h.dispatch(h.tap(120,312));
    CHECK(h.ui.screen()==Screen::NearbyReview);
    auto invite=h.tap(206,302);CHECK(invite.kind==IntentKind::NearbyChallenge && invite.nearbyMode==nearby::Mode::Tactical);
    CHECK(invite.nearbyOpenNonce==77 && !std::memcmp(invite.peer.bytes,view.peers[0].mac.bytes,6));
    CHECK(nearby::sameFighter(invite.nearbyFighters[0],h.model.nearbyLocalFighter));
    CHECK(nearby::sameFighter(invite.nearbyFighters[1],view.peers[0].fighter));h.dispatch(invite);
    h.dispatch(h.tap(120,312));
    // Independent CCW90 raw point selects Auto; release does not also invite.
    std::array<std::uint16_t,kPixels> tacticalPixels,autoPixels;
    CHECK(h.ui.render(h.state,h.model,tacticalPixels.data(),kPixels,90000));
    const auto autoPoint=display::panelToLogical(display::Orientation::Ccw90,{240,131});
    auto choice=h.tap(autoPoint.x,autoPoint.y);CHECK(choice.kind==IntentKind::Navigation);h.dispatch(choice);
    CHECK(h.ui.render(h.state,h.model,autoPixels.data(),kPixels,90000));
    CHECK(tacticalPixels[218*kSize+62]!=tacticalPixels[218*kSize+210]);
    CHECK(tacticalPixels[218*kSize+62]==autoPixels[218*kSize+210]);
    CHECK(tacticalPixels[218*kSize+210]==autoPixels[218*kSize+62]);
    CHECK(!h.event(TouchKind::Up,206,302));
    h.dispatch(h.tap(206,365));h.dispatch(h.tap(120,312)); // Retain mode within this live session.
    invite=h.tap(206,302);CHECK(invite.kind==IntentKind::NearbyChallenge && invite.nearbyMode==nearby::Mode::Auto);h.dispatch(invite);
    h.dispatch(h.tap(120,312));CHECK(!h.event(TouchKind::Down,206,302));
    ++view.peers[0].openNonce;h.sync();CHECK(h.ui.screen()==Screen::Nearby && !h.event(TouchKind::Up,206,302));
    // A replacement invitation cannot inherit a held acceptance, even if its
    // stage and session are unchanged while the mode or either fighter changes.
    view.stage=nearby::Stage::Incoming;view.session=42;view.opponent=view.peers[0].mac;
    view.offered[0]=view.peers[0].fighter;view.offered[1]=h.model.nearbyLocalFighter;view.offeredMode=nearby::Mode::Auto;
    const auto offer=view;
    for(unsigned change=0;change<5;++change) {
        view=offer;h.sync();CHECK(!h.event(TouchKind::Down,120,252));
        if(change==0)view.offeredMode=nearby::Mode::Tactical;
        if(change==1)++view.session;
        if(change==2)++view.opponent.bytes[5];
        if(change==3)++view.offered[0].level;
        if(change==4)++view.offered[1].protectionBonus;
        h.sync();CHECK(!h.event(TouchKind::Up,120,252));
    }
    view=offer;h.sync();auto accept=h.tap(120,252);CHECK(accept.kind==IntentKind::NearbyAccept && accept.nearbyMode==nearby::Mode::Auto);
    CHECK(accept.nearbySession==42 && !std::memcmp(accept.peer.bytes,view.opponent.bytes,6));
    CHECK(nearby::sameFighter(accept.nearbyFighters[0],view.offered[0]) && nearby::sameFighter(accept.nearbyFighters[1],view.offered[1]));h.dispatch(accept);
    CHECK(nearby::begin(view.offered[0],view.offered[1],nearby::Mode::Auto,123,view.match));view.stage=nearby::Stage::Playing;h.sync();
    CHECK(!h.tap(280,240) && !h.tap(280,306) && !h.swipe(206,300,206,180)); // No mode edits, attack input or PvP capture.
    CHECK(h.gameWrites==writes && !std::memcmp(&saved,&h.state,sizeof(State)));
}

void captureTimingControls() {
    auto ready=[](Harness& h) {
        h.choose();CHECK(apply(h.state,Action::Explore,100)==Error::None);h.sync();h.dispatch(h.tap(206,274));
        h.state.wildHp=h.state.wildMaxHp/2;h.sync();h.dispatch(h.tap(280,306));
        CHECK(h.ui.screen()==Screen::Capture && isValid(h.state));
    };
    // Main play-area Down commits the sampled intent immediately. Frame polling
    // and subsequent movement/release never change that accepted proposal.
    for(bool hit:{false,true}) for(const auto point:std::array<std::array<int,2>,6>{{{206,176},{403,206},{95,200},{206,330},{206,80},{206,306}}}) {
        Harness sparse,dense;ready(sparse);ready(dense);const auto before=sparse.state;
        nextCapturePhase(sparse,hit);dense.now=sparse.now;
        std::array<std::uint16_t,kPixels> frame;
        for(unsigned i=0;i<35;++i)CHECK(dense.ui.render(dense.state,dense.model,frame.data(),kPixels,dense.now-350+i*10));
        const auto a=sparse.event(TouchKind::Down,point[0],point[1]),b=dense.event(TouchKind::Down,point[0],point[1]);
        CHECK(a.kind==IntentKind::GameAction && a.action==Action::RingCapture && a.value==capturering::sample(sparse.now-sparse.captureEpoch,sparse.state.wildFormId).phaseMs);
        CHECK(a.value==b.value && sparse.ui.pending() && dense.ui.pending());
        CHECK(!sparse.event(TouchKind::Move,411,411) && !sparse.event(TouchKind::Cancel,206,176));
        CHECK(!sparse.event(TouchKind::Down,point[0],point[1]));
        CHECK(trade::sameState(sparse.state,before));
        sparse.dispatch(a,false);CHECK(trade::sameState(sparse.state,before));
        sparse.now+=600;CHECK(!sparse.event(TouchKind::Down,206,176)); // Failed save cannot re-arm held contact.
        sparse.model.writable=false;sparse.sync();CHECK(!sparse.event(TouchKind::Up,411,411)); // Release survives gates.
        sparse.model.writable=true;sparse.sync();nextCapturePhase(sparse,hit);
        CHECK(sparse.event(TouchKind::Down,206,176).action==Action::RingCapture);
        dense.dispatch(b);CHECK(dense.state.sequence==before.sequence+1);
        CHECK(!dense.event(TouchKind::Down,206,176) && !dense.event(TouchKind::Up,206,365));
        CHECK(dense.state.lastCapture.chance==ringCaptureChance(before,a.value) && dense.state.rngState!=before.rngState);
        CHECK(dense.state.lastCapture.result!=CaptureResult::Miss);
    }
    // Exact timing boundaries reach the core unchanged, including orange.
    // The same helper used for the visible percentage matches each saved roll.
    for(int offset:{-25,-24,-13,-12,0,12,13,24,25}) {
        Harness h;ready(h);const auto before=h.state;
        const int radius=capturering::sample(0,h.state.wildFormId).targetRadius+offset;
        if(radius<21 || radius>100)continue;
        const auto phase=static_cast<std::uint32_t>((100-radius)*30);
        h.now=h.captureEpoch+capturering::kCycleMs+phase-30;
        const auto expected=std::abs(offset)<=12 ? capturering::Grade::Green :
            std::abs(offset)<=24 ? capturering::Grade::Orange : capturering::Grade::Red;
        const auto proposal=h.event(TouchKind::Down,95,200);
        CHECK(proposal.action==Action::RingCapture&&proposal.value==phase);
        CHECK(capturering::sample(proposal.value,h.state.wildFormId).grade==expected);
        const auto displayed=ringCaptureChance(before,phase);
        h.dispatch(proposal);
        CHECK(h.state.lastCapture.chance==displayed&&displayed>0&&displayed<=captureChance(before));
        CHECK(h.state.lastCapture.result!=CaptureResult::Miss&&h.state.lastCapture.attempt==1);
    }
    // Header, lower gap, circle exterior and navigation never propose a throw.
    for(const auto point:std::array<std::array<int,2>,4>{{{206,79},{206,331},{0,0},{411,411}}}) {
        Harness h;ready(h);CHECK(!h.event(TouchKind::Down,point[0],point[1]));CHECK(!h.event(TouchKind::Up,206,176));
    }
    Harness back;ready(back);const auto saved=back.state;CHECK(!back.event(TouchKind::Down,206,365));
    const auto nav=back.event(TouchKind::Up,206,365);CHECK(nav.kind==IntentKind::Navigation && trade::sameState(back.state,saved));
    // Bottom navigation has a pressed visual outside the partial arena. It
    // requires a full frame until canceled/released, with no accidental throw.
    for(bool automatic:{false,true}) {
        Harness visual;ready(visual);
        if(automatic){visual.state.battleMode=BattleMode::Auto;visual.state.autoCapture=AutoCapture::Awaiting;visual.state.wildTurn=1;visual.sync();CHECK(isValid(visual.state));}
        const auto saved=visual.state;std::array<std::uint16_t,kPixels> idle,pressed,released;
        CHECK(visual.ui.render(visual.state,visual.model,idle.data(),kPixels,visual.now));
        CHECK(!visual.event(TouchKind::Down,206,365));CHECK(!visual.ui.captureAnimating(visual.state,visual.model));
        CHECK(!visual.ui.renderCaptureRegion(visual.state,visual.model,pressed.data(),kPixels,visual.now));
        CHECK(visual.ui.render(visual.state,visual.model,pressed.data(),kPixels,visual.now));
        CHECK(!std::equal(idle.begin()+348*kSize,idle.begin()+386*kSize,pressed.begin()+348*kSize));
        CHECK(!visual.event(TouchKind::Cancel,206,365));CHECK(visual.ui.captureAnimating(visual.state,visual.model));
        CHECK(visual.ui.render(visual.state,visual.model,released.data(),kPixels,visual.now));
        CHECK(std::equal(idle.begin()+348*kSize,idle.begin()+386*kSize,released.begin()+348*kSize));
        CHECK(!visual.event(TouchKind::Up,206,365));CHECK(trade::sameState(saved,visual.state));
        CHECK(!visual.event(TouchKind::Down,206,365));CHECK(!visual.ui.captureAnimating(visual.state,visual.model));
        const auto navigation=visual.event(TouchKind::Up,206,365);
        CHECK(automatic ? navigation.kind==IntentKind::GameAction&&navigation.action==Action::AutoResume : navigation.kind==IntentKind::Navigation);
        CHECK(!visual.ui.captureAnimating(visual.state,visual.model));CHECK(trade::sameState(saved,visual.state));
    }
    // Guard before submission; an accepted Down is deliberately final.
    for(unsigned fault=0;fault<5;++fault) {
        Harness h;ready(h);const auto before=h.state;
        if(fault==0)h.model.writable=false;
        if(fault==1)h.model.inputEnabled=false;
        if(fault==2)h.model.encounterRecoveryRequired=true;
        if(fault==3)h.state.wildHp=h.state.wildMaxHp;
        if(fault==4)h.now-=100;
        CHECK(!h.event(TouchKind::Down,206,176));CHECK(!h.event(TouchKind::Up,206,176));CHECK(h.gameWrites==1);
        if(fault!=3)CHECK(trade::sameState(before,h.state));
    }
    Harness debounce;ready(debounce);auto first=debounce.event(TouchKind::Down,206,176);CHECK(first.action==Action::RingCapture);
    debounce.dispatch(first,false);CHECK(!debounce.event(TouchKind::Up,206,176));
    CHECK(!debounce.event(TouchKind::Down,206,176,100));CHECK(!debounce.event(TouchKind::Up,206,176));
    debounce.now+=450;CHECK(debounce.event(TouchKind::Down,206,176).action==Action::RingCapture);
    // A verified hardware release after read failure/pause re-arms directly;
    // mere cancellation cannot clear an uncertain held contact.
    Harness released;ready(released);const auto proposed=released.event(TouchKind::Down,206,176);
    CHECK(proposed.action==Action::RingCapture);released.dispatch(proposed,false);released.ui.cancelTouch();
    released.now+=600;CHECK(!released.event(TouchKind::Down,206,176));
    released.ui.acknowledgeContactReleased();CHECK(released.ui.interactionIdle());
    CHECK(released.event(TouchKind::Down,206,176).action==Action::RingCapture);
    // Leaving and re-entering starts large; time is independent of frame rate.
    Harness epoch;ready(epoch);std::array<std::uint16_t,kPixels> initial,later;
    CHECK(epoch.ui.render(epoch.state,epoch.model,initial.data(),kPixels,epoch.now));
    epoch.now+=1000;CHECK(epoch.ui.render(epoch.state,epoch.model,later.data(),kPixels,epoch.now));CHECK(initial!=later);
    epoch.dispatch(epoch.tap(206,365));epoch.dispatch(epoch.tap(280,306));
    CHECK(epoch.ui.render(epoch.state,epoch.model,later.data(),kPixels,epoch.now));CHECK(initial==later);
    // Partial redraw is pixel-identical to a full frame, including moving rings,
    // changing sprite frames, real-shaped background storage and clipping.
    Harness region;ready(region);std::array<std::uint16_t,kPixels> background,full;
    std::array<std::uint16_t,kPixels+2> partial;partial.fill(0xbeef);
    std::array<std::uint16_t,32*32> spritePixels;std::array<std::uint8_t,128> spriteMask;spriteMask.fill(255);
    for(std::size_t i=0;i<background.size();++i)background[i]=static_cast<std::uint16_t>(i*17);
    region.model.artwork.background=background.data();region.model.artwork.backgroundPixels=kPixels;
    region.model.artwork.backgroundId=region.ui.artRequest(region.state,region.model,region.now).sceneId;
    auto& frame=region.model.artwork.sprite;frame.formId=region.state.wildFormId;frame.width=frame.height=32;
    frame.pixels=spritePixels.data();frame.pixelCount=spritePixels.size();frame.mask=spriteMask.data();frame.maskBytes=spriteMask.size();
    CHECK(region.ui.captureAnimating(region.state,region.model));
    spritePixels.fill(0x9876);CHECK(region.ui.render(region.state,region.model,partial.data()+1,kPixels,region.now));
    unsigned gradesSeen=0;
    for(unsigned phase=0;phase<=2400;phase+=37){
        spritePixels.fill(static_cast<std::uint16_t>(0x1234+phase));
        CHECK(region.ui.render(region.state,region.model,full.data(),kPixels,region.now+phase));
        CHECK(region.ui.renderCaptureRegion(region.state,region.model,partial.data()+1,kPixels,region.now+phase));
        CHECK(std::equal(full.begin(),full.end(),partial.begin()+1));CHECK(partial.front()==0xbeef&&partial.back()==0xbeef);
        const auto ring=capturering::sample(phase,region.state.wildFormId);
        gradesSeen|=1u<<static_cast<unsigned>(ring.grade);
        const auto rgb=[](int r,int g,int b){return static_cast<std::uint16_t>(((r>>3)<<11)|((g>>2)<<5)|(b>>3));};
        const auto color=ring.grade==capturering::Grade::Green ? rgb(84,234,134) :
            ring.grade==capturering::Grade::Orange ? rgb(255,164,60) : rgb(246,83,74);
        const int radius=(ring.radiusQ8+128)/256;
        CHECK(full[(176-radius)*kSize+206]==color); // Moving ring above every sprite frame.

    }
    CHECK(gradesSeen==7); // Red, orange and green dynamic labels all stay in the208px region.
    partial.fill(0xbeef);const auto finalTime=region.now+2400;
    CHECK(region.ui.render(region.state,region.model,full.data(),kPixels,finalTime));
    CHECK(region.ui.renderCaptureRegion(region.state,region.model,partial.data()+1,kPixels,finalTime));
    bool untouched=true,identical=true;
    for(int y=0;y<kSize;++y)for(int x=0;x<kSize;++x) {
        const bool inside=x>=Controller::kCaptureX&&x<Controller::kCaptureX+Controller::kCaptureWidth&&
            y>=Controller::kCaptureY&&y<Controller::kCaptureY+Controller::kCaptureHeight;
        if(inside)identical&=partial[1+y*kSize+x]==full[y*kSize+x];
        else untouched&=partial[1+y*kSize+x]==0xbeef;
    }
    CHECK(untouched&&identical&&partial.front()==0xbeef&&partial.back()==0xbeef);
    CHECK(!region.ui.renderCaptureRegion(region.state,region.model,nullptr,kPixels,region.now));
    CHECK(!region.ui.renderCaptureRegion(region.state,region.model,partial.data()+1,kPixels-1,region.now));
    region.model.inputEnabled=false;region.sync();CHECK(!region.ui.captureAnimating(region.state,region.model));
    CHECK(!region.ui.renderCaptureRegion(region.state,region.model,partial.data()+1,kPixels,region.now));
    // The target is one75% translucent field over the background, drawn below
    // exact-form sprite pixels. It remains visible away from the moving ring.
    Harness shade;ready(shade);std::array<std::uint16_t,kPixels> shaded;
    background.fill(0x3186);spritePixels.fill(0xf81f);spriteMask.fill(0);
    for(unsigned y=15;y<17;++y)for(unsigned x=15;x<17;++x){const auto i=y*32+x;spriteMask[i/8]|=1u<<(i%8);}
    shade.model.artwork.background=background.data();shade.model.artwork.backgroundPixels=kPixels;
    shade.model.artwork.backgroundId=shade.ui.artRequest(shade.state,shade.model,shade.now).sceneId;
    shade.model.artwork.sprite=frame;shade.model.artwork.sprite.formId=shade.state.wildFormId;
    CHECK(shade.ui.render(shade.state,shade.model,shaded.data(),kPixels,shade.now));
    const auto target=capturering::sample(0,shade.state.wildFormId).targetRadius;
    constexpr std::uint16_t green=((84>>3)<<11)|((234>>2)<<5)|(134>>3);
    constexpr auto blend=static_cast<std::uint16_t>(((((green>>11)*3+(0x3186>>11))/4)<<11)|
        (((((green>>5)&63)*3+((0x3186>>5)&63))/4)<<5)|(((green&31)*3+(0x3186&31))/4));
    CHECK(shaded[176*kSize+206+target]==blend);
    CHECK(shaded[176*kSize+206+target-13]==0x3186);
    CHECK(shaded[176*kSize+206+target+13]==0x3186);
    CHECK(shaded[176*kSize+206]==0xf81f); // Creature stays opaque and untinted.
    // Synthetic wide/tall/full original masks fit the176px arena without
    // touching title, throw button, footer or circular screen boundary.
    Harness art;ready(art);std::array<std::uint16_t,32*32> pixels;pixels.fill(0xf81f);
    std::array<std::uint8_t,32*32/8> mask{};std::array<std::uint16_t,kPixels+2> output;
    for(unsigned shape=0;shape<3;++shape) {
        const unsigned w=shape==0?32:shape==1?8:24,h=shape==0?8:shape==1?32:24;
        mask.fill(0);for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const auto i=y*32+x;mask[i/8]|=1u<<(i%8);}
        art.model.artwork.sprite={};auto& sprite=art.model.artwork.sprite;sprite.formId=art.state.wildFormId;
        sprite.pixels=pixels.data();sprite.pixelCount=pixels.size();sprite.mask=mask.data();sprite.maskBytes=mask.size();
        sprite.width=sprite.height=32;sprite.contentWidth=w;sprite.contentHeight=h;
        output.fill(0xbeef);CHECK(art.ui.render(art.state,art.model,output.data()+1,kPixels,90000));
        CHECK(output.front()==0xbeef&&output.back()==0xbeef);unsigned visible=0;
        for(int y=0;y<kSize;++y)for(int x=0;x<kSize;++x){const auto p=output[1+y*kSize+x];
            if(p==0xf81f){++visible;CHECK(x>=118&&x<294&&y>=88&&y<264);}
            if(!Controller::inside(x,y))CHECK(p==0);
        }
        CHECK(visible>1000);
    }
}

void fullRosterCaptureControls() {
    auto fill=[](Harness& h) {
        // A full set of distinct owned IDs, including duplicate species. This
        // test-only fixture changes capacity while an input may be held.
        h.state.sequence+=kCollectionCapacity;
        h.state.foregroundSequence=h.state.sequence;
        h.state.collectionCount=kCollectionCapacity;
        h.state.nextMemberId=kCollectionCapacity+1;
        h.state.captures=kCollectionCapacity-1;
        h.state.encounters+=kCollectionCapacity-1;
        h.state.steps+=100*(kCollectionCapacity-1);
        for(unsigned i=1;i<kCollectionCapacity;++i) {
            h.state.collection[i]=h.state.collection[0];
            h.state.collection[i].id=i+1;
            h.state.collection[i].capturedAtSequence=i;
        }
        CHECK(isValid(h.state));
    };
    // Full before entry, full while CATCH is held, and a capture screen made
    // stale by a restored/synced save all reject without proposing a write.
    for(unsigned context=0;context<3;++context) {
        Harness h;h.choose();h.encounter();
        h.state.wildHp=h.state.wildMaxHp/2;h.sync();
        if(context==1) CHECK(!h.event(TouchKind::Down,280,306));
        if(context==2) {h.dispatch(h.tap(280,306));CHECK(h.ui.screen()==Screen::Capture);}
        fill(h);const auto saved=h.state;const auto writes=h.gameWrites;
        if(context==1) CHECK(!h.event(TouchKind::Up,280,306));
        else if(context==2) CHECK(!h.event(TouchKind::Down,206,176));
        else h.sync();
        CHECK(h.ui.screen()==Screen::Battle && !h.ui.pending());
        CHECK(!h.event(TouchKind::Up,206,176));
        CHECK(!h.tap(280,306) && !h.ui.captureAnimating(h.state,h.model));
        CHECK(h.gameWrites==writes && trade::sameState(saved,h.state));
        CHECK(!h.state.captureAttempts && !captureChance(h.state));
        std::array<std::uint16_t,kPixels+2> first,later;first.fill(0xbeef);later.fill(0xbeef);
        CHECK(h.ui.render(h.state,h.model,first.data()+1,kPixels,h.now));
        CHECK(h.ui.render(h.state,h.model,later.data()+1,kPixels,h.now+10000));
        CHECK(first.front()==0xbeef && first.back()==0xbeef && later.front()==0xbeef && later.back()==0xbeef);
        // Capacity guidance stays visible after the usual gesture hint fades.
        CHECK(std::equal(first.begin()+1+61*kSize,first.begin()+1+76*kSize,later.begin()+1+61*kSize));
        CHECK(std::equal(first.begin()+1+333*kSize,first.begin()+1+348*kSize,later.begin()+1+333*kSize));
        CHECK(h.gameWrites==writes && trade::sameState(saved,h.state));
    }
    // Starting Auto with a full roster finishes its fight without pausing for
    // an unavailable capture, replacing anyone, or consuming a capture roll.
    Harness automatic;automatic.choose();fill(automatic);
    CHECK(apply(automatic.state,Action::Mode,1)==Error::None);automatic.sync();automatic.encounter();
    if(automatic.state.wildFormId==automatic.state.collection[0].formId) {
        const auto form=automatic.state.wildFormId==forms::kFirstProductionFormId?forms::kFirstProductionFormId+1:forms::kFirstProductionFormId;
        automatic.state.wildFormId=form;
        automatic.state.wildSpecies=static_cast<Species>(forms::find(form)->lineage);
        automatic.state.wildMaxHp=automatic.state.wildHp=combat::formProfile(form,automatic.state.wildLevel).stats.maxHp;
        CHECK(isValid(automatic.state)); automatic.sync();
    }
    const auto saved=automatic.state;
    const auto runAway=automatic.tap(206,275);
    CHECK(runAway.kind==IntentKind::GameAction && runAway.action==Action::Retreat);
    automatic.ui.resolve(); automatic.sync();
    CHECK(autoFightReady(automatic.state,automatic.model));
    automatic.dispatch(Intent{IntentKind::GameAction,Action::AutoFight,0});
    CHECK(automatic.ui.screen()==Screen::Result && automatic.state.phase==Phase::Home);
    CHECK(automatic.state.autoCapture==AutoCapture::None && automatic.state.collectionCount==kCollectionCapacity);
    CHECK(automatic.state.captures==saved.captures && automatic.state.nextMemberId==saved.nextMemberId);
    CHECK(automatic.state.lastCapture.result==CaptureResult::None);
    for(unsigned i=0;i<kCollectionCapacity;++i) CHECK(automatic.state.collection[i].id==saved.collection[i].id);
}

void fullRosterNavigation() {
    Harness h;h.choose();
    // Released history makes owned IDs differ from their slot indices. Each
    // visible member has its own form to detect stale artwork requests.
    constexpr unsigned released=100;
    h.state.sequence=h.state.foregroundSequence=kCollectionCapacity+released+20;
    h.state.collectionCount=kCollectionCapacity;
    h.state.captures=h.state.encounters=kCollectionCapacity-1+released;
    h.state.steps=h.state.encounters*100;
    h.state.nextMemberId=kCollectionCapacity+released+1;
    for(unsigned i=1;i<kCollectionCapacity;++i) {
        const auto formId=forms::kFirstProductionFormId+(i-1)%forms::kProductionFormCount;
        h.state.collection[i]=stableMemberFixture(formId).collection[1];
        h.state.collection[i].id=h.state.collection[i].capturedAtSequence=i+released+1;
        h.state.journal[(formId-1)/32]|=1u<<((formId-1)%32);
    }
    CHECK(isValid(h.state));h.sync();h.openHome(HomePanel::Partners);
    const auto saved=h.state;const auto writes=h.gameWrites;
    for(unsigned i=0;i<kCollectionCapacity;++i) {
        CHECK(h.ui.screen()==Screen::Collection && !h.ui.pending());
        const auto* expected=collectionMemberAtDisplayIndex(h.state,i);CHECK(expected);
        CHECK(h.ui.selectedMemberId()==expected->id && h.ui.artRequest(h.state,h.model,h.now).formId==expected->formId);
        if(i+1<kCollectionCapacity) h.dispatch(h.tap(355,180));
    }
    CHECK(trade::sameState(saved,h.state) && h.gameWrites==writes);
    const auto& last=*collectionMemberAtDisplayIndex(h.state,kCollectionCapacity-1);
    const auto lastId=last.id,lastForm=last.formId;
    CHECK(lastId>kCollectionCapacity);
    h.dispatch(h.tap(355,180));
    CHECK(h.ui.artRequest(h.state,h.model,h.now).formId==h.state.collection[0].formId);
    h.dispatch(h.tap(55,180));
    CHECK(h.ui.artRequest(h.state,h.model,h.now).formId==lastForm);
    auto select=h.tap(280,312);CHECK(select.action==Action::Select && select.value==lastId);
    h.dispatch(select,false);CHECK(trade::sameState(saved,h.state));
    select=h.tap(280,312);CHECK(select.action==Action::Select && select.value==lastId);h.dispatch(select);
    CHECK(h.state.activeCreatureId==lastId && h.state.collectionCount==kCollectionCapacity);
    CHECK(h.gameWrites==writes+1 && h.ui.artRequest(h.state,h.model,h.now).formId==lastForm);
}

void xpCompanionControls() {
    Harness h;h.choose();
    h.state.sequence=h.state.foregroundSequence=100;
    h.state.collectionCount=6;h.state.captures=h.state.encounters=5;h.state.steps=500;h.state.nextMemberId=7;
    for(unsigned i=1;i<6;++i){
        const auto formId=forms::kFirstProductionFormId+i;
        h.state.collection[i]=stableMemberFixture(formId).collection[1];
        h.state.collection[i].id=i+1;h.state.collection[i].capturedAtSequence=i;
        h.state.journal[(formId-1)/32]|=1u<<((formId-1)%32);
    }
    CHECK(isValid(h.state));h.sync();h.openHome(HomePanel::Partners);
    CHECK(h.ui.selectedMemberId()==1 && !h.tap(206,266)); // Active cannot be an extra companion.
    const auto original=h.state;
    h.browseMember(3);auto add=h.tap(206,266);
    CHECK(add.kind==IntentKind::GameAction && add.action==Action::PartyAdd && add.value==3);
    CHECK(!h.tap(206,266));h.dispatch(add,false);
    CHECK(trade::sameState(h.state,original) && h.ui.selectedMemberId()==3);
    add=h.tap(206,266);h.dispatch(add);
    CHECK(isPartyMember(h.state,3) && partyCount(h.state)==1 && h.ui.selectedMemberId()==3);
    CHECK(collectionMemberAtDisplayIndex(h.state,1)->id==3);
    for(auto id:{5u,2u}){h.browseMember(id);add=h.tap(206,266);CHECK(add.action==Action::PartyAdd && add.value==id);h.dispatch(add);CHECK(h.ui.selectedMemberId()==id);}
    CHECK(partyCount(h.state)==3 && h.state.partyMemberIds[0]==3 && h.state.partyMemberIds[1]==5 && h.state.partyMemberIds[2]==2);
    const unsigned order[]{1,3,5,2,6,4};
    for(unsigned i=0;i<6;++i){
        CHECK(collectionMemberAtDisplayIndex(h.state,i)->id==order[i]);h.browseMember(order[i]);
        CHECK(h.ui.artRequest(h.state,h.model,h.now).formId==findMember(h.state,order[i])->formId);
    }
    h.browseMember(6);const auto full=h.state;const auto writes=h.gameWrites;
    CHECK(!h.tap(206,266) && !h.ui.pending() && trade::sameState(full,h.state) && h.gameWrites==writes);
    h.browseMember(5);auto remove=h.tap(206,266);CHECK(remove.action==Action::PartyRemove && remove.value==5);
    h.dispatch(remove,false);CHECK(trade::sameState(full,h.state) && h.ui.selectedMemberId()==5);
    h.dispatch(h.tap(206,266));CHECK(!isPartyMember(h.state,5) && partyCount(h.state)==2 && h.ui.selectedMemberId()==5);
    CHECK(h.state.partyMemberIds[0]==3 && h.state.partyMemberIds[1]==2 && !h.state.partyMemberIds[2]);
    for(unsigned i=0;i<6;++i)CHECK(h.state.collection[i].id==original.collection[i].id);
    // A model lock, read-only state, live Nearby, or concurrent save revokes a
    // held assignment. None can leak an accepted party command to persistence.
    for(unsigned gate=0;gate<4;++gate){
        CHECK(!h.event(TouchKind::Down,206,266));
        nearby::View wire;
        if(gate==0)h.model.partyEditable=false;
        if(gate==1)h.model.writable=false;
        if(gate==2){wire.stage=nearby::Stage::Playing;h.model.nearby=&wire;}
        if(gate==3)CHECK(apply(h.state,Action::Feed)==Error::None);
        const auto saved=h.state;h.sync();CHECK(!h.event(TouchKind::Up,206,266) && !h.ui.pending());
        CHECK(trade::sameState(saved,h.state));
        h.model.partyEditable=h.model.writable=true;h.model.nearby=nullptr;h.sync();
    }
    // Becoming active removes that member from the extras while the UI keeps
    // showing the same owned ID at its new first position.
    h.browseMember(3);auto select=h.tap(280,312);CHECK(select.action==Action::Select && select.value==3);h.dispatch(select);
    CHECK(h.ui.selectedMemberId()==3 && h.state.activeCreatureId==3 && !isPartyMember(h.state,3));
    CHECK(collectionMemberAtDisplayIndex(h.state,0)->id==3 && !h.tap(206,266));
    // The added controls leave a full opaque sprite clear of the count, name,
    // identity and buttons, even at the maximum gyro tilt in either direction.
    h.ui.notice(nullptr);h.model.gyroEnabled=true;
    std::array<std::uint16_t,1024> pixels;pixels.fill(0xf81f);
    std::array<std::uint8_t,128> mask;mask.fill(0xff);
    auto& art=h.model.artwork.sprite;art.formId=h.ui.artRequest(h.state,h.model,h.now).formId;
    art.pixels=pixels.data();art.pixelCount=pixels.size();art.mask=mask.data();art.maskBytes=mask.size();art.width=art.height=32;
    std::array<std::uint16_t,kPixels> frame;
    for(int tilt:{-8,8}){
        h.model.tiltX=h.model.tiltY=tilt;CHECK(h.ui.render(h.state,h.model,frame.data(),frame.size(),h.now+10000));
        unsigned count=0;
        for(int y=0;y<kSize;++y)for(int x=0;x<kSize;++x)if(frame[y*kSize+x]==0xf81f){++count;CHECK(y>=106&&y<218&&x>=142&&x<270);}
        CHECK(count>5000);
    }
}

void homeStepVisibility() {
    Harness h;h.choose();h.model.lifetimeSteps=123456789;h.model.stepsAvailable=true;h.sync();
    std::array<std::uint16_t,kPixels> baseline,other;
    CHECK(h.ui.render(h.state,h.model,baseline.data(),kPixels,90000));
    for(unsigned status=0;status<3;++status) {
        h.model.stepsAvailable=status==2;h.model.stepsRecovering=status==1;
        h.state.encounterRate=status==2 ? EncounterRate::Off : EncounterRate::Normal;h.sync();
        CHECK(h.ui.render(h.state,h.model,other.data(),kPixels,90000));
        CHECK(std::equal(baseline.begin()+366*kSize,baseline.begin()+380*kSize,other.begin()+366*kSize));
        for(int y=380;y<kSize;++y)for(int x=0;x<kSize;++x)if(!Controller::inside(x,y))CHECK(other[y*kSize+x]==0);
    }
}

void soundSettings() {
    Harness h;h.choose();h.openHome(HomePanel::Settings);h.dispatch(h.tap(120,252));
    CHECK(h.ui.screen()==Screen::Sound && h.model.volumePercent==15 && !h.model.musicEnabled && !h.model.muted);
    const auto saved=h.state;const auto writes=h.gameWrites;
    const unsigned louder[]{30,50,75,100};
    for(auto level:louder) {
        // Independent native CCW90 point for the right edge.
        const auto point=display::panelToLogical(display::Orientation::Ccw90,{180,56});
        auto volume=h.tap(point.x,point.y);CHECK(volume.kind==IntentKind::Volume && volume.value==level && h.ui.pending());
        CHECK(!h.event(TouchKind::Up,355,180) && !h.tap(355,180));h.dispatch(volume);CHECK(h.model.volumePercent==level);
    }
    CHECK(!h.tap(355,180) && !h.swipe(250,180,160,180));
    const unsigned quieter[]{75,50,30,15,5,0};
    for(auto level:quieter) {
        // Independent native CCW90 point for the left edge.
        const auto point=display::panelToLogical(display::Orientation::Ccw90,{180,356});
        auto volume=h.tap(point.x,point.y);CHECK(volume.kind==IntentKind::Volume && volume.value==level);
        h.dispatch(volume);CHECK(h.model.volumePercent==level);
    }
    CHECK(!h.tap(55,180) && !h.swipe(160,180,250,180));
    auto volume=h.swipe(250,180,160,180);CHECK(volume.kind==IntentKind::Volume && volume.value==5);h.dispatch(volume);
    h.model.volumePercent=10;h.sync();volume=h.tap(355,180);CHECK(volume.value==15);h.dispatch(volume);
    h.model.volumePercent=10;h.sync();volume=h.tap(55,180);CHECK(volume.value==5);h.dispatch(volume);
    const auto mutePoint=display::panelToLogical(display::Orientation::Ccw90,{252,291});
    const auto musicPoint=display::panelToLogical(display::Orientation::Ccw90,{252,131});
    auto mute=h.tap(mutePoint.x,mutePoint.y);CHECK(mute.kind==IntentKind::ToggleMute && h.ui.pending());CHECK(!h.tap(280,252));h.dispatch(mute);CHECK(h.model.muted);
    auto music=h.tap(musicPoint.x,musicPoint.y);CHECK(music.kind==IntentKind::ToggleMusic && h.ui.pending());CHECK(!h.event(TouchKind::Up,280,252));h.dispatch(music);CHECK(h.model.musicEnabled && h.model.muted);
    h.dispatch(h.tap(120,252));CHECK(!h.model.muted && h.model.musicEnabled);
    // Sound preferences have their own save boundary; RAM controls stay usable
    // while a persistent warning reports a settings write failure.
    h.model.audioPreferencesWritable=false;h.model.writable=false;h.sync();
    volume=h.tap(355,180);CHECK(volume.kind==IntentKind::Volume);h.dispatch(volume);
    h.dispatch(h.tap(280,252));CHECK(!h.model.musicEnabled);
    std::array<std::uint16_t,kPixels+2> pixels;pixels.fill(0xbeef);
    CHECK(h.ui.render(h.state,h.model,pixels.data()+1,kPixels,90000));CHECK(pixels.front()==0xbeef && pixels.back()==0xbeef);
    CHECK(h.gameWrites==writes && std::memcmp(&saved,&h.state,sizeof(State))==0);
    h.dispatch(h.tap(206,365));CHECK(h.ui.screen()==Screen::Settings);
}

// Synthetic two-production-partner history; stable local IDs never equal slots.
State tradeUiState() {
    auto state=stableMemberFixture(18);auto& first=state.collection[0];
    first.formId=11;first.species=Species::Impmon;first.hp=forms::stats(11,first.level).maxHp;
    state.starterId=1;state.journal[0]|=1u<<10;CHECK(isValid(state));
    CHECK(trade::canOffer(state,1) && trade::canOffer(state,19));return state;
}
void tradingScreens() {
    Harness h;h.choose();h.state=tradeUiState();h.sync();const auto saved=h.state;const auto writes=h.gameWrites;
    nearby::View net;net.stage=nearby::Stage::Discovering;net.peerCount=2;
    for(unsigned i=0;i<2;++i){net.peers[i].mac.bytes[0]=2;net.peers[i].mac.bytes[5]=i+2;net.peers[i].fighter={19,18,1};net.peers[i].available=true;}
    tradewire::View wire;wire.stage=tradewire::Stage::Discovering;wire.peerCount=2;
    // Radio protocol arrays intentionally differ in order: join by exact MAC.
    for(unsigned i=0;i<2;++i){std::memcpy(wire.peers[1-i].identity.bytes,net.peers[i].mac.bytes,6);wire.peers[1-i].compatible=true;wire.peers[1-i].advertisement.available=true;wire.peers[1-i].advertisement.nonce=101+i;}
    h.model.nearby=&net;h.model.trade=&wire;h.model.tradeWritable=true;h.sync();h.openHome(HomePanel::Nearby);
    h.dispatch(h.tap(280,310));CHECK(h.ui.screen()==Screen::TradeChoose);
    CHECK(h.ui.artRequest(h.state,h.model,h.now).formId==11);
    h.dispatch(h.tap(355,180));CHECK(h.ui.artRequest(h.state,h.model,h.now).formId==18);
    CHECK(!h.event(TouchKind::Up,355,180));h.dispatch(h.swipe(160,180,250,180));CHECK(h.ui.artRequest(h.state,h.model,h.now).formId==11);
    const auto ccw=display::panelToLogical(display::Orientation::Ccw90,{180,56});h.dispatch(h.tap(ccw.x,ccw.y));
    auto invite=h.tap(206,312);CHECK(invite.kind==IntentKind::TradeInvite && invite.value==19 && trade::sameIdentity(invite.peer,wire.peers[1].identity));
    CHECK(h.ui.pending() && !h.tap(206,312));h.dispatch(invite);
    CHECK(!h.event(TouchKind::Down,206,312));++wire.peers[1].advertisement.nonce;h.sync();CHECK(h.ui.screen()==Screen::Nearby && !h.event(TouchKind::Up,206,312));
    h.dispatch(h.tap(280,310));wire.peerCount=0;h.sync();CHECK(h.ui.screen()==Screen::Nearby);
    wire.peerCount=2;h.sync();h.dispatch(h.tap(280,310));h.dispatch(h.tap(206,365));
    // Both immutable offers are visible, with the local/remote side independent
    // of the coordinator's lexicographic identity order.
    auto& t=wire.transcript;t.peers[0].bytes[0]=2;t.peers[0].bytes[5]=1;t.peers[1]=wire.peers[1].identity;
    t.session=42;t.revision=1;t.nonces[0]=91;t.nonces[1]=101;t.sourceSequences[0]=h.state.sequence;t.sourceSequences[1]=h.state.sequence;
    t.offers[0]=h.state.collection[0];t.offers[1]=h.state.collection[1];CHECK(trade::valid(t));
    wire.stage=tradewire::Stage::Reviewing;wire.localSide=0;wire.connected=true;wire.peerReviewed=true;h.sync();CHECK(h.ui.screen()==Screen::TradeReview);
    CHECK(h.ui.artRequest(h.state,h.model,h.now).formId==18 && h.ui.partnerArtRequest(h.state,h.model,h.now).formId==11);
    std::array<std::uint16_t,kPixels+2> pixels;pixels.fill(0xbeef);
    std::array<std::uint16_t,kPixels> overview;
    CHECK(h.ui.render(h.state,h.model,pixels.data()+1,kPixels,90000));std::copy_n(pixels.data()+1,kPixels,overview.data());
    for(unsigned i=0;i<5;++i){h.dispatch(h.tap(355,180));CHECK(h.ui.render(h.state,h.model,pixels.data()+1,kPixels,90000));CHECK(pixels.front()==0xbeef&&pixels.back()==0xbeef);}
    CHECK(std::memcmp(overview.data(),pixels.data()+1,sizeof(overview))==0);
    h.dispatch(h.swipe(250,180,160,180));h.dispatch(h.tap(55,180));CHECK(h.ui.render(h.state,h.model,pixels.data()+1,kPixels,90000));CHECK(std::memcmp(overview.data(),pixels.data()+1,sizeof(overview))==0);
    CHECK(!h.swipe(206,300,206,180)); // No battle/capture action on trade review.
    auto confirm=h.tap(260,310);CHECK(confirm.kind==IntentKind::TradeConfirm && confirm.tradeSession==42 && confirm.tradeRevision==1 && confirm.tradeFingerprint==trade::fingerprint(t));
    CHECK(!h.event(TouchKind::Up,260,310));h.dispatch(confirm);
    CHECK(!h.event(TouchKind::Down,260,310));++t.revision;h.sync();CHECK(!h.event(TouchKind::Up,260,310));
    CHECK(!h.event(TouchKind::Down,260,310));--t.offers[1].mood;h.sync();CHECK(!h.event(TouchKind::Up,260,310)); // Same revision, different offer.
    for(unsigned reason=0;reason<6;++reason){
        wire.localConfirmed=reason==0;wire.offerPending=reason==1;wire.peerReviewed=reason!=2;wire.connected=reason!=3;h.model.writable=reason!=4;wire.recoveryOffer=reason==5;h.sync();CHECK(!h.tap(260,310));
    }
    wire.localConfirmed=false;wire.offerPending=false;wire.peerReviewed=true;wire.connected=true;wire.recoveryOffer=false;h.model.writable=true;h.sync();
    wire.peerConfirmed=true;h.sync();CHECK(!h.tap(110,310));confirm=h.tap(260,310);CHECK(confirm.kind==IntentKind::TradeConfirm);h.dispatch(confirm);
    wire.peerConfirmed=false;h.sync();h.dispatch(h.tap(110,310));CHECK(h.ui.screen()==Screen::TradeChoose);h.dispatch(h.tap(355,180));auto offer=h.tap(206,312);CHECK(offer.kind==IntentKind::TradeOffer&&offer.value==19&&offer.tradeFingerprint==trade::fingerprint(t));h.dispatch(offer);
    h.dispatch(h.tap(206,365));wire.localSide=1;h.sync();CHECK(h.ui.artRequest(h.state,h.model,h.now).formId==11&&h.ui.partnerArtRequest(h.state,h.model,h.now).formId==18);
    wire.stage=tradewire::Stage::Prepared;wire.durable=tradewire::Durable::Prepared;wire.connected=false;h.model.writable=false;h.sync();
    CHECK(!h.tap(260,310)&&!h.tap(110,310));auto cancel=h.tap(206,365);CHECK(cancel.kind==IntentKind::TradeCancel&&cancel.tradeFingerprint==trade::fingerprint(t));h.dispatch(cancel);
    for(auto stage:{tradewire::Stage::Committing,tradewire::Stage::Applying}){wire.stage=stage;wire.durable=tradewire::Durable::Committed;h.sync();CHECK(!h.tap(206,365)&&!h.tap(260,310));}
    for(auto stage:{tradewire::Stage::Applied,tradewire::Stage::Aborted,tradewire::Stage::Cancelled}){wire.stage=stage;wire.durable=stage==tradewire::Stage::Applied?tradewire::Durable::Applied:tradewire::Durable::Aborted;h.sync();auto done=h.tap(206,365);CHECK(done.kind==IntentKind::TradeClose);h.dispatch(done);}
    CHECK(h.gameWrites==writes && std::memcmp(&saved,&h.state,sizeof(State))==0);
    // A prepared boot can open Nearby even while foreground game writes lock.
    Harness locked;locked.choose();locked.model.writable=false;locked.sync();locked.selectHome(HomePanel::Nearby);auto reconnect=locked.tap(206,323);CHECK(reconnect.kind==IntentKind::OpenNearby);
    // A lone playable partner (even with an old test fixture) cannot be offered.
    for(bool legacy:{false,true}){Harness one;one.choose();if(legacy)one.state=stableMemberFixture(18);one.sync();wire={};wire.stage=tradewire::Stage::Discovering;wire.peerCount=1;std::memcpy(wire.peers[0].identity.bytes,net.peers[0].mac.bytes,6);wire.peers[0].advertisement.available=true;wire.peers[0].advertisement.nonce=1;wire.peers[0].compatible=true;one.model.trade=&wire;one.model.tradeWritable=true;one.model.nearby=&net;one.sync();one.openHome(HomePanel::Nearby);one.dispatch(one.tap(280,310));CHECK(one.ui.screen()==Screen::TradeChoose&&!one.tap(206,312));CHECK(one.ui.render(one.state,one.model,pixels.data()+1,kPixels,one.now));one.dispatch(one.tap(206,365));CHECK(one.ui.screen()==Screen::Nearby);}
}

void autoCaptureChoice() {
    State before{},paused{};autobattle::Trace trace;bool found=false;
    for(unsigned seed=1;seed<200 && !found;++seed){
        auto candidate=newDevice(seed);CHECK(apply(candidate,Action::Hatch,1)==Error::None);CHECK(apply(candidate,Action::Mode,1)==Error::None);CHECK(apply(candidate,Action::Walk,100)==Error::None);
        candidate.wildFormId=18;candidate.wildSpecies=Species::Agumon;candidate.wildLevel=1;candidate.wildMaxHp=candidate.wildHp=forms::stats(18,1).maxHp;CHECK(isValid(candidate));
        auto result=candidate;autobattle::Trace chunk;CHECK(applyAutoFight(result,&chunk)==Error::None);
        if(result.autoCapture==AutoCapture::Awaiting){before=candidate;paused=result;trace=chunk;found=true;}
    }
    CHECK(found && trace.outcome==autobattle::Outcome::None && paused.captures==before.captures && !paused.captureAttempts);
    Harness h;h.state=before;h.sync();CHECK(h.ui.screen()==Screen::Encounter);h.dispatch(h.tap(206,274));
    auto away=h.tap(206,275);CHECK(away.kind==IntentKind::GameAction && away.action==Action::Retreat);
    h.ui.resolve(); h.sync();
    CHECK(autoFightReady(h.state,h.model));
    h.dispatch(Intent{IntentKind::GameAction,Action::AutoFight,0});CHECK(trade::sameState(h.state,paused));
    battlepresentation::Sequencer movie;CHECK(movie.startAuto(trace,h.now));h.model.battle=&movie.view();h.sync();CHECK(h.ui.screen()==Screen::Battle);
    const auto saved=h.state;const auto writes=h.gameWrites;unsigned frames=0;
    while(movie.locked() && frames++<500){h.now+=100;movie.poll(h.now);h.sync();if(movie.locked()){CHECK(h.ui.screen()==Screen::Battle);CHECK(!h.tap(206,365)&&!h.swipe(206,300,206,200));}}
    CHECK(!movie.locked() && h.ui.screen()==Screen::Capture && h.gameWrites==writes && trade::sameState(h.state,saved));
    for(unsigned i=0;i<10;++i){h.now+=500;h.sync();CHECK(h.ui.screen()==Screen::Capture&&!h.event(TouchKind::Up,206,300)&&!h.tap(206,60));}
    CHECK(h.state.captureAttempts==0&&h.gameWrites==writes); // Rendering/polling/unmatched releases never throw.
    // Declining is an explicit saved attack-only remainder; it never reprompts.
    auto resume=h.tap(206,365);CHECK(resume.kind==IntentKind::GameAction&&resume.action==Action::AutoResume);CHECK(!h.event(TouchKind::Up,206,365));h.dispatch(resume);
    CHECK(h.state.autoCapture==AutoCapture::None&&h.state.phase==Phase::Home&&h.state.captures==before.captures&&h.ui.screen()==Screen::Result);
    for(unsigned i=0;i<5;++i){h.sync();CHECK(h.ui.screen()==Screen::Result);}
    // Restarting at the durable pause requires a fresh press/release. A timing
    // red timing consumes exactly one throw and makes its reduced nonzero roll.
    Harness restored;restored.state=paused;restored.sync();CHECK(restored.ui.screen()==Screen::Capture);
    CHECK(!restored.event(TouchKind::Up,206,200)&&!restored.event(TouchKind::Up,206,300));
    unsigned throws=0;
    for(unsigned attempt=1;attempt<=3 && restored.ui.screen()==Screen::Capture;++attempt){
        const auto wildHp=restored.state.wildHp, hp=restored.state.hp;
        auto miss=timedThrow(restored,false);CHECK(miss.kind==IntentKind::GameAction&&miss.action==Action::RingCapture);restored.dispatch(miss);
        CHECK(restored.state.lastCapture.result==CaptureResult::Escaped&&restored.state.lastCapture.attempt==attempt&&restored.state.captures==paused.captures);
        CHECK(restored.state.phase==Phase::Encounter&&restored.state.wildHp==wildHp&&restored.state.hp==hp&&restored.state.captureDeferred==1);
        CHECK(restored.ui.screen()==Screen::Battle);
        ++throws;
        if(attempt==3) break;
        CHECK(autoFightReady(restored.state,restored.model));
        restored.dispatch(Intent{IntentKind::GameAction,Action::AutoFight,0});
    }
    CHECK(throws>=1&&restored.state.captures==paused.captures);
    if(restored.state.phase==Phase::Encounter){
        const auto attempts=restored.state.captureAttempts;
        restored.dispatch(Intent{IntentKind::GameAction,Action::AutoFight,0});
        CHECK(restored.state.captures==paused.captures);
        if(attempts>=3) CHECK(restored.state.autoCapture!=AutoCapture::Awaiting);
    }
    // A trade or Nearby flow can never expose wild capture controls.
    Harness isolated;isolated.state=paused;tradewire::View wire;wire.stage=tradewire::Stage::Reviewing;isolated.model.trade=&wire;isolated.sync();CHECK(isolated.ui.screen()==Screen::TradeReview&&!isolated.swipe(206,300,206,200));
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
    nearbyModeConsent();
    homeStepVisibility();
    captureTimingControls();
    fullRosterCaptureControls();
    fullRosterNavigation();
    xpCompanionControls();
    soundSettings();
    tradingScreens();
    autoCaptureChoice();
    battleArtworkSequence();
    homeCarousel();
    walkingCheckpoints();
    Harness h; const auto egg=h.state;
    // Exact cached Nearby consent fields add 56 host bytes to the prior512 B controller.
    CHECK(h.ui.screen()==Screen::Egg && sizeof(Controller)<=576);
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
    CHECK(!h.event(TouchKind::Up,206,300)); // No throw without a fresh press.
    CHECK(!h.event(TouchKind::Down,206,60));CHECK(!h.event(TouchKind::Up,206,180,60));
    CHECK(std::memcmp(&beforeFlick,&h.state,sizeof(State))==0);

    // On-target timing submits only the sampled cycle phase; core owns the odds.
    const auto flick=timedThrow(h,true);
    CHECK(flick.kind==IntentKind::GameAction && flick.action==Action::RingCapture && capturering::sample(flick.value,h.state.wildFormId).grade==capturering::Grade::Green);
    CHECK(ringCaptureChance(h.state,flick.value)==captureChance(h.state));
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

    // A submitted Down cannot be replayed by a held contact after context change.
    Harness stale;stale.choose();stale.encounter();
    while(stale.state.wildHp>stale.state.wildMaxHp/2)stale.strike();
    stale.dispatch(stale.tap(280,306));const auto accepted=stale.event(TouchKind::Down,206,300);
    CHECK(accepted.action==Action::RingCapture);stale.dispatch(accepted,false);
    CHECK(apply(stale.state,Action::Walk,1)==Error::None);stale.sync();
    stale.now+=600;CHECK(!stale.event(TouchKind::Down,206,300));
    stale.model.inputEnabled=false;stale.sync();CHECK(!stale.event(TouchKind::Up,206,200));
    stale.model.inputEnabled=true;stale.sync();stale.dispatch(stale.tap(280,306));
    CHECK(!stale.ui.touch(stale.state,stale.model,{TouchKind::Down,206,200,stale.now-1}));
    CHECK(!stale.event(TouchKind::Up,206,200));

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
    // A miss resumes the fight, and an exact duplicate would merge. Keep throwing
    // until a new form actually takes the next collection slot.
    for(unsigned round=0;h.state.collectionCount<2&&round<24;++round){
        if(h.state.phase==Phase::Home){
            const auto maximum=combat::formProfile(activeMember(h.state)->formId,h.state.level).stats.maxHp;
            for(unsigned guard=0;h.state.hp<maximum&&guard<40;++guard)CHECK(apply(h.state,Action::Rest)==Error::None);
            CHECK(apply(h.state,Action::Walk,100)==Error::None);
        }
        while(h.state.phase==Phase::Encounter&&h.state.wildHp>h.state.wildMaxHp/2)CHECK(apply(h.state,Action::Attack)==Error::None);
        for(unsigned attempt=0;attempt<3&&h.state.phase==Phase::Encounter&&h.state.collectionCount<2;++attempt){
            if(h.state.captureDeferred)CHECK(apply(h.state,Action::Attack)==Error::None);
            while(h.state.phase==Phase::Encounter&&h.state.wildHp>h.state.wildMaxHp/2)CHECK(apply(h.state,Action::Attack)==Error::None);
            if(h.state.phase!=Phase::Encounter||h.state.captureDeferred)break;
            const auto owned=h.state.collectionCount;CHECK(apply(h.state,Action::Capture)==Error::None);
            if(h.state.collectionCount==owned&&h.state.phase==Phase::Home)break;
        }
        while(h.state.phase==Phase::Encounter)CHECK(apply(h.state,Action::Attack)==Error::None);
    }
    h.sync();
    CHECK(h.state.collectionCount>=2);
    CHECK(h.ui.screen()==Screen::Home);
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
    art.dispatch(art.tap(120,309)); // Left-column Rest, beside Toilet.
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
        evolution.browseMember(19); // Active member is first; its stable ID is not its slot index.
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
    meetRoute(evolution.state, 19, route); evolution.sync();
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
    roster.state.captures=roster.state.encounters=kCollectionCapacity-1; roster.state.steps=100*(kCollectionCapacity-1);
    roster.state.collectionCount=kCollectionCapacity; roster.state.nextMemberId=kCollectionCapacity+1;
    for (unsigned i=1;i<kCollectionCapacity;++i) {
        roster.state.collection[i]=roster.state.collection[0];
        roster.state.collection[i].id=i+1; roster.state.collection[i].capturedAtSequence=i;
    }
    CHECK(isValid(roster.state)); roster.sync(); roster.openHome(HomePanel::Partners);
    CHECK(roster.ui.screen()==Screen::Collection);
    CHECK(!roster.tap(280,312)); // Already equipped instance cannot be selected again.
    roster.dispatch(roster.tap(120,312)); roster.dispatch(roster.tap(206,312));
    CHECK(roster.ui.screen()==Screen::Evolution); // Active partner never shows Release.
    roster.dispatch(roster.tap(206,365)); roster.dispatch(roster.tap(206,365));
    roster.browseMember(2); roster.dispatch(roster.tap(120,312));
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
    roster.dispatch(release,false); CHECK(roster.ui.screen()==Screen::Stats && roster.state.collectionCount==kCollectionCapacity);
    roster.dispatch(roster.tap(206,312)); roster.dispatch(roster.tap(206,302));
    CHECK(roster.ui.screen()==Screen::Collection && roster.state.collectionCount==kCollectionCapacity-1);
    CHECK(!findMember(roster.state,2) && roster.state.activeCreatureId==1 && roster.state.nextMemberId==kCollectionCapacity+1);
    CHECK(roster.state.collection[1].id==3); // Slot moved; stable ID did not.
    roster.browseMember(3); auto equip=roster.tap(280,312); CHECK(equip.action==Action::Select && equip.value==3);
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
    meetRoute(baby.state, 19, babyRoute); baby.sync();
    baby.openHome(HomePanel::Partners); baby.browseMember(19); baby.dispatch(baby.tap(120,312));
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
