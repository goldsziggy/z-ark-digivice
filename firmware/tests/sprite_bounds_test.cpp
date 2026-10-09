#include "sprite_bounds.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace digivice::sprite;
namespace {
unsigned checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
bool equals(const OpaqueBounds& bounds, unsigned x, unsigned y, unsigned width, unsigned height) {
    return bounds.x == x && bounds.y == y && bounds.width == width && bounds.height == height;
}
void bit(std::vector<std::uint8_t>& masks, unsigned width, unsigned frame, unsigned x, unsigned y) {
    const auto index = frame * width * width + y * width + x;
    masks[index / 8] |= static_cast<std::uint8_t>(1u << (index % 8));
}
void emptyFullAndEveryPixel() {
    for (const unsigned side : {16u,32u}) {
        std::vector<std::uint8_t> masks(side * side / 8);
        OpaqueBounds bounds{1,2,3,4};
        const Clip clip{100,0,1};
        require(opaqueClipBounds(masks.data(),masks.size(),side,side,1,clip,bounds) &&
                equals(bounds,0,0,0,0),"empty clip has full-frame fallback");
        std::fill(masks.begin(),masks.end(),0xff);
        require(opaqueClipBounds(masks.data(),masks.size(),side,side,1,clip,bounds) &&
                equals(bounds,0,0,side,side),"fully opaque frame includes every edge");
        for (unsigned y = 0; y < side; ++y) for (unsigned x = 0; x < side; ++x) {
            std::fill(masks.begin(),masks.end(),0);
            bit(masks,side,0,x,y);
            require(opaqueClipBounds(masks.data(),masks.size(),side,side,1,clip,bounds) &&
                    equals(bounds,x,y,1,1),"every single pixel honors row-major low-bit order");
        }
    }
}
void animationUnion() {
    constexpr unsigned side = 32, frames = 48, stride = side * side / 8;
    std::vector<std::uint8_t> masks(frames * stride,0xff);
    // Neighboring clips are deliberately full; they must not inflate this union.
    std::fill(masks.begin()+40*stride,masks.end(),0);
    bit(masks,side,40,9,10);
    bit(masks,side,43,5,15);
    bit(masks,side,47,19,25);
    OpaqueBounds bounds;
    require(opaqueClipBounds(masks.data(),masks.size(),side,side,frames,{100,40,8},bounds) &&
            equals(bounds,5,10,15,16),"union spans all clip frames, excludes adjacent clips");
    require(opaqueClipBounds(masks.data(),masks.size(),side,side,frames,{100,47,1},bounds) &&
            equals(bounds,19,25,1,1),"final frame range stays in allocation");
    require(opaqueClipBounds(masks.data(),masks.size(),side,side,frames,{100,41,1},bounds) &&
            equals(bounds,0,0,0,0),"empty frame does not retain earlier bounds");
    bit(masks,side,40,0,0); bit(masks,side,47,31,31);
    require(opaqueClipBounds(masks.data(),masks.size(),side,side,frames,{100,40,8},bounds) &&
            equals(bounds,0,0,32,32),"opposite corners across frames yield full union");
}
void invalidInputs() {
    std::uint8_t tiny = 0xff;
    auto rejected = [&](unsigned width, unsigned height, unsigned frames, Clip clip,
                        std::size_t capacity, bool null = false) {
        OpaqueBounds bounds{7,8,9,10};
        require(!opaqueClipBounds(null ? nullptr : &tiny,capacity,width,height,frames,clip,bounds) &&
                equals(bounds,0,0,0,0),"invalid metadata clears output before reading tiny buffer");
    };
    rejected(16,16,1,{100,0,1},32,true);
    for (const unsigned dimension : {0u,1u,15u,17u,31u,33u,255u,256u,65535u}) {
        rejected(dimension,dimension,1,{100,0,1},SIZE_MAX);
        rejected(16,dimension,1,{100,0,1},SIZE_MAX);
    }
    for (const unsigned frames : {0u,49u,65535u}) rejected(16,16,frames,{100,0,1},SIZE_MAX);
    rejected(16,16,1,{100,0,0},SIZE_MAX);
    rejected(16,16,48,{100,0,9},SIZE_MAX);
    rejected(16,16,48,{100,0,255},SIZE_MAX);
    rejected(16,16,48,{100,48,1},SIZE_MAX);
    rejected(16,16,48,{100,65535,255},SIZE_MAX);
    rejected(16,16,48,{100,47,2},SIZE_MAX);
    rejected(16,16,1,{100,0,1},0);
    rejected(16,16,1,{100,0,1},31);
    rejected(32,32,48,{100,40,8},48*128-1);
    rejected(32,32,48,{100,0,1},128); // Capacity must describe the entire cache.
}
}
int main() {
    emptyFullAndEveryPixel(); animationUnion(); invalidInputs();
    std::printf("sprite bounds: %u checks passed; stable six-clip metadata %zu bytes; no allocation in helper\n",
                checks,sizeof(OpaqueBounds)*kAnimationCount);
}
