#include "../main/display_orientation.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace digivice::display;
namespace {
unsigned checks=0;
#define CHECK(value) do { ++checks; if (!(value)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#value); std::exit(1); } } while(false)
bool same(Point a,Point b) { return a.x==b.x && a.y==b.y; }
bool same(Rect a,Rect b) { return a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height; }
constexpr Orientation turns[]{Orientation::Native,Orientation::Cw90,Orientation::Ccw90};

void coordinates() {
    // Independent goldens establish direction and the last valid pixel (411).
    const Point logical[]{{0,0},{411,0},{0,411},{411,411},{206,300}};
    const Point native[][5]{
        {{0,0},{411,0},{0,411},{411,411},{206,300}},
        {{411,0},{411,411},{0,0},{0,411},{111,206}},
        {{0,411},{0,0},{411,411},{411,0},{300,205}}
    };
    for(unsigned t=0;t<3;++t) {
        for(unsigned i=0;i<5;++i) {
            CHECK(same(logicalToPanel(turns[t],logical[i]),native[t][i]));
            CHECK(same(panelToLogical(turns[t],native[t][i]),logical[i]));
        }
        for(const Point invalid: {Point{-1,0},Point{0,-1},Point{412,0},Point{0,412}}) {
            CHECK(same(logicalToPanel(turns[t],invalid),{-1,-1}));
            CHECK(same(panelToLogical(turns[t],invalid),{-1,-1}));
        }
        CHECK(same(logicalRectToPanel(turns[t],{0,0,412,412}),{0,0,412,412}));
    }
    CHECK(same(logicalRectToPanel(Orientation::Cw90,{10,20,3,2}),{390,10,2,3}));
    CHECK(same(logicalRectToPanel(Orientation::Ccw90,{10,20,3,2}),{20,399,2,3}));
    CHECK(same(logicalRectToPanel(Orientation::Cw90,{411,411,1,1}),{0,411,1,1}));
    CHECK(!validOrientation(static_cast<Orientation>(99)));
    CHECK(same(panelToLogical(static_cast<Orientation>(99),{0,0}),{-1,-1}));
    CHECK(same(logicalRectToPanel(static_cast<Orientation>(99),{0,0,1,1}),{0,0,0,0}));
    for(const Rect invalid: {Rect{-1,0,1,1},Rect{0,-1,1,1},Rect{412,0,1,1},Rect{0,412,1,1},
                           Rect{0,0,0,1},Rect{0,0,1,0},Rect{411,411,2,1},Rect{411,411,1,2},
                           Rect{0,0,std::numeric_limits<int>::max(),1}}) {
        CHECK(!validRect(invalid));
        CHECK(same(logicalRectToPanel(Orientation::Cw90,invalid),{0,0,0,0}));
    }
}

void smallGoldens() {
    // Asymmetric rectangle, padded source rows, nonzero origin and exact wire
    // byte order. Row padding is not part of the image and must never leak.
    const std::array<std::uint16_t,8> source{0x1234,0x5678,0x9abc,0xffff,0xffff,0xdef0,0x1357,0x2468};
    const std::uint8_t expected[][12]{
        {0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0,0x13,0x57,0x24,0x68},
        {0xde,0xf0,0x12,0x34,0x13,0x57,0x56,0x78,0x24,0x68,0x9a,0xbc},
        {0x9a,0xbc,0x24,0x68,0x56,0x78,0x13,0x57,0x12,0x34,0xde,0xf0}
    };
    for(unsigned t=0;t<3;++t) {
        std::array<std::uint8_t,14> output; output.fill(0xa5);
        CHECK(packStripe(turns[t],{10,20,3,2},source.data(),source.size(),5,0,t==0 ? 2 : 3,output.data()+1,12));
        CHECK(std::equal(std::begin(expected[t]),std::end(expected[t]),output.begin()+1));
        CHECK(output.front()==0xa5 && output.back()==0xa5);
        // A stripe starts at a transformed row, not a logical source row.
        output.fill(0xa5); const unsigned rowBytes=t==0 ? 6 : 4;
        CHECK(packStripe(turns[t],{10,20,3,2},source.data(),source.size(),5,1,1,output.data()+1,rowBytes));
        CHECK(std::equal(expected[t]+rowBytes,expected[t]+2*rowBytes,output.begin()+1));
        CHECK(output.front()==0xa5 && std::all_of(output.begin()+1+rowBytes,output.end(),[](auto v){return v==0xa5;}));
    }
}

void stripes() {
    // Stitch full-panel and non-square subrectangles exactly as the HAL does.
    // 412 rows end in a12-row stripe; rotated19x35 subrects end in3 rows.
    constexpr std::size_t maxPixels=412*412;
    static std::array<std::uint16_t,maxPixels+2> source{};
    std::array<std::uint8_t,412*16*2+2> output{};
    for(const Rect logical: {Rect{0,0,412,412},Rect{31,37,19,35}}) {
        const unsigned stride=logical.width==412 ? 412 : 23;
        const unsigned count=(logical.height-1)*stride+logical.width;
        source.fill(0xbeef);
        for(int y=0;y<logical.height;++y) for(int x=0;x<logical.width;++x)
            source[1+y*stride+x]=static_cast<std::uint16_t>(x*179+y*29);
        const auto before=source;
        for(const auto turn:turns) {
            const bool rotated=turn!=Orientation::Native;
            const int width=rotated ? logical.height : logical.width;
            const int height=rotated ? logical.width : logical.height;
            unsigned covered=0;
            for(int row=0;row<height;row+=16) {
                const int rows=std::min(16,height-row);
                const unsigned bytes=width*rows*2;
                output.fill(0xa5);
                CHECK(packStripe(turn,logical,source.data()+1,count,stride,row,rows,output.data()+1,bytes));
                for(int dy=0;dy<rows;++dy) for(int x=0;x<width;++x) {
                    int sx=x,sy=row+dy;
                    if(turn==Orientation::Cw90) {sx=row+dy;sy=logical.height-1-x;}
                    if(turn==Orientation::Ccw90) {sx=logical.width-1-row-dy;sy=x;}
                    const auto value=static_cast<std::uint16_t>(sx*179+sy*29);
                    const auto index=1+(dy*width+x)*2;
                    CHECK(output[index]==(value>>8) && output[index+1]==(value&255));
                    ++covered;
                }
                CHECK(output.front()==0xa5 && std::all_of(output.begin()+1+bytes,output.end(),[](auto v){return v==0xa5;}));
            }
            CHECK(covered==static_cast<unsigned>(logical.width*logical.height));
            CHECK(source==before);
        }
    }
}

void invalidArguments() {
    std::array<std::uint16_t,32> source{}; source.fill(0x1234);
    const auto before=source;
    std::array<std::uint8_t,32> output{};
    auto reject=[&](Orientation turn,Rect rect,const std::uint16_t* pixels,std::size_t count,
                    std::size_t stride,int row,int rows,std::uint8_t* destination,std::size_t capacity) {
        output.fill(0xa5);
        CHECK(!packStripe(turn,rect,pixels,count,stride,row,rows,destination,capacity));
        CHECK(std::all_of(output.begin(),output.end(),[](auto v){return v==0xa5;}));
        CHECK(source==before);
    };
    const Rect rect{10,20,3,2};
    for(const auto turn:turns) {
        const unsigned width=turn==Orientation::Native ? 3 : 2;
        reject(turn,rect,nullptr,8,5,0,1,output.data(),32);
        reject(turn,rect,source.data(),8,5,0,1,nullptr,32);
        reject(turn,rect,source.data(),7,5,0,1,output.data(),32);
        reject(turn,rect,source.data(),8,2,0,1,output.data(),32);
        reject(turn,rect,source.data(),8,413,0,1,output.data(),32);
        reject(turn,rect,source.data(),8,std::numeric_limits<std::size_t>::max(),0,1,output.data(),32);
        reject(turn,rect,source.data(),8,5,-1,1,output.data(),32);
        reject(turn,rect,source.data(),8,5,0,0,output.data(),32);
        reject(turn,rect,source.data(),8,5,0,17,output.data(),32);
        reject(turn,rect,source.data(),8,5,3,1,output.data(),32);
        reject(turn,rect,source.data(),8,5,1,3,output.data(),32);
        reject(turn,rect,source.data(),8,5,0,1,output.data(),width*2-1);
        reject(turn,rect,source.data(),8,5,0,1,reinterpret_cast<std::uint8_t*>(source.data()),32);
        reject(turn,rect,source.data(),8,5,0,1,reinterpret_cast<std::uint8_t*>(source.data()+1),32);
        reject(turn,{411,411,2,2},source.data(),8,5,0,1,output.data(),32);
    }
    reject(static_cast<Orientation>(99),rect,source.data(),8,5,0,1,output.data(),32);
    // The former boot clear passed its zeroed DMA stripe as both source and
    // destination. The helper must reject even that alias: HAL clears directly.
    source.fill(0);
    CHECK(!packStripe(Orientation::Cw90,{0,0,3,2},source.data(),6,3,0,3,
                      reinterpret_cast<std::uint8_t*>(source.data()),12));
    CHECK(std::all_of(source.begin(),source.end(),[](auto v){return v==0;}));
}
}

int main() {
    coordinates(); smallGoldens(); stripes(); invalidArguments();
    std::printf("display orientation: %u checks passed; native/CW/CCW, bounds, stripes and alias rejection; no hardware\n",checks);
}
