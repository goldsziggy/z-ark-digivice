#include "background_decode.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
unsigned checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr,"FAIL %u: %s\n",__LINE__,#condition); std::exit(1); } } while(false)
std::vector<std::uint8_t> read(const std::string& path) {
    std::ifstream file(path,std::ios::binary);
    CHECK(file.good());
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void jpegFixtures() {
    using namespace digivice::assets;
    const char* scenes[]{"meadow","forest","beach","ruins","cavern","snow","volcanic","digital"};
    for (const auto* scene : scenes) {
        for (const unsigned width : {412U,480U}) {
            const auto path = width == 412 ? std::string("assets/device/scene-")+scene+"-412-v1.jpg" :
                std::string("assets/backgrounds/jpeg/")+scene+"-480.jpg";
            auto data = read(path);
            JpegInfo info;
            CHECK(inspectBackgroundJpeg(data.data(),data.size(),info));
            CHECK(info.width == width && info.height == width);
            CHECK(!inspectBackgroundJpeg(data.data(),data.size()-1,info));
            CHECK(info.width == 0 && info.height == 0);
            auto bad = data; bad[0] = 0;
            CHECK(!inspectBackgroundJpeg(bad.data(),bad.size(),info));
            bool frame = false;
            for (std::size_t i = 2; i+9 < data.size(); ++i) if (data[i] == 0xff && data[i+1] == 0xc0) {
                bad = data; bad[i+1] = 0xc2; // Progressive must not reach ROM.
                CHECK(!inspectBackgroundJpeg(bad.data(),bad.size(),info));
                bad = data; bad[i+7] = 0x7f; bad[i+8] = 0xff; // Oversized width.
                CHECK(!inspectBackgroundJpeg(bad.data(),bad.size(),info));
                bad = data; bad[i+2] = 0xff; bad[i+3] = 0xff; // Segment overrun.
                CHECK(!inspectBackgroundJpeg(bad.data(),bad.size(),info));
                frame = true; break;
            }
            CHECK(frame);
        }
    }
    JpegInfo info;
    std::vector<std::uint8_t> oversized(kMaximumJpegBytes+1,0);
    CHECK(!inspectBackgroundJpeg(oversized.data(),oversized.size(),info));
    CHECK(!inspectBackgroundJpeg(nullptr,0,info));
}
void blocks() {
    using namespace digivice::assets;
    for (const unsigned side : {412U,480U}) {
        std::vector<std::uint16_t> pixels(kBackgroundPixels+1,0xface);
        std::size_t total = 0;
        for (unsigned top = 0; top < side; top += 16) for (unsigned left = 0; left < side; left += 16) {
            const unsigned width = std::min(16U,side-left), height = std::min(16U,side-top);
            std::uint8_t rgb[16*16*3]{};
            for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
                auto* p = rgb+(y*width+x)*3;
                p[0] = static_cast<std::uint8_t>((left+x)%256);
                p[1] = static_cast<std::uint8_t>((top+y)%256); p[2] = 200;
            }
            std::size_t wrote = 0;
            CHECK(writeBackgroundBlock({side,side},left,top,left+width-1,top+height-1,
                rgb,width*height*3,pixels.data(),kBackgroundPixels,wrote));
            total += wrote;
        }
        CHECK(total == kBackgroundPixels && pixels[kBackgroundPixels] == 0xface);
        bool allCorrect = true;
        for (unsigned y = 0; y < kBackgroundSide; ++y) for (unsigned x = 0; x < kBackgroundSide; ++x) {
            const auto r = (x*side/kBackgroundSide)%256*3/5;
            const auto g = (y*side/kBackgroundSide)%256*3/5;
            const auto expected = static_cast<std::uint16_t>(((r>>3)<<11) | ((g>>2)<<5) | 15);
            allCorrect = allCorrect && pixels[y*kBackgroundSide+x] == expected;
        }
        CHECK(allCorrect);
        std::uint8_t rgb[16*16*3]{}; std::size_t wrote = 999;
        const auto before = pixels;
        CHECK(!writeBackgroundBlock({side,side},side,0,side,0,rgb,3,pixels.data(),kBackgroundPixels,wrote));
        CHECK(!writeBackgroundBlock({side,side},0,0,16,0,rgb,51,pixels.data(),kBackgroundPixels,wrote));
        CHECK(!writeBackgroundBlock({side,side},0,0,15,15,rgb,1,pixels.data(),kBackgroundPixels,wrote));
        CHECK(!writeBackgroundBlock({side,side},0,0,15,15,rgb,sizeof(rgb),pixels.data(),1,wrote));
        CHECK(pixels == before && wrote == 0);
    }
}
}
int main() {
    jpegFixtures(); blocks();
    std::printf("Background preflight/resampling: %u checks passed;16 repository JPEGs. ROM decoder not executed on host.\n",checks);
}
