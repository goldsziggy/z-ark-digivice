#include "touch_cst820.hpp"
#include <algorithm>
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace digivice::display;
namespace {
unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); }} while(false)
constexpr Orientation turns[]{Orientation::Native,Orientation::Cw90,Orientation::HalfTurn,Orientation::Ccw90};
Point oracle(int angle,int x,int y,int side) {
    x=x*412/side; y=y*412/side;
    if(angle==90)return {y,411-x};
    if(angle==180)return {411-x,411-y};
    if(angle==270)return {411-y,x};
    return {x,y};
}
void allPixels() {
    std::vector<std::uint16_t> source(412*412), output(480*480);
    for(unsigned i=0;i<source.size();++i)source[i]=(i*173+29)&65535;
    for(const int side:{412,480})for(unsigned t=0;t<4;++t) {
        for(int row=0;row<side;row+=16)CHECK(blitPhysicalRows(turns[t],{0,0,412,412},source.data(),412,side,row,std::min(16,side-row),output.data()));
        for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
            const auto expected=oracle(t*90,x,y,side), actual=physicalToLogical(turns[t],{x,y},side);
            CHECK(actual.x==expected.x&&actual.y==expected.y);
            CHECK(output[y*side+x]==source[expected.y*412+expected.x]);
            if(side==480){
                std::uint8_t packet[7]{1,static_cast<std::uint8_t>(0x80|(x>>8)),static_cast<std::uint8_t>(x),static_cast<std::uint8_t>(y>>8),static_cast<std::uint8_t>(y),0,0};
                TouchPoint point;CHECK(decodeCst820(packet,turns[t],point));
                CHECK(point.fresh&&point.pressed&&point.x==expected.x&&point.y==expected.y);
            }
        }
        // All logical hit targets/swipe endpoints retain exactly their logical
        // coordinate after rotation. Check every pixel, not just target centers.
        for(int y=0;y<412;++y)for(int x=0;x<412;++x) {
            const auto panel=logicalToPanel(turns[t],{x,y});
            const auto logical=physicalToLogical(turns[t],{(panel.x*side+411)/412,(panel.y*side+411)/412},side);
            CHECK(logical.x==x&&logical.y==y);
        }
        // Non-square partial region and padded source stride: unchanged outside.
        output.assign(480*480,0xa55a);
        const Rect rect{61,115,139,44}; const auto region=logicalRectToPhysical(turns[t],rect,side);
        for(int row=0;row<region.height;row+=16)CHECK(blitPhysicalRows(turns[t],rect,source.data()+115*412+61,412,side,row,std::min(16,region.height-row),output.data()));
        for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
            const auto p=oracle(t*90,x,y,side);
            const bool inside=p.x>=61&&p.x<200&&p.y>=115&&p.y<159;
            CHECK(output[y*side+x]==(inside?source[p.y*412+p.x]:0xa55a));
        }
    }
    CHECK(!blitPhysicalRows(Orientation::Native,{0,0,412,412},source.data(),412,480,0,16,source.data()));
    CHECK(!blitPhysicalRows(Orientation::Native,{0,0,412,412},source.data(),412,481,0,16,output.data()));
    CHECK(!blitPhysicalRows(Orientation::Native,{0,0,412,412},source.data(),412,480,479,2,output.data()));
}
void halfTurnWire() {
    std::uint16_t source[]{0x1234,0x5678,0x9abc,0,0,0xdef0,0x1357,0x2468};
    std::uint8_t output[12]{};
    constexpr std::uint8_t expected[]{0x24,0x68,0x13,0x57,0xde,0xf0,0x9a,0xbc,0x56,0x78,0x12,0x34};
    CHECK(packStripe(Orientation::HalfTurn,{10,20,3,2},source,8,5,0,2,output,sizeof output));
    CHECK(std::equal(std::begin(output),std::end(output),std::begin(expected)));
    const auto p=logicalRectToPanel(Orientation::HalfTurn,{10,20,3,2});
    CHECK(p.x==399&&p.y==390&&p.width==3&&p.height==2);
}
void invalidTouch() {
    std::uint8_t p[7]{}; TouchPoint out{true,42,73,true};
    CHECK(decodeCst820(p,Orientation::Native,out)&&!out.pressed&&out.fresh);
    for(unsigned count=2;count<16;++count){p[0]=count;CHECK(!decodeCst820(p,Orientation::Native,out));}
    p[0]=1;p[1]=0xc0;CHECK(!decodeCst820(p,Orientation::Native,out));
    p[1]=1;p[2]=224;CHECK(!decodeCst820(p,Orientation::Native,out)); // x480
    p[1]=0;p[2]=0;p[3]=1;p[4]=224;CHECK(!decodeCst820(p,Orientation::Native,out));
    CHECK(!decodeCst820(p,static_cast<Orientation>(99),out));
}
}
int main(){allPixels();halfTurnWire();invalidTouch();std::printf("physical display/touch: %u checks passed, four rotations, 412/480, partial frames and CST820\n",checks);}
