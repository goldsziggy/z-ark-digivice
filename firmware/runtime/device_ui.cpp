#include "device_ui.hpp"
#include "capture_ring.hpp"
#include "expeditions.hpp"
#include "forms.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace digivice::deviceui {
namespace {
constexpr std::uint16_t rgb(int r, int g, int b) {
    return static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
constexpr auto bg = rgb(9, 20, 29), panel = rgb(18, 42, 51), edge = rgb(38, 83, 87);
constexpr auto ink = rgb(236, 246, 217), dim = rgb(173, 196, 190);
constexpr auto mint = rgb(144, 231, 174), amber = rgb(255, 193, 96), red = rgb(239, 118, 108);
// Original hand-entered 5x7 glyphs, dedicated to CC0 with this source. Each
// row uses the low five bits. No external font files or private artwork.
constexpr const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+:/%.!?<>=";
constexpr std::uint8_t glyphs[][7] = {
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14},{0,0,0,31,0,0,0},{0,4,4,31,4,4,0},
 {0,4,4,0,4,4,0},{1,2,2,4,8,8,16},{25,25,2,4,8,19,19},
 {0,0,0,0,0,6,6},{4,4,4,4,4,0,4},{14,17,1,2,4,0,4},
 {2,4,8,16,8,4,2},{8,4,2,1,2,4,8},{0,0,31,0,31,0,0}
};
struct ClipSpan { std::int16_t begin, end; };
// Compile-time circle clip: exactly the same integer boundary as inside().
// One pair of exclusive row bounds avoids checking every written pixel.
constexpr auto makeClipSpans() {
    std::array<ClipSpan, kSize> rows{};
    for (int y=0; y<kSize; ++y) {
        const int dy=y-206, squared=204*204-dy*dy;
        if (squared<0) continue;
        int reach=0;
        while ((reach+1)*(reach+1)<=squared) ++reach;
        rows[y]={static_cast<std::int16_t>(206-reach),static_cast<std::int16_t>(207+reach)};
    }
    return rows;
}
constexpr auto clipSpans=makeClipSpans();

struct Canvas {
    std::uint16_t* p;
    int clipLeft=0, clipTop=0, clipRight=kSize, clipBottom=kSize;
    void pixel(int x, int y, std::uint16_t color) {
        if (x>=clipLeft && x<clipRight && y>=clipTop && y<clipBottom && Controller::inside(x, y)) p[y * kSize + x] = color;
    }
    void span(int y, int begin, int end, std::uint16_t color) {
        if (y<clipTop || y>=clipBottom) return;
        begin=std::max({begin,static_cast<int>(clipSpans[y].begin),clipLeft});
        end=std::min({end,static_cast<int>(clipSpans[y].end),clipRight});
        if (begin<end) std::fill(p+y*kSize+begin,p+y*kSize+end,color);
    }
    void rect(int x, int y, int w, int h, std::uint16_t color) {
        for (int yy=std::max(0,y); yy<std::min(kSize,y+h); ++yy) span(yy,x,x+w,color);
    }
    void circle(int x, int y, int radius, std::uint16_t color, bool fill = true) {
        const int outer=radius*radius, inner=(radius-2)*(radius-2);
        int reach=0, hole=-1;
        for (int yy=-radius; yy<=radius; ++yy) {
            const int outerSquared=outer-yy*yy;
            // Row extents grow toward the center and shrink afterward. Across
            // the whole circle these loops take O(radius), not O(radius^2).
            while ((reach+1)*(reach+1)<=outerSquared) ++reach;
            while (reach*reach>outerSquared) --reach;
            const int holeSquared=inner-1-yy*yy; // Strict d<inner is excluded.
            if (fill || holeSquared<0) {
                hole=-1; span(y+yy,x-reach,x+reach+1,color);
            } else {
                while ((hole+1)*(hole+1)<=holeSquared) ++hole;
                while (hole*hole>holeSquared) --hole;
                span(y+yy,x-reach,x-hole,color);
                span(y+yy,x+hole+1,x+reach+1,color);
            }
        }
    }
    void shadedAnnulus(int x,int y,int innerRadius,int outerRadius,std::uint16_t color) {
        const int inner=innerRadius*innerRadius,outer=outerRadius*outerRadius;
        int reach=0,hole=-1;
        const auto blend=[&](int row,int begin,int end) {
            if(row<clipTop || row>=clipBottom)return;
            begin=std::max({begin,static_cast<int>(clipSpans[row].begin),clipLeft});
            end=std::min({end,static_cast<int>(clipSpans[row].end),clipRight});
            for(int column=begin;column<end;++column) {
                auto& pixel=p[row*kSize+column];
                // 75% target color over the existing background in RGB565.
                pixel=static_cast<std::uint16_t>(((((color>>11)*3+(pixel>>11))/4)<<11)|
                    (((((color>>5)&63)*3+((pixel>>5)&63))/4)<<5)|(((color&31)*3+(pixel&31))/4));
            }
        };
        for(int yy=-outerRadius;yy<=outerRadius;++yy) {
            const int outerSquared=outer-yy*yy;
            while((reach+1)*(reach+1)<=outerSquared)++reach;
            while(reach*reach>outerSquared)--reach;
            const int holeSquared=inner-1-yy*yy;
            if(holeSquared<0){hole=-1;blend(y+yy,x-reach,x+reach+1);}
            else {
                while((hole+1)*(hole+1)<=holeSquared)++hole;
                while(hole*hole>holeSquared)--hole;
                blend(y+yy,x-reach,x-hole);blend(y+yy,x+hole+1,x+reach+1);
            }
        }
    }
    bool scene(const Artwork& art, const ArtRequest& request) {
        if (!art.background || art.backgroundPixels<kPixels || !art.backgroundId || !request.sceneId ||
            std::strncmp(art.backgroundId,request.sceneId,48)!=0) return false;
        for (int y=clipTop;y<clipBottom;++y) {
            const int begin=std::max<int>(clipLeft,clipSpans[y].begin),end=std::min<int>(clipRight,clipSpans[y].end);
            if (begin<end) std::copy(art.background+y*kSize+begin,art.background+y*kSize+end,p+y*kSize+begin);
        }
        return true;
    }
    void shadow(int x,int y,int halfWidth) {
        for(int dy=-2;dy<=2;++dy) for(int dx=-halfWidth+std::abs(dy)*3;dx<=halfWidth-std::abs(dy)*3;++dx) {
            const int xx=x+dx,yy=y+dy;
            if(xx<clipLeft || xx>=clipRight || yy<clipTop || yy>=clipBottom || !Controller::inside(xx,yy)) continue;
            const auto value=p[yy*kSize+xx];
            p[yy*kSize+xx]=static_cast<std::uint16_t>(((((value>>11)&31)*3/5)<<11)|((((value>>5)&63)*3/5)<<5)|((value&31)*3/5));
        }
    }
    bool spriteFrame(int x,int y,int maximumSide,const SpriteFrame& frame,const ArtRequest& request,bool mirror=false) {
        if (!request.formId || frame.formId!=request.formId || frame.animation!=request.animation ||
            !frame.pixels || !frame.mask || (frame.width!=16 && frame.width!=32) || frame.height!=frame.width ||
            frame.pixelCount<static_cast<std::size_t>(frame.width)*frame.height ||
            frame.maskBytes<static_cast<std::size_t>(frame.width)*frame.height/8) return false;
        const bool bounded=frame.contentWidth && frame.contentHeight;
        if((frame.contentWidth!=0)!=(frame.contentHeight!=0) ||
           (bounded && (frame.contentX+frame.contentWidth>frame.width || frame.contentY+frame.contentHeight>frame.height))) return false;
        const int width=bounded ? frame.contentWidth : frame.width, height=bounded ? frame.contentHeight : frame.height;
        const int scale=std::min(16,maximumSide/std::max(width,height));
        if (scale<1) return false;
        const int contentX=bounded ? (mirror ? frame.width-frame.contentX-frame.contentWidth : frame.contentX) : 0;
        const int left=x-width*scale/2-contentX*scale,top=y-height*scale/2-(bounded ? frame.contentY*scale : 0);
        int bottom=-1;
        for(int row=frame.height-1;row>=0 && bottom<0;--row) for(int column=0;column<frame.width;++column) {
            const auto index=static_cast<std::size_t>(row)*frame.width+column;
            if(frame.mask[index/8] & (1u<<(index%8))) {bottom=row;break;}
        }
        if(bottom>=0) shadow(x,top+(bottom+1)*scale+1,width*scale/3);
        for(int row=0;row<frame.height;++row) for(int column=0;column<frame.width;++column) {
            const auto index=static_cast<std::size_t>(row)*frame.width+column;
            if (frame.mask[index/8] & (1u<<(index%8))) rect(left+(mirror ? frame.width-1-column : column)*scale,top+row*scale,scale,scale,frame.pixels[index]);
        }
        return true;
    }
    void text(int x, int y, const char* value, int scale, std::uint16_t color, std::size_t max = 32) {
        if (!value) return;
        for (std::size_t n = 0; value[n] && n < max; ++n) {
            char ch = value[n]; if (ch >= 'a' && ch <= 'z') ch -= 'a'-'A';
            const char* found = ch == ' ' ? nullptr : std::strchr(alphabet, ch);
            if (found) {
                const auto& glyph = glyphs[found - alphabet];
                for (int yy = 0; yy < 7; ++yy) for (int xx = 0; xx < 5; ++xx)
                    if (glyph[yy] & (1u << (4-xx))) rect(x+xx*scale,y+yy*scale,scale,scale,color);
            }
            x += 6*scale;
        }
    }
    void center(int y, const char* value, int scale = 2, std::uint16_t color = ink, std::size_t max = 25) {
        const auto n = std::min(std::strlen(value ? value : ""), max);
        text((kSize-static_cast<int>(n)*6*scale+scale)/2,y,value,scale,color,max);
    }
    void bar(int x, int y, int width, std::uint32_t value, std::uint32_t maximum, std::uint16_t color) {
        rect(x,y,width,8,edge);
        const auto fill = maximum ? static_cast<int>(static_cast<std::uint64_t>(std::min(value,maximum))*width/maximum) : 0;
        rect(x,y,fill,8,color);
    }
    // 9x10 heart from the existing rects. The 5x7 font has no heart glyph.
    void heart(int x, int y, bool filled, std::uint16_t color) {
        if (filled) {
            rect(x+1,y,3,2,color); rect(x+5,y,3,2,color);
            rect(x,y+2,9,4,color);
            rect(x+1,y+6,7,1,color);
            rect(x+2,y+7,5,1,color);
            rect(x+3,y+8,3,1,color);
            rect(x+4,y+9,1,1,color);
        } else {
            rect(x+1,y,3,1,color); rect(x+5,y,3,1,color);
            rect(x,y+1,1,4,color); rect(x+8,y+1,1,4,color);
            rect(x+3,y+1,1,2,color); rect(x+5,y+1,1,2,color);
            rect(x+1,y+5,1,2,color); rect(x+7,y+5,1,2,color);
            rect(x+2,y+7,1,1,color); rect(x+6,y+7,1,1,color);
            rect(x+3,y+8,1,1,color); rect(x+5,y+8,1,1,color);
            rect(x+4,y+9,1,1,color);
        }
    }
    void badge(int y,const char* value,int scale=2,std::uint16_t color=ink,std::size_t max=40) {
        const auto length=std::min(std::strlen(value ? value : ""),max);
        const int width=static_cast<int>(length)*6*scale-scale;
        rect((kSize-width)/2-6,y-4,width+12,7*scale+8,panel);
        center(y,value,scale,color,max);
    }
    void chevron(int x,int y,int direction,std::uint16_t color,int size=8) {
        for(int d=0;d<size;++d) { rect(x+direction*d,y-size+d,3,3,color); rect(x+direction*d,y+size-d,3,3,color); }
    }
    // Original geometric symbols: fist, heavy hammer, spark, shield,
    // reflected arrows, and magic ward. No raster allocation or artwork input.
    void icon(int x,int y,unsigned choice,bool defense,int scale,std::uint16_t color) {
        const auto box=[&](int xx,int yy,int w,int h) {rect(x+xx*scale,y+yy*scale,w*scale,h*scale,color);};
        if(!defense && choice==0) { box(-6,-6,12,9);box(-5,3,8,5);box(-8,-3,3,7); for(int i=-4;i<=4;i+=3)rect(x+i*scale,y-5*scale,scale,4*scale,panel); }
        else if(!defense && choice==1) {box(-8,-8,15,7);box(-2,-1,4,13);box(-7,10,14,2);}
        else if(!defense) {box(-1,-11,3,23);box(-11,-1,23,3);for(int i=3;i<8;++i){box(i,i,2,2);box(-i,-i,2,2);box(i,-i,2,2);box(-i,i,2,2);}}
        else if(choice==0) {box(-8,-9,17,3);box(-8,-6,3,10);box(6,-6,3,10);for(int i=0;i<8;++i){box(-8+i,4+i,3,2);box(6-i,4+i,3,2);}}
        else if(choice==1) {box(-9,-6,19,3);box(-9,4,19,3);for(int i=0;i<6;++i){box(4+i,-11+i,2,2);box(4+i,-1-i,2,2);box(-10+i,-1+i,2,2);box(-10+i,9-i,2,2);}}
        else {circle(x,y,11*scale,color,false);circle(x,y,7*scale,color,false);box(-1,-4,3,9);box(-4,-1,9,3);}
    }
    void egg(int x, int y, int frame) {
        for (int yy = -43; yy <= 43; ++yy) for (int xx = -33; xx <= 33; ++xx) {
            const int width = 34-((43-yy)*9/86);
            if (xx*xx*43*43+yy*yy*width*width <= width*width*43*43)
                pixel(x+xx,y+yy, ((xx/10+yy/12+frame)&3)==0 ? mint : ink);
        }
        rect(x-6,y-7,4,8,bg); rect(x+6,y-7,4,8,bg);
    }
    // A named creature never borrows another species or a test avatar when its
    // exact asset is absent/corrupt. This is a status symbol, not a creature.
    void missingArt(int x, int y) {
        rect(x-37,y-37,74,74,panel);
        rect(x-37,y-37,74,2,dim); rect(x-37,y+35,74,2,dim);
        rect(x-37,y-37,2,74,dim); rect(x+35,y-37,2,74,dim);
        text(x-7,y-25,"?",3,dim);
        text(x-32,y+13,"ART MISSING",1,dim,11);
    }
};
constexpr auto timingRed=rgb(246,83,74),timingOrange=rgb(255,164,60),timingGreen=rgb(84,234,134);
std::uint16_t timingColor(capturering::Grade grade) {
    return grade==capturering::Grade::Green ? timingGreen : grade==capturering::Grade::Orange ? timingOrange : timingRed;
}
const char* timingName(capturering::Grade grade) {
    return grade==capturering::Grade::Green ? "GREEN" : grade==capturering::Grade::Orange ? "ORANGE" : "RED";
}
bool captureAction(Action action) { return action==Action::Flick || action==Action::RingCapture || action==Action::Focus; }
void captureScene(Canvas& c,const State& state,const Model& model,const ArtRequest& art,const capturering::Sample& ring) {
    const bool focus=state.autoCapture==AutoCapture::FocusStrike || state.autoCapture==AutoCapture::FocusBlock;
    const bool strike=state.autoCapture==AutoCapture::FocusStrike;
    c.rect(62,49,288,28,panel);
    c.center(55,focus ? (strike ? "STRIKE! TIME THE RING" : "BLOCK! TIME THE RING") : "TIME THE RING",2,amber);
    // The broad target is below the exact encountered sprite; its colors never
    // obscure the creature. Only the moving timing ring is drawn above it.
    c.shadedAnnulus(206,176,ring.targetRadius-capturering::kBandHalfWidth,
        ring.targetRadius+capturering::kBandHalfWidth,timingGreen);
    if(!c.spriteFrame(206,176,176,model.artwork.sprite,art)) c.missingArt(206,176);
    const auto radius=static_cast<int>((ring.radiusQ8+128)/256);
    const auto color=timingColor(ring.grade);
    c.circle(206,176,radius,color,false);
    c.circle(206,176,std::max(1,radius-1),color,false);
    char label[48];
    if(focus) {
        const auto percent=focusPercent(strike,ring.phaseMs,state.wildFormId);
        if(strike) std::snprintf(label,sizeof(label),"%s HIT X%u.%u - TAP PLAY AREA",timingName(ring.grade),static_cast<unsigned>(percent/100),static_cast<unsigned>(percent%100/10));
        else std::snprintf(label,sizeof(label),percent==0 ? "%s BLOCKS ALL - TAP PLAY AREA" : "%s TAKES %u%% - TAP PLAY AREA",timingName(ring.grade),static_cast<unsigned>(percent));
        c.badge(269,label,1,color,32);
        c.badge(298,"AUTO PAUSED - ONE TAP",1,dim,36);
        return;
    }
    std::snprintf(label,sizeof(label),"%s %u%% - TAP PLAY AREA",timingName(ring.grade),static_cast<unsigned>(ringCaptureChance(state,ring.phaseMs)));
    c.badge(269,label,1,color,32);
    std::snprintf(label,sizeof(label),"%u THROWS LEFT%s",static_cast<unsigned>(3-std::min<std::uint32_t>(3,state.captureAttempts)),
        state.autoCapture==AutoCapture::Awaiting ? " - AUTO PAUSED" : "");
    c.badge(298,label,1,dim,36);
}
sprite::Animation battleAnimation(const battlepresentation::View* view,battlepresentation::Actor actor) {
    using Actor=battlepresentation::Actor;
    if(!view || !view->locked || view->capturePresentation || view->actor==Actor::None)
        return sprite::Animation::Idle;
    const bool incoming=view->actor==Actor::Opponent || view->reflected;
    if(view->flash && actor==(incoming ? Actor::Player : Actor::Opponent)) return sprite::Animation::Hurt;
    return view->actor==actor ? sprite::Animation::Attack : sprite::Animation::Idle;
}
int horizontalTap(int x,int y,bool picker) {
    // Shared unobtrusive edge targets; battle's existing side icons are also
    // tappable. The icon band ends before CATCH (y282), never replacing it.
    if(y>=134 && y<246) {
        if(x>=30 && x<84) return -1;
        if(x>=328 && x<382) return 1;
    }
    if(picker && y>=248 && y<282) {
        if(x>=80 && x<176) return -1;
        if(x>=238 && x<334) return 1;
    }
    return 0;
}
bool focusOpen(const State& state) {
    return state.phase==Phase::Encounter && (state.autoCapture==AutoCapture::FocusStrike || state.autoCapture==AutoCapture::FocusBlock);
}
// Rules 18 focus prompt: the same ring and tap as capture, answering Strike or Block.
bool canFocus(const State& state,const Model& model) {
    return model.writable && model.inputEnabled && !model.encounterRecoveryRequired && focusOpen(state);
}
bool canCapture(const State& state,const Model& model) {
    // Eligibility only: never run a speculative throw or consume even a copied RNG.
    return model.writable && model.inputEnabled && !model.encounterRecoveryRequired &&
        (state.battleMode==BattleMode::Tactical || state.autoCapture==AutoCapture::Awaiting) && captureChance(state)>0;
}
bool legal(const State& state, const Model& model, Action action, std::uint32_t value = 0) {
    if (!model.writable || !model.inputEnabled) return false;
    if ((action==Action::PartyAdd || action==Action::PartyRemove) &&
        (!model.partyEditable || model.encounterRecoveryRequired ||
         (model.nearby && model.nearby->stage!=nearby::Stage::Closed))) return false;
    State copy = state;
    return apply(copy, action, value) == Error::None;
}
bool routeReady(const State& state, const Model& model, const CreatureMember& member, const forms::EvolutionEdge& edge) {
    const bool active = member.id == state.activeCreatureId;
    const auto action = active ? Action::Evolve : Action::EvolveMember;
    const auto value = active ? static_cast<std::uint32_t>(edge.to) : ((member.id << 16) | (edge.to & 0xffffu));
    return legal(state, model, action, value);
}
bool anyRouteReady(const State& state, const Model& model, const CreatureMember& member) {
    for (unsigned i = 0; i < 2; ++i) {
        const auto* edge = forms::outgoing(member.formId, i);
        if (!edge) break;
        if (routeReady(state, model, member, *edge)) return true;
    }
    return false;
}
std::size_t peerCount(const nearby::View* view) {
    return view ? std::min(view->peerCount,nearby::kMaxPeers) : 0;
}
bool nearbyFeedback(const Model& model) {
    static_assert(kNearbyFeedbackMs<=nearby::kAutoPaceMs,"Protocol must preserve each displayed exchange");
    const auto* view=model.nearby;
    return view && (view->stage==nearby::Stage::Playing || view->stage==nearby::Stage::Finished) &&
        model.nearbyTurnElapsedMs<kNearbyFeedbackMs && view->match.sequence && nearby::valid(view->match);
}
bool activeTrade(const tradewire::View* view) {
    return view && view->stage!=tradewire::Stage::Closed && view->stage!=tradewire::Stage::Discovering;
}
const tradewire::Peer* tradingPeer(const Model& model,const trade::Identity& identity) {
    if(!model.trade) return nullptr;
    for(std::size_t i=0;i<std::min(model.trade->peerCount,tradewire::kMaxPeers);++i)
        if(trade::sameIdentity(model.trade->peers[i].identity,identity)) return &model.trade->peers[i];
    return nullptr;
}
trade::Identity peerIdentity(const nearby::Mac& mac) {
    trade::Identity result;std::memcpy(result.bytes,mac.bytes,sizeof(result.bytes));return result;
}
std::uint32_t tradeContext(const tradewire::View* v) {
    if(!v) return 0;
    // Presentation epoch only. Real consent carries the canonical transcript
    // identity and is independently checked by the runtime/protocol.
    return static_cast<unsigned>(v->stage) | (v->localSide<<8) |
        (static_cast<unsigned>(v->durable)<<9) | (static_cast<unsigned>(v->peerDurable)<<12) |
        (v->localConfirmed?1u<<15:0) | (v->peerConfirmed?1u<<16:0) |
        (v->peerReviewed?1u<<17:0) | (v->offerPending?1u<<18:0) |
        (v->connected?1u<<19:0) | (v->recoveryOffer?1u<<20:0);
}
bool changeTrade(const Model& model) {
    const auto* v=model.trade;
    return v && model.tradeWritable && v->stage==tradewire::Stage::Reviewing &&
        v->localSide<2 &&
        v->durable==tradewire::Durable::None && !v->localConfirmed && !v->peerConfirmed &&
        !v->offerPending && !v->recoveryOffer && trade::valid(v->transcript);
}
bool confirmTrade(const State& state,const Model& model) {
    const auto* v=model.trade;
    return v && model.tradeWritable && model.writable && v->stage==tradewire::Stage::Reviewing &&
        v->durable==tradewire::Durable::None && v->connected && v->peerReviewed &&
        !v->localConfirmed && !v->offerPending && !v->recoveryOffer && v->localSide<2 &&
        trade::valid(v->transcript) && trade::canOffer(state,v->transcript.offers[v->localSide].id) &&
        trade::sameMember(*findMember(state,v->transcript.offers[v->localSide].id),v->transcript.offers[v->localSide]);
}
std::uint32_t tradeCandidate(const State& state,std::uint32_t current,bool next) {
    unsigned start=state.collectionCount ? state.collectionCount-1 : 0;
    for(unsigned i=0;i<state.collectionCount;++i) if(state.collection[i].id==current) start=i;
    for(unsigned n=1;n<=state.collectionCount;++n) {
        const auto i=(start+(next ? n : state.collectionCount-n))%state.collectionCount;
        if(trade::canOffer(state,state.collection[i].id)) return state.collection[i].id;
    }
    return 0;
}
std::uint32_t maxHp(const State& state) {
    const auto* member = activeMember(state);
    return member ? forms::stats(member->formId, member->level).maxHp : 0;
}
// A duplicate exact-form catch merges into the oldest copy and does not append.
// The saved capture record is the Digimon that was just caught.
std::uint32_t capturedFormId(const State& state) {
    if(state.message==Message::Captured && state.lastCapture.result==CaptureResult::Captured &&
       state.lastCapture.sequence==state.sequence && state.lastCapture.targetFormId)
        return state.lastCapture.targetFormId;
    if(state.message==Message::Captured && state.collectionCount)
        return state.collection[state.collectionCount-1].formId;
    const auto* member=activeMember(state);
    return member ? member->formId : 0;
}
bool captureJoinedCollection(const State& state) {
    if(state.message!=Message::Captured || !state.lastCapture.sequence || state.lastCapture.sequence!=state.sequence) return false;
    for(std::size_t i=0;i<state.collectionCount;++i)
        if(state.collection[i].capturedAtSequence==state.sequence) return true;
    return false;
}
const char* shortMessage(Message message) {
    switch (message) {
    case Message::Fed: return "A TASTY LITTLE BREAK";
    case Message::Played: return "GOOD TIMES TOGETHER";
    case Message::Rested: return "FEELING BETTER";
    case Message::Hatched: return "YOUR ADVENTURE BEGINS";
    case Message::Won: return "BATTLE WON!";
    case Message::Captured: return "A NEW DIGIMON!";
    case Message::CaptureMissed: return "TRY ANOTHER THROW";
    case Message::CaptureEnded: return "THE WILD DIGIMON LEFT";
    case Message::Retreated: return "HOME SAFE - CHECK YOUR PARTNER";
    case Message::Treated: return "INJURY TREATED";
    case Message::Toileted: return "TOILET NEED CLEARED";
    case Message::Evolved: return "A NEW FORM!";
    case Message::Selected: return "PARTNER READY";
    default: return "ONE ADVENTURE AT A TIME";
    }
}
enum Id { EggOpen=1, Prev, Next, Choose, Hatch, Back, Care, Explore, Team, Settings,
 Feed, Play, Rest, Toilet, Battle, Attack, Heavy, Magic, Capture, Auto, Retreat,
 Again, MemberNext, MemberPrev, MemberSelect, Mute, Gyro, Mode, ModeConfirm, Setup, Sleep,
 MemberStats, StatsPrevious, StatsNext, ReleaseOpen, ReleaseConfirm, EvolveOpen, EvolutionPrevious, EvolutionNext,
 EvolutionDetails, EvolutionReview, EvolutionConfirm, EvolutionDone, Encounters, RateOff, RateRelaxed, RateNormal, RateFrequent,
 NearbyOpen, NearbyPrevious, NearbyNext, NearbyReview, NearbyChallenge, NearbyAccept, NearbyCancel, NearbyClose,
 NearbyPhysical, NearbyMagic, NearbyHeavy, NearbyCommit, NearbyBrace, NearbyCounter, NearbyWard,
 HomePrevious, HomeNext, HomeOpen, SoundOpen, Music, TradeOpen, TradeSelect, TradeChange,
 TradeConfirm, TradeCancel, TradeClose, AutoResume, NearbyTactical, NearbyAuto, PartyToggle, Treat, FocusSkip,
 Tile0, Tile1, Tile2, Tile3, BoxOpen, BoxSort, ExpeditionOpen, ExpeditionEnter, ExpeditionSolo };
std::size_t boxPages(const State& state) { return std::max<std::size_t>(1,(state.collectionCount+kTiles-1)/kTiles); }
constexpr unsigned kHomePanelCount = 5;
constexpr std::size_t kExpeditionCount = sizeof(expeditions::kScenarios) / sizeof(expeditions::kScenarios[0]);
const expeditions::Scenario& selectedExpedition(std::uint8_t index) {
    return expeditions::kScenarios[index % kExpeditionCount];
}
bool expeditionReady(const State& state, const expeditions::Scenario& scenario) {
    return scenario.kind == expeditions::Kind::Dungeon ? state.dungeonKeys > 0 : state.bossSigils > 0;
}
// Member card and the Squad/Box controls sit above y 320, clear of the shared BACK spot.
constexpr int kDetailX=116, kDetailY=228, kDetailW=180, kDetailH=36;
constexpr int kPrimaryX=116, kPrimaryY=270, kPrimaryW=180, kPrimaryH=44;
constexpr int kRosterX=116, kRosterY=284, kRosterW=180, kRosterH=36;
constexpr int kSortX=149, kSortY=284, kSortW=114, kSortH=36;
// Status word under a tile's level: injury first, then a ready route, then its role.
const char* tileStatus(const State& state,const Model& model,const CreatureMember& member,std::uint16_t& color) {
    if(isInjured(member)) { color=red; return "HURT"; }
    if(anyRouteReady(state,model,member)) { color=mint; return "READY"; }
    color=dim;
    return member.id==state.activeCreatureId ? "PARTNER" : isPartyMember(state,member.id) ? "SQUAD" : "";
}
constexpr const char* homeTitles[]{"CARE","PARTNERS","SETTINGS","NEARBY","DUNGEONS"};
constexpr const char* homeActions[]{"OPEN CARE","PARTNERS","SETTINGS","FIND NEARBY","DUNGEONS"};
} // namespace

int Controller::hitIndex(const Button* choices, std::size_t n, int x, int y) {
    for (std::size_t i=0;i<n;++i) if (hits(choices[i],x,y)) return static_cast<int>(i);
    for (std::size_t i=0;i<n;++i) if (choices[i].padded && hits(choices[i],x,y,kNavPad)) return static_cast<int>(i);
    return -1;
}
bool Controller::inside(int x, int y) {
    if (x < 0 || y < 0 || x >= kSize || y >= kSize) return false;
    const int dx=x-206, dy=y-206;
    return dx*dx+dy*dy <= 204*204;
}
void Controller::resetTouch() {
    down_=battleGesture_=browseGesture_=false; cancelled_=true; downButton_=0;
}
std::uint64_t Controller::captureElapsed(std::uint64_t now) const {
    if(captureEpoch_==UINT64_MAX || now<captureEpoch_) captureEpoch_=now;
    return now-captureEpoch_;
}
void Controller::cancelEvolution() {
    evolution_.cancel(); evolutionMember_=evolutionForm_=evolutionTarget_=0;
    if (screen_==Screen::EvolutionReview) screen_=Screen::Evolution;
}
void Controller::rollExpeditionVariant() {
    const auto count=expeditions::variantCount(selectedExpedition(expeditionIndex_));
    const auto mix=lastAt_*0x9E3779B97F4A7C15ull ^ (static_cast<std::uint64_t>(expeditionIndex_+1)<<17);
    expeditionVariant_=count ? static_cast<std::uint8_t>(mix%count) : 0;
}
void Controller::cancelTouch() {
    resetTouch(); cancelEvolution(); releaseMember_=0;
    if (screen_==Screen::ReleaseReview) screen_=Screen::Stats;
    if (screen_==Screen::NearbyReview) screen_=Screen::Nearby;
}
void Controller::acknowledgeContactReleased() {
    resetTouch(); captureContactBlocked_=false;
}
void Controller::notice(const char* message) {
    std::snprintf(notice_,sizeof(notice_),"%s",message ? message : "");
}
void Controller::resolve(const char* message) {
    const bool captureRetry=pending_ && screen_==Screen::Capture;
    pending_=false; resetTouch();
    if(captureRetry) { captureEpoch_=UINT64_MAX; captureEligible_=false; }
    if (message) notice(message);
}
bool Controller::acknowledgeWalking(const State& before, const State& after, Action action, std::uint32_t value) {
    if (!initialized_ || sequence_!=before.sequence || phase_!=before.phase ||
        before.sequence==UINT32_MAX || after.sequence!=before.sequence+1 ||
        !before.onboardingComplete || !isValid(before) || !isValid(after)) return false;
    // These are native runtime copies, not deserialized/reconstructed views.
    // Normalize only the approved background fields. Comparing all remaining
    // bytes is deliberately conservative: an unexpected field (or padding)
    // difference falls back to ordinary revision invalidation, never a bypass.
    State comparable;
    std::memcpy(&comparable,&after,sizeof(comparable));
    comparable.sequence=before.sequence;
    if (action==Action::AccrueSteps) {
        if (!value || value>UINT32_MAX-before.explorationSteps ||
            after.explorationSteps!=before.explorationSteps+value) return false;
        comparable.explorationSteps=before.explorationSteps;
        comparable.encounterProgress=before.encounterProgress;
        comparable.pendingEncounter=before.pendingEncounter;
    } else if (action==Action::EncounterSeed) {
        if (!value || before.encounterRng || before.encounterTarget) return false;
    } else return false;
    comparable.encounterRng=before.encounterRng;
    comparable.encounterTarget=before.encounterTarget;
    if (std::memcmp(&comparable,&before,sizeof(before))!=0) return false;
    auto review=evolution_;
    if (review.target() && !review.propose(after,review.target())) return false;
    evolution_=review;
    // Preserve animation time and a pending proposal's eventual commit marker.
    if (actionSequence_!=UINT32_MAX && (actionSequence_==before.sequence ||
        (pending_ && actionSequence_==before.sequence+1))) ++actionSequence_;
    sequence_=after.sequence;
    return true;
}
void Controller::update(const State& state, const Model& model) {
    const auto tradeTag=model.trade ? trade::fingerprint(model.trade->transcript) : 0;
    const auto tradeEpoch=tradeContext(model.trade);
    const bool tradeChanged=tradeTag!=tradeFingerprint_ || tradeEpoch!=tradeContext_;
    const auto previousTradeStage=static_cast<tradewire::Stage>(tradeContext_&255u);
    if(tradeChanged) {
        resetTouch();
        if(tradeTag!=tradeFingerprint_) tradePage_=0;
        if(activeTrade(model.trade)) screen_=Screen::TradeReview;
        else if(screen_==Screen::TradeReview && previousTradeStage!=tradewire::Stage::Closed && previousTradeStage!=tradewire::Stage::Discovering)
            screen_=model.nearby && model.nearby->stage==nearby::Stage::Discovering ? Screen::Nearby : Screen::Home;
    }
    tradeFingerprint_=tradeTag;tradeContext_=tradeEpoch;
    if(screen_==Screen::TradeChoose) {
        if(!trade::canOffer(state,tradeMemberId_)) {
            const auto candidate=tradeCandidate(state,0,true);
            if(candidate!=tradeMemberId_) {tradeMemberId_=candidate;resetTouch();}
        }
        if(!activeTrade(model.trade)) {
            const auto* peer=tradingPeer(model,tradePeer_);
            if(!peer || peer->advertisement.nonce!=tradePeerNonce_) {resetTouch();screen_=Screen::Nearby;}
        }
    }
    const bool revision = !initialized_ || sequence_ != state.sequence || phase_ != state.phase;
    const bool presentationLocked = model.battle && model.battle->locked;
    const auto nearbyStage=model.nearby ? model.nearby->stage : nearby::Stage::Closed;
    const auto nearbySession=model.nearby ? model.nearby->session : 0;
    const auto nearbySequence=model.nearby ? model.nearby->match.sequence : 0;
    const bool nearbyPending=model.nearby && model.nearby->localChoicePending;
    const bool feedback=nearbyFeedback(model);
    if (model.nearby && nearbyIndex_>=peerCount(model.nearby)) nearbyIndex_=0;
    const auto peer=model.nearby && nearbyIndex_<peerCount(model.nearby) ? model.nearby->peers[nearbyIndex_].mac : nearby::Mac{};
    const auto peerFighter=model.nearby && nearbyIndex_<peerCount(model.nearby) ? model.nearby->peers[nearbyIndex_].fighter : nearby::Fighter{};
    const auto peerNonce=model.nearby && nearbyIndex_<peerCount(model.nearby) ? model.nearby->peers[nearbyIndex_].openNonce : 0;
    const auto opponent=model.nearby ? model.nearby->opponent : nearby::Mac{};
    const auto offeredMode=model.nearby ? model.nearby->offeredMode : nearby::Mode::Tactical;
    const nearby::Fighter offered[]{model.nearby ? model.nearby->offered[0] : nearby::Fighter{},model.nearby ? model.nearby->offered[1] : nearby::Fighter{}};
    const auto role=static_cast<std::uint8_t>(model.nearby ? (model.nearby->host ? 1 : 0) |
        (model.nearby->match.attacker<<1) | (static_cast<unsigned>(model.nearby->match.mode)<<2) |
        (static_cast<unsigned>(model.nearby->match.status)<<3) : 255);
    const bool nearbyChanged=peerNonce!=nearbyPeerNonce_ || !nearby::sameMac(opponent,nearbyOpponent_) || offeredMode!=nearbyOfferedMode_ ||
        !nearby::sameFighter(offered[0],nearbyOffered_[0]) || !nearby::sameFighter(offered[1],nearbyOffered_[1]) ||
        role!=nearbyRole_ || feedback!=nearbyFeedback_ || !nearby::sameFighter(peerFighter,nearbyFighter_) || nearbyStage!=nearbyStage_ || nearbySession!=nearbySession_ ||
        nearbySequence!=nearbySequence_ || nearbyPending!=nearbyPending_ || std::memcmp(peer.bytes,nearbyPeer_.bytes,6)!=0;
    if (nearbyChanged) {
        resetTouch();
        if (screen_==Screen::NearbyReview) screen_=Screen::Nearby;
        if (nearbySequence!=nearbySequence_ || role!=nearbyRole_) { battleSelection_=combat::Move::Physical; defenseSelection_=0; }
    }
    nearbyStage_=nearbyStage; nearbySession_=nearbySession; nearbySequence_=nearbySequence;
    nearbyPending_=nearbyPending; nearbyPeer_=peer; nearbyFighter_=peerFighter; nearbyFeedback_=feedback; nearbyRole_=role;
    nearbyPeerNonce_=peerNonce; nearbyOpponent_=opponent; nearbyOfferedMode_=offeredMode;
    nearbyOffered_[0]=offered[0]; nearbyOffered_[1]=offered[1];
    if (nearbyStage==nearby::Stage::Closed) nearbyMode_=nearby::Mode::Tactical;
    const bool contextChanged = presentationLocked!=battleLocked_ || revision || starterStage_ != model.starterStage || selectedId_ != model.selectedId || starterForm_!=model.starterFormId || starterCount_!=model.starterCount ||
        writable_ != model.writable || enabled_ != model.inputEnabled || partyEditable_ != model.partyEditable;
    const auto* evolvedMember = findMember(state, evolutionMember_);
    const bool evolutionCommitted = revision && screen_==Screen::EvolutionReview &&
        (lastAction_==Action::Evolve || lastAction_==Action::EvolveMember) && state.sequence==actionSequence_ &&
        evolvedMember && evolvedMember->formId==evolutionTarget_ &&
        state.message==Message::Evolved;
    if (contextChanged) {
        resetTouch();
        if (evolutionCommitted) { evolution_.cancel(); screen_=Screen::EvolutionResult; }
        else cancelEvolution();
        if (screen_==Screen::ReleaseReview) {
            releaseMember_=0;
            screen_=revision && lastAction_==Action::Release && state.sequence==actionSequence_ && state.message==Message::Released ?
                Screen::Collection : Screen::Stats;
        }
        if (screen_==Screen::NearbyReview) screen_=Screen::Nearby;
        if (screen_==Screen::ModeReview) {
            // A review authorizes one proposal against the viewed state only.
            proposedMode_=255; screen_=Screen::Settings;
        }
    }
    if (screen_==Screen::EvolutionReview && !evolution_.target() && !pending_ && !evolutionCommitted)
        cancelEvolution(); // Failed/uncertain commit requires a fresh review.
    if (screen_==Screen::ReleaseReview && !releaseMember_ && !pending_) screen_=Screen::Stats;
    if (!state.onboardingComplete) {
        screen_ = model.starterStage == onboarding::Stage::Egg ? Screen::Egg :
            model.starterStage == onboarding::Stage::Selecting ? Screen::Starter : Screen::StarterReview;
    } else if (!initialized_ || phase_ == Phase::Egg) {
        screen_ = state.phase == Phase::Encounter ? Screen::Encounter : Screen::Home;
    } else if (phase_ != state.phase) {
        screen_ = state.phase == Phase::Encounter ? Screen::Encounter : Screen::Result;
    } else if (revision && screen_ == Screen::Capture) {
        // Another action/step can alter capture legality; require a fresh arm.
        screen_ = Screen::Battle;
    }
    if (presentationLocked) screen_=Screen::Battle;
    else if (battleLocked_) screen_=state.phase==Phase::Encounter ? Screen::Battle : Screen::Result;
    if(!presentationLocked && state.phase==Phase::Encounter && (state.autoCapture==AutoCapture::Awaiting || focusOpen(state)) &&
        !activeTrade(model.trade) && (!model.nearby || model.nearby->stage==nearby::Stage::Closed)) screen_=Screen::Capture;
    if(activeTrade(model.trade) && screen_!=Screen::TradeChoose && !presentationLocked) screen_=Screen::TradeReview;
    if (revision) { notice_[0]=0; pending_=false; battleSelection_=combat::Move::Physical; }
    battleLocked_=presentationLocked;
    if (model.starterStage != starterStage_ || model.selectedId != selectedId_) pending_=false;
    // Display order can change after selection/party edits. Keep the exact
    // viewed instance whenever it survives, without reordering durable slots.
    const auto selectedIndex=displayIndexForMember(state,memberId_);
    if(selectedIndex<state.collectionCount) memberIndex_=static_cast<std::uint8_t>(selectedIndex);
    else {
        if(memberIndex_>=state.collectionCount) memberIndex_=0;
        const auto* selected=collectionMemberAtDisplayIndex(state,memberIndex_);
        memberId_=selected ? selected->id : 0;
    }
    if(boxPage_>=boxPages(state)) boxPage_=static_cast<std::uint8_t>(boxPages(state)-1); // Release can remove the last page.
    const bool captureEligible=screen_==Screen::Capture && !presentationLocked && !pending_ && (canCapture(state,model) || canFocus(state,model));
    if(!captureEligible || captureEligible!=captureEligible_ || contextChanged || captureEpochForm_!=state.wildFormId)
        captureEpoch_=UINT64_MAX;
    captureEligible_=captureEligible; captureEpochForm_=state.wildFormId;
    sequence_=state.sequence; phase_=state.phase; starterStage_=model.starterStage;
    selectedId_=model.selectedId; starterForm_=model.starterFormId; starterCount_=model.starterCount; writable_=model.writable; enabled_=model.inputEnabled; partyEditable_=model.partyEditable; initialized_=true;
}
std::size_t Controller::buttons(const State& state, const Model& model, Button* out) const {
    std::size_t count=0;
    auto add=[&](int x,int y,int w,int h,const char* label,int id,bool enabled=true) {
        out[count++]={x,y,w,h,label,id,enabled && model.inputEnabled && !pending_ && !battleLocked_};
    };
    auto left=[&](int row,const char* label,int id,bool enabled=true) { add(62,230+row*52,139,44,label,id,enabled); };
    auto right=[&](int row,const char* label,int id,bool enabled=true) { add(211,230+row*52,139,44,label,id,enabled); };
    auto nav=[&](const char* label,int id,bool enabled=true) { add(kNavX,kNavY,kNavW,kNavH,label,id,enabled); out[count-1].padded=true; };
    auto back=[&]() { nav("BACK",Back); };
    switch(screen_) {
    case Screen::Egg:
        add(116,260,180,54,"MEET PARTNER",EggOpen,model.writable);
        add(144,332,124,38,"SETUP",Setup); break;
    case Screen::Starter:
        add(116,282,180,44,"CHOOSE",Choose,model.writable); back(); break;
    case Screen::StarterReview:
        add(116,252,180,52,"HATCH",Hatch,model.writable && model.starterStage==onboarding::Stage::Confirming);
        back(); break;
    case Screen::Home:
        add(30,134,54,112,"",HomePrevious);
        add(328,134,54,112,"",HomeNext);
        add(116,300,180,46,homeActions[static_cast<unsigned>(homePanel_)],HomeOpen,state.phase==Phase::Home); break;
    case Screen::Care:
        left(0,"FEED",Feed,legal(state,model,Action::Feed)); right(0,"PLAY",Play,legal(state,model,Action::Play));
        // Rules 17: an injured partner rests only to half health, so Treat takes Rest's slot.
        if (activeMember(state) && isInjured(*activeMember(state))) left(1,"TREAT",Treat,legal(state,model,Action::Treat));
        else left(1,"REST +25",Rest,legal(state,model,Action::Rest));
        right(1,"TOILET",Toilet,legal(state,model,Action::Toilet)); back(); break;
    case Screen::Explore:
        add(100,270,212,48,"ENCOUNTER SETTINGS",Encounters); back(); break;
    case Screen::Encounter:
        add(116,248,180,52,state.battleMode==BattleMode::Auto ? "AUTO BATTLE" : "BATTLE",Battle); break;
    case Screen::Battle:
        if (state.battleMode==BattleMode::Auto) add(116,250,180,52,"RUN AWAY",Retreat,legal(state,model,Action::Retreat));
        else {
            add(48,282,140,44,"RUN AWAY",Retreat,legal(state,model,Action::Retreat));
            add(274,282,76,44,"CATCH",Capture,canCapture(state,model));
        }
        back(); break;
    case Screen::Capture:
        if(state.autoCapture==AutoCapture::Awaiting) nav("SKIP / RESUME FIGHT",AutoResume,legal(state,model,Action::AutoResume));
        else if(focusOpen(state)) nav("LET AUTO PLAY",FocusSkip,legal(state,model,Action::Focus,kFocusNoTap));
        else back();
        break;
    case Screen::Result: add(116,274,180,50,"HOME",Again); break;
    case Screen::Collection: {
        const auto* member=selectedMember(state);
        if(member) {
            const bool active=member->id==state.activeCreatureId;
            const bool squad=isPartyMember(state,member->id);
            // TREAT or MAKE PARTNER stays its own button. Squad membership is a
            // second button, so a production Digimon can join or leave without
            // giving up Make Partner.
            const char* lead=nullptr; int leadId=0; bool leadOn=false;
            if(active && isInjured(*member) && legal(state,model,Action::Treat)) {
                lead="TREAT"; leadId=Treat; leadOn=true;
            } else if(!active && legal(state,model,Action::Select,member->id)) {
                lead="MAKE PARTNER"; leadId=MemberSelect; leadOn=true;
            }
            const char* squadLabel=nullptr; bool squadOn=false;
            if(squad && !active && legal(state,model,Action::PartyRemove,member->id)) {
                squadLabel="REMOVE FROM SQUAD"; squadOn=true;
            } else if(!active && legal(state,model,Action::PartyAdd,member->id)) {
                squadLabel="ADD TO SQUAD"; squadOn=true;
            } else if(!active && !squad && partyCount(state)==kPartyCapacity) {
                squadLabel="SQUAD FULL"; squadOn=false;
            }
            add(kDetailX,kDetailY,kDetailW,kDetailH,"DETAILS",MemberStats);
            if(lead && squadLabel) {
                add(62,kPrimaryY,140,kPrimaryH,lead,leadId,leadOn);
                add(210,kPrimaryY,140,kPrimaryH,squadLabel,PartyToggle,squadOn);
            } else if(lead) add(kPrimaryX,kPrimaryY,kPrimaryW,kPrimaryH,lead,leadId,leadOn);
            else if(squadLabel) add(kPrimaryX,kPrimaryY,kPrimaryW,kPrimaryH,squadLabel,PartyToggle,squadOn);
        }
        back(); break;
    }
    case Screen::Squad: case Screen::Box:
        // The whole tile is the target. An empty squad slot opens Box to choose who joins.
        for(std::size_t i=0;i<kTiles;++i) {
            const auto* member=tileMember(state,model,i);
            if(member) add(kTileX[i%2],kTileY[i/2],kTileW,kTileH,memberName(*member),Tile0+static_cast<int>(i));
            else if(screen_==Screen::Squad && i) add(kTileX[i%2],kTileY[i/2],kTileW,kTileH,"ADD TO SQUAD",Tile0+static_cast<int>(i),state.collectionCount>1);
        }
        if(screen_==Screen::Squad) add(kRosterX,kRosterY,kRosterW,kRosterH,"ALL DIGIMON",BoxOpen,state.collectionCount>0);
        if(screen_==Screen::Box) {
            const char* sorts[]{"NEW","LV","READY","HURT"};
            add(kSortX,kSortY,kSortW,kSortH,sorts[boxOrder_<4?boxOrder_:0],BoxSort);
        }
        back(); break;
    case Screen::Stats: {
        const auto* member=selectedMember(state);
        const bool active=member && member->id==state.activeCreatureId;
        if (member && !active) add(62,236,139,46,"DIGIVOLVE",EvolveOpen,state.phase==Phase::Home);
        if (active) add(108,282,196,44,"DIGIVOLVE",EvolveOpen,state.phase==Phase::Home);
        else add(108,282,196,44,"RELEASE DIGIMON",ReleaseOpen,member &&
            legal(state,model,Action::Release,memberId_));
        back(); break;
    }
    case Screen::ReleaseReview:
        add(108,278,196,48,"RELEASE",ReleaseConfirm,releaseMember_ && legal(state,model,Action::Release,releaseMember_)); back(); break;
    case Screen::Evolution: {
        const auto* member=selectedMember(state);
        const auto* edge=member ? forms::outgoing(member->formId,evolutionIndex_) : nullptr;
        add(62,282,100,44,evolutionPage_ ? "CLOSE INFO" : "? INFO",EvolutionDetails,edge);
        add(174,282,176,44,"SELECT",EvolutionReview,edge && evolutionPage_!=3);
        back(); break;
    }
    case Screen::EvolutionReview: {
        const bool active=!evolutionMember_ || evolutionMember_==state.activeCreatureId;
        const auto action=active ? Action::Evolve : Action::EvolveMember;
        const auto value=active ? evolution_.target() : ((evolutionMember_<<16)|(evolution_.target()&0xffffu));
        add(108,278,196,48,"DIGIVOLVE",EvolutionConfirm,evolution_.target() &&
            legal(state,model,action,value)); back(); break;
    }
    case Screen::EvolutionResult:
        add(108,278,196,48,"VIEW STATS",EvolutionDone); back(); break;
    case Screen::Settings:
        add(100,117,212,44,"SCREEN TIMEOUT",Sleep);
        add(100,172,212,44,"ENCOUNTER SETTINGS",Encounters);
        left(0,"SOUND SETTINGS",SoundOpen);
        right(0,model.gyroEnabled ? "GYRO ON" : "GYRO OFF",Gyro,model.motionAvailable);
        left(1,state.battleMode==BattleMode::Auto ? "MODE: AUTO" : "MODE: TACTICAL",Mode,
            legal(state,model,Action::Mode,state.battleMode==BattleMode::Auto ? 0 : 1));
        right(1,"WIFI SETUP",Setup); back(); break;
    case Screen::Sound:
        left(0,model.muted ? "MUTE: ON" : "MUTE: OFF",Mute);
        right(0,model.musicEnabled ? "MUSIC: ON" : "MUSIC: OFF",Music);
        back(); break;
    case Screen::TradeChoose: {
        const auto* peer=tradingPeer(model,tradePeer_);
        const bool initial=model.trade && model.trade->stage==tradewire::Stage::Discovering;
        const bool available=initial ? peer && peer->compatible && peer->advertisement.available && peer->advertisement.nonce==tradePeerNonce_ : changeTrade(model);
        add(108,282,196,44,initial ? "OFFER TO TRADE" : "CHANGE OFFER",TradeSelect,
            model.writable && model.tradeWritable && available && trade::canOffer(state,tradeMemberId_));
        back(); break;
    }
    case Screen::TradeReview: {
        const auto* v=model.trade;
        if(v && v->stage==tradewire::Stage::Reviewing && v->durable==tradewire::Durable::None) {
            add(62,282,100,44,"CHANGE",TradeChange,changeTrade(model));
            add(174,282,176,44,"CONFIRM BOTH",TradeConfirm,confirmTrade(state,model));
        }
        const bool terminal=!v || v->stage==tradewire::Stage::Closed || v->stage==tradewire::Stage::Discovering ||
            v->stage==tradewire::Stage::Applied || v->stage==tradewire::Stage::Aborted || v->stage==tradewire::Stage::Cancelled;
        if(terminal) nav("DONE",TradeClose);
        else if(v->durable!=tradewire::Durable::Committed && v->durable!=tradewire::Durable::Applied &&
                v->stage!=tradewire::Stage::Committing && v->stage!=tradewire::Stage::Applying)
            nav(v->durable==tradewire::Durable::Prepared ? "ASK TO CANCEL" : "CANCEL",TradeCancel,trade::valid(v->transcript));
        break;
    }
    case Screen::EncounterSettings:
        left(0,"PAUSED",RateOff,legal(state,model,Action::EncounterRate,0));
        right(0,"RELAXED",RateRelaxed,legal(state,model,Action::EncounterRate,1));
        left(1,"NORMAL",RateNormal,legal(state,model,Action::EncounterRate,2));
        right(1,"FREQUENT",RateFrequent,legal(state,model,Action::EncounterRate,3)); back(); break;
    case Screen::NearbyReview:
        add(62,218,140,44,nearbyMode_==nearby::Mode::Tactical ? "> TACTICAL" : "TACTICAL",NearbyTactical);
        add(210,218,140,44,nearbyMode_==nearby::Mode::Auto ? "> AUTO" : "AUTO",NearbyAuto);
        add(108,278,196,48,"CHALLENGE",NearbyChallenge,model.nearby &&
            model.nearby->stage==nearby::Stage::Discovering && nearbyIndex_<peerCount(model.nearby) && model.nearby->peers[nearbyIndex_].available);
        back(); break;
    case Screen::Nearby: {
        const auto* nearby=model.nearby;
        if (nearbyFeedback(model)) {
            // Keep the exchange readable before offering the next choices.
            // LEAVE remains available below; no game rule or network clock changes.
        } else if (nearby && nearby->stage==nearby::Stage::Discovering) {
            const bool peerReady=nearbyIndex_<peerCount(nearby) && nearby->peers[nearbyIndex_].available;
            add(62,282,149,44,"BATTLE",NearbyReview,peerReady && !activeTrade(model.trade));
            const auto* trading=peerReady ? tradingPeer(model,peerIdentity(nearby->peers[nearbyIndex_].mac)) : nullptr;
            add(218,282,132,44,"TRADE",TradeOpen,trading && trading->compatible && trading->advertisement.available &&
                model.trade->stage==tradewire::Stage::Discovering && model.tradeWritable && model.writable);
        } else if (nearby && nearby->stage==nearby::Stage::Incoming) {
            left(0,"ACCEPT",NearbyAccept); right(0,"DECLINE",NearbyCancel);
        } else if (nearby && (nearby->stage==nearby::Stage::Outgoing || nearby->stage==nearby::Stage::Accepting || nearby->stage==nearby::Stage::Reconnecting)) {
            add(108,282,196,44,"CANCEL",NearbyCancel);
        }
        nav("LEAVE",NearbyClose); break;
    }
    case Screen::ModeReview:
        add(116,262,180,50,"CONFIRM",ModeConfirm,proposedMode_<=1 && legal(state,model,Action::Mode,proposedMode_));
        back(); break;
    case Screen::Expeditions:
        add(116,270,180,44,"ENTER",ExpeditionEnter,state.phase==Phase::Home); back(); break;
    case Screen::ExpeditionLobby:
        if(!expeditionSolo_) add(116,270,180,44,"START SOLO",ExpeditionSolo,
            state.phase==Phase::Home && expeditionReady(state,selectedExpedition(expeditionIndex_)));
        back(); break;
    }
    return count;
}
Intent Controller::navigate(Screen next) {
    screen_=next; notice_[0]=0; resetTouch();
    captureEligible_=next==Screen::Capture;
    captureEpoch_=captureEligible_ ? lastAt_ : UINT64_MAX;
    return {IntentKind::Navigation};
}
Intent Controller::navigateHorizontal(bool next,const State& state,const Model& model) {
    if(screen_==Screen::TradeChoose) {
        const auto candidate=tradeCandidate(state,tradeMemberId_,next);
        if(!candidate || candidate==tradeMemberId_) return {};
        tradeMemberId_=candidate;return {IntentKind::Navigation};
    }
    if(screen_==Screen::TradeReview) {tradePage_=static_cast<std::uint8_t>((tradePage_+(next?1:4))%5);return {IntentKind::Navigation};}
    if(screen_==Screen::Sound) {
        constexpr std::uint8_t levels[]{0,5,15,30,50,75,100};
        if(next) {
            for(const auto level:levels) if(level>model.volumePercent) {
                pending_=true; resetTouch(); return {IntentKind::Volume,Action::Feed,level};
            }
        } else for(unsigned i=sizeof(levels)/sizeof(levels[0]);i>0;--i) if(levels[i-1]<model.volumePercent) {
            pending_=true; resetTouch(); return {IntentKind::Volume,Action::Feed,levels[i-1]};
        }
        return {};
    }
    if(screen_==Screen::Home) return activate(next ? HomeNext : HomePrevious,state,model);
    if(screen_==Screen::Expeditions) {
        expeditionIndex_=static_cast<std::uint8_t>((expeditionIndex_+(next ? 1 : kExpeditionCount-1))%kExpeditionCount);
        expeditionSolo_=false; rollExpeditionVariant(); return {IntentKind::Navigation};
    }
    if(screen_==Screen::Starter) return activate(next ? Next : Prev,state,model);
    if(screen_==Screen::Collection) return activate(next ? MemberNext : MemberPrev,state,model);
    if(screen_==Screen::Box) {
        const auto pages=boxPages(state);
        if(pages<2) return {};
        boxPage_=static_cast<std::uint8_t>((boxPage_+(next ? 1 : pages-1))%pages);
        return {IntentKind::Navigation};
    }
    if(screen_==Screen::Stats) return activate(next ? StatsNext : StatsPrevious,state,model);
    if(screen_==Screen::Evolution) {
        if(evolutionPage_) { evolutionPage_=evolutionPage_==1 ? 2 : 1; return {IntentKind::Navigation}; }
        return activate(next ? EvolutionNext : EvolutionPrevious,state,model);
    }
    if(screen_==Screen::Nearby && model.nearby && model.nearby->stage==nearby::Stage::Discovering)
        return activate(next ? NearbyNext : NearbyPrevious,state,model);
    notice_[0]=0;
    const int direction=next ? 1 : 2;
    if(screen_==Screen::Nearby && model.nearby && model.nearby->match.attacker!=(model.nearby->host ? 0 : 1))
        defenseSelection_=static_cast<std::uint8_t>((defenseSelection_+direction)%3);
    else {
        const unsigned index=battleSelection_==combat::Move::Physical ? 0 : battleSelection_==combat::Move::Heavy ? 1 : 2;
        const combat::Move choices[]{combat::Move::Physical,combat::Move::Heavy,combat::Move::Magic};
        battleSelection_=choices[(index+direction)%3];
    }
    return {IntentKind::Navigation};
}
Intent Controller::proposeTrade(IntentKind kind,const Model& model,std::uint32_t memberId) {
    Intent intent{kind,Action::Feed,memberId};intent.peer=tradePeer_;
    if(model.trade) {
        const auto& t=model.trade->transcript;
        intent.tradeSession=t.session;intent.tradeRevision=t.revision;intent.tradeFingerprint=trade::fingerprint(t);
    }
    pending_=true;resetTouch();return intent;
}
Intent Controller::propose(const State& state, const Model& model, Action action, std::uint32_t value) {
    if (action==Action::Focus ? (!canFocus(state,model) || value>kFocusNoTap || !legal(state,model,action,value)) :
        captureAction(action) ? (!canCapture(state,model) ||
        (action==Action::Flick ? value>kFlickMaxValue : value>=capturering::kCycleMs)) : !legal(state,model,action,value)) return {};
    pending_=true; actionAt_=lastAt_; actionSequence_=state.sequence+1;
    lastAction_=action; actionValue_=value; resetTouch();
    return {IntentKind::GameAction,action,value};
}
Intent Controller::activate(int id, const State& state, const Model& model) {
    switch(id) {
    case EggOpen: case Choose: case Hatch: pending_=true; return {IntentKind::StarterConfirm};
    case Prev: return {IntentKind::StarterPrevious};
    case Next: return {IntentKind::StarterNext};
    case HomePrevious: case HomeNext:
        homePanel_=static_cast<HomePanel>((static_cast<unsigned>(homePanel_)+(id==HomeNext ? 1 : kHomePanelCount-1))%kHomePanelCount);
        return navigate(Screen::Home);
    case HomeOpen: {
        constexpr int actions[]{Care,Team,Settings,NearbyOpen,ExpeditionOpen};
        return activate(actions[static_cast<unsigned>(homePanel_)],state,model);
    }
    case ExpeditionOpen:
        expeditionIndex_=0; expeditionSolo_=false; rollExpeditionVariant(); return navigate(Screen::Expeditions);
    case ExpeditionEnter:
        expeditionSolo_=false; return navigate(Screen::ExpeditionLobby);
    case ExpeditionSolo:
        if(expeditionSolo_ || state.phase!=Phase::Home || !expeditionReady(state,selectedExpedition(expeditionIndex_))) return {};
        expeditionSolo_=true; return {IntentKind::Navigation};
    case Back:
        if(screen_==Screen::TradeChoose) return navigate(activeTrade(model.trade) ? Screen::TradeReview : Screen::Nearby);
        if (!state.onboardingComplete) return {IntentKind::StarterBack};
        if (screen_==Screen::Capture) return navigate(Screen::Battle);
        if (screen_==Screen::Battle) return navigate(Screen::Encounter);
        if (screen_==Screen::Stats) return navigate(Screen::Collection);
        if (screen_==Screen::Collection) {
            if (memberReturn_!=Screen::Box) return navigate(Screen::Squad);
            std::uint8_t order[kCollectionCapacity];
            const auto count=sortedBox(state,model,order);
            std::size_t index=count;
            for(std::size_t i=0;i<count;++i) if(state.collection[order[i]].id==memberId_) { index=i; break; }
            boxPage_=static_cast<std::uint8_t>(std::min(index/kTiles,boxPages(state)-1));
            return navigate(Screen::Box);
        }
        if (screen_==Screen::Box) return navigate(Screen::Squad);
        if (screen_==Screen::NearbyReview) return navigate(Screen::Nearby);
        if (screen_==Screen::ReleaseReview) { releaseMember_=0; return navigate(Screen::Stats); }
        if (screen_==Screen::Evolution) { if(evolutionPage_) { evolutionPage_=0; return navigate(Screen::Evolution); } return navigate(Screen::Stats); }
        if (screen_==Screen::EvolutionReview) { cancelEvolution(); return navigate(Screen::Evolution); }
        if (screen_==Screen::EvolutionResult) return navigate(Screen::Home);
        if (screen_==Screen::ExpeditionLobby) { expeditionSolo_=false; return navigate(Screen::Expeditions); }
        if (screen_==Screen::EncounterSettings) return navigate(Screen::Settings);
        if (screen_==Screen::Sound) return navigate(Screen::Settings);
        if (screen_==Screen::ModeReview) { proposedMode_=255; return navigate(Screen::Settings); }
        return navigate(Screen::Home);
    case Care: return navigate(Screen::Care);
    case Explore: return navigate(Screen::Explore);
    case Team: boxPage_=0; memberReturn_=Screen::Squad; return navigate(Screen::Squad);
    case BoxOpen: boxPage_=0; return navigate(Screen::Box);
    case BoxSort:
        boxOrder_=static_cast<std::uint8_t>((boxOrder_+1)%4);
        boxPage_=0;
        return {IntentKind::Navigation};
    case Tile0: case Tile1: case Tile2: case Tile3: {
        const auto* member=tileMember(state,model,static_cast<std::size_t>(id-Tile0));
        if(!member) { boxPage_=0; return navigate(Screen::Box); }
        const auto index=displayIndexForMember(state,member->id);
        memberIndex_=index<state.collectionCount ? static_cast<std::uint8_t>(index) : 0;
        memberId_=member->id; memberReturn_=screen_; return navigate(Screen::Collection);
    }
    case Settings: return navigate(Screen::Settings);
    case SoundOpen: return navigate(Screen::Sound);
    case TradeOpen: {
        if(!model.nearby || nearbyIndex_>=peerCount(model.nearby) || activeTrade(model.trade)) return {};
        tradePeer_=peerIdentity(model.nearby->peers[nearbyIndex_].mac);
        const auto* peer=tradingPeer(model,tradePeer_);if(!peer) return {};
        tradePeerNonce_=peer->advertisement.nonce;tradeMemberId_=tradeCandidate(state,0,true);
        return navigate(Screen::TradeChoose);
    }
    case TradeChange:
        if(!changeTrade(model)) return {};
        tradeMemberId_=model.trade->transcript.offers[model.trade->localSide].id;
        return navigate(Screen::TradeChoose);
    case TradeSelect:
        if(!trade::canOffer(state,tradeMemberId_)) return {};
        return proposeTrade(activeTrade(model.trade) ? IntentKind::TradeOffer : IntentKind::TradeInvite,model,tradeMemberId_);
    case TradeConfirm:
        if(!confirmTrade(state,model)) return {};
        return proposeTrade(IntentKind::TradeConfirm,model);
    case TradeCancel: return proposeTrade(IntentKind::TradeCancel,model);
    case TradeClose: return proposeTrade(IntentKind::TradeClose,model);
    case Feed: return propose(state,model,Action::Feed);
    case Play: return propose(state,model,Action::Play);
    case Rest: return propose(state,model,Action::Rest);
    case Toilet: return propose(state,model,Action::Toilet);
    case Treat: return propose(state,model,Action::Treat);
    case Battle: return navigate(Screen::Battle);
    case Attack: battleSelection_=combat::Move::Physical; return {IntentKind::Navigation};
    case Heavy: battleSelection_=combat::Move::Heavy; return {IntentKind::Navigation};
    case Magic: battleSelection_=combat::Move::Magic; return {IntentKind::Navigation};
    case Capture: return navigate(Screen::Capture);
    case Auto: return propose(state,model,Action::AutoFight);
    case Retreat: return propose(state,model,Action::Retreat);
    case AutoResume: return propose(state,model,Action::AutoResume);
    case FocusSkip: return propose(state,model,Action::Focus,kFocusNoTap);
    case Again: return navigate(Screen::Home);
    case MemberNext: case MemberPrev: {
        if(state.collectionCount<2) return {};
        memberIndex_=(memberIndex_+(id==MemberNext ? 1 : state.collectionCount-1))%state.collectionCount;
        const auto* member=collectionMemberAtDisplayIndex(state,memberIndex_);
        memberId_=member ? member->id : 0; return {IntentKind::Navigation};
    }
    case MemberSelect: return propose(state,model,Action::Select,memberId_);
    case PartyToggle: return propose(state,model,isPartyMember(state,memberId_) ? Action::PartyRemove : Action::PartyAdd,memberId_);
    case MemberStats: statsPage_=0; return navigate(Screen::Stats);
    case StatsPrevious: statsPage_=(statsPage_+3)%4; return {IntentKind::Navigation};
    case StatsNext: statsPage_=(statsPage_+1)%4; return {IntentKind::Navigation};
    case ReleaseOpen:
        if (!selectedMember(state)) return {};
        releaseMember_=memberId_;
        return navigate(Screen::ReleaseReview);
    case ReleaseConfirm: {
        const auto id=releaseMember_; releaseMember_=0;
        return propose(state,model,Action::Release,id);
    }
    case EvolveOpen: evolutionIndex_=evolutionPage_=0; cancelEvolution(); return navigate(Screen::Evolution);
    case EvolutionPrevious: case EvolutionNext: {
        const auto* member=selectedMember(state);
        if (!member || !forms::outgoing(member->formId,1)) return {};
        evolutionIndex_=evolutionIndex_ ? 0 : 1; evolutionPage_=0; cancelEvolution(); return {IntentKind::Navigation};
    }
    case EvolutionDetails: evolutionPage_=evolutionPage_ ? 0 : 1; return {IntentKind::Navigation};
    case EvolutionReview: {
        const auto* member=selectedMember(state);
        const auto* edge=member ? forms::outgoing(member->formId,evolutionIndex_) : nullptr;
        if (!edge || !member) return {};
        if (!evolution_.propose(state,edge->to,member->id) || !model.writable) { evolutionPage_=3; return navigate(Screen::Evolution); }
        evolutionMember_=member->id; evolutionForm_=member->formId; evolutionTarget_=edge->to;
        return navigate(Screen::EvolutionReview);
    }
    case EvolutionConfirm: {
        State candidate;
        const auto target=evolution_.target();
        const auto memberId=evolutionMember_;
        if (!evolution_.confirm(state,candidate)) { cancelEvolution(); return navigate(Screen::Evolution); }
        const bool bench=memberId && memberId!=state.activeCreatureId;
        const auto value=bench ? ((memberId<<16)|(target&0xffffu)) : target;
        return propose(state,model,bench ? Action::EvolveMember : Action::Evolve,value);
    }
    case EvolutionDone: {
        const auto evolved=evolutionMember_ ? evolutionMember_ : state.activeCreatureId;
        const auto index=displayIndexForMember(state,evolved);
        memberIndex_=index<state.collectionCount ? static_cast<std::uint8_t>(index) : 0;
        memberId_=evolved; statsPage_=0; return navigate(Screen::Stats);
    }
    case NearbyOpen: nearbyIndex_=0; navigate(Screen::Nearby); return {IntentKind::OpenNearby};
    case NearbyPrevious: case NearbyNext:
        if (!model.nearby || peerCount(model.nearby)<2) return {};
        nearbyIndex_=static_cast<std::uint8_t>((nearbyIndex_+(id==NearbyNext ? 1 : peerCount(model.nearby)-1))%peerCount(model.nearby));
        return {IntentKind::Navigation};
    case NearbyReview: return navigate(Screen::NearbyReview);
    case NearbyTactical: case NearbyAuto:
        if (!model.nearby || model.nearby->stage!=nearby::Stage::Discovering) return {};
        nearbyMode_=id==NearbyAuto ? nearby::Mode::Auto : nearby::Mode::Tactical;
        return {IntentKind::Navigation};
    case NearbyChallenge: {
        if (!model.nearby || model.nearby->stage!=nearby::Stage::Discovering || nearbyIndex_>=peerCount(model.nearby)) return {};
        const auto& peer=model.nearby->peers[nearbyIndex_];
        if (!peer.available || !peer.compatible || activeTrade(model.trade)) return {};
        Intent result{IntentKind::NearbyChallenge,Action::Feed,nearbyIndex_};
        result.peer=peerIdentity(peer.mac); result.nearbyOpenNonce=peer.openNonce; result.nearbyMode=nearbyMode_;
        result.nearbyFighters[0]=model.nearbyLocalFighter; result.nearbyFighters[1]=peer.fighter;
        navigate(Screen::Nearby); return result;
    }
    case NearbyAccept: {
        if (!model.nearby || model.nearby->stage!=nearby::Stage::Incoming) return {};
        const auto& offer=*model.nearby; Intent result{IntentKind::NearbyAccept};
        result.peer=peerIdentity(offer.opponent); result.nearbySession=offer.session; result.nearbyMode=offer.offeredMode;
        result.nearbyFighters[0]=offer.offered[0]; result.nearbyFighters[1]=offer.offered[1]; return result;
    }
    case NearbyCancel: return {IntentKind::NearbyCancel};
    case NearbyClose: navigate(Screen::Home); return {IntentKind::CloseNearby};
    case NearbyPhysical: battleSelection_=combat::Move::Physical; return {IntentKind::Navigation};
    case NearbyMagic: battleSelection_=combat::Move::Magic; return {IntentKind::Navigation};
    case NearbyHeavy: battleSelection_=combat::Move::Heavy; return {IntentKind::Navigation};
    case NearbyCommit: {
        if (!model.nearby) return {};
        const bool attack=model.nearby->match.attacker==(model.nearby->host ? 0 : 1);
        const auto choice=attack ? (battleSelection_==combat::Move::Magic ? nearby::Choice::Magic : battleSelection_==combat::Move::Heavy ? nearby::Choice::Heavy : nearby::Choice::Physical) :
            defenseSelection_==1 ? nearby::Choice::Counter : defenseSelection_==2 ? nearby::Choice::Ward : nearby::Choice::Brace;
        if (nearbyFeedback(model) || !model.nearby || model.nearby->stage!=nearby::Stage::Playing || model.nearby->localChoicePending ||
            !nearby::legalChoice(model.nearby->match,model.nearby->host ? 0 : 1,choice)) return {};
        pending_=true; return {IntentKind::NearbyChoose,Action::Feed,static_cast<std::uint32_t>(choice)};
    }
    case NearbyBrace: case NearbyCounter: case NearbyWard: {
        const auto choice=id==NearbyBrace ? nearby::Choice::Brace : id==NearbyCounter ? nearby::Choice::Counter : nearby::Choice::Ward;
        if (nearbyFeedback(model) || !model.nearby || !nearby::legalChoice(model.nearby->match,model.nearby->host ? 0 : 1,choice)) return {};
        pending_=true; return {IntentKind::NearbyChoose,Action::Feed,static_cast<std::uint32_t>(choice)};
    }
    case Encounters: return navigate(Screen::EncounterSettings);
    case RateOff: case RateRelaxed: case RateNormal: case RateFrequent:
        return propose(state,model,Action::EncounterRate,static_cast<std::uint32_t>(id-RateOff));
    case Sleep: {
        constexpr std::uint32_t seconds[]{0,30,60,120,300};
        unsigned current=0; for(unsigned i=0;i<5;++i) if(seconds[i]==model.sleepTimeoutSeconds) current=i;
        pending_=true; resetTouch();
        return {IntentKind::SleepTimeout,Action::Feed,seconds[(current+1)%5]};
    }
    case Mute: pending_=true; resetTouch(); return {IntentKind::ToggleMute};
    case Music: pending_=true; resetTouch(); return {IntentKind::ToggleMusic};
    case Gyro: return {IntentKind::ToggleGyro};
    case Setup: return {IntentKind::OpenSetup};
    case Mode:
        proposedMode_=state.battleMode==BattleMode::Auto ? 0 : 1;
        return navigate(Screen::ModeReview);
    case ModeConfirm:
        if (proposedMode_>1) return {};
        return propose(state,model,Action::Mode,proposedMode_);
    default: return {};
    }
}
Intent Controller::touch(const State& state, const Model& model, Touch event) {
    update(state,model);
    // A release must clear the latch even while save/presentation/input gates
    // are closed. Nothing after accepted Down can undo or repeat that throw.
    if(captureContactBlocked_) {
        if(event.kind==TouchKind::Up) captureContactBlocked_=false;
        resetTouch(); return {};
    }
    if(event.kind==TouchKind::Down && screen_==Screen::Capture &&
        event.y>=80 && event.y<kNavY-kNavPad && inside(event.x,event.y)) {
        captureContactBlocked_=true;
        if(down_ || !model.inputEnabled || model.encounterRecoveryRequired || pending_ || battleLocked_ ||
            !(canCapture(state,model) || canFocus(state,model)) || event.atMs<lastAt_ ||
            (captureAcceptedAt_!=UINT64_MAX && (event.atMs<captureAcceptedAt_ || event.atMs-captureAcceptedAt_<450))) {
            resetTouch(); return {};
        }
        lastAt_=event.atMs;
        const auto value=capturering::sample(captureElapsed(event.atMs),state.wildFormId).phaseMs;
        captureAcceptedAt_=event.atMs;
        return propose(state,model,focusOpen(state) ? Action::Focus : Action::RingCapture,value);
    }
    if (event.kind==TouchKind::Cancel) { cancelTouch(); return {}; }
    if (!model.inputEnabled || model.encounterRecoveryRequired || pending_ || battleLocked_) { resetTouch(); return {}; }
    const bool inFlight = captureAction(lastAction_) && state.sequence==actionSequence_ &&
        event.atMs>=actionAt_ && event.atMs-actionAt_<550;
    if (inFlight) { resetTouch(); return {}; }
    if (screen_==Screen::Capture && event.atMs<lastAt_) { resetTouch(); return {}; }
    if (event.kind==TouchKind::Down) {
        if (down_) { resetTouch(); return {}; } // A second contact invalidates the first.
        Button choices[8]; const auto n=buttons(state,model,choices);
        const int hit=hitIndex(choices,n,event.x,event.y);
        // The padded bottom button also accepts contacts on the bezel edge just outside the circle.
        if (!inside(event.x,event.y) && (hit<0 || !choices[hit].padded)) return {};
        down_=true; cancelled_=tapMoved_=false; downX_=event.x; downY_=event.y; downAt_=lastAt_=event.atMs;
        const bool buttonOrigin=hit>=0;
        if (buttonOrigin && choices[hit].enabled) { downButton_=choices[hit].id; }
        const bool nearbyPicker=screen_==Screen::Nearby && !nearbyFeedback(model) && model.nearby && model.nearby->stage==nearby::Stage::Playing &&
            model.nearby->match.mode==nearby::Mode::Tactical && !model.nearby->localChoicePending;
        battleGesture_=!buttonOrigin && event.y>=130 && event.y<336 && (nearbyPicker ||
            (screen_==Screen::Battle && state.battleMode==BattleMode::Tactical && state.phase==Phase::Encounter));
        // Box tiles cover the page, so a swipe that starts on a tile still turns it.
        browseGesture_=(screen_==Screen::Home && event.y>=112 && event.y<282) ||
            (screen_==Screen::Box && event.y>=kTileY[0] && event.y<kTileY[1]+kTileH && boxPages(state)>1) ||
            (!buttonOrigin && event.y>=100 && event.y<280 &&
            (screen_==Screen::Starter || screen_==Screen::Collection || screen_==Screen::Stats || screen_==Screen::Sound || screen_==Screen::TradeChoose || screen_==Screen::TradeReview ||
             (screen_==Screen::Evolution && evolutionPage_!=3) || screen_==Screen::Expeditions ||
             (screen_==Screen::Nearby && model.nearby && model.nearby->stage==nearby::Stage::Discovering)));
        return {};
    }
    if (!down_ || cancelled_) return {};
    // A press that began on a button survives roll-off drift while the finger stays
    // on that button plus a margin; release there counts whatever the slop. The
    // padded bottom button's margin also reaches the bezel edge outside the circle.
    bool padHold=false;
    if (downButton_) {
        Button choices[8]; const auto n=buttons(state,model,choices);
        for (std::size_t i=0;i<n;++i) {
            const auto& b=choices[i];
            if (b.id!=downButton_ || !hits(b,event.x,event.y,b.padded ? kNavPad : kHoldPad)) continue;
            const int dx=event.x-downX_, dy=event.y-downY_;
            // Roll-off drift, not a deliberate swipe across the button.
            padHold=b.padded || (inside(event.x,event.y) && dx*dx+dy*dy<=kHoldSlop*kHoldSlop);
        }
    }
    if (event.atMs<lastAt_ || event.atMs-downAt_>10000 || (!padHold && !inside(event.x,event.y))) { resetTouch(); return {}; }
    lastAt_=event.atMs;
    if((event.x-downX_)*(event.x-downX_)+(event.y-downY_)*(event.y-downY_)>24*24) tapMoved_=true;
    if (event.kind==TouchKind::Move) {
        if (!battleGesture_ && !browseGesture_ && tapMoved_ && !padHold) resetTouch();
        return {};
    }
    if (event.kind!=TouchKind::Up) return {};
    const bool battleGesture=battleGesture_, browseGesture=browseGesture_; const int pressed=downButton_;
    down_=false; battleGesture_=browseGesture_=false; downButton_=0;
    const bool tileOrigin=pressed && screen_==Screen::Box;
    if (battleGesture || browseGesture) {
        const int dx=event.x-downX_,dy=event.y-downY_;
        const auto elapsed=event.atMs-downAt_;
        const bool swipeTiming=elapsed>=40 && elapsed<=1500;
        if (!swipeTiming && !tileOrigin) return {};
        const int ax=std::abs(dx),ay=std::abs(dy);
        // A clear axis is mandatory: diagonal or downward movement is no action.
        if (swipeTiming && ax>=40 && ax*2>=ay*3) return navigateHorizontal(dx<0,state,model);
        if (battleGesture && dy<=-40 && ay*2>=ax*3) {
            const bool heavy=battleSelection_==combat::Move::Heavy && (screen_!=Screen::Nearby ||
                (model.nearby && model.nearby->match.attacker==(model.nearby->host ? 0 : 1)));
            const auto energy=screen_==Screen::Nearby && model.nearby ? model.nearby->match.energy[model.nearby->host ? 0 : 1] : state.energy;
            if(heavy && energy<6) { notice("HEAVY NEEDS 6 ENERGY"); return {IntentKind::Navigation}; }
            if (screen_==Screen::Nearby) return activate(NearbyCommit,state,model);
            const auto action=battleSelection_==combat::Move::Magic ? Action::Magic :
                battleSelection_==combat::Move::Heavy ? Action::Heavy : Action::Attack;
            return propose(state,model,action);
        }
        // A Box tile that was not swiped stays a tile tap, even inside an edge-arrow strip.
        if (!tileOrigin) {
            const auto direction=horizontalTap(downX_,downY_,battleGesture);
            if(!tapMoved_ && direction && direction==horizontalTap(event.x,event.y,battleGesture))
                return navigateHorizontal(direction>0,state,model);
            return {};
        }
    }
    if (!pressed || event.atMs-downAt_<20 || event.atMs-downAt_>1800 ||
        (!padHold && (event.x-downX_)*(event.x-downX_)+(event.y-downY_)*(event.y-downY_)>24*24)) return {};
    Button choices[8]; const auto n=buttons(state,model,choices);
    for (std::size_t i=0;i<n;++i) {
        const auto& b=choices[i];
        if (b.id==pressed && b.enabled && hits(b,event.x,event.y,b.padded ? kNavPad : kHoldPad))
            return activate(pressed,state,model);
    }
    return {};
}

ArtRequest Controller::artRequest(const State& state,const Model& model,std::uint64_t now) const {
    if(model.encounterRecoveryRequired) return {};
    constexpr const char* scenes[]{"scene-meadow-412-v1","scene-forest-412-v1","scene-beach-412-v1","scene-ruins-412-v1",
        "scene-cavern-412-v1","scene-snow-412-v1","scene-volcanic-412-v1","scene-digital-412-v1"};
    ArtRequest request;
    request.sceneId=scenes[0]; request.elapsedMs=now;
    const auto* member=activeMember(state);
    switch(screen_) {
    case Screen::Starter: case Screen::StarterReview:
        request.formId=model.starterFormId ? model.starterFormId : forms::initialForm(combat::starterSpecies(model.selectedId)); break;
    case Screen::Home: case Screen::Care: case Screen::Explore:
        request.formId=member ? member->formId : 0; break;
    case Screen::Encounter: case Screen::Battle: case Screen::Capture:
        request.formId=model.battle && model.battle->locked ? model.battle->enemyFormId : state.wildFormId;
        request.animation=battleAnimation(model.battle,battlepresentation::Actor::Opponent);
        if(model.battle && model.battle->locked && model.battle->capturePresentation && model.battle->captureCaught && model.battle->captureElapsedMs>=2300) request.animation=sprite::Animation::Celebrate;
        request.sceneId=scenes[(state.encounters ? state.encounters-1 : 0)%8]; break;
    case Screen::Result:
        request.formId=state.message==Message::Captured ? capturedFormId(state) : member ? member->formId : 0;
        request.sceneId=scenes[(state.encounters ? state.encounters-1 : 0)%8];
        request.animation=sprite::Animation::Celebrate; break;
    case Screen::Evolution: {
        const auto* evolving=selectedMember(state);
        const auto* route=evolving ? forms::outgoing(evolving->formId,evolutionIndex_) : nullptr;
        request.formId=route ? route->to : evolving ? evolving->formId : 0; break;
    }
    case Screen::EvolutionReview:
        request.formId=evolutionTarget_ ? evolutionTarget_ : evolutionForm_; break;
    case Screen::EvolutionResult: {
        const auto* evolved=findMember(state,evolutionMember_);
        request.formId=evolved ? evolved->formId : evolutionTarget_;
        request.animation=sprite::Animation::Celebrate; break;
    }
    case Screen::Expeditions: case Screen::ExpeditionLobby: {
        const auto theme=selectedExpedition(expeditionIndex_).theme;
        request.sceneId=theme==expeditions::Theme::Tide ? scenes[2] : theme==expeditions::Theme::Ember ? scenes[6] : scenes[1];
        if(screen_==Screen::ExpeditionLobby) {
            request.formId=member ? member->formId : 0;
            if(expeditionSolo_) request.animation=sprite::Animation::Celebrate;
        }
        break;
    }
    case Screen::ReleaseReview: case Screen::Stats: case Screen::Collection:
        request.formId=selectedMember(state) ? selectedMember(state)->formId : 0; break;
    case Screen::Squad: case Screen::Box: break; // Each tile has its own request.
    case Screen::TradeChoose: {
        const auto* offered=findMember(state,tradeMemberId_);request.formId=offered ? offered->formId : 0;
        request.sceneId=scenes[7];break;
    }
    case Screen::TradeReview:
        if(model.trade && model.trade->localSide<2 && trade::valid(model.trade->transcript))
            request.formId=model.trade->transcript.offers[1-model.trade->localSide].formId;
        request.sceneId=scenes[7];break;
    case Screen::Nearby: case Screen::NearbyReview:
        if (model.nearby) {
            const auto& n=*model.nearby;
            if (n.stage==nearby::Stage::Discovering && nearbyIndex_<peerCount(&n)) request.formId=n.peers[nearbyIndex_].fighter.formId;
            else if (n.stage==nearby::Stage::Incoming || n.stage==nearby::Stage::Outgoing || n.stage==nearby::Stage::Accepting) request.formId=n.offered[n.host ? 1 : 0].formId;
            else if (n.stage==nearby::Stage::Playing || n.stage==nearby::Stage::Reconnecting || n.stage==nearby::Stage::Finished) request.formId=n.match.fighters[n.host ? 1 : 0].formId;
        }
        request.sceneId=scenes[7]; break;
    case Screen::EncounterSettings: case Screen::Settings: case Screen::ModeReview: case Screen::Sound:
        request.sceneId=scenes[7]; break;
    case Screen::Egg: break;
    }
    const bool recent=state.sequence==actionSequence_ && now>=actionAt_ && now-actionAt_<550;
    if (recent) {
        request.elapsedMs=now-actionAt_;
        if (screen_==Screen::Home || screen_==Screen::Care) {
            if (lastAction_==Action::Rest) request.animation=sprite::Animation::Sleep;
            else if (lastAction_==Action::Feed || lastAction_==Action::Play) request.animation=sprite::Animation::Care;
        } else if (screen_==Screen::Battle && !(model.battle && model.battle->locked) &&
                   (lastAction_==Action::Attack || lastAction_==Action::Heavy || lastAction_==Action::Magic)) {
            request.animation=sprite::Animation::Hurt;
        }
    }
    return request;
}

ArtRequest Controller::partnerArtRequest(const State& state,const Model& model,std::uint64_t now) const {
    if(model.encounterRecoveryRequired) return {};
    ArtRequest request; request.elapsedMs=now;
    if(screen_==Screen::Battle) {
        const auto* member=activeMember(state);
        const auto* view=model.battle && model.battle->locked ? model.battle : nullptr;
        request.formId=view ? view->playerFormId : member ? member->formId : 0;
        request.animation=battleAnimation(view,battlepresentation::Actor::Player);
    } else if(screen_==Screen::Nearby && model.nearby) {
        const auto& view=*model.nearby;
        if(view.stage==nearby::Stage::Playing || view.stage==nearby::Stage::Reconnecting || view.stage==nearby::Stage::Finished)
            request.formId=view.match.fighters[view.host ? 0 : 1].formId;
    } else if(screen_==Screen::TradeReview && model.trade && model.trade->localSide<2 && trade::valid(model.trade->transcript)) {
        request.formId=model.trade->transcript.offers[model.trade->localSide].formId;
    }
    return request;
}

std::size_t Controller::sortedBox(const State& state,const Model& model,std::uint8_t* order) const {
    const auto count=std::min<std::size_t>(state.collectionCount,kCollectionCapacity);
    for(std::size_t i=0;i<count;++i) order[i]=static_cast<std::uint8_t>(i);
    bool ready[kCollectionCapacity]{};
    if(boxOrder_==2) for(std::size_t i=0;i<count;++i) ready[i]=anyRouteReady(state,model,state.collection[i]);
    const auto newer=[&](std::size_t a,std::size_t b) {
        const auto& ma=state.collection[a]; const auto& mb=state.collection[b];
        if(ma.capturedAtSequence!=mb.capturedAtSequence) return ma.capturedAtSequence>mb.capturedAtSequence;
        return ma.id<mb.id;
    };
    const auto before=[&](std::size_t a,std::size_t b) {
        if(boxOrder_==1) {
            if(state.collection[a].level!=state.collection[b].level) return state.collection[a].level>state.collection[b].level;
            return state.collection[a].id<state.collection[b].id;
        }
        if(boxOrder_==2 && ready[a]!=ready[b]) return ready[a];
        if(boxOrder_==3) {
            const auto ah=isInjured(state.collection[a]), bh=isInjured(state.collection[b]);
            if(ah!=bh) return ah;
        }
        return newer(a,b);
    };
    for(std::size_t i=1;i<count;++i) {
        const auto key=order[i];
        std::size_t j=i;
        while(j>0 && before(key,order[j-1])) { order[j]=order[j-1]; --j; }
        order[j]=key;
    }
    return count;
}
const CreatureMember* Controller::tileMember(const State& state,const Model& model,std::size_t tile) const {
    if(tile>=kTiles) return nullptr;
    if(screen_==Screen::Squad) {
        if(!tile) return activeMember(state);
        const auto id=state.partyMemberIds[tile-1];
        return id ? findMember(state,id) : nullptr;
    }
    if(screen_==Screen::Box) {
        std::uint8_t order[kCollectionCapacity];
        const auto count=sortedBox(state,model,order);
        const auto slot=static_cast<std::size_t>(boxPage_)*kTiles+tile;
        if(slot>=count) return nullptr;
        return &state.collection[order[slot]];
    }
    return nullptr;
}

ArtRequest Controller::tileArtRequest(const State& state,const Model& model,std::size_t tile,std::uint64_t now) const {
    if(model.encounterRecoveryRequired) return {};
    ArtRequest request; request.elapsedMs=now;
    const auto* member=tileMember(state,model,tile);
    request.formId=member ? member->formId : 0;
    return request;
}

bool Controller::captureAnimating(const State& state,const Model& model) const {
    return screen_==Screen::Capture && downButton_==0 && !pending_ && !battleLocked_ && !notice_[0] && (canCapture(state,model) || canFocus(state,model));
}
bool Controller::renderCaptureRegion(const State& state,const Model& model,std::uint16_t* pixels,
                                     std::size_t capacity,std::uint64_t now) const {
    if(!pixels || capacity<kPixels || !captureAnimating(state,model) ||
        (captureAction(lastAction_) && state.sequence==actionSequence_ && now>=actionAt_ && now-actionAt_<550)) return false;
    const auto art=artRequest(state,model,now);
    Canvas c{pixels,kCaptureX,kCaptureY,kCaptureX+kCaptureWidth,kCaptureY+kCaptureHeight};
    if(!c.scene(model.artwork,art)) c.circle(206,206,204,bg);
    captureScene(c,state,model,art,capturering::sample(captureElapsed(now),state.wildFormId));
    return true;
}
bool Controller::render(const State& state, const Model& model, std::uint16_t* pixels,
                        std::size_t capacity, std::uint64_t now) const {
    if (!pixels || capacity<kPixels) return false;
    std::fill(pixels,pixels+kPixels,0);
    const bool recent = state.sequence==actionSequence_ && now>=actionAt_ && now-actionAt_<550;
    const auto art=artRequest(state,model,now);
    Canvas c{pixels};
    if(model.encounterRecoveryRequired) {
        c.circle(206,206,204,bg);
        c.badge(144,"SAVE RECOVERY",2,amber);
        c.center(197,"RESTART TO CHECK SAVED STATE",1,ink,32);
        c.center(227,"YOUR PARTNERS HAVE NOT BEEN RESET",1,dim,36);
        return true;
    }
    if(!c.scene(model.artwork,art)) c.circle(206,206,204,bg);
    c.circle(206,206,197,edge,false); c.circle(206,206,192,panel,false);
    auto actor=[&](int x,int y,int scale) {
        const bool supplied=c.spriteFrame(x,y,scale*16,model.artwork.sprite,art);
        if(!supplied) c.missingArt(x,y);
        return supplied;
    };
    c.center(29,"DIGIVICE",2,mint);
    c.rect(129,50,154,2,edge);
    const int tx=model.gyroEnabled ? std::clamp<int>(model.tiltX,-8,8) : 0;
    const int ty=model.gyroEnabled ? std::clamp<int>(model.tiltY,-8,8) : 0;
    // Opaque information plates preserve a vivid scene without asking small
    // text to compete with its detail. Creature stages stay open to the scene.
    const auto plate=[&](int x,int y,int w,int h) { c.rect(x,y,w,h,panel); };
    switch(screen_) {
    case Screen::Egg: plate(62,66,288,30); plate(62,214,288,20); break;
    case Screen::Starter: case Screen::StarterReview: break;
    case Screen::Home: break;
    case Screen::Care: plate(62,61,288,51); plate(62,199,288,30); break;
    case Screen::Explore: plate(62,66,288,32); plate(52,168,308,92); break;
    case Screen::Collection: break;
    case Screen::Squad: case Screen::Box: break; // Tiles are opaque plates.
    case Screen::Stats: plate(50,60,312,169); break;
    case Screen::Evolution:
        if(evolutionPage_) plate(50,61,312,218);
        break;
    case Screen::EvolutionReview: break;
    case Screen::ReleaseReview: plate(50,62,312,209); break;
    case Screen::EvolutionResult: break;
    case Screen::Encounter: break;
    case Screen::Battle: break;
    case Screen::Capture: plate(62,49,288,28); break;
    case Screen::Result: plate(62,69,288,33); plate(62,228,288,28); break;
    case Screen::Settings: plate(62,63,288,49); break;
    case Screen::Sound: plate(62,63,288,165); break;
    case Screen::TradeChoose: break;
    case Screen::TradeReview: if(tradePage_) plate(50,61,312,218); break;
    case Screen::EncounterSettings: plate(50,66,312,163); break;
    case Screen::ModeReview: plate(50,67,312,170); break;
    case Screen::NearbyReview: plate(50,67,312,204); break;
    case Screen::Nearby:
        if(!model.nearby || (model.nearby->stage!=nearby::Stage::Playing && model.nearby->stage!=nearby::Stage::Finished && model.nearby->stage!=nearby::Stage::Reconnecting && model.nearby->stage!=nearby::Stage::Discovering)) plate(50,90,312,180);
        break;
    case Screen::Expeditions: case Screen::ExpeditionLobby: plate(48,58,316,200); break;
    }
    const bool browse=screen_==Screen::Starter || screen_==Screen::Collection || screen_==Screen::Stats || screen_==Screen::Sound ||
        (screen_==Screen::Box && boxPages(state)>1) ||
        (screen_==Screen::TradeChoose && tradeMemberId_ && tradeCandidate(state,tradeMemberId_,true)!=tradeMemberId_) || (screen_==Screen::TradeReview && model.trade && trade::valid(model.trade->transcript)) ||
        (screen_==Screen::Evolution && evolutionPage_!=3) || screen_==Screen::Expeditions ||
        (screen_==Screen::Nearby && model.nearby && model.nearby->stage==nearby::Stage::Discovering && peerCount(model.nearby)>1);
    const bool picker=!battleLocked_ && ((screen_==Screen::Battle && state.battleMode==BattleMode::Tactical) ||
        (screen_==Screen::Nearby && model.nearby && model.nearby->stage==nearby::Stage::Playing && model.nearby->match.mode==nearby::Mode::Tactical && !model.nearby->localChoicePending && !nearbyFeedback(model)));
    const bool fullRosterBattle=screen_==Screen::Battle && !(model.battle && model.battle->locked) &&
        state.collectionCount>=kCollectionCapacity;
    const std::uint8_t hint=browse ? 1 : picker ? 2 : 0;
    if(hint!=hintContext_) { hintSeen_|=hintContext_; hintContext_=hint; hintAt_=(hint && !(hintSeen_&hint)) ? now : UINT64_MAX; }
    if(hint && hintAt_!=UINT64_MAX && now>=hintAt_ && now-hintAt_>=7000) { hintSeen_|=hint; hintAt_=UINT64_MAX; }
    const auto hintAge=hintAt_!=UINT64_MAX && now>=hintAt_ ? now-hintAt_ : UINT64_MAX;
    const auto pulse=static_cast<int>((now/40)%40); const int glow=pulse<20 ? pulse : 40-pulse;
    const auto arrowColor=rgb(104+glow*3,151+glow*3,143+glow*3);
    const auto carousel=[&](unsigned selected,bool defense,const char* skill) {
        const char* attacks[]{"PHYSICAL","HEAVY","MAGIC"}; const char* guards[]{"BRACE","COUNTER","WARD"};
        const auto* labels=defense ? guards : attacks;
        c.circle(206,279,32,panel); c.circle(206,279,32,edge,false);
        c.icon(206,278,selected,defense,2,defense ? mint : amber);
        c.icon(111,278,(selected+2)%3,defense,1,dim); c.icon(301,266,(selected+1)%3,defense,1,dim);
        c.chevron(147,278,-1,arrowColor,5); c.chevron(259,278,1,arrowColor,5);
        c.badge(315,labels[selected],2,ink);
        if(skill) c.badge(239,skill,std::strlen(skill)<=24 ? 2 : 1,ink,42);
    };
    char label[48]{};
    switch(screen_) {
    case Screen::Egg:
        c.center(74,"SOMETHING IS STIRRING",2);
        c.egg(206+tx,166+ty,static_cast<int>(now/600)%2);
        c.center(222,"CHOOSE YOUR FIRST DIGIMON",1,dim);
        break;
    case Screen::Starter: case Screen::StarterReview: {
        const auto count=model.starterCount==11 ? 11u : 8u;
        const auto id=model.selectedId>=1 && model.selectedId<=count ? model.selectedId : 1;
        const auto* offer=model.starterFormId ? forms::find(model.starterFormId) : nullptr;
        c.badge(69,screen_==Screen::Starter ? "CHOOSE A PARTNER" : "READY TO HATCH?",2);
        const bool supplied=actor(206+tx,(screen_==Screen::Starter ? 190 : 170)+ty,screen_==Screen::Starter ? 9 : 6);
        c.badge(98,offer ? offer->name : combat::starterName(id),2,mint);
        if(screen_==Screen::Starter) {
            std::snprintf(label,sizeof(label),supplied ? "%u / %u" : "%u / %u - ART MISSING",id,count); c.badge(266,label,1,dim);
        } else { c.badge(228,"THIS SAVES YOUR CHOICE",1,amber); }
        if(model.starterStage==onboarding::Stage::AwaitingCommit) c.center(316,"SAVING...",2,amber);
        break;
    }
    case Screen::Home: {
        c.badge(60,homeTitles[static_cast<unsigned>(homePanel_)],2,mint);
        c.badge(84,creatureName(state),std::strlen(creatureName(state))<=24 ? 2 : 1,ink,36);
        if(!c.spriteFrame(206+tx/2,196+ty/2,176,model.artwork.sprite,art))
            c.missingArt(206+tx/2,196+ty/2);
        for(unsigned i=0;i<kHomePanelCount;++i) c.circle(206+(static_cast<int>(i)-2)*16,354,4,
            i==static_cast<unsigned>(homePanel_) ? mint : edge);
        std::snprintf(label,sizeof(label),"STEPS %llu",static_cast<unsigned long long>(model.lifetimeSteps));
        c.badge(369,label,1,dim,27);
        char keys[24]{};
        const char* status=model.stepStatus && std::strcmp(model.stepStatus,"STEP SAVE RECOVERY")==0 ? "STEP SAVE RECOVERY" :
            model.stepsRecovering ? "SENSOR RECOVERING" : !model.stepsAvailable ? "SENSOR UNAVAILABLE" :
            state.encounterRate==EncounterRate::Off ? "ENCOUNTERS PAUSED" : "LIFETIME TOTAL";
        if((homePanel_==HomePanel::Nearby || homePanel_==HomePanel::Dungeons) && std::strcmp(status,"LIFETIME TOTAL")==0) {
            std::snprintf(keys,sizeof(keys),"DUNGEON KEYS %u",static_cast<unsigned>(state.dungeonKeys));
            status=keys;
        }
        c.badge(384,status,1,(homePanel_==HomePanel::Nearby || homePanel_==HomePanel::Dungeons) && status==keys ? mint : dim,24);
        break;
    }
    case Screen::Care: {
        c.center(68,creatureName(state),2,mint);
        std::snprintf(label,sizeof(label),"LV %u   HP %u/%u",static_cast<unsigned>(state.level),static_cast<unsigned>(state.hp),static_cast<unsigned>(maxHp(state)));
        c.center(92,label,2,dim);
        actor(206+tx,158+ty,5);
        const auto* partner=activeMember(state);
        const auto nextXp=partner && state.level<kMaxLevel ? xpForLevel(state.level+1)-partner->xp : 0;
        std::snprintf(label,sizeof(label),"XP %u  NEXT +%u",static_cast<unsigned>(partner?partner->xp:0),static_cast<unsigned>(nextXp));
        c.center(186,label,1,dim,36);
        const auto toilet=partner?toiletNeed(*partner):0;
        const auto missed=partner && careWasMissed(*partner);
        const auto mistakes=partner?careMistakes(*partner):0;
        if (partner && isInjured(*partner)) {
            std::snprintf(label,sizeof(label),injuryLevel(*partner)>=3?"HURT - TREAT NOW  MISTAKES %u":"HURT - TREAT  MISTAKES %u",static_cast<unsigned>(mistakes));
            c.center(200,label,1,amber,36);
        } else {
            if (mistakes) std::snprintf(label,sizeof(label),missed?"TOILET %u  MISSED  MISTAKES %u":toilet>=25?"TOILET %u  NEEDED  MISTAKES %u":"TOILET %u  MISTAKES %u",static_cast<unsigned>(toilet),static_cast<unsigned>(mistakes));
            else std::snprintf(label,sizeof(label),missed?"TOILET %u  MISSED CARE":toilet>=25?"TOILET %u  NEEDED":"TOILET %u",static_cast<unsigned>(toilet));
            c.center(200,label,1,missed||toilet>=25?amber:dim,36);
        }
        std::snprintf(label,sizeof(label),"FOOD %u   MOOD %u   EN %u",static_cast<unsigned>(state.fullness),static_cast<unsigned>(state.mood),static_cast<unsigned>(state.energy));
        c.center(214,label,1,dim,40);
        if (partner && anyRouteReady(state,model,*partner)) c.badge(228,"DIGIVOLUTION REQUIREMENTS MET",1,mint,36);
        break;
    }
    case Screen::Explore:
        c.center(75,"EXPLORE TOGETHER",2);
        actor(206+tx,133+ty,4);
        std::snprintf(label,sizeof(label),"%llu TOTAL STEPS",static_cast<unsigned long long>(model.lifetimeSteps)); c.center(174,label,2,mint,28);
        std::snprintf(label,sizeof(label),"THIS SESSION %u",static_cast<unsigned>(model.sessionSteps)); c.center(198,label,1,dim);
        c.center(218,model.stepStatus ? model.stepStatus : model.stepsAvailable ? "PEDOMETER READY" : "PEDOMETER UNAVAILABLE",1,dim,40);
        c.center(240,state.encounterRate==EncounterRate::Off ? "ENCOUNTERS PAUSED" :
            model.encounterReady ? "WALK TO MEET DIGIMON" : "SENSOR NOT READY",2,amber,40); break;
    case Screen::Encounter: {
        c.badge(68,wildName(state),2,amber);
        std::snprintf(label,sizeof(label),"WILD LV %u   HP %u/%u",static_cast<unsigned>(state.wildLevel),static_cast<unsigned>(state.wildHp),static_cast<unsigned>(state.wildMaxHp));
        c.badge(93,label,1,dim); c.bar(140,109,132,state.wildHp,state.wildMaxHp,amber);
        actor(206+tx,180+ty,8);
        c.badge(324,"LOWER HP TO HALF TO CAPTURE",1,dim,36); break;
    }
    case Screen::Battle: {
        const auto* playback=model.battle && model.battle->locked ? model.battle : nullptr;
        if(playback && playback->capturePresentation) {
            const auto elapsed=playback->captureElapsedMs;
            const auto* target=forms::find(playback->enemyFormId);
            const bool revealed=elapsed>=(playback->captureMiss ? 500u : 2300u);
            c.badge(65,target ? target->name : "WILD DIGIMON",2,amber,25);
            std::snprintf(label,sizeof(label),"THROW %u / 3",static_cast<unsigned>(playback->captureAttempt)); c.badge(91,label,1,dim);
            if(elapsed>=500 && !playback->captureMiss && playback->captureChance) {
                std::snprintf(label,sizeof(label),"THAT THROW: %u%%",static_cast<unsigned>(playback->captureChance)); c.badge(113,label,1,dim);
            }
            if(elapsed<500 || playback->captureMiss || revealed) {
                actor(206,191,8);
            }
            if(elapsed<500) {
                const int t=static_cast<int>(elapsed);
                const int x=206+(playback->captureMiss ? 90*t/500 : 0),y=303-113*t/500-4*28*t*(500-t)/(500*500);
                c.circle(x,y,24,mint);c.circle(x,y,20,ink,false);c.rect(x-9,y-2,18,4,panel);
                c.badge(280,"THROWING...",2,mint);
            } else if(!revealed) {
                const auto beat=(elapsed-500)/600;
                const auto phase=(elapsed-500)%600;
                const int wobble=phase<150 ? static_cast<int>(phase)*8/150 : phase<450 ? 8-static_cast<int>(phase-150)*16/300 : -8+static_cast<int>(phase-450)*8/150;
                const int x=206+wobble;
                c.shadow(x,227,33);c.circle(x,192,34,mint);c.circle(x,192,28,ink,false);c.rect(x-32,190,64,5,panel);c.circle(x,192,8,ink);c.circle(x,192,4,panel);
                c.circle(x,192,44+static_cast<int>(phase/100),edge,false);
                for(unsigned i=0;i<3;++i)c.circle(182+static_cast<int>(i)*24,267,4,i<=beat ? mint : edge);
            } else {
                c.badge(268,playback->captureMiss ? "MISS" : playback->captureCaught ? "NEW DIGIMON!" : "BROKE FREE",2,playback->captureCaught ? mint : amber);
                if(playback->captureMiss)c.badge(295,"NO CAPTURE ROLL",1,dim);
                else if(playback->captureCaught)c.badge(295,"PARTNER SAVED",1,dim);
            }
            if(!playback->captureCaught || !revealed) {
                std::snprintf(label,sizeof(label),playback->captureRemaining ? "%u THROWS REMAIN" : "THE WILD DIGIMON LEAVES",static_cast<unsigned>(playback->captureRemaining));
                // A terminal result is not disclosed until the cinematic reveal.
                if(playback->captureRemaining || revealed)c.badge(322,label,1,dim,36);
            }
            break;
        }
        const auto* member=activeMember(state);
        const auto playerForm=playback ? playback->playerFormId : member ? member->formId : 0;
        const auto enemyForm=playback ? playback->enemyFormId : state.wildFormId;
        const auto* player=forms::find(playerForm); const auto* enemy=forms::find(enemyForm);
        const auto playerHp=playback ? playback->playerHp : state.hp;
        const auto enemyHp=playback ? playback->enemyHp : state.wildHp;
        const auto playerMaximum=playback ? playback->playerMaxHp : maxHp(state);
        const auto enemyMaximum=playback ? playback->enemyMaxHp : state.wildMaxHp;
        if(fullRosterBattle) {
            std::snprintf(label,sizeof(label),"%s - DIGIMON %u/%u FULL",state.battleMode==BattleMode::Auto ? "AUTO" : "TACTICAL",
                static_cast<unsigned>(state.collectionCount),static_cast<unsigned>(kCollectionCapacity));
            c.badge(65,label,1,amber);
        } else c.badge(65,state.battleMode==BattleMode::Auto ? "AUTO BATTLE" : "TACTICAL BATTLE",1,mint);
        auto sideText=[&](int x,int y,const char* value,std::uint16_t color) {
            const auto length=std::min<std::size_t>(value ? std::strlen(value) : 0,22);
            c.rect(x-static_cast<int>(length)*3-4,y-3,static_cast<int>(length)*6+8,13,panel);
            c.text(x-static_cast<int>(length)*3,y,value,1,color,22);
        };
        sideText(118,91,player ? player->name : "PARTNER",mint); sideText(294,91,enemy ? enemy->name : "WILD",amber);
        std::snprintf(label,sizeof(label),"HP %u/%u",static_cast<unsigned>(playerHp),static_cast<unsigned>(playerMaximum)); sideText(118,106,label,dim);
        std::snprintf(label,sizeof(label),"HP %u/%u",static_cast<unsigned>(enemyHp),static_cast<unsigned>(enemyMaximum)); sideText(294,106,label,dim);
        const bool incoming=playback && (playback->actor==battlepresentation::Actor::Opponent || playback->reflected);
        const int hitX=incoming ? 118 : 294;
        if (playback && playback->flash) c.circle(hitX,176,58,ink);
        const auto playerRequest=partnerArtRequest(state,model,now);
        if(!c.spriteFrame(118,185,112,model.partnerArtwork,playerRequest,model.partnerArtwork.nativeFacing==SpriteFacing::Left)) c.missingArt(118,185);
        if(!c.spriteFrame(294,185,112,model.artwork.sprite,art,model.artwork.sprite.nativeFacing==SpriteFacing::Right)) c.missingArt(294,185);
        c.bar(64,120,108,playerHp,playerMaximum,mint); c.bar(240,120,108,enemyHp,enemyMaximum,amber);
        if (playback) {
            if (playback->actor!=battlepresentation::Actor::None && playback->phase!=battlepresentation::Phase::Summary) {
                const int travel=std::min<std::uint32_t>(1000,playback->progressPermille)*176/1000;
                c.circle(incoming ? 294-travel : 118+travel,176,playback->move==autobattle::Move::Magic ? 8 : 5,playback->move==autobattle::Move::Magic ? amber : mint);
            }
            if (playback->phase==battlepresentation::Phase::Summary) {
                c.badge(267,autobattle::outcomeName(playback->outcome),2,mint,30);
                c.badge(298,"RESULT SAVED",1,dim);
            } else {
                c.badge(247,playback->actorName ? playback->actorName : "",1,dim,42);
                c.badge(270,playback->moveName ? playback->moveName : "",playback->moveName && std::strlen(playback->moveName)<=24 ? 2 : 1,ink,42);
                if(playback->damage && playback->progressPermille>=292) {
                    std::snprintf(label,sizeof(label),"-%u",static_cast<unsigned>(playback->damage));
                    const int y=143-static_cast<int>(std::min<std::uint32_t>(1000,playback->progressPermille))*10/1000;
                    const int x=hitX-static_cast<int>(std::strlen(label))*6;
                    c.rect(x-4,y-3,static_cast<int>(std::strlen(label))*12+8,20,panel);c.text(x,y,label,2,amber);
                }
                if(playback->aimMiss || playback->reflected || playback->paused)
                    c.badge(307,playback->aimMiss ? "MISSED THE RING" : playback->reflected ? "COUNTER REFLECTED" : "BATTLE PAUSED",1,amber,36);
            }
        } else {
            const auto profile=member ? combat::formProfile(member->formId,member->level) : combat::Profile{};
            const auto selected=battleSelection_==combat::Move::Physical ? 0u : battleSelection_==combat::Move::Heavy ? 1u : 2u;
            const auto* skill=selected==2 ? profile.magicSkill : selected==1 ? profile.heavySkill : profile.physicalSkill;
            if(state.battleMode==BattleMode::Tactical) carousel(selected,false,skill);
            const auto guard=wildGuard(state);
            const auto* guardName=guard==combat::Defense::Brace ? "BRACE" : guard==combat::Defense::Counter ? "COUNTER" : guard==combat::Defense::Ward ? "WARD" : "NONE";
            std::snprintf(label,sizeof(label),"%s  EN %u",guardName,static_cast<unsigned>(state.energy)); c.badge(79,label,1,dim,42);
            if (state.lastCritical) c.badge(307,"CRITICAL HIT",2,amber,36);
        }
        break;
    }
    case Screen::Capture:
        captureScene(c,state,model,art,capturering::sample(captureElapsed(now),state.wildFormId));
        break;
    case Screen::Result: {
        const bool merged=state.message==Message::Captured && state.lastCapture.result==CaptureResult::Captured &&
            state.lastCapture.sequence==state.sequence && !captureJoinedCollection(state);
        const auto* caught=merged ? forms::find(state.lastCapture.targetFormId) : nullptr;
        c.center(78,caught && caught->name ? caught->name : shortMessage(state.message),2,mint);
        if (merged) c.center(100,"BONUS XP MERGED",1,amber);
        else if (state.lastCritical) c.center(100,"CRITICAL HIT",2,amber);
        actor(206+tx,166+ty,5);
        std::snprintf(label,sizeof(label),"LEVEL %u   DIGIMON %u/%u",static_cast<unsigned>(state.level),static_cast<unsigned>(state.collectionCount),static_cast<unsigned>(kCollectionCapacity));
        c.center(235,label,1,dim); break;
    }
    case Screen::Collection:
        if(selectedMember(state)) {
            const auto& member=*selectedMember(state);
            c.badge(60,memberName(member),std::strlen(memberName(member))<=24 ? 2 : 1,mint,36);
            const auto maximum=forms::stats(member.formId,member.level).maxHp;
            constexpr int barX=128, barW=108;
            c.bar(barX,86,barW,member.hp,maximum,isInjured(member)?red:mint);
            std::snprintf(label,sizeof(label),"%u/%u",static_cast<unsigned>(member.hp),static_cast<unsigned>(maximum));
            c.text(barX+barW+8,86,label,1,dim,8);
            constexpr int heartW=9, heartGap=8, heartCount=4;
            const auto filled=std::min<std::uint32_t>(4,member.bond/50);
            int hx=(kSize-(heartCount*heartW+(heartCount-1)*heartGap))/2;
            for(int i=0;i<heartCount;++i) {
                c.heart(hx,98,static_cast<std::uint32_t>(i)<filled,static_cast<std::uint32_t>(i)<filled?red:dim);
                hx+=heartW+heartGap;
            }
            const auto* first=forms::outgoing(member.formId,0);
            const auto* second=forms::outgoing(member.formId,1);
            if(first && second) std::snprintf(label,sizeof(label),"CARE %u/%u",static_cast<unsigned>(careMistakes(member)),static_cast<unsigned>(cleanRouteMistakeLimit(first->to)));
            else std::snprintf(label,sizeof(label),"CARE %u",static_cast<unsigned>(careMistakes(member)));
            const auto hurtLevel=injuryLevel(member);
            const char* hurt=hurtLevel==1?"HURT":hurtLevel==2?"WORSE":hurtLevel==3?"NEGLECTED":nullptr;
            const auto careN=std::strlen(label), hurtN=hurt?std::strlen(hurt):0;
            const auto total=careN+(hurt?hurtN+2:0);
            const int textX=(kSize-static_cast<int>(total)*6+1)/2;
            c.text(textX,112,label,1,dim,16);
            if(hurt) c.text(textX+static_cast<int>(careN+2)*6,112,hurt,1,hurtLevel==1?amber:red,12);
            actor(206+tx,168+ty,4); // Stays above DETAILS; gyro tilt stays clear of the buttons.
        }
        break;
    case Screen::Squad: case Screen::Box: {
        const bool box=screen_==Screen::Box;
        if(box) std::snprintf(label,sizeof(label),"ALL DIGIMON %u",static_cast<unsigned>(state.collectionCount));
        else std::snprintf(label,sizeof(label),"SQUAD %u/%u",static_cast<unsigned>(partyCount(state)),static_cast<unsigned>(kPartyCapacity));
        c.badge(60,label,2,mint);
        for(std::size_t i=0;i<kTiles;++i) {
            const auto* member=tileMember(state,model,i);
            if(!member && (box || !i)) continue;
            const int x=kTileX[i%2], y=kTileY[i/2];
            const bool pressed=down_ && downButton_==Tile0+static_cast<int>(i);
            const bool active=member && member->id==state.activeCreatureId;
            c.rect(x,y,kTileW,kTileH,active ? mint : edge);
            c.rect(x+2,y+2,kTileW-4,kTileH-4,pressed ? edge : panel);
            if(!member) {
                const auto color=state.collectionCount>1 ? dim : edge;
                c.text(x+kTileW/2-8,y+20,"+",3,color);
                c.text(x+(kTileW-71)/2,y+58,"ADD TO SQUAD",1,color);
                continue;
            }
            c.text(x+6,y+6,memberName(*member),1,active ? mint : ink,20);
            const auto request=tileArtRequest(state,model,i,now);
            if(!c.spriteFrame(x+34,y+52,56,model.tileArtwork[i],request)) c.text(x+27,y+38,"?",3,dim);
            std::snprintf(label,sizeof(label),"LV %u",static_cast<unsigned>(member->level));
            c.text(x+64,y+22,label,2,ink,5); // "LV 50" is 58 px, inside the 124 px tile face.
            std::uint16_t statusColor=dim;
            const char* status=tileStatus(state,model,*member,statusColor);
            c.text(x+64,y+44,status,1,statusColor,9);
            const auto maximum=forms::stats(member->formId,member->level).maxHp;
            c.bar(x+64,y+60,56,member->hp,maximum,isInjured(*member) ? red : mint);
            std::snprintf(label,sizeof(label),"%u/%u",static_cast<unsigned>(member->hp),static_cast<unsigned>(maximum));
            c.text(x+64,y+74,label,1,dim,9);
        }
        if(box) {
            std::snprintf(label,sizeof(label),"%u/%u",static_cast<unsigned>(boxPage_+1),static_cast<unsigned>(boxPages(state)));
            c.text(78,64,label,1,dim,5); // Beside the title; the sort chip owns the row under the tiles.
        }
        break;
    }
    case Screen::ReleaseReview: {
        const auto* member=findMember(state,releaseMember_);
        c.center(75,"RELEASE THIS DIGIMON?",2,amber);
        c.center(127,member ? memberName(*member) : "UNKNOWN DIGIMON",1,ink,36);
        std::snprintf(label,sizeof(label),"OWNED ID %u",static_cast<unsigned>(releaseMember_)); c.center(153,label,2,mint);
        c.center(191,"THIS FREES ONE PARTNER SLOT",1,dim,36);
        c.center(215,"RELEASE CANNOT BE UNDONE",1,amber,36);
        c.center(245,"BACK KEEPS THIS DIGIMON",1,dim,36); break;
    }
    case Screen::Stats: {
        if (!selectedMember(state)) break;
        const auto& member=*selectedMember(state);
        const auto profile=combat::formProfile(member.formId,member.level);
        const auto effective=memberBattleProfile(state,member);
        const auto care=memberCare(member);
        c.center(65,memberName(member),std::strlen(memberName(member))>21 ? 1 : 2,mint,36);
        std::snprintf(label,sizeof(label),"LV %u  %s  PAGE %u/4",static_cast<unsigned>(member.level),profile.type,statsPage_+1);
        c.center(90,label,1,dim,36);
        std::snprintf(label,sizeof(label),"OWNED ID %u",static_cast<unsigned>(member.id)); c.center(103,label,1,dim,36);
        auto row=[&](int y,const char* title,std::uint32_t value) {
            std::snprintf(label,sizeof(label),"%s %u",title,static_cast<unsigned>(value)); c.center(y,label,2,ink,28);
        };
        if (statsPage_==0) {
            std::snprintf(label,sizeof(label),"HP %u / %u",static_cast<unsigned>(member.hp),static_cast<unsigned>(profile.stats.maxHp)); c.center(117,label,2);
            const auto stat=[&](int y,const char* name,std::uint32_t base,std::uint32_t value) {
                std::snprintf(label,sizeof(label),"%s %u +%u =%u",name,static_cast<unsigned>(base),static_cast<unsigned>(value-base),static_cast<unsigned>(value)); c.center(y,label,2,ink,26);
            };
            stat(139,"ATK",profile.stats.attack,effective.stats.attack); stat(161,"DEF",profile.stats.defense,effective.stats.defense);
            stat(183,"MAG",profile.stats.magic,effective.stats.magic); stat(205,"RES",profile.stats.resistance,effective.stats.resistance);
            c.badge(240,"BASE + CARE = EFFECTIVE",1,dim,36);
        } else if (statsPage_==1) {
            row(113,"FULLNESS",member.fullness); row(134,"MOOD",member.mood); row(155,"ENERGY",member.energy); row(176,"BOND",member.bond);
            std::snprintf(label,sizeof(label),"XP %u  NEXT +%u",static_cast<unsigned>(member.xp),
                static_cast<unsigned>(member.level<kMaxLevel ? xpForLevel(member.level+1)-member.xp : 0)); c.center(197,label,1,dim,36);
            std::snprintf(label,sizeof(label),careWasMissed(member)?"TOILET %u  MISSED CARE":toiletNeed(member)>=25?"TOILET %u  NEEDED":"TOILET %u",
                static_cast<unsigned>(toiletNeed(member)));
            c.center(214,label,1,careWasMissed(member)||toiletNeed(member)>=25?amber:dim,36);
        } else if (statsPage_==2) {
            c.center(115,"PHYSICAL",1,dim); c.center(130,profile.physicalSkill,std::strlen(profile.physicalSkill)<=24 ? 2 : 1,ink,40);
            c.center(151,"HEAVY",1,dim); c.center(166,profile.heavySkill,std::strlen(profile.heavySkill)<=24 ? 2 : 1,ink,40);
            c.center(187,"MAGIC",1,dim); c.center(202,profile.magicSkill,std::strlen(profile.magicSkill)<=24 ? 2 : 1,ink,40);
        } else {
            c.center(116,"TYPE ADVANTAGE",2,mint);
            c.center(146,"GROVE > TIDE > EMBER > GROVE",1,ink,36);
            c.center(172,"ADVANTAGE 125% / REVERSE 80%",1,dim,36);
            c.center(196,"SAME OR NEUTRAL TYPE 100%",1,dim,36);
        }
        std::snprintf(label,sizeof(label),"CARE RANK %u  OFF +%u  GUARD +%u",static_cast<unsigned>(std::min<std::uint32_t>(4,member.bond/50)),static_cast<unsigned>(care.offense),static_cast<unsigned>(care.protection));
        c.badge(264,label,1,mint,40);
        if (anyRouteReady(state,model,member)) c.badge(340,"DIGIVOLUTION REQUIREMENTS MET",1,mint,30);
        break;
    }
    case Screen::Evolution: {
        const auto* member=selectedMember(state);
        const auto* route=member ? forms::outgoing(member->formId,evolutionIndex_) : nullptr;
        const auto* target=route ? forms::find(route->to) : nullptr;
        c.badge(67,"DIGIVOLUTION",2,mint);
        if (!member || !route || !target) {
            c.center(130,member ? memberName(*member) : "NO PARTNER",1,ink,36);
            c.center(164,"NO FURTHER ROUTE",2,amber);
            c.center(195,"THIS FORM IS COMPLETE",1,dim,32); break;
        }
        const auto need=forms::evolutionNeed(*route);
        const auto level=member->id==state.activeCreatureId ? state.level : member->level;
        const auto bond=member->id==state.activeCreatureId ? state.bond : member->bond;
        const auto care=carePoints(*member);
        const bool ready=routeReady(state,model,*member,*route);
        const auto previewLevel=std::max<std::uint32_t>(level,need.level);
        const auto before=combat::formProfile(member->formId,previewLevel);
        const auto after=combat::formProfile(target->id,previewLevel);
        if (evolutionPage_==0) {
            actor(206,184,9);
            std::snprintf(label,sizeof(label),"%s  %u/%u",ready ? "READY" : "LOCKED",evolutionIndex_+1,forms::outgoing(member->formId,1) ? 2 : 1);
            c.badge(262,label,1,ready ? mint : amber,40);
            if (ready) c.badge(284,"DIGIVOLUTION REQUIREMENTS MET",1,mint,36);
        } else if (evolutionPage_==1) {
            std::snprintf(label,sizeof(label),"BASE STATS AT LEVEL %u",static_cast<unsigned>(previewLevel)); c.center(113,label,1,dim,36);
            const std::uint32_t oldStats[]{before.stats.maxHp,before.stats.attack,before.stats.defense,before.stats.magic,before.stats.resistance};
            const std::uint32_t newStats[]{after.stats.maxHp,after.stats.attack,after.stats.defense,after.stats.magic,after.stats.resistance};
            const char* names[]{"HP","ATTACK","DEFENSE","MAGIC","RESIST"};
            for (unsigned i=0;i<5;++i) {
                std::snprintf(label,sizeof(label),"%s %u > %u",names[i],static_cast<unsigned>(oldStats[i]),static_cast<unsigned>(newStats[i]));
                c.center(132+static_cast<int>(i)*19,label,2,ink,28);
            }
        } else if (evolutionPage_==2) {
            std::snprintf(label,sizeof(label),"TYPE %s > %s",before.type,after.type); c.center(115,label,1,mint,40);
            c.center(136,"NEW PHYSICAL / HEAVY / MAGIC",1,dim,40);
            c.center(153,after.physicalSkill,std::strlen(after.physicalSkill)<=24 ? 2 : 1,ink,40); c.center(173,after.heavySkill,std::strlen(after.heavySkill)<=24 ? 2 : 1,ink,40); c.center(193,after.magicSkill,std::strlen(after.magicSkill)<=24 ? 2 : 1,ink,40);
            c.center(216,"LEVEL XP CARE AND ID ARE KEPT",1,dim,40);
        } else {
            c.center(132,"NOT READY YET",2,amber);
            c.center(166,level<need.level ? "GAIN LEVELS THROUGH PLAY" : bond<need.bond ? "CARE TO BUILD YOUR BOND" :
                isInjured(*member) ? "TREAT THE INJURY FIRST" : !careRouteOpen(*member,evolutionIndex_) ? "TOO MANY CARE MISTAKES - TRY THE OTHER ROUTE" : "KEEP CARING TO DIGIVOLVE",1,dim,40);
            if (ready) c.center(191,"DIGIVOLUTION REQUIREMENTS MET",1,mint,40);
            if(!model.writable) c.center(214,"SAVE RECOVERY REQUIRED",1,amber,36);
            else if(state.phase!=Phase::Home) c.center(214,"RETURN HOME TO DIGIVOLVE",1,amber,36);
        }
        c.badge(92,target->name,std::strlen(target->name)>21 ? 1 : 2,ink,36);
        if(evolutionPage_==1) {
            const auto care=memberCare(*member);
            std::snprintf(label,sizeof(label),"CARE ADDS +%u ATK/MAG +%u DEF/RES",static_cast<unsigned>(care.offense),static_cast<unsigned>(care.protection));
            c.center(225,label,1,mint,42);
        }
        if(evolutionPage_) {
            std::snprintf(label,sizeof(label),"LV %u/%u  BOND %u/%u  CARE %u/%u",static_cast<unsigned>(level),need.level,static_cast<unsigned>(bond),need.bond,static_cast<unsigned>(care),need.care);
            c.center(240,label,1,ready ? mint : amber,40);
            if(evolutionPage_!=3) c.center(264,"TAP / SWIPE FOR STATS / SKILLS",1,dim,36);
        }
        break;
    }
    case Screen::EvolutionReview: {
        const auto* before=forms::find(evolutionForm_);
        const auto* target=forms::find(evolutionTarget_);
        c.badge(72,"READY TO DIGIVOLVE?",2,amber);
        c.badge(99,before ? before->name : "PARTNER",1,dim,40);
        c.badge(121,target ? target->name : "NEW FORM",2,mint,25);
        actor(206,195,7);
        c.badge(260,"SAVES FORM - LEVEL XP CARE ID KEPT",1,dim,40); break;
    }
    case Screen::EvolutionResult: {
        c.badge(76,"DIGIVOLUTION COMPLETE",2,mint);
        actor(206+tx,172+ty,9);
        const auto* evolved=findMember(state,evolutionMember_);
        const char* evolvedName=evolved ? memberName(*evolved) : creatureName(state);
        c.badge(249,evolvedName,std::strlen(evolvedName)>21 ? 1 : 2,mint,36);
        c.badge(272,"NEW FORM SAVED",1,dim,36); break;
    }
    case Screen::Settings:
        if(model.sleepTimeoutSeconds) std::snprintf(label,sizeof(label),"SCREEN IDLE: %us",static_cast<unsigned>(model.sleepTimeoutSeconds));
        else std::snprintf(label,sizeof(label),"SCREEN IDLE: OFF");
        c.center(69,label,2,ink,25);
        c.center(96,"TAP OR MOVE TO WAKE",1,dim,32); break;
    case Screen::Sound:
        c.center(72,"SOUND",2,mint);
        c.center(108,"VOLUME",1,dim);
        std::snprintf(label,sizeof(label),"%u%%",static_cast<unsigned>(model.volumePercent));
        c.center(148,label,3,ink); c.bar(126,192,160,model.volumePercent,100,mint);
        c.center(211,model.muted ? "SOUND IS MUTED" : model.volumePercent ? "QUIET TO LOUD" : "SILENT",1,dim);
        c.badge(294,"MUSIC IS OPTIONAL",1,dim,36);
        c.badge(317,!model.audioPreferencesWritable ? "SOUND SETTINGS NOT SAVED" :
            !model.audioAvailable ? "SPEAKER NOT READY" : "SAVES AS YOU CHANGE",1,
            !model.audioPreferencesWritable ? amber : dim,36); break;
    case Screen::TradeChoose: {
        const auto* member=findMember(state,tradeMemberId_);
        c.badge(67,"CHOOSE YOUR OFFER",2,mint);
        if(member && trade::canOffer(state,member->id)) {
            c.badge(95,memberName(*member),std::strlen(memberName(*member))>21 ? 1 : 2,ink,36);
            actor(206,185,7);
            std::snprintf(label,sizeof(label),"LV %u  HP %u/%u  ID %u",static_cast<unsigned>(member->level),static_cast<unsigned>(member->hp),
                static_cast<unsigned>(combat::formProfile(member->formId,member->level).stats.maxHp),static_cast<unsigned>(member->id));c.badge(248,label,1,dim,42);
            c.badge(268,"ONE OTHER PARTNER STAYS WITH YOU",1,mint,40);
        } else {
            c.center(126,"KEEP ONE PLAYABLE PARTNER",1,amber,40);
            c.center(159,"CATCH ANOTHER BEFORE TRADING",1,ink,40);
            c.center(202,"YOUR COLLECTION HAS NOT CHANGED",1,dim,40);
        }
        break;
    }
    case Screen::TradeReview: {
        const auto* v=model.trade;
        const bool valid=v && v->localSide<2 && trade::valid(v->transcript);
        if(!valid) {
            c.badge(78,"TRADE UNAVAILABLE",2,amber);
            c.center(144,"WAIT FOR BOTH CURRENT OFFERS",1,dim,40);
            c.center(183,"NO PARTNERS HAVE BEEN EXCHANGED",1,dim,40);break;
        }
        const auto& give=v->transcript.offers[v->localSide];const auto& get=v->transcript.offers[1-v->localSide];
        const bool applied=v->durable==tradewire::Durable::Applied;
        const bool aborted=v->stage==tradewire::Stage::Aborted || v->stage==tradewire::Stage::Cancelled;
        c.badge(67,applied ? "PARTNER RECEIVED" : aborted ? "TRADE CANCELED" : "REVIEW BOTH OFFERS",2,applied ? mint : amber);
        if(!tradePage_) {
            auto side=[&](int x,int y,const char* text,std::uint16_t color) {
                const auto length=std::min<std::size_t>(std::strlen(text),22);
                c.rect(x-static_cast<int>(length)*3-4,y-3,static_cast<int>(length)*6+8,13,panel);
                c.text(x-static_cast<int>(length)*3,y,text,1,color,22);
            };
            side(118,91,"YOU GIVE",dim);side(294,91,"YOU GET",dim);
            side(118,107,memberName(give),mint);side(294,107,memberName(get),amber);
            std::snprintf(label,sizeof(label),"LV %u HP %u/%u",static_cast<unsigned>(give.level),static_cast<unsigned>(give.hp),static_cast<unsigned>(combat::formProfile(give.formId,give.level).stats.maxHp));side(118,122,label,dim);
            std::snprintf(label,sizeof(label),"LV %u HP %u/%u",static_cast<unsigned>(get.level),static_cast<unsigned>(get.hp),static_cast<unsigned>(combat::formProfile(get.formId,get.level).stats.maxHp));side(294,122,label,dim);
            const auto own=partnerArtRequest(state,model,now);
            if(!c.spriteFrame(118,188,104,model.partnerArtwork,own,model.partnerArtwork.nativeFacing==SpriteFacing::Left))c.missingArt(118,188);
            if(!c.spriteFrame(294,188,104,model.artwork.sprite,art,model.artwork.sprite.nativeFacing==SpriteFacing::Right))c.missingArt(294,188);
            std::snprintf(label,sizeof(label),"XP %u BOND %u",static_cast<unsigned>(give.xp),static_cast<unsigned>(give.bond));side(118,248,label,dim);
            std::snprintf(label,sizeof(label),"XP %u BOND %u",static_cast<unsigned>(get.xp),static_cast<unsigned>(get.bond));side(294,248,label,dim);
        } else {
            const auto& member=tradePage_<3 ? give : get;const bool care=tradePage_==2 || tradePage_==4;
            const auto profile=combat::formProfile(member.formId,member.level);const auto bonus=memberCare(member);
            c.badge(94,memberName(member),std::strlen(memberName(member))>21 ? 1 : 2,ink,36);
            std::snprintf(label,sizeof(label),"%s  LV %u  %s",tradePage_<3 ? "YOU GIVE" : "YOU GET",static_cast<unsigned>(member.level),care ? "PROGRESS" : "BASE STATS");c.center(117,label,1,dim,40);
            if(!care) {
                std::snprintf(label,sizeof(label),"HP %u / %u",static_cast<unsigned>(member.hp),static_cast<unsigned>(profile.stats.maxHp));c.center(141,label,2,ink);
                std::snprintf(label,sizeof(label),"ATK %u  DEF %u",static_cast<unsigned>(profile.stats.attack),static_cast<unsigned>(profile.stats.defense));c.center(167,label,2,ink);
                std::snprintf(label,sizeof(label),"MAG %u  RES %u",static_cast<unsigned>(profile.stats.magic),static_cast<unsigned>(profile.stats.resistance));c.center(193,label,2,ink);
                std::snprintf(label,sizeof(label),"CARE OFF +%u  GUARD +%u",static_cast<unsigned>(bonus.offense),static_cast<unsigned>(bonus.protection));c.center(225,label,1,mint,40);
            } else {
                std::snprintf(label,sizeof(label),"XP %u  NEXT +%u",static_cast<unsigned>(member.xp),static_cast<unsigned>(member.level<kMaxLevel ? xpForLevel(member.level+1)-member.xp : 0));c.center(141,label,1,ink,40);
                std::snprintf(label,sizeof(label),"BOND %u  ENERGY %u",static_cast<unsigned>(member.bond),static_cast<unsigned>(member.energy));c.center(167,label,1,ink,40);
                std::snprintf(label,sizeof(label),"FOOD %u  MOOD %u",static_cast<unsigned>(member.fullness),static_cast<unsigned>(member.mood));c.center(193,label,1,ink,40);
                c.center(225,"LEVEL XP AND CARE TRAVEL WITH THEM",1,mint,40);
            }
            std::snprintf(label,sizeof(label),"DETAILS %u/4",static_cast<unsigned>(tradePage_));c.center(248,label,1,dim,40);
        }
        const char* status="CHECK BOTH OFFERS BEFORE CONFIRMING";
        if(applied)status=v->peerDurable==tradewire::Durable::Applied ? "BOTH DEVICES SAVED THE TRADE" : "SAVED HERE - WAITING FOR OTHER DEVICE";
        else if(aborted)status="YOUR ORIGINAL PARTNER STAYS WITH YOU";
        else if(v->recoveryOffer)status="PAST TRADE - REVIEW BEFORE CANCELING";
        else if(!v->connected)status="RECONNECT THE SAME TWO DEVICES";
        else if(v->stage==tradewire::Stage::Inviting)status="WAITING FOR THE OTHER PLAYER";
        else if(v->durable==tradewire::Durable::Prepared)status="OFFER LOCKED - KEEP DEVICES NEARBY";
        else if(v->stage==tradewire::Stage::Committing || v->stage==tradewire::Stage::Applying)status="SAVING TRADE - KEEP DEVICES NEARBY";
        else if(v->offerPending || !v->peerReviewed)status="WAITING FOR BOTH CURRENT OFFERS";
        else if(v->localConfirmed)status=v->peerConfirmed ? "BOTH CONFIRMED - PREPARING TRADE" : "YOU CONFIRMED - WAITING FOR THEM";
        else if(v->peerConfirmed)status="THEY CONFIRMED - YOUR CHOICE";
        c.badge(268,status,1,dim,44);
        if(v->stage!=tradewire::Stage::Reviewing || v->durable!=tradewire::Durable::None) {
            if(v->durable==tradewire::Durable::Prepared)c.badge(310,"CANCEL MAY NEED THE OTHER DEVICE",1,amber,44);
            else if(model.tradeStatus)c.badge(310,model.tradeStatus,1,amber,44);
        }
        break;
    }
    case Screen::EncounterSettings: {
        c.center(76,"WILD ENCOUNTERS",2);
        std::snprintf(label,sizeof(label),"PACE: %s",encounterRateName(state.encounterRate)); c.center(108,label,2,mint);
        std::uint32_t minimum=0,maximum=0; encounterStepRange(state.encounterRate,false,minimum,maximum);
        if (maximum) std::snprintf(label,sizeof(label),"GAP %u-%u WALKING STEPS",static_cast<unsigned>(minimum),static_cast<unsigned>(maximum));
        else std::snprintf(label,sizeof(label),"ENCOUNTERS ARE PAUSED");
        c.center(139,label,1,dim,40);
        c.center(158,"STEPS COUNT ON EVERY SCREEN",1,dim,40);
        c.center(177,"ONE DIGIMON CAN WAIT AT A TIME",1,dim,36);
        c.center(196,"NEW ENCOUNTERS WAIT FOR HOME",1,dim,36);
        c.center(215,"CHANGE SAVES IMMEDIATELY",1,amber,32); break;
    }
    case Screen::NearbyReview: {
        c.center(76,"REVIEW CHALLENGE",2,amber);
        const auto* n=model.nearby;
        const auto* peer=n && nearbyIndex_<peerCount(n) ? &n->peers[nearbyIndex_] : nullptr;
        const auto* enemy=peer ? forms::find(peer->fighter.formId) : nullptr;
        c.center(118,enemy ? enemy->name : "PLAYER UNAVAILABLE",2,ink,25);
        if (peer) {
            std::snprintf(label,sizeof(label),"DEVICE %02X%02X  LV %u",peer->mac.bytes[4],peer->mac.bytes[5],static_cast<unsigned>(peer->fighter.level)); c.center(146,label,1,dim,40);
        }
        const auto* local=forms::find(model.nearbyLocalFighter.formId);
        std::snprintf(label,sizeof(label),"YOU: %s LV %u",local ? local->name : creatureName(state),static_cast<unsigned>(model.nearbyLocalFighter.level)); c.center(172,label,1,mint,40);
        c.center(195,"CHOOSE THIS DUEL'S MODE",1,amber,36);
        c.center(268,"NO CAPTURE / XP - BOTH MUST AGREE",1,dim,40); break;
    }
    case Screen::Nearby: {
        const auto* n=model.nearby;
        const auto stage=n ? n->stage : nearby::Stage::Closed;
        c.badge(66,stage==nearby::Stage::Playing || stage==nearby::Stage::Finished || stage==nearby::Stage::Reconnecting ? "FRIENDLY DUEL" : "NEARBY PLAYERS",1,mint);
        if (stage==nearby::Stage::Discovering) {
            if (nearbyIndex_<peerCount(n)) {
                const auto& peer=n->peers[nearbyIndex_]; const auto* form=forms::find(peer.fighter.formId);
                actor(206,176,8);
                c.badge(93,form ? form->name : "UNKNOWN PARTNER",2,ink,24);
                std::snprintf(label,sizeof(label),"PLAYER %u/%u  DEVICE %02X%02X",nearbyIndex_+1,static_cast<unsigned>(peerCount(n)),peer.mac.bytes[4],peer.mac.bytes[5]); c.badge(243,label,1,dim,40);
                std::snprintf(label,sizeof(label),"LEVEL %u  %s",static_cast<unsigned>(peer.fighter.level),peer.available ? "AVAILABLE" : "BUSY"); c.badge(264,label,1,peer.available ? mint : amber,36);
            } else {
                c.center(119,"LOOKING FOR PLAYERS",2,ink);
                c.center(157,"OPEN NEARBY ON BOTH DEVICES",1,dim,36);
                c.center(185,"KEEP THE TWO DEVICES CLOSE",1,dim,36);
                c.center(211,"WIFI IS PAUSED WHILE NEARBY",1,amber,36);
                std::snprintf(label,sizeof(label),"DUNGEON KEYS %u",static_cast<unsigned>(state.dungeonKeys));
                c.center(239,label,1,mint,36);
                if(state.dungeonKeys) c.center(263,"GROVE, TIDE, AND EMBER",1,dim,36);
            }
        } else if (stage==nearby::Stage::Incoming || stage==nearby::Stage::Outgoing || stage==nearby::Stage::Accepting) {
            const auto* host=forms::find(n->offered[0].formId); const auto* guest=forms::find(n->offered[1].formId);
            c.center(95,stage==nearby::Stage::Incoming ? "CHALLENGE RECEIVED" : "WAITING FOR ACCEPTANCE",1,amber,36);
            std::snprintf(label,sizeof(label),"DEVICE %02X%02X",n->opponent.bytes[4],n->opponent.bytes[5]); c.center(111,label,1,dim,36);
            std::snprintf(label,sizeof(label),"%s LV %u",host ? host->name : "HOST",static_cast<unsigned>(n->offered[0].level)); c.center(139,label,1,ink,42);
            c.center(159,"VS",1,dim);
            std::snprintf(label,sizeof(label),"%s LV %u",guest ? guest->name : "GUEST",static_cast<unsigned>(n->offered[1].level)); c.center(179,label,1,ink,42);
            c.center(198,n->offeredMode==nearby::Mode::Auto ? "AUTO FRIENDLY DUEL" : "TACTICAL FRIENDLY DUEL",1,mint,36);
            c.center(217,"FULL HP - NO CAPTURE OR XP",1,dim,40);
        } else if (stage==nearby::Stage::Playing || stage==nearby::Stage::Reconnecting || stage==nearby::Stage::Finished) {
            const auto& match=n->match; const unsigned local=n->host ? 0 : 1, remote=1-local;
            const auto player=combat::formProfile(match.fighters[local].formId,match.fighters[local].level);
            const auto enemy=combat::formProfile(match.fighters[remote].formId,match.fighters[remote].level);
            auto side=[&](int x,int y,const char* value,std::uint16_t color) {
                const auto length=std::min<std::size_t>(value ? std::strlen(value) : 0,22); c.rect(x-static_cast<int>(length)*3-4,y-3,static_cast<int>(length)*6+8,13,panel); c.text(x-static_cast<int>(length)*3,y,value,1,color,22);
            };
            side(118,92,player.name,mint); side(294,92,enemy.name,amber);
            const bool feedback=nearbyFeedback(model);
            const auto elapsed=feedback ? model.nearbyTurnElapsedMs : kNoNearbyTurn;
            constexpr auto impactAt=1200u;
            const unsigned attacker=match.lastAttacker<2 ? match.lastAttacker : 0, defender=1-attacker;
            const unsigned receiver=match.lastReflected ? attacker : defender;
            const auto displayHp=[&](unsigned index,std::uint32_t maximum) {
                const auto restored=std::uint64_t(match.hp[index])+match.lastDamage[index];
                return feedback && elapsed<impactAt ? static_cast<std::uint32_t>(std::min<std::uint64_t>(restored,maximum)) : match.hp[index];
            };
            const auto localHp=displayHp(local,player.stats.maxHp), remoteHp=displayHp(remote,enemy.stats.maxHp);
            std::snprintf(label,sizeof(label),"HP %u/%u",static_cast<unsigned>(localHp),static_cast<unsigned>(player.stats.maxHp)); side(118,108,label,dim);
            std::snprintf(label,sizeof(label),"HP %u/%u",static_cast<unsigned>(remoteHp),static_cast<unsigned>(enemy.stats.maxHp)); side(294,108,label,dim);
            const auto actorX=[&](unsigned index) { return index==local ? 118 : 294; };
            const int direction=attacker==local ? 1 : -1;
            int lunge=0;
            if (feedback && elapsed>=600 && elapsed<1000) {
                const auto t=elapsed-600;
                lunge=static_cast<int>(t<200 ? t : 400-t)*12/200;
            }
            const int localX=118+(feedback && attacker==local ? direction*lunge : 0);
            const int remoteX=294+(feedback && attacker==remote ? direction*lunge : 0);
            const bool flash=feedback && elapsed>=impactAt && elapsed<impactAt+180;
            if (flash) c.circle(actorX(receiver),176,58,ink);
            const auto playerArt=partnerArtRequest(state,model,now);
            // VFX are independent of sprite animation: an available local SD
            // Idle frame stays usable throughout the exchange without reloading.
            if(!c.spriteFrame(localX,185,112,model.partnerArtwork,playerArt,model.partnerArtwork.nativeFacing==SpriteFacing::Left)) c.missingArt(localX,185);
            if(!c.spriteFrame(remoteX,185,112,model.artwork.sprite,art,model.artwork.sprite.nativeFacing==SpriteFacing::Right)) c.missingArt(remoteX,185);
            c.bar(64,121,108,localHp,player.stats.maxHp,mint); c.bar(240,121,108,remoteHp,enemy.stats.maxHp,amber);
            if (feedback) {
                const auto attacking=combat::formProfile(match.fighters[attacker].formId,match.fighters[attacker].level);
                const auto defending=combat::formProfile(match.fighters[defender].formId,match.fighters[defender].level);
                const auto* skill=match.lastAttack==nearby::Choice::Magic ? attacking.magicSkill :
                    match.lastAttack==nearby::Choice::Heavy ? attacking.heavySkill : attacking.physicalSkill;
                // Guard rings, projectile and impact each have bounded geometry
                // and no random decisions or writes to the authoritative match.
                if (elapsed<impactAt+180) {
                    const auto color=match.lastDefense==nearby::Choice::Ward ? mint : match.lastDefense==nearby::Choice::Counter ? amber : edge;
                    c.circle(actorX(defender),176,56,color,false);
                    if (match.lastDefense==nearby::Choice::Ward) c.circle(actorX(defender),176,60,color,false);
                    else if (match.lastDefense==nearby::Choice::Counter) {
                        c.rect(actorX(defender)-58,170,5,13,color); c.rect(actorX(defender)+54,170,5,13,color);
                    }
                }
                if (elapsed>=600 && elapsed<impactAt) {
                    const auto outboundMs=match.lastReflected ? 400u : 600u;
                    const auto t=elapsed-600;
                    const int x=t<=outboundMs ? actorX(attacker)+direction*static_cast<int>(t)*176/static_cast<int>(outboundMs) :
                        actorX(defender)-direction*static_cast<int>(t-outboundMs)*176/200;
                    c.circle(x,176,match.lastAttack==nearby::Choice::Magic ? 8 : 5,match.lastAttack==nearby::Choice::Magic ? amber : mint);
                }
                if (flash) {
                    const auto hitX=actorX(receiver); c.rect(hitX-19,173,38,4,ink); c.rect(hitX-2,161,4,32,ink);
                }
                std::snprintf(label,sizeof(label),"%s %s",defending.name,nearby::choiceName(match.lastDefense));
                c.badge(305,label,1,elapsed<600 ? mint : dim,44);
                c.badge(246,attacking.name,1,dim,42);
                c.badge(269,skill,std::strlen(skill)<=24 ? 2 : 1,elapsed>=600 ? ink : dim,42);
                if(elapsed>=impactAt) {
                    std::snprintf(label,sizeof(label),"-%u",static_cast<unsigned>(match.lastDamage[receiver]));
                    const int x=actorX(receiver)-static_cast<int>(std::strlen(label))*6,y=139-static_cast<int>(elapsed-impactAt)*10/1200;
                    c.rect(x-4,y-3,static_cast<int>(std::strlen(label))*12+8,20,panel); c.text(x,y,label,2,amber);
                    if(match.lastReflected) c.badge(326,"COUNTER REFLECTED",1,amber);
                    else if(match.lastCritical) c.badge(326,"CRITICAL HIT",1,amber);
                }
            } else if (stage==nearby::Stage::Finished) {
                const bool won=(match.status==nearby::Status::HostWon && n->host) || (match.status==nearby::Status::GuestWon && !n->host);
                c.badge(256,match.status==nearby::Status::Draw ? "FRIENDLY DRAW" : won ? "YOU WON!" : "GOOD DUEL!",2,mint);
                c.badge(290,"PARTNER SAVE UNCHANGED",1,dim,36);
            } else if (stage==nearby::Stage::Reconnecting) {
                c.badge(253,"RECONNECTING",2,amber);
                c.badge(279,"CHOICES PAUSED - STAY NEARBY",1,dim,36);
            } else {
                const bool attack=match.attacker==local;
                if (match.mode==nearby::Mode::Auto || n->localChoicePending) {
                    c.badge(270,n->localChoicePending ? "CHOICE SENT" : "AUTO DUEL",2,mint);
                    c.badge(300,"WAITING FOR OTHER PLAYER",1,dim,36);
                } else {
                    const unsigned selected=attack ? (battleSelection_==combat::Move::Physical ? 0 : battleSelection_==combat::Move::Heavy ? 1 : 2) : defenseSelection_;
                    const auto* skill=attack ? (selected==2 ? player.magicSkill : selected==1 ? player.heavySkill : player.physicalSkill) :
                        selected==0 ? "BLOCKS PHYSICAL" : selected==1 ? "REFLECTS HEAVY" : "BLOCKS MAGIC";
                    carousel(selected,!attack,skill);
                    std::snprintf(label,sizeof(label),"%s  EN %u",attack ? "ATTACK" : "DEFEND",static_cast<unsigned>(match.energy[local])); c.badge(80,label,1,dim,40);
                }
            }
        } else {
            c.center(120,stage==nearby::Stage::TimedOut ? "CONNECTION TIMED OUT" : stage==nearby::Stage::Incompatible ? "RULES DO NOT MATCH" :
                stage==nearby::Stage::Cancelled ? "DUEL CANCELLED" : "NEARBY RADIO",2,amber);
            c.center(166,model.nearbyStatus ? model.nearbyStatus : "NO ACTIVE CONNECTION",1,dim,44);
            c.center(201,"PARTNER SAVE UNCHANGED",1,dim,36);
            c.center(252,"LEAVE THEN OPEN TO TRY AGAIN",1,dim,40);
        }
        if(!picker) c.badge(kNavY+kNavH+6,"WIFI PAUSED - NO XP",1,dim,32);
        break;
    }
    case Screen::Expeditions: {
        const auto& scenario=selectedExpedition(expeditionIndex_);
        const bool dungeon=scenario.kind==expeditions::Kind::Dungeon;
        c.center(74,dungeon ? "DUNGEON" : "BOSS",2,mint);
        c.center(108,expeditions::variantName(scenario,expeditionVariant_),2);
        std::snprintf(label,sizeof(label),"FLOORS %u",static_cast<unsigned>(scenario.floors));
        c.center(148,label,2);
        if(scenario.minPlayers==scenario.maxPlayers) std::snprintf(label,sizeof(label),"PLAYERS %u",static_cast<unsigned>(scenario.minPlayers));
        else std::snprintf(label,sizeof(label),"PLAYERS %u-%u",static_cast<unsigned>(scenario.minPlayers),static_cast<unsigned>(scenario.maxPlayers));
        c.center(180,label,2);
        c.center(214,dungeon ? "COSTS 1 KEY" : "COSTS 1 SIGIL",1,dim);
        if(dungeon) std::snprintf(label,sizeof(label),"KEYS %u",static_cast<unsigned>(state.dungeonKeys));
        else std::snprintf(label,sizeof(label),"SIGILS %u",static_cast<unsigned>(state.bossSigils));
        c.center(236,label,1,mint);
        break;
    }
    case Screen::ExpeditionLobby: {
        const auto& scenario=selectedExpedition(expeditionIndex_);
        const bool dungeon=scenario.kind==expeditions::Kind::Dungeon;
        const bool ready=expeditionReady(state,scenario);
        c.center(74,expeditions::variantName(scenario,expeditionVariant_),2,mint);
        if(expeditionSolo_) {
            actor(206,128,5);
            c.center(176,"SOLO RUN OPEN",2,amber);
            std::snprintf(label,sizeof(label),"FLOOR 1 OF %u",static_cast<unsigned>(scenario.floors));
            c.center(208,label,2);
            c.center(236,dungeon ? "DUNGEON KEY STAYS" : "BOSS SIGIL STAYS",1,dim);
        } else {
            c.center(118,"WAITING",2,amber);
            c.center(150,"FOR ANOTHER PLAYER",2);
            c.center(190,"BOTH DEVICES STAY HERE",1,dim);
            c.center(214,ready ? "OR START SOLO" : dungeon ? "NEED A DUNGEON KEY" : "NEED A BOSS SIGIL",1,ready ? mint : amber);
        }
        break;
    }
    case Screen::ModeReview:
        c.center(78,proposedMode_==1 ? "SWITCH TO AUTO?" : "SWITCH TO TACTICAL?",2,amber);
        c.center(127,proposedMode_==1 ? "ATTACKS RUN FOR YOU" : "YOU CHOOSE EACH",2);
        c.center(153,proposedMode_==1 ? "YOU FLICK TO CAPTURE" : "BATTLE ACTION",2);
        c.center(203,"THIS SETTING IS SAVED",1,dim);
        c.center(224,"BACK KEEPS THE CURRENT MODE",1,dim,36); break;
    }
    if(browse || picker) {
        const bool multiple=picker || screen_==Screen::Starter || screen_==Screen::Stats || screen_==Screen::Sound || screen_==Screen::TradeReview ||
            (screen_==Screen::TradeChoose && tradeCandidate(state,tradeMemberId_,true)!=tradeMemberId_) || (screen_==Screen::Collection && state.collectionCount>1) ||
            (screen_==Screen::Box && boxPages(state)>1) || (screen_==Screen::Evolution && (evolutionPage_ || (selectedMember(state) && forms::outgoing(selectedMember(state)->formId,1)))) ||
            screen_==Screen::Nearby || screen_==Screen::Expeditions;
        if(multiple) {
            c.chevron(49,180,-1,screen_==Screen::Sound && !model.volumePercent ? dim : arrowColor);
            c.chevron(352,180,1,screen_==Screen::Sound && model.volumePercent>=100 ? dim : arrowColor);
        }
    }
    Button choices[8]; const auto n=battleLocked_ ? 0 : buttons(state,model,choices);
    // Footer hints sit under the bottom BACK/LEAVE spot when one is shown, never beneath it.
    bool navShown=false;
    for(std::size_t i=0;i<n;++i) navShown=navShown || choices[i].padded;
    const int footerY=navShown ? kNavY+kNavH+6 : 337;
    if(fullRosterBattle) c.badge(footerY,"FINISH BATTLE TO RELEASE",1,amber);
    else if(hintAge<7000) {
        const auto color=hintAge<5000 ? dim : rgb(173-static_cast<int>((hintAge-5000)*115/2000),196-static_cast<int>((hintAge-5000)*112/2000),190-static_cast<int>((hintAge-5000)*110/2000));
        c.badge(footerY,picker ? (navShown ? "SWIPE UP TO COMMIT" : "TAP / SWIPE TO CHOOSE - UP TO COMMIT") : "TAP ARROWS OR SWIPE",1,color,36);
    }
    for(std::size_t i=0;i<n;++i) {
        const auto& b=choices[i];
        if(b.id==HomePrevious || b.id==HomeNext) {
            const auto color=!b.enabled ? dim : down_ && b.id==downButton_ ? ink : arrowColor;
            c.circle(b.x+b.w/2,190,18,panel);
            c.chevron(b.x+b.w/2,190,b.id==HomePrevious ? -1 : 1,color);
            continue;
        }
        if(b.id>=Tile0 && b.id<=Tile3) continue; // Drawn with their sprite, level and status above.
        const bool modeSelected=(b.id==NearbyTactical && nearbyMode_==nearby::Mode::Tactical) ||
            (b.id==NearbyAuto && nearbyMode_==nearby::Mode::Auto);
        c.rect(b.x,b.y,b.w,b.h,b.enabled ? (modeSelected ? mint : edge) : panel);
        c.rect(b.x+2,b.y+2,b.w-4,b.h-4,b.enabled && down_ && b.id==downButton_ ? edge : panel);
        const int scale=std::strlen(b.label)*12 <= static_cast<std::size_t>(b.w-10) ? 2 : 1;
        const int width=static_cast<int>(std::strlen(b.label))*6*scale-scale;
        c.text(b.x+(b.w-width)/2,b.y+(b.h-7*scale)/2,b.label,scale,b.enabled ? ink : dim);
    }
    if(recent && captureAction(lastAction_) && !(model.battle && model.battle->locked && model.battle->capturePresentation)) {
        FlickTrajectory flight;
        if(lastAction_==Action::RingCapture || decodeFlick(actionValue_,flight)) {
            if(lastAction_==Action::RingCapture) { flight.landingX=206;flight.landingY=176;flight.hit=true; }
            const int progress=std::min<int>(500,static_cast<int>(now-actionAt_));
            const int x=206+(flight.landingX-206)*progress/500;
            const int y=300+(flight.landingY-300)*progress/500-4*28*progress*(500-progress)/(500*500);
            c.circle(x,y,23-progress*13/500,mint);
            c.center(370,progress<440 ? "THROWING..." : flight.hit ? "CONNECTED!" : "MISSED THE RING",1,amber);
        }
    }
    if(notice_[0]) {
        c.rect(51,114,310,46,panel); c.rect(57,118,298,38,bg);
        // Long failure diagnostics remain bounded; important save state is also
        // represented by the persistent read-only footer below.
        c.center(131,notice_,1,amber,47);
    }
    if(!model.writable) {
        const bool tradeLock=model.tradeWritable && model.trade &&
            (model.trade->durable==tradewire::Durable::Prepared || model.trade->durable==tradewire::Durable::Committed);
        c.center(388,tradeLock ? "TRADE IN PROGRESS" : "SAVE READ ONLY",1,tradeLock ? amber : red,20);
    }
    else if(!model.inputEnabled) c.center(388,"INPUT PAUSED",1,amber,16);
    return true;
}
const char* screenName(Screen screen) {
    switch(screen) {
    case Screen::Egg: return "egg";
    case Screen::Starter: return "starter";
    case Screen::StarterReview: return "starter-review";
    case Screen::Home: return "home";
    case Screen::Care: return "care";
    case Screen::Explore: return "explore";

    case Screen::Encounter: return "encounter";
    case Screen::Battle: return "battle";
    case Screen::Capture: return "capture";
    case Screen::Result: return "result";
    case Screen::Collection: return "collection";
    case Screen::Stats: return "stats";
    case Screen::ReleaseReview: return "release-review";
    case Screen::Evolution: return "digivolution";
    case Screen::EvolutionReview: return "digivolution-review";
    case Screen::EvolutionResult: return "digivolution-result";
    case Screen::EncounterSettings: return "encounter-settings";
    case Screen::Settings: return "settings";
    case Screen::Sound: return "sound";
    case Screen::TradeChoose: return "trade-choose";
    case Screen::TradeReview: return "trade-review";
    case Screen::ModeReview: return "mode-review";
    case Screen::Nearby: return "nearby";
    case Screen::NearbyReview: return "nearby-review";
    case Screen::Squad: return "squad";
    case Screen::Box: return "box";
    case Screen::Expeditions: return "expeditions";
    case Screen::ExpeditionLobby: return "expedition-lobby";
    }
    return "unknown";
}
} // namespace digivice::deviceui
